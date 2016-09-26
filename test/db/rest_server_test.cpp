#include <gtest/gtest.h>
#include "db/rest/server.hpp"

TEST(DbRestServerTest, Start)
{
    auto server = db::rest::server{};
    server.start();
}
