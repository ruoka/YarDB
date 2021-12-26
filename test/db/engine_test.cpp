#include <cstdio>
#include <gtest/gtest.h>
#include "db/engine.hpp"
#define XSON_JSON_HIDE_IOSTREAM
#include "xson/json.hpp"

using namespace std;
using namespace xson;

class DbEngineTest : public ::testing::Test
{
protected:

    void SetUp()
    {
        auto fs = fstream{};
        fs.open(test_file, ios::out);
        fs.close();
    }

    void TearDown()
    {
        remove(test_file.c_str());
    }

    const string test_file = "./engine_test.db";

    void dump(db::engine& engine)
    {
        auto all = object{}, documents = object{};
        engine.read(all, documents);
        clog << "Dump " << engine.collection() << ":\n" << json::stringify(documents) << endl;
    }
};

TEST_F(DbEngineTest, Create1)
{
    auto engine = db::engine{test_file};
    auto document = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         selector = object{},
         documents = object{};
    engine.collection("Create1"s);
    EXPECT_TRUE(engine.create(document));
    dump(engine);
    EXPECT_TRUE(engine.read(selector, documents));
    ASSERT_EQ(1u, documents.size());
    std::clog << json::stringify(documents[0]) << std::endl;
    EXPECT_TRUE(documents[0].match(document));
}

TEST_F(DbEngineTest, Create9)
{
    auto engine = db::engine{test_file};
    engine.collection("Create9"s);
    for(auto i = 1u; i < 10u; ++i)
    {
        auto document = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
             all = object{},
             documents = object{};
        EXPECT_TRUE(engine.create(document));
        dump(engine);
        EXPECT_TRUE(engine.read(all, documents));
        ASSERT_EQ(i, documents.size());
        EXPECT_TRUE(documents[i-1].match(document));
    }
}

TEST_F(DbEngineTest, ReadEmptyCollection)
{
    auto engine = db::engine{test_file};
    auto selector = object{}, documents = object{};
    engine.collection("ReadEmptyCollection"s);
    EXPECT_FALSE(engine.read(selector, documents));
    dump(engine);
    EXPECT_EQ(0u, documents.size());
}

TEST_F(DbEngineTest, UpdateEmptyCollection)
{
    auto engine = db::engine{test_file};
    auto selector = object{}, document = object{}, documents = object{};
    engine.collection("UpdateEmptyCollection"s);
    EXPECT_FALSE(engine.update(selector, document));
    EXPECT_FALSE(engine.read(selector, documents));
    dump(engine);
    EXPECT_EQ(0u, documents.size());
}

TEST_F(DbEngineTest, Update1ByID)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 4}, {"D"s, 5}, {"E"s, 6}},
         selector = object{"_id"s, 1ll},
         documents = object{};
    engine.collection("Update1ByID"s);
    EXPECT_TRUE(engine.create(document1));
    dump(engine);
    EXPECT_TRUE(engine.update(selector, document2));
    dump(engine);
    EXPECT_TRUE(engine.read(selector, documents));
    ASSERT_EQ(1u, documents.size());
    std::clog << json::stringify(documents[0]) << std::endl;
    EXPECT_TRUE(documents[0].match(document2));
}

TEST_F(DbEngineTest, Update2ByValue)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document3 = object{{"A"s, 3}, {"B"s, 3}, {"C"s, 4}},
         document4 = object{{"A"s, 4}, {"D"s, 5}, {"E"s, 6}},
         selector = object{},
         documents = object{};
    engine.collection("Update2ByValue"s);
    EXPECT_TRUE(engine.create(document1));
    EXPECT_TRUE(engine.create(document2));
    EXPECT_TRUE(engine.create(document3));
    dump(engine);
    EXPECT_TRUE(engine.read(selector, documents));
    EXPECT_EQ(3u,documents.size());
    selector = object{"A"s, 1};
    EXPECT_TRUE(engine.update(selector, document4));
    dump(engine);
    selector = object{};
    documents = object{};
    EXPECT_TRUE(engine.read(selector, documents));
    ASSERT_EQ(3u, documents.size());
    EXPECT_TRUE(documents[0].match(document4));
    EXPECT_FALSE(documents[0].match(document2));
    EXPECT_TRUE(documents[1].match(document4));
    EXPECT_FALSE(documents[1].match(document2));
}

TEST_F(DbEngineTest, Update1ByKey)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 2}, {"B"s, 2}, {"C"s, 3}},
         document3 = object{{"A"s, 3}, {"B"s, 3}, {"C"s, 4}},
         document4 = object{{"A"s, 1}, {"D"s, 5}, {"E"s, 6}},
         selector = object{},
         documents = object{};
    engine.collection("Update1ByKey"s);
    EXPECT_TRUE(engine.create(document1));
    EXPECT_TRUE(engine.create(document2));
    EXPECT_TRUE(engine.create(document3));
    dump(engine);
    EXPECT_TRUE(engine.read(selector, documents));
    EXPECT_EQ(3u, documents.size());
    selector = object{"A"s, 1};
    EXPECT_TRUE(engine.update(selector, document4));
    dump(engine);
    selector = object{};
    documents = object{};
    EXPECT_TRUE(engine.read(selector, documents));
    ASSERT_EQ(3u, documents.size());
    EXPECT_TRUE(documents[0].match(document4));
    EXPECT_TRUE(documents[0].match(document1));
    EXPECT_FALSE(documents[1].match(document4));
    EXPECT_TRUE(documents[1].match(document2));
    EXPECT_FALSE(documents[2].match(document4));
    EXPECT_TRUE(documents[2].match(document3));
}

TEST_F(DbEngineTest, DestroyEmptyCollection)
{
    auto engine = db::engine{test_file};
    auto selector = object{"id"s, 1ll},
         documents = object{};
    engine.collection("DestroyEmptyCollection"s);
    EXPECT_FALSE(engine.destroy(selector, documents));
    dump(engine);
    EXPECT_FALSE(engine.read(selector, documents));
    EXPECT_EQ(0u, documents.size());
}

TEST_F(DbEngineTest, Destroy1ByID)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document3 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         all = object{},
         documents = object{};
    engine.collection("Destroy1ByID"s);
    EXPECT_TRUE(engine.create(document1));
    EXPECT_TRUE(engine.create(document2));
    EXPECT_TRUE(engine.create(document3));
    dump(engine);
    EXPECT_TRUE(engine.read(all, documents));
    EXPECT_EQ(3u, documents.size());
    long long id = documents[1]["_id"s];
    auto selector = object{"_id"s, id};
    documents = object{};
    EXPECT_TRUE(engine.destroy(selector, documents));
    dump(engine);
    documents = object{};
    EXPECT_TRUE(engine.read(all, documents));
    EXPECT_EQ(2u, documents.size());
}

TEST_F(DbEngineTest, Destroy2ByValue)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document3 = object{{"A"s, 2}, {"B"s, 3}, {"C"s, 4}},
         selector = object{},
         documents = object{};
    engine.collection("Destroy2ByValue"s);
    EXPECT_TRUE(engine.create(document1));
    EXPECT_TRUE(engine.create(document2));
    EXPECT_TRUE(engine.create(document3));
    dump(engine);
    EXPECT_TRUE(engine.read(selector, documents));
    EXPECT_EQ(3u, documents.size());
    selector = object{"A"s, 1};
    documents = object{};
    EXPECT_TRUE(engine.destroy(selector, documents));
    dump(engine);
    selector = object{};
    documents = object{};
    engine.read(selector, documents);
    EXPECT_EQ(1u, documents.size());
    EXPECT_TRUE(documents[0].match(document3));
}

TEST_F(DbEngineTest, History)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}},
         document2 = object{{"A"s, 1}, {"B"s, 2}},
         document3 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         selector = object{"_id"s, 1ll},
         all = object{};
    engine.collection("History"s);
    EXPECT_TRUE(engine.create(document1));
    dump(engine);
    EXPECT_TRUE(engine.update(selector, document2));
    dump(engine);
    EXPECT_TRUE(engine.update(selector, document3));
    dump(engine);

    auto documents = object{};
    EXPECT_TRUE(engine.read(all, documents));
    EXPECT_EQ(1u, documents.size());

    auto history = object{};
    EXPECT_TRUE(engine.history(selector, history));
    EXPECT_EQ(3u, history.size());

    clog << "Hstory:"s << xson::json::stringify(history) << endl;
}
