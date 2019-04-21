#include <boost/asio.hpp>
#include <vector>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

struct ServerConfig {
    boost::asio::ip::tcp::endpoint my_end;
    boost::asio::ip::tcp::endpoint other_end;
    int num_workers = std::thread::hardware_concurrency();
};

class TunnelServerImpl;

class TunnelServer {
public:
    TunnelServer(boost::asio::io_service& service, ServerConfig config);
    ~TunnelServer();

    void Start();
    void Stop();
    // Mostly convenience function: just to ensure that output is not garbage
    void WaitStart();

    TunnelServer(const TunnelServer&) = delete;
    TunnelServer& operator=(const TunnelServer&) = delete;

private:
    std::shared_ptr<TunnelServerImpl> impl_;
    std::vector<std::thread> workers_;
    std::mutex mut_;
    std::condition_variable cv_;
    int num_to_start_;
};
