#include <boost/asio.hpp>
#include <vector>
#include <thread>
#include <memory>
#include <mutex>

struct ServerConfig {
    boost::asio::ip::tcp::endpoint my_end;
    boost::asio::ip::tcp::endpoint other_end;
    int num_workers = std::thread::hardware_concurrency();
};

class TunnelServer : public std::enable_shared_from_this<TunnelServer> {
public:
    TunnelServer(boost::asio::io_service& service, ServerConfig config);
    ~TunnelServer();

    void Start();
    void Stop();

    TunnelServer(const TunnelServer&) = delete;
    TunnelServer& operator=(const TunnelServer&) = delete;

private:
    void Listen();
    void AsyncAccept(const boost::system::error_code& ec, boost::asio::ip::tcp::socket sock);
    void HandleConnection(std::shared_ptr<boost::asio::ip::tcp::socket>&& conn);

    ServerConfig config_;

    std::vector<std::thread> workers_;
    boost::asio::io_service& service_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::atomic<bool> is_stopped_{false};
    std::mutex mut_;
};
