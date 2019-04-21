#include <src/server.hpp>
#include <gtest/gtest.h>
#include <glog/logging.h>

boost::asio::io_service service;

static ServerConfig GetConfig(int num_threads) {
    using boost::asio::ip::make_address;
    return {{make_address("127.0.0.1"), 10000}, {make_address("127.0.0.1"), 10001}, num_threads};
}

TEST(Basic, starts_and_stops) {
    for (int i = 0; i < 100; ++i) {
        auto server = std::make_shared<TunnelServer>(service, GetConfig(1));
        server->Start();
        server->Stop();
    }
}

TEST(Basic, auto_stop_in_destructor) {
    for (int i = 0; i < 100; ++i) {
        auto server = std::make_shared<TunnelServer>(service, GetConfig(1));
        server->Start();
    }
}

TEST(Basic, delay_not_causes_lock) {
    for (int i = 0; i < 100; ++i) {
        auto server = std::make_shared<TunnelServer>(service, GetConfig(1));
        server->Start();
        usleep(100);
        server->Stop();
    }
}

TEST(Threaded, starts_and_stops) {
    for (int num_threads = 2; num_threads < 12; ++num_threads) {
        for (int i = 0; i < 100; ++i) {
            auto server = std::make_shared<TunnelServer>(service, GetConfig(num_threads));
            server->Start();
            server->Stop();
        }
    }
}

TEST(Threaded, auto_stop_in_destructor) {
    for (int num_threads = 2; num_threads < 12; ++num_threads) {
        for (int i = 0; i < 100; ++i) {
            auto server = std::make_shared<TunnelServer>(service, GetConfig(num_threads));
            server->Start();
        }
    }
}

TEST(Threaded, delay_not_causes_lock) {
    for (int num_threads = 2; num_threads < 12; num_threads += 2) {
        for (int i = 0; i < 100; ++i) {
            auto server = std::make_shared<TunnelServer>(service, GetConfig(num_threads));
            server->Start();
            usleep(100);
            server->Stop();
        }
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    google::InitGoogleLogging(argv[0]);
    return RUN_ALL_TESTS();
}
