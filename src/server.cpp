#include <src/server.hpp>
#include <glog/logging.h>
#include <sstream>

namespace asio = boost::asio;

TunnelServer::TunnelServer(asio::io_service &service, ServerConfig config)
    : config_(std::move(config)), service_(service), acceptor_(service_) {
}

void TunnelServer::Start() {
    if (is_stopped_.load()) {
	throw std::runtime_error("Stopped server cannot be restarted");
    }
    asio::post(service_, std::bind(&TunnelServer::Listen, shared_from_this()));
    for (int i = 0; i < config_.num_workers; ++i) {
        workers_.emplace_back([me = shared_from_this()] {
            std::stringstream ss;
            ss << "Started service runner thread " << std::this_thread::get_id();
            LOG(INFO) << ss.str();

            me->service_.run();

            ss.clear();
            ss << "Stopped service runner thread " << std::this_thread::get_id();
            LOG(INFO) << ss.str();
        });
    }
}

void TunnelServer::Stop() {
    std::lock_guard lock(mut_);
    if (!is_stopped_.exchange(true)) {
	LOG(INFO) << "Stopping server";
        if (acceptor_.is_open()) {
            acceptor_.cancel();
        }
        LOG(INFO) << "Acceptor calcelled";
    }
}

TunnelServer::~TunnelServer() {
    Stop();
    for (auto &worker : workers_) {
        worker.join();
    }
}

using namespace asio::ip;
void TunnelServer::Listen() {
    {
        std::lock_guard guard(mut_);
        if (is_stopped_.load()) {
            return;
        }

        acceptor_.open(config_.my_end.protocol());
    }
    acceptor_.bind(config_.my_end);
    acceptor_.listen();
    using namespace std::placeholders;
    acceptor_.async_accept(std::bind(&TunnelServer::AsyncAccept, shared_from_this(), _1, _2));
}

void TunnelServer::AsyncAccept(const boost::system::error_code &ec,
                               boost::asio::ip::tcp::socket sock) {
    if (ec) {
        LOG(INFO) << ec.message();
        return;
    }

    auto conn = std::make_shared<tcp::socket>(std::move(sock));
    asio::post(service_, [conn = std::move(conn), me = shared_from_this()]() mutable {
        me->HandleConnection(std::move(conn));
    });
    using namespace std::placeholders;
    acceptor_.async_accept(std::bind(&TunnelServer::AsyncAccept, shared_from_this(), _1, _2));
}

void TunnelServer::HandleConnection(std::shared_ptr<tcp::socket> &&conn) {
    auto create_one_way_channel_handler = [](std::shared_ptr<tcp::socket> from,
                                             std::shared_ptr<tcp::socket> to) {
        return [from = std::move(from), to = std::move(to)] {
            std::string buffer(1024, 0);
            while (true) {
                size_t size = from->read_some(boost::asio::buffer(buffer));
                {
                    boost::asio::const_buffer view(buffer.data(), size);
                    while (view.size()) {
                        view += to->write_some(view);
                    }
                }
            }
        };
    };

    auto to_conn = std::make_shared<tcp::socket>(service_);
    to_conn->connect(config_.other_end);

    asio::post(service_, create_one_way_channel_handler(conn, to_conn));
    asio::dispatch(service_, create_one_way_channel_handler(std::move(to_conn), std::move(conn)));
}
