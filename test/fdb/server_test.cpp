#include <iostream>
#include <gtest/gtest.h>
#include "net/connector.hpp"
#include "xson/json.hpp"
#include "fdb/messages.hpp"
#include "fdb/server.hpp"

TEST(FdbServerTest, Create)
{
    fdb::engine e;
    fdb::server s{e};
    s.start();
}

TEST(FdbServerTest, Connect)
{
    fdb::header header;
    fdb::reply reply;
    fdb::create create;
    fdb::read read;

    auto server = net::connect("localhost", "50888");

    create.document = { {"A", 123}, {"B", 456}, {"C", 678.9} };
    server << create << net::flush;
    std::clog << "Sent:\n" << xson::json::stringify(create.document) << std::endl;

    server >> header >> reply;
    std::clog << "Received:\n" << xson::json::stringify(reply.document) << std::endl;

    server << read << net::flush;
    std::clog << "Sent:\n" << xson::json::stringify(read.selector) << std::endl;

    server >> header >> reply;
    std::clog << "Received:\n" << xson::json::stringify(reply.document) << std::endl;
}
