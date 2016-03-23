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
        fdb::object selector;
        std::vector<fdb::object> documents;
        m_engine.read(selector, documents);

        for(auto document : documents)
            std::clog << xson::json::stringify(document) << std::endl;
    }
};

TEST_F(FdbEngineTest, Create)
{
    fdb::object document = {{"A"s, 1}, {"B"s, 2}, {"C"s, 3}};
    m_engine.create(document);
    dump();
}

TEST_F(FdbEngineTest, Read)
{
    fdb::object selector;
    std::vector<fdb::object> documents;
    m_engine.read(selector, documents);
}

TEST_F(FdbEngineTest, Update)
{
    fdb::object selector, document;
    m_engine.update(selector, document);
    dump();
}

TEST_F(FdbEngineTest, Destroy)
{
    fdb::object selector{"_id"s, 1};
    m_engine.destroy(selector);
    dump();
}

TEST_F(FdbEngineTest, Create3AndDestroy1)
{
    fdb::object document1 = {{"A"s, 1}, {"B"s, 2}, {"C"s, 3}};
    m_engine.create(document1);
    fdb::object document2 = {{"A"s, 1}, {"B"s, 2}, {"C"s, 3}};
    m_engine.create(document2);
    fdb::object document3 = {{"A"s, 1}, {"B"s, 2}, {"C"s, 3}};
    m_engine.create(document3);

    fdb::object selector;
    std::vector<fdb::object> documents;
    m_engine.read(selector, documents);

    std::int64_t id = documents[1]["_id"s];

    selector = {"_id"s, id};
    m_engine.destroy(selector);
}
