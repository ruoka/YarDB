#include <gtest/gtest.h>
#include "db/rest/server.hpp"

TEST(DbRestServerTest, Start)
{
    auto engine = db::engine{};
    auto server = db::rest::server{engine};
    server.start();
}
