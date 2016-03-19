#include <iostream>
#include <gtest/gtest.h>
#include "net/connector.hpp"
#include "xson/json.hpp"
#include "fdb/server.hpp"

TEST(FdbServerTest, Create)
{
    fdb::server s;
    s.start();
}

TEST(FdbServerTest, Connect)
{
    auto server = net::connect("localhost", "50888");

    fdb::create create;
    create.document = { {"A", 123}, {"B", 456}, {"C", 678.9} };
    server << create << std::flush;
    std::clog << "Sent:\n" << xson::json::stringify(create.document) << std::endl;

    server << fdb::query{} << std::flush;;

    fdb::header header;
    fdb::reply reply;
    server >> header >> reply;
    std::clog << "Received:\n" << xson::json::stringify(reply.document) << std::endl;
}
