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
            require_true(engine.create(document).has_value());
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
                require_true(engine.create(document).has_value());
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
            require_eq(engine.update(selector, document).value(), 0u);
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
            require_true(engine.create(document1).has_value());
            dump(engine);
            require_true(engine.update(selector, document2).value() > 0);
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
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());
            require_true(engine.create(document3).has_value());
            dump(engine);
            require_true(engine.read(selector, documents));
            require_true(3u == documents.size());
            selector = object{"A"s, 1};
            require_true(engine.update(selector, document4).value() > 0);
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
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());
            require_true(engine.create(document3).has_value());
            dump(engine);
            require_true(engine.read(selector, documents));
            require_true(3u == documents.size());
            selector = object{"A"s, 1};
            require_true(engine.update(selector, document4).value() > 0);
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
            require_eq(engine.destroy(selector, documents).value(), 0u);
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
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());
            require_true(engine.create(document3).has_value());
            dump(engine);
            require_true(engine.read(all, documents));
            require_true(3u == documents.size());
            xson::integer_type id = documents[1]["_id"s];
            auto selector = object{"_id"s, id};
            documents = object{};
            require_true(engine.destroy(selector, documents).value() > 0);
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
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());
            require_true(engine.create(document3).has_value());
            dump(engine);
            require_true(engine.read(selector, documents));
            require_true(3 == documents.size());
            selector = object{"A"s, 1};
            documents = object{};
            require_true(engine.destroy(selector, documents).value() > 0);
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
            require_true(engine.create(document1).has_value());
            dump(engine);
            require_true(engine.update(selector, document2).value() > 0);
            dump(engine);
            require_true(engine.update(selector, document3).value() > 0);
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
            require_true(engine.create(document1).has_value());
            dump(engine);
            require_true(engine.create(document2).has_value());
            require_true(engine.create(document3).has_value());
            engine.collection("C2");
            require_true(engine.create(document4).has_value());
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

        section("ReadByIndexedDuplicateKey") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"status"s, "active"s}, {"name"s, "alpha"s}},
                document2 = object{{"status"s, "active"s}, {"name"s, "beta"s}},
                document3 = object{{"status"s, "pending"s}, {"name"s, "gamma"s}},
                selector = object{{"status"s, "active"s}},
                documents = object{};
            engine.collection("ReadByIndexedDuplicateKey"s);
            require_true(engine.index({"status"s}).has_value());
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());
            require_true(engine.create(document3).has_value());
            require_true(engine.read(selector, documents));
            require_eq(2u, documents.size());
            require_true(documents[0].match(document1) || documents[0].match(document2));
            require_true(documents[1].match(document1) || documents[1].match(document2));
            require_false(documents[0].match(documents[1]));
        };

        section("ReadCountRangeOnIndexedField") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"age"s, 20ll}, {"name"s, "young"s}},
                document2 = object{{"age"s, 30ll}, {"name"s, "mid"s}},
                document3 = object{{"age"s, 40ll}, {"name"s, "old"s}},
                over_25 = object{{"age"s, object{{"$gt"s, 25ll}}}},
                documents = object{};
            engine.collection("ReadCountRange"s);
            require_true(engine.index({"age"s}).has_value());
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());
            require_true(engine.create(document3).has_value());

            require_eq(engine.count(over_25), 2u);
            require_true(engine.read(over_25, documents));
            require_eq(documents.get<object::array>().size(), 2u);
        };

        section("TypedNumericIndexSurvivesRestart") = []
        {
            const auto test_file = "./engine_typed_index_restart_test.db";
            const auto setup = fixture{test_file};
            {
                auto engine = yar::db::engine{test_file};
                engine.collection("TypedIndexRestart"s);
                require_true(engine.index({"value"s}).has_value());
                for(const auto value : {2ll, 9ll, 10ll, 100ll})
                {
                    auto document = object{{"value"s, value}};
                    require_true(engine.create(document).has_value());
                }
            }

            auto engine = yar::db::engine{test_file};
            engine.collection("TypedIndexRestart"s);
            const auto over_9 = object{{"value"s, object{{"$gt"s, 9ll}}}};
            auto documents = object{};
            require_eq(engine.count(over_9), 2u);
            require_true(engine.read(over_9, documents));
            require_eq(documents.size(), 2u);
        };

        section("ReadCountInSelector") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"status"s, "active"s}},
                document2 = object{{"status"s, "pending"s}},
                document3 = object{{"status"s, "inactive"s}},
                in_map = object{{"0"s, "active"s}, {"1"s, "pending"s}},
                in_selector = object{{"status"s, object{{"$in"s, in_map}}}},
                documents = object{};
            engine.collection("ReadCountIn"s);
            require_true(engine.index({"status"s}).has_value());
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());
            require_true(engine.create(document3).has_value());

            require_eq(engine.count(in_selector), 2u);
            require_true(engine.read(in_selector, documents));
            require_eq(documents.get<object::array>().size(), 2u);
        };

        section("ReplaceExistingDocument") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto original = object{{"name"s, "before"s}, {"value"s, 1ll}},
                replacement = object{{"name"s, "after"s}, {"value"s, 2ll}},
                selector = object{},
                documents = object{};
            engine.collection("Replace"s);
            require_true(engine.create(original).has_value());
            selector = object{{"_id"s, original["_id"s]}};
            replacement["_id"s] = original["_id"s];

            require_true(engine.replace(selector, replacement).value() > 0);
            require_true(engine.read(selector, documents));
            require_eq(documents.get<object::array>().size(), 1u);
            require_eq(documents[0]["name"s].get<string>(), "after"s);
            require_eq(static_cast<xson::integer_type>(documents[0]["value"s]), 2ll);
        };

        section("ReindexDiscoversExistingDocuments") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"tag"s, "alpha"s}, {"value"s, 1ll}},
                document2 = object{{"tag"s, "beta"s}, {"value"s, 2ll}},
                selector = object{{"tag"s, "alpha"s}},
                documents = object{};
            engine.collection("Reindex"s);
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());
            require_true(engine.index({"tag"s}).has_value());
            require_true(engine.reindex().has_value());

            require_true(engine.read(selector, documents));
            require_eq(documents.get<object::array>().size(), 1u);
            require_eq(documents[0]["value"s], xson::integer_type{1});
        };

        section("IndexOnlyCount") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"status"s, "active"s}, {"name"s, "alpha"s}},
                document2 = object{{"status"s, "active"s}, {"name"s, "beta"s}},
                document3 = object{{"status"s, "pending"s}, {"name"s, "gamma"s}},
                active = object{{"status"s, "active"s}},
                not_deleted = object{{"status"s, object{{"$ne"s, "deleted"s}}}},
                documents = object{};
            engine.collection("IndexOnlyCount"s);
            require_true(engine.index({"status"s}).has_value());
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());
            require_true(engine.create(document3).has_value());

            require_eq(engine.count(object{}), 3u);
            require_eq(engine.count(active), 2u);

            require_true(engine.read(active, documents));
            require_eq(engine.count(active), documents.get<object::array>().size());

            require_eq(engine.count(not_deleted), 3u);
        };

        section("CreateWriteFailureRollsBack") = []
        {
            const auto test_file = "./engine_create_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document = object{{"value"s, 1ll}},
                documents = object{};
            engine.collection("CreateFailure"s);

            fail_next_write(engine);
            const auto result = engine.create(document);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_false(engine.read(object{}, documents));
            require_eq(documents.size(), 0u);
        };

        section("UpdateWriteFailureRollsBack") = []
        {
            const auto test_file = "./engine_update_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"name"s, "first"s}, {"value"s, 1ll}},
                document2 = object{{"name"s, "second"s}, {"value"s, 1ll}},
                updates = object{{"value"s, 2ll}},
                selector = object{},
                documents = object{};
            engine.collection("UpdateFailure"s);
            require_true(engine.create(document1).has_value());
            require_true(engine.create(document2).has_value());

            fail_next_write(engine);
            const auto result = engine.update(selector, updates);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_true(engine.read(selector, documents));
            require_eq(documents.size(), 2u);
            require_eq(documents[0]["value"s], xson::integer_type{1});
            require_eq(documents[1]["value"s], xson::integer_type{1});
        };

        section("UpdateStatusFailureRollsBack") = []
        {
            const auto test_file = "./engine_update_status_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document = object{{"value"s, 1ll}},
                updates = object{{"value"s, 2ll}},
                documents = object{},
                history = object{};
            engine.collection("UpdateStatusFailure"s);
            require_true(engine.create(document).has_value());
            const auto selector = object{{"_id"s, document["_id"s]}};
            const auto original_size = std::filesystem::file_size(test_file);

            fail_write(engine, 2);
            const auto result = engine.update(selector, updates);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_eq(std::filesystem::file_size(test_file), original_size);
            require_true(engine.read(selector, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["value"s], xson::integer_type{1});
            require_true(engine.history(selector, history));
            require_eq(history.size(), 1u);
        };

        section("DestroyWriteFailureRollsBack") = []
        {
            const auto test_file = "./engine_destroy_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document = object{{"value"s, 1ll}},
                selector = object{},
                documents = object{};
            engine.collection("DestroyFailure"s);
            require_true(engine.create(document).has_value());

            fail_next_write(engine);
            const auto result = engine.destroy(selector);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_true(engine.read(selector, documents));
            require_eq(documents.size(), 1u);
        };

        section("ReplaceWriteFailureRollsBack") = []
        {
            const auto test_file = "./engine_replace_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto original = object{{"value"s, 1ll}},
                replacement = object{{"value"s, 2ll}},
                documents = object{};
            engine.collection("ReplaceFailure"s);
            require_true(engine.create(original).has_value());
            const auto selector = object{{"_id"s, original["_id"s]}};
            replacement["_id"s] = original["_id"s];

            fail_next_write(engine);
            const auto result = engine.replace(selector, replacement);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_true(engine.read(selector, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["value"s], xson::integer_type{1});
        };

        section("IndexWriteFailureRollsBack") = []
        {
            const auto test_file = "./engine_index_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            engine.collection("IndexFailure"s);

            fail_next_write(engine);
            const auto result = engine.index({"status"s});

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_true(engine.indexed_keys().empty());
        };

        section("UpsertCreateReturnsCreatedDocument") = []
        {
            const auto test_file = "./engine_upsert_create_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document = object{{"name"s, "created"s}},
                documents = object{};
            engine.collection("UpsertCreate"s);

            const auto result = engine.upsert(object{{"_id"s, 42ll}}, document, documents);

            require_eq(result.value(), 1u);
            require_eq(documents.size(), 1u);
            require_true(documents[0].match(document));
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
            require_true(engine.index({"A", "B", "Z"}).has_value());
            require_true(engine.upsert(document1, document1).has_value());
            require_true(engine.upsert(document2, document2).has_value());
            require_true(engine.upsert(document3, document3).has_value());
            dump(engine);
            require_true(engine.destroy(selector, documents).has_value());
            dump(engine);
            require_true(engine.index({"D", "1", "2"}).has_value());
            require_true(engine.reindex().has_value());
        };
    };
    return true;
}

const auto test_registrar = test_set();

}
