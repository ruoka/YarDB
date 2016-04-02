#include <iostream>
#include <gtest/gtest.h>
#include "net/connector.hpp"
#include "xson/json.hpp"
#include "fdb/messages.hpp"
#include "fdb/server.hpp"

TEST(FdbServerTest, Create)
{
    auto e = fdb::engine{};
    auto s = fdb::server{e};
    s.start();
}

TEST(FdbServerTest, Connect)
{
    auto header = fdb::header{};
    auto reply = fdb::reply{};
    auto create = fdb::create{};
    auto read = fdb::read{};

    auto server = net::connect("localhost", "50888");

    create.document = { {"A", 123}, {"B", 456}, {"C", 678.9} };
    server << create << net::flush;
    std::clog << "Sent:\n" << xson::json::stringify(create.document) << std::endl;

    server >> header >> reply;
    std::clog << "Received:\n" << xson::json::stringify(reply.document) << std::endl;

    read.selector = {"_id", 2};
    server << read << net::flush;
    std::clog << "Sent:\n" << xson::json::stringify(read.selector) << std::endl;

    server >> header >> reply;
    std::clog << "Received:\n" << xson::json::stringify(reply.document) << std::endl;
}
