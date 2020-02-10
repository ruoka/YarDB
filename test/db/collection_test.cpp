#include <cstdio>
#include <gtest/gtest.h>
#include "xson/json.hpp"
#include "db/engine.hpp"

using namespace xson;

class DbEngineTest2 : public ::testing::Test
{
protected:

    void SetUp()
    {
        auto fs = std::fstream{};
        fs.open(test_file, std::ios::out);
        fs.close();
    }

    void TearDown()
    {
        std::remove(test_file.c_str());
    }

    const std::string test_file = "./engine_test.db";

    void dump(db::engine& engine)
    {
        auto selector = object{}, documents = object{};
        engine.read(selector, documents);
        std::clog << "Dump:\n" << json::stringify(documents) << std::endl;
    }
};

TEST_F(DbEngineTest2, Create2Collections)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"D"s, 4}, {"E"s, 5}, {"F"s, 6}},
         document3 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document4 = object{{"D"s, 4}, {"E"s, 5}, {"F"s, 6}},
         all = object{},
         documents = object{};
    engine.collection("C1");
    engine.create(document1);
    engine.create(document2);
    engine.create(document3);
    engine.collection("C2");
    engine.create(document4);
    engine.collection("C1");
    dump(engine);
    engine.collection("C2");
    dump(engine);
    engine.collection("C1");
    engine.read(all, documents);
    ASSERT_EQ(3, documents.size());
    EXPECT_TRUE(documents[0].match(document1));
    EXPECT_TRUE(documents[1].match(document2));
    EXPECT_TRUE(documents[2].match(document3));
    documents = {};
    engine.collection("C2");
    engine.read(all, documents);
    ASSERT_EQ(1, documents.size());
    EXPECT_TRUE(documents[0].match(document4));
}

TEST_F(DbEngineTest2, Create2Keys)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 4}, {"C"s, 3}},
         document2 = object{{"A"s, 2}, {"B"s, 5}, {"C"s, 3}},
         document3 = object{{"A"s, 3}, {"B"s, 6}, {"C"s, 3}},
         selector = object{{"id"s, 1ll}},
         documents = object{};
    engine.collection("Create2Keys");
    engine.index({"A", "B", "Z"});
    engine.upsert(document1, document1);
    engine.upsert(document2, document2);
    engine.upsert(document3, document3);
    dump(engine);
    engine.destroy(selector, documents);
    dump(engine);
    engine.index({"D", "1", "2"});
    engine.reindex();
}
