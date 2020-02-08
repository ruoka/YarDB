#include <gtest/gtest.h>
#include "http/server.hpp"

using namespace std::string_literals;

TEST(HttpServerTest2,TestRegexRoutes)
{
    auto server = http::server{};

    auto handler = [](const std::string& uri){return "<p>"s + uri + "</p>"s;};

    server.get("/[a-z]*").response(handler);

    server.get("/[a-z]+/[0-9]+").response(handler);

    server.listen("8080");
}
