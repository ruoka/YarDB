#include <cstdio>
#include <gtest/gtest.h>
#include "xson/json.hpp"
#include "db/engine.hpp"

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
    auto document = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         selector = object{},
         documents = object{};
    engine.collection("Create1"s);
    engine.create(document);
    dump(engine);
    engine.read(selector, documents);
    ASSERT_EQ(1, documents.size());
    EXPECT_TRUE(documents[0].match(document));
}

TEST_F(DbEngineTest, Create9)
{
    auto engine = db::engine{test_file};
    engine.collection("Create9"s);
    for(auto i = 1; i < 10; ++i)
    {
        auto document = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
             all = object{},
             documents = object{};
        engine.create(document);
        dump(engine);
        engine.read(all, documents);
        ASSERT_EQ(i, documents.size());
        EXPECT_TRUE(documents[i-1].match(document));
    }
}

TEST_F(DbEngineTest, ReadEmptyCollection)
{
    auto engine = db::engine{test_file};
    auto selector = object{}, documents = object{};
    engine.collection("ReadEmptyCollection"s);
    engine.read(selector, documents);
    dump(engine);
    EXPECT_EQ(0, documents.size());
}

TEST_F(DbEngineTest, UpdateEmptyCollection)
{
    auto engine = db::engine{test_file};
    auto selector = object{}, document = object{}, documents = object{};
    engine.collection("UpdateEmptyCollection"s);
    engine.update(selector, document);
    engine.read(selector, documents);
    dump(engine);
    EXPECT_EQ(0, documents.size());
}

TEST_F(DbEngineTest, Update1ByID)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         document2 = object{{u8"A"s, 4}, {u8"D"s, 5}, {u8"E"s, 6}};
    auto selector = object{u8"_id"s, 1}, documents = object{};
    engine.collection("Update1ByID"s);
    engine.create(document1);
    dump(engine);
    engine.update(selector, document2);
    dump(engine);
    engine.read(selector, documents);
    ASSERT_EQ(1, documents.size());
    EXPECT_TRUE(documents[0].match(document2));
}

TEST_F(DbEngineTest, Update2ByValue)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         document2 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         document3 = object{{u8"A"s, 3}, {u8"B"s, 3}, {u8"C"s, 4}},
         document4 = object{{u8"A"s, 4}, {u8"D"s, 5}, {u8"E"s, 6}},
         selector = object{},
         documents = object{};
    engine.collection("Update2ByValue"s);
    engine.create(document1);
    engine.create(document2);
    engine.create(document3);
    dump(engine);
    engine.read(selector, documents);
    EXPECT_EQ(3, documents.size());
    selector = object{u8"A"s, 1};
    engine.update(selector, document4);
    dump(engine);
    selector = {};
    documents = {};
    engine.read(selector, documents);
    ASSERT_EQ(3, documents.size());
    EXPECT_TRUE(documents[0].match(document4));
    EXPECT_FALSE(documents[0].match(document2));
    EXPECT_TRUE(documents[1].match(document4));
    EXPECT_FALSE(documents[1].match(document2));
}

TEST_F(DbEngineTest, Update1ByKey)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         document2 = object{{u8"A"s, 2}, {u8"B"s, 2}, {u8"C"s, 3}},
         document3 = object{{u8"A"s, 3}, {u8"B"s, 3}, {u8"C"s, 4}},
         document4 = object{{u8"A"s, 1}, {u8"D"s, 5}, {u8"E"s, 6}},
         selector = object{},
         documents = object{};
    engine.collection("Update1ByKey"s);
    engine.create(document1);
    engine.create(document2);
    engine.create(document3);
    dump(engine);
    engine.read(selector, documents);
    EXPECT_EQ(3, documents.size());
    selector = object{u8"A"s, 1};
    engine.update(selector, document4);
    dump(engine);
    selector = {};
    documents = {};
    engine.read(selector, documents);
    ASSERT_EQ(3, documents.size());
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
    auto selector = object{u8"_id"s, 1}, documents = object{};
    engine.collection("DestroyEmptyCollection"s);
    EXPECT_THROW(engine.destroy(selector), out_of_range);
    dump(engine);
    engine.read(selector, documents);
    EXPECT_EQ(0, documents.size());
}

TEST_F(DbEngineTest, Destroy1ByID)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         document2 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         document3 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         all = object{},
         documents = object{};
    engine.collection("Destroy1ByID"s);
    engine.create(document1);
    engine.create(document2);
    engine.create(document3);
    dump(engine);
    engine.read(all, documents);
    EXPECT_EQ(3, documents.size());
    long long id = documents[1][u8"_id"s];
    auto selector = object{u8"_id"s, id};
    engine.destroy(selector);
    dump(engine);
    documents = {};
    engine.read(all, documents);
    EXPECT_EQ(2, documents.size());
}

TEST_F(DbEngineTest, Destroy2ByValue)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         document2 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}},
         document3 = object{{u8"A"s, 2}, {u8"B"s, 3}, {u8"C"s, 4}},
         selector = object{},
         documents = object{};
    engine.collection("Destroy2ByValue"s);
    engine.create(document1);
    engine.create(document2);
    engine.create(document3);
    dump(engine);
    engine.read(selector, documents);
    EXPECT_EQ(3, documents.size());
    selector = object{u8"A"s, 1};
    EXPECT_THROW(engine.destroy(selector), invalid_argument);
    dump(engine);
    selector = {};
    documents = {};
    engine.read(selector, documents);
    EXPECT_EQ(3, documents.size());
}

TEST_F(DbEngineTest, History)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{u8"A"s, 1}},
         document2 = object{{u8"A"s, 1}, {u8"B"s, 2}},
         document3 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    auto selector = object{u8"_id"s, 1}, all = object{};
    engine.collection("History"s);
    engine.create(document1);
    dump(engine);
    engine.update(selector, document2);
    dump(engine);
    engine.update(selector, document3);
    dump(engine);

    auto documents = object{};
    engine.read(all, documents);
    EXPECT_EQ(1, documents.size());

    auto history = object{};
    engine.history(selector, history);
    EXPECT_EQ(3, history.size());

    clog << "Hstory:"s << xson::json::stringify(history) << endl;
}
