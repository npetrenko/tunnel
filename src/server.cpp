#include <src/server.hpp>
#include <glog/logging.h>
#include <sstream>
#include <condition_variable>
#include <shared_mutex>

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
    bool is_stopped_{false};
    std::shared_mutex mut_;
};

TunnelServer::TunnelServer(boost::asio::io_service& service, ServerConfig config) {
    num_to_start_ = config.num_workers;
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
        workers_.emplace_back([service_p = &(impl_->service_), this] {
            std::stringstream ss;
            ss << "Started service runner thread " << std::this_thread::get_id();
            LOG(INFO) << ss.str();

            {
                std::lock_guard lock(mut_);
                --num_to_start_;
                if (!num_to_start_) {
                    cv_.notify_all();
                }
            }

            service_p->run();

            ss.clear();
            ss << "Stopped service runner thread " << std::this_thread::get_id();
            LOG(INFO) << ss.str();
        });
    }
}

void TunnelServer::WaitStart() {
    std::unique_lock lock(mut_);
    cv_.wait(lock, [&] { return num_to_start_ == 0; });
}

void TunnelServer::Stop() {
    impl_->Stop();
}

TunnelServerImpl::TunnelServerImpl(asio::io_service& service, ServerConfig config)
    : config_(std::move(config)), service_(service), acceptor_(service_) {
}

void TunnelServerImpl::Start() {
    if (is_stopped_) {
        throw std::runtime_error("Stopped server cannot be restarted");
    }
    asio::post(service_, std::bind(&TunnelServerImpl::Listen, shared_from_this()));
}

void TunnelServerImpl::Stop() {
    std::unique_lock lock(mut_);
    if (!is_stopped_) {
        is_stopped_ = true;
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
    std::shared_lock guard(mut_);
    if (is_stopped_) {
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
        std::shared_lock guard(mut_);
        acceptor_.async_accept(
            std::bind(&TunnelServerImpl::AsyncAccept, shared_from_this(), _1, _2));
    } catch (boost::wrapexcept<boost::system::system_error>& e) {
        LOG(INFO) << e.what();
    }
}

struct OneWayChannel : public std::enable_shared_from_this<OneWayChannel> {
    OneWayChannel(std::shared_ptr<tcp::socket> from_, std::shared_ptr<tcp::socket> to_)
        : from(std::move(from_)), to(std::move(to_)) {
        buffer.resize(2048);
    }

    void ReadAndResend() {
        from->async_read_some(boost::asio::buffer(buffer), [me = shared_from_this()](
                                                               boost::system::error_code ec,
                                                               size_t read_size) {
            try {
                if (ec == boost::asio::error::eof) {
                    LOG(INFO) << "Sending to half-open state because of EOF";
                    me->to->shutdown(me->to->shutdown_send);
                    return;
                } else if (ec) {
                    LOG(INFO) << ec.message();
                    return;
                }

                boost::asio::write(*(me->to), boost::asio::buffer(me->buffer.data(), read_size));
                me->ReadAndResend();
            } catch (boost::wrapexcept<boost::system::system_error>& e) {
                LOG(INFO) << e.what();
                return;
            }
        });
    }

    std::vector<char> buffer;
    std::shared_ptr<tcp::socket> from, to;
};

void TunnelServerImpl::HandleConnection(std::shared_ptr<tcp::socket>&& conn) {
    {
        std::stringstream ss;
        ss << "Recieved connection from ";
        ss << conn->remote_endpoint();
        LOG(INFO) << ss.str();
    }
    auto create_one_way_channel_handler = [](std::shared_ptr<tcp::socket> from,
                                             std::shared_ptr<tcp::socket> to) {
        auto channel = std::make_shared<OneWayChannel>(std::move(from), std::move(to));
        return std::bind(&OneWayChannel::ReadAndResend, channel);
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
