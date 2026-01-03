module yar;
import :engine;
import tester;
import std;
import xson;

namespace yar::engine_unit_test {

using namespace std;
using namespace xson;

class fixture
{
public:
    fixture(string_view f) : file{f}
    {
        // Remove any existing PID file
        string pid_file = string{file} + ".pid";
        remove(pid_file.c_str());

        auto fs = fstream{};
        fs.open(file,ios::out);
        fs.close();
    }

    ~fixture()
    {
        remove(file.c_str());
        // Also remove the PID lock file
        string pid_file = string{file} + ".pid";
        remove(pid_file.c_str());
    }
private:
    std::string file;
};

void dump(yar::db::engine& engine)
{
    auto all = object{}, documents = object{};
    engine.read(all, documents);
    clog << "Dump " << engine.collection() << ":\n" << json::stringify(documents) << endl;
};

auto test_set()
{
    using namespace tester::basic;
    using namespace tester::assertions;

    test_case("database engine CRUD functions are working, [yardb]") = []
    {
        const auto test_file = "./engine_test.db";
        const auto setup = fixture{test_file};

        section("Create1") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                selector = object{},
                documents = object{};
            engine.collection("Create1"s);
            require_true(engine.create(document));
            dump(engine);
            require_true(engine.read(selector, documents));
            require_true(1u == documents.size());
            std::clog << json::stringify(documents[0]) << std::endl;
            require_true(documents[0].match(document));
        };

        section("Create9") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            engine.collection("Create9"s);
            for(auto i = 1u; i < 10u; ++i)
            {
                auto document = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                    all = object{},
                    documents = object{};
                require_true(engine.create(document));
                dump(engine);
                require_true(engine.read(all, documents));
                require_true(i == documents.size());
                require_true(documents[i-1].match(document));
            }
        };

        section("ReadEmptyCollection") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto selector = object{}, documents = object{};
            engine.collection("ReadEmptyCollection"s);
            require_false(engine.read(selector, documents));
            dump(engine);
            require_true(0u == documents.size());
        };

        section("UpdateEmptyCollection") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto selector = object{}, document = object{}, documents = object{};
            engine.collection("UpdateEmptyCollection"s);
            require_false(engine.update(selector, document));
            require_false(engine.read(selector, documents));
            dump(engine);
            require_true(0u == documents.size());
        };

        section("Update1ByID") = []
        {
            const auto test_file = "./engine_update1_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                document2 = object{{"A"s, 4}, {"D"s, 5}, {"E"s, 6}},
                selector = object{"_id"s, 1ll},
                documents = object{};
            engine.collection("Update1ByID"s);
            require_true(engine.create(document1));
            dump(engine);
            require_true(engine.update(selector, document2));
            dump(engine);
            require_true(engine.read(selector, documents));
            require_true(1u == documents.size());
            std::clog << json::stringify(documents[0]) << std::endl;
            require_true(documents[0].match(document2));
        };

        section("Update2ByValue") = []
        {
            const auto test_file = "./engine_update2_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                document2 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                document3 = object{{"A"s, 3}, {"B"s, 3}, {"C"s, 4}},
                document4 = object{{"A"s, 4}, {"D"s, 5}, {"E"s, 6}},
                selector = object{},
                documents = object{};
            engine.collection("Update2ByValue"s);
            require_true(engine.create(document1));
            require_true(engine.create(document2));
            require_true(engine.create(document3));
            dump(engine);
            require_true(engine.read(selector, documents));
            require_true(3u == documents.size());
            selector = object{"A"s, 1};
            require_true(engine.update(selector, document4));
            dump(engine);
            selector = object{};
            documents = object{};
            require_true(engine.read(selector, documents));
            require_true(3u == documents.size());
            require_true(documents[0].match(document4));
            require_false(documents[0].match(document2));
            require_true(documents[1].match(document4));
            require_false(documents[1].match(document2));
        };

        section("Update1ByKey") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                document2 = object{{"A"s, 2}, {"B"s, 2}, {"C"s, 3}},
                document3 = object{{"A"s, 3}, {"B"s, 3}, {"C"s, 4}},
                document4 = object{{"A"s, 1}, {"D"s, 5}, {"E"s, 6}},
                selector = object{},
                documents = object{};
            engine.collection("Update1ByKey"s);
            require_true(engine.create(document1));
            require_true(engine.create(document2));
            require_true(engine.create(document3));
            dump(engine);
            require_true(engine.read(selector, documents));
            require_true(3u == documents.size());
            selector = object{"A"s, 1};
            require_true(engine.update(selector, document4));
            dump(engine);
            selector = object{};
            documents = object{};
            require_true(engine.read(selector, documents));
            require_true(3 == documents.size());
            require_true(documents[0].match(document4));
            require_true(documents[0].match(document1));
            require_false(documents[1].match(document4));
            require_true(documents[1].match(document2));
            require_false(documents[2].match(document4));
            require_true(documents[2].match(document3));
        };

        section("DestroyEmptyCollection") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto selector = object{"id"s, 1ll},
                documents = object{};
            engine.collection("DestroyEmptyCollection"s);
            require_false(engine.destroy(selector, documents));
            dump(engine);
            require_false(engine.read(selector, documents));
            require_true(0 == documents.size());
        };

        section("Destroy1ByID") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                document2 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                document3 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                all = object{},
                documents = object{};
            engine.collection("Destroy1ByID"s);
            require_true(engine.create(document1));
            require_true(engine.create(document2));
            require_true(engine.create(document3));
            dump(engine);
            require_true(engine.read(all, documents));
            require_true(3u == documents.size());
            xson::integer_type id = documents[1]["_id"s];
            auto selector = object{"_id"s, id};
            documents = object{};
            require_true(engine.destroy(selector, documents));
            dump(engine);
            documents = object{};
            require_true(engine.read(all, documents));
            require_true(2 == documents.size());
        };

        section("Destroy2ByValue") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                document2 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                document3 = object{{"A"s, 2}, {"B"s, 3}, {"C"s, 4}},
                selector = object{},
                documents = object{};
            engine.collection("Destroy2ByValue"s);
            require_true(engine.create(document1));
            require_true(engine.create(document2));
            require_true(engine.create(document3));
            dump(engine);
            require_true(engine.read(selector, documents));
            require_true(3 == documents.size());
            selector = object{"A"s, 1};
            documents = object{};
            require_true(engine.destroy(selector, documents));
            dump(engine);
            selector = object{};
            documents = object{};
            engine.read(selector, documents);
            require_true(1u == documents.size());
            require_true(documents[0].match(document3));
        };

        section("History") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"A"s, 1}},
                document2 = object{{"A"s, 1}, {"B"s, 2}},
                document3 = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                selector = object{"_id"s, 1ll},
                all = object{};
            engine.collection("History"s);
            require_true(engine.create(document1));
            dump(engine);
            require_true(engine.update(selector, document2));
            dump(engine);
            require_true(engine.update(selector, document3));
            dump(engine);

            auto documents = object{};
            require_true(engine.read(all, documents));
            require_true(1u == documents.size());

            auto history = object{};
            require_true(engine.history(selector, history));
            require_true(3u == history.size());

            clog << "Hstory:"s << xson::json::stringify(history) << endl;
        };


        section("Create2Collections") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
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
            require_true(3u == documents.size());
            require_true(documents[0].match(document1));
            require_true(documents[1].match(document2));
            require_true(documents[2].match(document3));
            documents = object{};
            engine.collection("C2");
            engine.read(all, documents);
            require_true(1u == documents.size());
            require_true(documents[0].match(document4));
        };

        section("Create2Keys") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
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
        };
    };
    return true;
}

const auto test_registrar = test_set();

}
