#include <src/server.hpp>
#include <glog/logging.h>
#include <sstream>

namespace asio = boost::asio;

class TunnelServerImpl : public std::enable_shared_from_this<TunnelServerImpl> {
public:
    friend class TunnelServer;
    TunnelServerImpl(boost::asio::io_service& service, ServerConfig config);
    ~TunnelServerImpl();

    void Start();
    void Stop();

    TunnelServerImpl(const TunnelServer&) = delete;
    TunnelServerImpl& operator=(const TunnelServer&) = delete;

private:
    void Listen();
    void AsyncAccept(const boost::system::error_code& ec, boost::asio::ip::tcp::socket sock);
    void HandleConnection(std::shared_ptr<boost::asio::ip::tcp::socket>&& conn);

    ServerConfig config_;

    boost::asio::io_service& service_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::atomic<bool> is_stopped_{false};
    std::mutex mut_;
};

TunnelServer::TunnelServer(boost::asio::io_service& service, ServerConfig config) {
    impl_ = std::make_shared<TunnelServerImpl>(service, std::move(config));
}

TunnelServer::~TunnelServer() {
    impl_.reset();
    for (auto& worker : workers_) {
        worker.join();
    }
}

void TunnelServer::Start() {
    impl_->Start();
    for (int i = 0; i < impl_->config_.num_workers; ++i) {
        workers_.emplace_back([service_p = &(impl_->service_)] {
            std::stringstream ss;
            ss << "Started service runner thread " << std::this_thread::get_id();
            LOG(INFO) << ss.str();

            service_p->run();

            ss.clear();
            ss << "Stopped service runner thread " << std::this_thread::get_id();
            LOG(INFO) << ss.str();
        });
    }
}

void TunnelServer::Stop() {
    impl_->Stop();
}

TunnelServerImpl::TunnelServerImpl(asio::io_service& service, ServerConfig config)
    : config_(std::move(config)), service_(service), acceptor_(service_) {
}

void TunnelServerImpl::Start() {
    if (is_stopped_.load()) {
        throw std::runtime_error("Stopped server cannot be restarted");
    }
    asio::post(service_, std::bind(&TunnelServerImpl::Listen, shared_from_this()));
}

void TunnelServerImpl::Stop() {
    std::lock_guard lock(mut_);
    if (!is_stopped_.exchange(true)) {
        LOG(INFO) << "Stopping server";
        if (acceptor_.is_open()) {
            acceptor_.cancel();
        }
        LOG(INFO) << "Acceptor calcelled";
    }
}

TunnelServerImpl::~TunnelServerImpl() {
    Stop();
}

using namespace asio::ip;
void TunnelServerImpl::Listen() {
    std::lock_guard guard(mut_);
    if (is_stopped_.load()) {
        return;
    }

    acceptor_.open(config_.my_end.protocol());
    acceptor_.bind(config_.my_end);
    acceptor_.listen();
    using namespace std::placeholders;
    acceptor_.async_accept(std::bind(&TunnelServerImpl::AsyncAccept, shared_from_this(), _1, _2));
}

void TunnelServerImpl::AsyncAccept(const boost::system::error_code& ec,
                                   boost::asio::ip::tcp::socket sock) {
    if (ec) {
        LOG(INFO) << ec.message();
        return;
    }

    auto conn = std::make_shared<tcp::socket>(std::move(sock));
    asio::post(service_, [conn = std::move(conn), me = shared_from_this()]() mutable {
        me->HandleConnection(std::move(conn));
    });

    try {
        using namespace std::placeholders;
        std::lock_guard guard(mut_);
        acceptor_.async_accept(
            std::bind(&TunnelServerImpl::AsyncAccept, shared_from_this(), _1, _2));
    } catch (boost::wrapexcept<boost::system::system_error>& e) {
        LOG(INFO) << e.what();
    }
}

void TunnelServerImpl::HandleConnection(std::shared_ptr<tcp::socket>&& conn) {
    auto create_one_way_channel_handler = [](std::shared_ptr<tcp::socket> from,
                                             std::shared_ptr<tcp::socket> to) {
        return [from = std::move(from), to = std::move(to)] {
            std::string buffer(1024, 0);
            try {
                while (true) {
                    size_t size = from->read_some(boost::asio::buffer(buffer));
                    {
                        boost::asio::const_buffer view(buffer.data(), size);
                        while (view.size()) {
                            view += to->write_some(view);
                        }
                    }
                }
            } catch (boost::wrapexcept<boost::system::system_error>& e) {
                LOG(INFO) << e.what();
		return;
            }
        };
    };

    auto to_conn = std::make_shared<tcp::socket>(service_);
    try {
        to_conn->connect(config_.other_end);
    } catch (boost::wrapexcept<boost::system::system_error>& e) {
        LOG(INFO) << e.what();
        return;
    }

    asio::post(service_, create_one_way_channel_handler(conn, to_conn));
    asio::dispatch(service_, create_one_way_channel_handler(std::move(to_conn), std::move(conn)));
}
