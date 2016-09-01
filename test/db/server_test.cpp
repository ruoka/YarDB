#include <iostream>
#include <gtest/gtest.h>
#include "net/connector.hpp"
#include "xson/json.hpp"
#include "db/messages.hpp"
#include "db/server.hpp"
#include "db/engine.hpp"

using namespace std;
using namespace xson;

TEST(DbServerTest, Create)
{
    auto engine = db::engine{};
    auto server = db::server{engine, "50888"};
    server.start();
}

TEST(DbServerTest, Connect)
{
    auto create = db::msg::create{};
    auto read  = db::msg::read{};
    auto header = db::msg::header{};
    auto reply = db::msg::reply{};

    auto server = net::connect("localhost", "50888");

    create.document = { {"A", 123}, {"B", 456}, {"C", 678.9} };
    server << create << net::flush;
    clog << "Sent:\n" << json::stringify(create.document) << endl;

    server >> header >> reply;
    clog << "Received:\n" << json::stringify(reply.document) << endl;

    read.selector = {"_id", 2};
    server << read << net::flush;
    clog << "Sent:\n" << json::stringify(read.selector) << endl;

    server >> header >> reply;
    clog << "Received:\n" << json::stringify(reply.document) << endl;
}
