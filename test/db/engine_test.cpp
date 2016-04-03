#include <gtest/gtest.h>
#include "xson/json.hpp"
#include "db/engine.hpp"

using namespace std;
using namespace xson;
using namespace db;

class DbEngineTest : public ::testing::Test
{
protected:

    void SetUp()
    {}

    void TearDown()
    {}

    const string tempfile = "./engine_test.db";

    engine m_engine = tempfile;

    void dump()
    {
        auto selector = object{};
        auto documents = vector<object>{};
        m_engine.read(selector, documents);

        for(auto document : documents)
            clog << json::stringify(document) << endl;
    }
};

TEST_F(DbEngineTest, Create)
{
    auto document = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    m_engine.create(document);
    dump();
}

TEST_F(DbEngineTest, Read)
{
    auto selector = object{};
    auto documents = vector<object>{};
    m_engine.read(selector, documents);
}

TEST_F(DbEngineTest, Update)
{
    auto selector = object{};
    auto document = object{};
    m_engine.update(selector, document);
    dump();
}

TEST_F(DbEngineTest, Destroy)
{
    auto selector = object{u8"_id"s, 1};
    m_engine.destroy(selector);
    dump();
}

TEST_F(DbEngineTest, Create3AndDestroy1)
{
    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    m_engine.create(document1);
    auto document2 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    m_engine.create(document2);
    auto document3 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    m_engine.create(document3);

    auto selector = object{};
    auto documents = vector<object>{};
    m_engine.read(selector, documents);

    long long id = documents[1][u8"_id"s];
    selector = {u8"_id"s, id};
    m_engine.destroy(selector);
}
