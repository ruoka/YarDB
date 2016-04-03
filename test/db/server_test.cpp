#include <iostream>
#include <gtest/gtest.h>
#include "net/connector.hpp"
#include "xson/json.hpp"
#include "db/messages.hpp"
#include "db/server.hpp"

using namespace std;
using namespace xson;
using namespace db;

TEST(DbServerTest, Create)
{
    auto e = engine{};
    auto s = server{e, "50888"};
    s.start();
}

TEST(DbServerTest, Connect)
{
    auto create = msg::create{};
    auto read  = msg::read{};
    auto header = msg::header{};
    auto reply = msg::reply{};

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
