#include <src/server.hpp>
#include <gtest/gtest.h>

#include <iostream>

boost::asio::io_service service;

static ServerConfig GetConfig(int num_threads) {
    using boost::asio::ip::make_address;
    return {{make_address("127.0.0.1"), 10000}, {make_address("127.0.0.1"), 10001}, num_threads};
}

TEST(BASIC, starts_and_stops) {
    auto server = std::make_shared<TunnelServer>(service, GetConfig(1));
    server->Start();
    server->Stop();
}

TEST(BASIC, auto_stop_in_destructor) {
    auto server = std::make_shared<TunnelServer>(service, GetConfig(1));
    server->Start();
}

TEST(BASIC, delay_not_causes_lock) {
    auto server = std::make_shared<TunnelServer>(service, GetConfig(1));
    server->Start();
    sleep(10);
    server->Stop();
}
