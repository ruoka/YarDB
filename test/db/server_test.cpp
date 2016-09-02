#include <iostream>
#include <gtest/gtest.h>
#include "net/connector.hpp"
#include "xson/json.hpp"
#include "db/engine.hpp"
#include "db/fast/messages.hpp"
#include "db/fast/server.hpp"

using namespace std;
using namespace xson;

TEST(DbServerTest, Create)
{
    auto engine = db::engine{};
    auto server = db::fast::server{engine, "50888"};
    server.start();
}

TEST(DbServerTest, Connect)
{
    auto create = db::fast::create{};
    auto read  = db::fast::read{};
    auto header = db::fast::header{};
    auto reply = db::fast::reply{};

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
