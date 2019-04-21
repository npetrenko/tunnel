#include <boost/asio.hpp>
#include <vector>
#include <thread>
#include <memory>
#include <mutex>
#include <shared_mutex>

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

    TunnelServer(const TunnelServer&) = delete;
    TunnelServer& operator=(const TunnelServer&) = delete;

private:
    std::shared_ptr<TunnelServerImpl> impl_;
    std::vector<std::thread> workers_;
};
