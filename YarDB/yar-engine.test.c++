#include "catch_amalgamated.hpp"
import std;
import yar;
import xson;

using namespace std;
using namespace xson;

class fixture
{
public:
    fixture(string_view f) : file{f}
    {
        auto fs = fstream{};
        fs.open(file,ios::out);
        fs.close();
    }

    ~fixture()
    {
        remove(file.c_str());
    }
private:
    std::string file;
};

void dump(db::engine& engine)
{
    auto all = object{}, documents = object{};
    engine.read(all, documents);
    clog << "Dump " << engine.collection() << ":\n" << json::stringify(documents) << endl;
};

TEST_CASE("database engine CRUD functions are working")
{

const auto test_file = "./engine_test.db";
const auto setup = fixture{test_file};

SECTION("Create1")
{
    auto engine = db::engine{test_file};
    auto document = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         selector = object{},
         documents = object{};
    engine.collection("Create1"s);
    REQUIRE(engine.create(document));
    dump(engine);
    REQUIRE(engine.read(selector, documents));
    CHECK(1u == documents.size());
    std::clog << json::stringify(documents[0]) << std::endl;
    REQUIRE(documents[0].match(document));
}

SECTION("Create9")
{
    auto engine = db::engine{test_file};
    engine.collection("Create9"s);
    for(auto i = 1u; i < 10u; ++i)
    {
        auto document = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
             all = object{},
             documents = object{};
        REQUIRE(engine.create(document));
        dump(engine);
        REQUIRE(engine.read(all, documents));
        CHECK(i == documents.size());
        REQUIRE(documents[i-1].match(document));
    }
}

SECTION("ReadEmptyCollection")
{
    auto engine = db::engine{test_file};
    auto selector = object{}, documents = object{};
    engine.collection("ReadEmptyCollection"s);
    CHECK_FALSE(engine.read(selector, documents));
    dump(engine);
    CHECK(0u == documents.size());
}

SECTION("UpdateEmptyCollection")
{
    auto engine = db::engine{test_file};
    auto selector = object{}, document = object{}, documents = object{};
    engine.collection("UpdateEmptyCollection"s);
    CHECK_FALSE(engine.update(selector, document));
    CHECK_FALSE(engine.read(selector, documents));
    dump(engine);
    CHECK(0u == documents.size());
}

SECTION("Update1ByID")
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 4}, {"D"s, 5}, {"E"s, 6}},
         selector = object{"_id"s, 1ll},
         documents = object{};
    engine.collection("Update1ByID"s);
    REQUIRE(engine.create(document1));
    dump(engine);
    REQUIRE(engine.update(selector, document2));
    dump(engine);
    REQUIRE(engine.read(selector, documents));
    CHECK(1u == documents.size());
    std::clog << json::stringify(documents[0]) << std::endl;
    REQUIRE(documents[0].match(document2));
}

SECTION("Update2ByValue")
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document3 = object{{"A"s, 3}, {"B"s, 3}, {"C"s, 4}},
         document4 = object{{"A"s, 4}, {"D"s, 5}, {"E"s, 6}},
         selector = object{},
         documents = object{};
    engine.collection("Update2ByValue"s);
    REQUIRE(engine.create(document1));
    REQUIRE(engine.create(document2));
    REQUIRE(engine.create(document3));
    dump(engine);
    REQUIRE(engine.read(selector, documents));
    CHECK(3u == documents.size());
    selector = object{"A"s, 1};
    REQUIRE(engine.update(selector, document4));
    dump(engine);
    selector = object{};
    documents = object{};
    REQUIRE(engine.read(selector, documents));
    CHECK(3u == documents.size());
    REQUIRE(documents[0].match(document4));
    CHECK_FALSE(documents[0].match(document2));
    REQUIRE(documents[1].match(document4));
    CHECK_FALSE(documents[1].match(document2));
}

SECTION("Update1ByKey")
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 2}, {"B"s, 2}, {"C"s, 3}},
         document3 = object{{"A"s, 3}, {"B"s, 3}, {"C"s, 4}},
         document4 = object{{"A"s, 1}, {"D"s, 5}, {"E"s, 6}},
         selector = object{},
         documents = object{};
    engine.collection("Update1ByKey"s);
    REQUIRE(engine.create(document1));
    REQUIRE(engine.create(document2));
    REQUIRE(engine.create(document3));
    dump(engine);
    REQUIRE(engine.read(selector, documents));
    CHECK(3u == documents.size());
    selector = object{"A"s, 1};
    REQUIRE(engine.update(selector, document4));
    dump(engine);
    selector = object{};
    documents = object{};
    REQUIRE(engine.read(selector, documents));
    CHECK(3 == documents.size());
    REQUIRE(documents[0].match(document4));
    REQUIRE(documents[0].match(document1));
    CHECK_FALSE(documents[1].match(document4));
    REQUIRE(documents[1].match(document2));
    CHECK_FALSE(documents[2].match(document4));
    REQUIRE(documents[2].match(document3));
}

SECTION("DestroyEmptyCollection")
{
    auto engine = db::engine{test_file};
    auto selector = object{"id"s, 1ll},
         documents = object{};
    engine.collection("DestroyEmptyCollection"s);
    CHECK_FALSE(engine.destroy(selector, documents));
    dump(engine);
    CHECK_FALSE(engine.read(selector, documents));
    CHECK(0 == documents.size());
}

SECTION("Destroy1ByID")
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document3 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         all = object{},
         documents = object{};
    engine.collection("Destroy1ByID"s);
    REQUIRE(engine.create(document1));
    REQUIRE(engine.create(document2));
    REQUIRE(engine.create(document3));
    dump(engine);
    REQUIRE(engine.read(all, documents));
    CHECK(3u == documents.size());
    long id = documents[1]["_id"s];
    auto selector = object{"_id"s, id};
    documents = object{};
    REQUIRE(engine.destroy(selector, documents));
    dump(engine);
    documents = object{};
    REQUIRE(engine.read(all, documents));
    CHECK(2 == documents.size());
}

SECTION("Destroy2ByValue")
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document2 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         document3 = object{{"A"s, 2}, {"B"s, 3}, {"C"s, 4}},
         selector = object{},
         documents = object{};
    engine.collection("Destroy2ByValue"s);
    REQUIRE(engine.create(document1));
    REQUIRE(engine.create(document2));
    REQUIRE(engine.create(document3));
    dump(engine);
    REQUIRE(engine.read(selector, documents));
    CHECK(3 == documents.size());
    selector = object{"A"s, 1};
    documents = object{};
    REQUIRE(engine.destroy(selector, documents));
    dump(engine);
    selector = object{};
    documents = object{};
    engine.read(selector, documents);
    CHECK(1u == documents.size());
    REQUIRE(documents[0].match(document3));
}

SECTION("History")
{
    auto engine = db::engine{test_file};
    auto document1 = object{{"A"s, 1}},
         document2 = object{{"A"s, 1}, {"B"s, 2}},
         document3 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
         selector = object{"_id"s, 1ll},
         all = object{};
    engine.collection("History"s);
    REQUIRE(engine.create(document1));
    dump(engine);
    REQUIRE(engine.update(selector, document2));
    dump(engine);
    REQUIRE(engine.update(selector, document3));
    dump(engine);

    auto documents = object{};
    REQUIRE(engine.read(all, documents));
    CHECK(1u == documents.size());

    auto history = object{};
    REQUIRE(engine.history(selector, history));
    CHECK(3u == history.size());

    clog << "Hstory:"s << xson::json::stringify(history) << endl;
}


SECTION("Create2Collections")
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
    dump(engine);
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
    CHECK(3u == documents.size());
    REQUIRE(documents[0].match(document1));
    REQUIRE(documents[1].match(document2));
    REQUIRE(documents[2].match(document3));
    documents = object{};
    engine.collection("C2");
    engine.read(all, documents);
    CHECK(1u == documents.size());
    REQUIRE(documents[0].match(document4));
}

SECTION("Create2Keys")
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

}
