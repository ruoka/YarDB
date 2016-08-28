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
        auto fs = std::fstream{};
        fs.open(test_file, std::ios::out);
        fs.close();
    }

    void TearDown()
    {
        std::remove(test_file.c_str());
    }

    const string test_file = "./engine_test.db";

    void dump(db::engine& e)
    {
        auto selector = object{};
        auto documents = vector<object>{};
        e.read(selector, documents);
        clog << "documents:\n";
        for(auto document : documents)
            clog << json::stringify(document) << endl;
    }
};

TEST_F(DbEngineTest, Create1)
{
    auto engine = db::engine{test_file};
    auto document = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document);
    dump(engine);
    auto selector = object{};
    auto documents = vector<object>{};
    engine.read(selector, documents);
    ASSERT_EQ(1, documents.size());
    EXPECT_TRUE(documents[0].has(document));
}

TEST_F(DbEngineTest, ReadEmptyCollection)
{
    auto engine = db::engine{test_file};
    auto selector = object{};
    auto documents = vector<object>{};
    engine.read(selector, documents);
    dump(engine);
    EXPECT_EQ(0, documents.size());
}

TEST_F(DbEngineTest, UpdateEmptyCollection)
{
    auto engine = db::engine{test_file};
    auto selector = object{};
    auto document = object{};
    engine.update(selector, document);
    dump(engine);
    auto documents = vector<object>{};
    engine.read(selector, documents);
    EXPECT_EQ(0, documents.size());
}

TEST_F(DbEngineTest, Update1ByID)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document1);
    dump(engine);
    auto document2 = object{{u8"A"s, 4}, {u8"D"s, 5}, {u8"E"s, 6}};
    auto selector = object{u8"_id"s, 1};
    engine.update(selector, document2);
    dump(engine);
    auto documents = vector<object>{};
    engine.read(selector, documents);
    ASSERT_EQ(1, documents.size());
    EXPECT_TRUE(documents[0].has(document2));
}

TEST_F(DbEngineTest, Update2ByValue)
{
    auto engine = db::engine{test_file};

    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document1);
    auto document2 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document2);
    auto document3 = object{{u8"A"s, 2}, {u8"B"s, 3}, {u8"C"s, 4}};
    engine.create(document3);

    {
        dump(engine);
        auto selector1 = object{};
        auto documents1 = vector<object>{};
        engine.read(selector1, documents1);
        EXPECT_EQ(3, documents1.size());
    }

    auto document4 = object{{u8"A"s, 4}, {u8"D"s, 5}, {u8"E"s, 6}};
    auto selector4 = object{u8"A"s, 1};
    engine.update(selector4, document4);

    {
        dump(engine);
        auto selector3 = object{};
        auto documents3 = vector<object>{};
        engine.read(selector3, documents3);
        ASSERT_EQ(3, documents3.size());
    }
}

TEST_F(DbEngineTest, Replace1ByID)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document1);
    auto document2 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document2);
    auto document3 = object{{u8"A"s, 2}, {u8"B"s, 3}, {u8"C"s, 4}};
    engine.create(document3);
    dump(engine);
    auto document4 = object{{u8"X"s, 4}, {u8"Y"s, 5}, {u8"Z"s, 6}};
    auto selector4 = object{u8"_id"s, 2};
    engine.update(selector4, document4, true);
    dump(engine);
    auto selector = object{};
    auto documents = vector<object>{};
    engine.read(selector, documents);
    ASSERT_EQ(3, documents.size());
    EXPECT_TRUE(documents[1].has(document4));
    EXPECT_FALSE(documents[1].has(document2));
}

TEST_F(DbEngineTest, DestroyEmptyCollection)
{
    auto engine = db::engine{test_file};
    auto selector = object{u8"_id"s, 1};
    engine.destroy(selector);
    dump(engine);
    auto documents = vector<object>{};
    engine.read(selector, documents);
    EXPECT_EQ(0, documents.size());
}

TEST_F(DbEngineTest, Destroy1ByID)
{
    auto engine = db::engine{test_file};

    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document1);
    auto document2 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document2);
    auto document3 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document3);

    {
        dump(engine);
        auto selector1 = object{};
        auto documents1 = vector<object>{};
        engine.read(selector1, documents1);
        EXPECT_EQ(3, documents1.size());
    }

    auto selector2 = object{};
    auto documents = vector<object>{};
    engine.read(selector2, documents);

    long long id = documents[1][u8"_id"s];
    selector2 = {u8"_id"s, id};
    engine.destroy(selector2);

    {
        dump(engine);
        auto selector3 = object{};
        auto documents3 = vector<object>{};
        engine.read(selector3, documents3);
        EXPECT_EQ(2, documents3.size());
    }
}

TEST_F(DbEngineTest, Destroy2ByValue)
{
    auto engine = db::engine{test_file};

    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document1);
    auto document2 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    engine.create(document2);
    auto document3 = object{{u8"A"s, 2}, {u8"B"s, 3}, {u8"C"s, 4}};
    engine.create(document3);

    {
        dump(engine);
        auto selector1 = object{};
        auto documents1 = vector<object>{};
        engine.read(selector1, documents1);
        EXPECT_EQ(3, documents1.size());
    }

    auto selector2 = object{u8"A"s, 1};
    engine.destroy(selector2);

    {
        dump(engine);
        auto selector3 = object{};
        auto documents3 = vector<object>{};
        engine.read(selector3, documents3);
        EXPECT_EQ(1, documents3.size());
    }
}
