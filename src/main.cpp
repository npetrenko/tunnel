#include <src/server.hpp>
#include <boost/asio.hpp>
#include <gflags/gflags.h>

DEFINE_string(src_addr, "127.0.0.1", "IP address to listen on");
DEFINE_int32(src_port, 10000, "Port to listen on");

DEFINE_string(dest_addr, "", "IP address to tunnel to");
DEFINE_int32(dest_port, -1, "Destination port");

DEFINE_int32(num_threads, std::thread::hardware_concurrency(), "Number of threads to run on");

static bool ValidateAddr(const char* flag_name, const std::string& addr) {
    try {
        boost::asio::ip::make_address(addr);
        return true;
    } catch (...) {
        return false;
    }
}

static bool ValidatePort(const char* flag_name, int32_t port) {
    if (port > 0 && port <= std::numeric_limits<unsigned short>::max()) {
        return true;
    }
    return false;
}

static bool ValidateThreads(const char* flag_name, int32_t num_threads) {
    if (num_threads > 0) {
        return true;
    }
    return false;
}

DEFINE_validator(src_addr, ValidateAddr);
DEFINE_validator(dest_addr, ValidateAddr);
DEFINE_validator(src_port, ValidatePort);
DEFINE_validator(dest_port, ValidatePort);
DEFINE_validator(num_threads, ValidateThreads);

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    boost::asio::io_service service;
    ServerConfig config{{boost::asio::ip::make_address(FLAGS_src_addr),
                         static_cast<unsigned short>(FLAGS_src_port)},
                        {boost::asio::ip::make_address(FLAGS_dest_addr),
                         static_cast<unsigned short>(FLAGS_dest_port)},
                        FLAGS_num_threads};

    auto server = std::make_shared<TunnelServer>(service, std::move(config));
    server->Start();

    server->Stop();

    return 0;
}
