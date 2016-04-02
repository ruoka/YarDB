#include <gtest/gtest.h>
#include "xson/json.hpp"
#include "fdb/engine.hpp"

using namespace std::string_literals;

class FdbEngineTest : public ::testing::Test
{
protected:

    void SetUp()
    {}

    void TearDown()
    {}

    const std::string tempfile = "./engine_test.db";

    fdb::engine m_engine = tempfile;

    void dump()
    {
        auto selector = fdb::object{};
        auto documents = std::vector<fdb::object>{};
        m_engine.read(selector, documents);

        for(auto document : documents)
            std::clog << xson::json::stringify(document) << std::endl;
    }
};

TEST_F(FdbEngineTest, Create)
{
    auto document = fdb::object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}};
    m_engine.create(document);
    dump();
}

TEST_F(FdbEngineTest, Read)
{
    auto selector = fdb::object{};
    auto documents = std::vector<fdb::object>{};
    m_engine.read(selector, documents);
}

TEST_F(FdbEngineTest, Update)
{
    auto selector = fdb::object{};
    auto document = fdb::object{};
    m_engine.update(selector, document);
    dump();
}

TEST_F(FdbEngineTest, Destroy)
{
    auto selector = fdb::object{"_id"s, 1};
    m_engine.destroy(selector);
    dump();
}

TEST_F(FdbEngineTest, Create3AndDestroy1)
{
    auto document1 = fdb::object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}};
    m_engine.create(document1);
    auto document2 = fdb::object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}};
    m_engine.create(document2);
    auto document3 = fdb::object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}};
    m_engine.create(document3);

    auto selector = fdb::object{};
    auto documents = std::vector<fdb::object>{};
    m_engine.read(selector, documents);

    std::int64_t id = documents[1]["_id"s];

    selector = {"_id"s, id};
    m_engine.destroy(selector);
}
