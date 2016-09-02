#include <cstdio>
#include <gtest/gtest.h>
#include "xson/json.hpp"
#include "db/engine.hpp"

using namespace std;
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

TEST_F(DbEngineTest2, Create2Collections)
{
    auto engine = db::engine{test_file};
    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    auto document2 = object{{u8"D"s, 4}, {u8"E"s, 5}, {u8"F"s, 6}};
    auto document3 = object{{u8"A"s, 1}, {u8"B"s, 2}, {u8"C"s, 3}};
    auto document4 = object{{u8"D"s, 4}, {u8"E"s, 5}, {u8"F"s, 6}};

    engine.collection("d1");
    engine.create(document1);
    engine.create(document2);
    engine.create(document3);

    engine.collection("d2");
    engine.create(document4);

    engine.collection("d1");
    dump(engine);

    engine.collection("d2");
    dump(engine);

    auto selector = object{};
    auto documents = vector<object>{};

    engine.collection("d1");
    engine.read(selector, documents);
    ASSERT_EQ(3, documents.size());
    EXPECT_TRUE(documents[0].match(document1));
    EXPECT_TRUE(documents[1].match(document2));
    EXPECT_TRUE(documents[2].match(document3));

    documents.clear();

    engine.collection("d2");
    engine.read(selector, documents);
    ASSERT_EQ(1, documents.size());
    EXPECT_TRUE(documents[0].match(document4));
}

TEST_F(DbEngineTest2, Create2Keys)
{
    auto engine = db::engine{test_file};
    engine.collection("test");
    try
    {
        engine.index({"A", "B"});
    }
    catch(...){}

    auto document1 = object{{u8"A"s, 1}, {u8"B"s, 4}, {u8"C"s, 3}};
    auto document2 = object{{u8"A"s, 2}, {u8"B"s, 5}, {u8"C"s, 3}};
    auto document3 = object{{u8"A"s, 3}, {u8"B"s, 6}, {u8"C"s, 3}};

    engine.update(document1, document1, true);
    engine.update(document2, document2, true);
    engine.update(document3, document3, true);
    dump(engine);

    auto selector = object{{u8"_id"s, 1}};
    engine.destroy(selector);
    dump(engine);

    engine.dump();

    try
    {
        engine.index({"D"});
        engine.reindex();
    }
    catch(...){}

    engine.dump();
}
