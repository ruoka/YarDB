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

void dump(yar::db::engine& engine, std::string_view collection)
{
    auto all = object{}, documents = object{};
    engine.read(collection, all, documents);
    clog << "Dump " << collection << ":\n" << json::stringify(documents) << endl;
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
            require_true(engine.create("Create1"s, document).has_value());
            dump(engine, "Create1"s);
            require_true(engine.read("Create1"s, selector, documents));
            require_true(1u == documents.size());
            std::clog << json::stringify(documents[0]) << std::endl;
            require_true(documents[0].match(document));
        };

        section("StorageLockRejectsConcurrentOpen") = []
        {
            const auto test_file = "./engine_lock_concurrent_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};

            require_throws([test_file]
            {
                auto second = yar::db::engine{test_file};
            });
        };

        section("StorageLockReleasesOnDestruction") = []
        {
            const auto test_file = "./engine_lock_release_test.db";
            const auto setup = fixture{test_file};
            {
                auto engine = yar::db::engine{test_file};
                require_true(std::filesystem::exists(test_file + ".pid"s));
            }

            require_false(std::filesystem::exists(test_file + ".pid"s));
            auto reopened = yar::db::engine{test_file};
        };

        section("StorageLockOwnershipMovesWithEngine") = []
        {
            const auto test_file = "./engine_lock_move_test.db";
            const auto setup = fixture{test_file};
            {
                auto original = yar::db::engine{test_file};
                auto moved = std::move(original);
                require_throws([test_file]
                {
                    auto second = yar::db::engine{test_file};
                });
            }

            auto reopened = yar::db::engine{test_file};
        };

        section("ExistingStorageLockRequiresManualRecovery") = []
        {
            const auto test_file = "./engine_lock_stale_test.db";
            const auto setup = fixture{test_file};
            const auto lock_file = test_file + ".pid"s;
            {
                auto stale = std::ofstream{lock_file};
                stale << "stale\n";
            }

            require_throws([test_file]
            {
                auto engine = yar::db::engine{test_file};
            });

            require_true(std::filesystem::remove(lock_file));
            auto recovered = yar::db::engine{test_file};
        };

        section("TruncatedTailRecoversLastCompleteRecord") = []
        {
            const auto test_file = "./engine_truncated_tail_test.db";
            const auto setup = fixture{test_file};
            auto last_complete = std::uintmax_t{0};
            {
                auto engine = yar::db::engine{test_file};
                auto first = object{{"value"s, 1ll}},
                    second = object{{"value"s, 2ll}};
                require_true(engine.create("TruncatedTail"s, first).has_value());
                last_complete = std::filesystem::file_size(test_file);
                require_true(engine.create("TruncatedTail"s, second).has_value());
            }

            const auto truncated_size = std::filesystem::file_size(test_file) - 4;
            std::filesystem::resize_file(test_file, truncated_size);

            auto recovered = yar::db::engine{test_file};
            auto documents = object{};
            require_true(recovered.read("TruncatedTail"s, object{}, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["value"s], xson::integer_type{1});
            require_eq(std::filesystem::file_size(test_file), last_complete);
        };

        section("InvalidRecordStatusFailsClosed") = []
        {
            const auto test_file = "./engine_corrupt_status_test.db";
            const auto setup = fixture{test_file};
            {
                auto engine = yar::db::engine{test_file};
                auto document = object{{"value"s, 1ll}};
                require_true(engine.create("CorruptStatus"s, document).has_value());
            }

            const auto original_size = std::filesystem::file_size(test_file);
            {
                auto file = std::fstream{test_file, std::ios::in | std::ios::out | std::ios::binary};
                file.seekp(0);
                file.put('X');
                file.flush();
            }

            require_throws([test_file]
            {
                auto engine = yar::db::engine{test_file};
            });
            require_eq(std::filesystem::file_size(test_file), original_size);
        };

        section("Create9") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            for(auto i = 1u; i < 10u; ++i)
            {
                auto document = object{{"A"s, 1}, {"B"s, 2}, {"C"s, 3}},
                    all = object{},
                    documents = object{};
                require_true(engine.create("Create9"s, document).has_value());
                dump(engine, "Create9"s);
                require_true(engine.read("Create9"s, all, documents));
                require_true(i == documents.size());
                require_true(documents[i-1].match(document));
            }
        };

        section("ReadEmptyCollection") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto selector = object{}, documents = object{};
            require_false(engine.read("ReadEmptyCollection"s, selector, documents));
            dump(engine, "ReadEmptyCollection"s);
            require_true(0u == documents.size());
        };

        section("UpdateEmptyCollection") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto selector = object{}, document = object{}, documents = object{};
            require_eq(engine.update("UpdateEmptyCollection"s, selector, document).value(), 0u);
            require_false(engine.read("UpdateEmptyCollection"s, selector, documents));
            dump(engine, "UpdateEmptyCollection"s);
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
            require_true(engine.create("Update1ByID"s, document1).has_value());
            dump(engine, "Update1ByID"s);
            require_true(engine.update("Update1ByID"s, selector, document2).value() > 0);
            dump(engine, "Update1ByID"s);
            require_true(engine.read("Update1ByID"s, selector, documents));
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
            require_true(engine.create("Update2ByValue"s, document1).has_value());
            require_true(engine.create("Update2ByValue"s, document2).has_value());
            require_true(engine.create("Update2ByValue"s, document3).has_value());
            dump(engine, "Update2ByValue"s);
            require_true(engine.read("Update2ByValue"s, selector, documents));
            require_true(3u == documents.size());
            selector = object{"A"s, 1};
            require_true(engine.update("Update2ByValue"s, selector, document4).value() > 0);
            dump(engine, "Update2ByValue"s);
            selector = object{};
            documents = object{};
            require_true(engine.read("Update2ByValue"s, selector, documents));
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
            require_true(engine.create("Update1ByKey"s, document1).has_value());
            require_true(engine.create("Update1ByKey"s, document2).has_value());
            require_true(engine.create("Update1ByKey"s, document3).has_value());
            dump(engine, "Update1ByKey"s);
            require_true(engine.read("Update1ByKey"s, selector, documents));
            require_true(3u == documents.size());
            selector = object{"A"s, 1};
            require_true(engine.update("Update1ByKey"s, selector, document4).value() > 0);
            dump(engine, "Update1ByKey"s);
            selector = object{};
            documents = object{};
            require_true(engine.read("Update1ByKey"s, selector, documents));
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
            require_eq(engine.destroy("DestroyEmptyCollection"s, selector, documents).value(), 0u);
            dump(engine, "DestroyEmptyCollection"s);
            require_false(engine.read("DestroyEmptyCollection"s, selector, documents));
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
            require_true(engine.create("Destroy1ByID"s, document1).has_value());
            require_true(engine.create("Destroy1ByID"s, document2).has_value());
            require_true(engine.create("Destroy1ByID"s, document3).has_value());
            dump(engine, "Destroy1ByID"s);
            require_true(engine.read("Destroy1ByID"s, all, documents));
            require_true(3u == documents.size());
            xson::integer_type id = documents[1]["_id"s];
            auto selector = object{"_id"s, id};
            documents = object{};
            require_true(engine.destroy("Destroy1ByID"s, selector, documents).value() > 0);
            dump(engine, "Destroy1ByID"s);
            documents = object{};
            require_true(engine.read("Destroy1ByID"s, all, documents));
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
            require_true(engine.create("Destroy2ByValue"s, document1).has_value());
            require_true(engine.create("Destroy2ByValue"s, document2).has_value());
            require_true(engine.create("Destroy2ByValue"s, document3).has_value());
            dump(engine, "Destroy2ByValue"s);
            require_true(engine.read("Destroy2ByValue"s, selector, documents));
            require_true(3 == documents.size());
            selector = object{"A"s, 1};
            documents = object{};
            require_true(engine.destroy("Destroy2ByValue"s, selector, documents).value() > 0);
            dump(engine, "Destroy2ByValue"s);
            selector = object{};
            documents = object{};
            engine.read("Destroy2ByValue"s, selector, documents);
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
            require_true(engine.create("History"s, document1).has_value());
            dump(engine, "History"s);
            require_true(engine.update("History"s, selector, document2).value() > 0);
            dump(engine, "History"s);
            require_true(engine.update("History"s, selector, document3).value() > 0);
            dump(engine, "History"s);

            auto documents = object{};
            require_true(engine.read("History"s, all, documents));
            require_true(1u == documents.size());

            auto history = object{};
            require_true(engine.history("History"s, selector, history));
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
            require_true(engine.create("C1", document1).has_value());
            dump(engine, "C1");
            require_true(engine.create("C1", document2).has_value());
            require_true(engine.create("C1", document3).has_value());
            require_true(engine.create("C2", document4).has_value());
            dump(engine, "C2");
            dump(engine, "C2");
            engine.read("C1", all, documents);
            require_true(3u == documents.size());
            require_true(documents[0].match(document1));
            require_true(documents[1].match(document2));
            require_true(documents[2].match(document3));
            documents = object{};
            engine.read("C2", all, documents);
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
            require_true(engine.index("ReadByIndexedDuplicateKey"s, {"status"s}).has_value());
            require_true(engine.create("ReadByIndexedDuplicateKey"s, document1).has_value());
            require_true(engine.create("ReadByIndexedDuplicateKey"s, document2).has_value());
            require_true(engine.create("ReadByIndexedDuplicateKey"s, document3).has_value());
            require_true(engine.read("ReadByIndexedDuplicateKey"s, selector, documents));
            require_eq(2u, documents.size());
            require_true(documents[0].match(document1) or documents[0].match(document2));
            require_true(documents[1].match(document1) or documents[1].match(document2));
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
            require_true(engine.index("ReadCountRange"s, {"age"s}).has_value());
            require_true(engine.create("ReadCountRange"s, document1).has_value());
            require_true(engine.create("ReadCountRange"s, document2).has_value());
            require_true(engine.create("ReadCountRange"s, document3).has_value());

            require_eq(engine.count("ReadCountRange"s, over_25), 2u);
            require_true(engine.read("ReadCountRange"s, over_25, documents));
            require_eq(documents.get<object::array>().size(), 2u);
        };

        section("ReadOrderByFieldIgnoresIdOrder") = [test_file]
        {
            // Insert so _id order is 30, 10, 20 — field order must not follow _id.
            auto engine = yar::db::engine{test_file};
            auto high = object{{"value"s, 30ll}, {"name"s, "high"s}},
                low = object{{"value"s, 10ll}, {"name"s, "low"s}},
                mid = object{{"value"s, 20ll}, {"name"s, "mid"s}},
                documents = object{};
            require_true(engine.create("OrderByField"s, high).has_value());
            require_true(engine.create("OrderByField"s, low).has_value());
            require_true(engine.create("OrderByField"s, mid).has_value());

            auto asc = object{{"$orderby"s, "value"s}};
            require_true(engine.read("OrderByField"s, asc, documents));
            require_eq(documents.size(), 3u);
            require_eq(static_cast<xson::integer_type>(documents[0]["value"s]), 10);
            require_eq(static_cast<xson::integer_type>(documents[1]["value"s]), 20);
            require_eq(static_cast<xson::integer_type>(documents[2]["value"s]), 30);

            auto desc = object{{"$orderby"s, "value"s}, {"$desc"s, true}};
            documents = object{};
            require_true(engine.read("OrderByField"s, desc, documents));
            require_eq(static_cast<xson::integer_type>(documents[0]["value"s]), 30);
            require_eq(static_cast<xson::integer_type>(documents[1]["value"s]), 20);
            require_eq(static_cast<xson::integer_type>(documents[2]["value"s]), 10);

            auto top_desc = object{{"$orderby"s, "value"s}, {"$desc"s, true}, {"$top"s, 2ll}};
            documents = object{};
            require_true(engine.read("OrderByField"s, top_desc, documents));
            require_eq(documents.size(), 2u);
            require_eq(static_cast<xson::integer_type>(documents[0]["value"s]), 30);
            require_eq(static_cast<xson::integer_type>(documents[1]["value"s]), 20);

            auto skip_asc = object{{"$orderby"s, "value"s}, {"$skip"s, 1ll}};
            documents = object{};
            require_true(engine.read("OrderByField"s, skip_asc, documents));
            require_eq(documents.size(), 2u);
            require_eq(static_cast<xson::integer_type>(documents[0]["value"s]), 20);
            require_eq(static_cast<xson::integer_type>(documents[1]["value"s]), 30);
        };

        section("ReadTopZeroReturnsEmpty") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"name"s, "a"s}},
                document2 = object{{"name"s, "b"s}},
                document3 = object{{"name"s, "c"s}},
                documents = object{};
            require_true(engine.create("TopZero"s, document1).has_value());
            require_true(engine.create("TopZero"s, document2).has_value());
            require_true(engine.create("TopZero"s, document3).has_value());

            auto top_zero = object{{"$top"s, 0ll}};
            require_false(engine.read("TopZero"s, top_zero, documents));
            require_true(documents.is_array());
            require_eq(documents.get<object::array>().size(), 0u);

            // Collection is intact — $top=0 must not dump every row.
            documents = object{};
            require_true(engine.read("TopZero"s, object{}, documents));
            require_eq(documents.get<object::array>().size(), 3u);
        };

        section("DestroyTopZeroDeletesNothing") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"name"s, "a"s}},
                document2 = object{{"name"s, "b"s}},
                documents = object{};
            require_true(engine.create("DestroyTopZero"s, document1).has_value());
            require_true(engine.create("DestroyTopZero"s, document2).has_value());

            auto top_zero = object{{"$top"s, 0ll}};
            require_eq(engine.destroy("DestroyTopZero"s, top_zero, documents).value(), 0u);
            require_eq(documents.get<object::array>().size(), 0u);

            documents = object{};
            require_true(engine.read("DestroyTopZero"s, object{}, documents));
            require_eq(documents.get<object::array>().size(), 2u);
        };

        section("Utf8StringSurvivesRestart") = []
        {
            // FAST string terminator cleared high bits; without escape/unescape,
            // UTF-8 corrupted on disk and failed reopen/validate_and_recover.
            const auto test_file = "./engine_utf8_restart_test.db";
            std::remove(test_file);
            std::remove((std::string{test_file} + ".pid").c_str());

            {
                auto engine = yar::db::engine{test_file};
                auto document = object{
                    {"name"s, "café"s},
                    {"note"s, "Hello 世界 🌍"s}
                };
                require_true(engine.create("Utf8"s, document).has_value());

                auto documents = object{};
                require_true(engine.read("Utf8"s, object{}, documents));
                require_eq(documents.get<object::array>().size(), 1u);
                require_eq(documents[0]["name"s].get<string>(), "café"s);
                require_eq(documents[0]["note"s].get<string>(), "Hello 世界 🌍"s);
            }

            {
                auto engine = yar::db::engine{test_file};
                auto documents = object{};
                require_true(engine.read("Utf8"s, object{}, documents));
                require_eq(documents.get<object::array>().size(), 1u);
                require_eq(documents[0]["name"s].get<string>(), "café"s);
                require_eq(documents[0]["note"s].get<string>(), "Hello 世界 🌍"s);
            }

            std::remove(test_file);
            std::remove((std::string{test_file} + ".pid").c_str());
        };

        section("TypedNumericIndexSurvivesRestart") = []
        {
            const auto test_file = "./engine_typed_index_restart_test.db";
            const auto setup = fixture{test_file};
            {
                auto engine = yar::db::engine{test_file};
                require_true(engine.index("TypedIndexRestart"s, {"value"s}).has_value());
                for(const auto value : {2ll, 9ll, 10ll, 100ll})
                {
                    auto document = object{{"value"s, value}};
                    require_true(engine.create("TypedIndexRestart"s, document).has_value());
                }
            }

            auto engine = yar::db::engine{test_file};
            const auto over_9 = object{{"value"s, object{{"$gt"s, 9ll}}}};
            auto documents = object{};
            require_eq(engine.count("TypedIndexRestart"s, over_9), 2u);
            require_true(engine.read("TypedIndexRestart"s, over_9, documents));
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
            require_true(engine.index("ReadCountIn"s, {"status"s}).has_value());
            require_true(engine.create("ReadCountIn"s, document1).has_value());
            require_true(engine.create("ReadCountIn"s, document2).has_value());
            require_true(engine.create("ReadCountIn"s, document3).has_value());

            require_eq(engine.count("ReadCountIn"s, in_selector), 2u);
            require_true(engine.read("ReadCountIn"s, in_selector, documents));
            require_eq(documents.get<object::array>().size(), 2u);
        };

        section("ReplaceExistingDocument") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto original = object{{"name"s, "before"s}, {"value"s, 1ll}},
                replacement = object{{"name"s, "after"s}, {"value"s, 2ll}},
                selector = object{},
                documents = object{};
            require_true(engine.create("Replace"s, original).has_value());
            selector = object{{"_id"s, original["_id"s]}};
            replacement["_id"s] = original["_id"s];

            require_true(engine.replace("Replace"s, selector, replacement).value() > 0);
            require_true(engine.read("Replace"s, selector, documents));
            require_eq(documents.get<object::array>().size(), 1u);
            require_eq(documents[0]["name"s].get<string>(), "after"s);
            require_eq(static_cast<xson::integer_type>(documents[0]["value"s]), 2ll);
        };

        section("ReplaceRejectsDuplicatePrimaryKey") = []
        {
            // replace used to overwrite m_primary_keys[id] and orphan the victim.
            const auto test_file = "./engine_replace_duplicate_id_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto first = object{{"name"s, "keep"s}},
                second = object{{"name"s, "victim"s}},
                documents = object{};
            require_true(engine.create("ReplaceDupId"s, first).has_value());
            require_true(engine.create("ReplaceDupId"s, second).has_value());

            auto clobber = object{
                {"_id"s, second["_id"s]},
                {"name"s, "clobber"s}};
            const auto selector = object{{"_id"s, first["_id"s]}};
            const auto result = engine.replace("ReplaceDupId"s, selector, clobber);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::conflict);
            require_true(engine.read("ReplaceDupId"s, object{}, documents));
            require_eq(documents.size(), 2u);
            require_true(engine.read("ReplaceDupId"s, object{{"_id"s, second["_id"s]}}, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["name"s].get<string>(), "victim"s);
        };

        section("ReplacePreservesHistoryChain") = [test_file]
        {
            // create → update → replace must keep a walkable history (not sever
            // previous or mark the prior live record as deleted).
            auto engine = yar::db::engine{test_file};
            auto original = object{{"name"s, "v1"s}, {"value"s, 1ll}},
                patch = object{{"name"s, "v2"s}},
                replacement = object{{"name"s, "v3"s}, {"value"s, 3ll}},
                selector = object{},
                history = object{};
            require_true(engine.create("ReplaceHistory"s, original).has_value());
            selector = object{{"_id"s, original["_id"s]}};
            require_true(engine.update("ReplaceHistory"s, selector, patch).value() > 0);
            replacement["_id"s] = original["_id"s];
            require_true(engine.replace("ReplaceHistory"s, selector, replacement).value() > 0);

            require_true(engine.history("ReplaceHistory"s, selector, history));
            require_eq(history.size(), 3u);
            require_eq(history[0]["name"s].get<string>(), "v3"s);
            require_eq(history[1]["name"s].get<string>(), "v2"s);
            require_eq(history[2]["name"s].get<string>(), "v1"s);
        };

        section("ReindexDiscoversExistingDocuments") = [test_file]
        {
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"tag"s, "alpha"s}, {"value"s, 1ll}},
                document2 = object{{"tag"s, "beta"s}, {"value"s, 2ll}},
                selector = object{{"tag"s, "alpha"s}},
                documents = object{};
            require_true(engine.create("Reindex"s, document1).has_value());
            require_true(engine.create("Reindex"s, document2).has_value());
            require_true(engine.index("Reindex"s, {"tag"s}).has_value());
            require_true(engine.reindex().has_value());

            require_true(engine.read("Reindex"s, selector, documents));
            require_eq(documents.get<object::array>().size(), 1u);
            require_eq(documents[0]["value"s], xson::integer_type{1});
        };

        section("NestedSecondaryIndexDoesNotBrickRestart") = []
        {
            // Indexing an object/array field used to throw bad_variant_access from
            // make_secondary_key after the create flush — and again in populate_indexes
            // on reopen, leaving the database unopenable.
            const auto test_file = "./engine_nested_secondary_restart.db";
            std::remove(test_file);
            std::remove((std::string{test_file} + ".pid").c_str());

            {
                auto engine = yar::db::engine{test_file};
                auto nested = object{
                    {"address"s, object{{"city"s, "NYC"s}}},
                    {"name"s, "nested"s}
                };
                auto primitive = object{{"address"s, "plain"s}, {"name"s, "flat"s}};
                require_true(engine.create("NestedIdx"s, nested).has_value());
                require_true(engine.index("NestedIdx"s, {"address"s}).has_value());
                require_true(engine.reindex().has_value());
                require_true(engine.create("NestedIdx"s, primitive).has_value());

                auto by_plain = object{{"address"s, "plain"s}};
                auto documents = object{};
                require_true(engine.read("NestedIdx"s, by_plain, documents));
                require_eq(documents.size(), 1u);
                require_eq(documents[0]["name"s].get<string>(), "flat"s);
            }

            {
                auto engine = yar::db::engine{test_file};
                auto all = object{};
                auto documents = object{};
                require_true(engine.read("NestedIdx"s, all, documents));
                require_eq(documents.size(), 2u);

                auto by_plain = object{{"address"s, "plain"s}};
                documents = object{};
                require_true(engine.read("NestedIdx"s, by_plain, documents));
                require_eq(documents.size(), 1u);
            }

            std::remove(test_file);
            std::remove((std::string{test_file} + ".pid").c_str());
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
            require_true(engine.index("IndexOnlyCount"s, {"status"s}).has_value());
            require_true(engine.create("IndexOnlyCount"s, document1).has_value());
            require_true(engine.create("IndexOnlyCount"s, document2).has_value());
            require_true(engine.create("IndexOnlyCount"s, document3).has_value());

            require_eq(engine.count("IndexOnlyCount"s, object{}), 3u);
            require_eq(engine.count("IndexOnlyCount"s, active), 2u);

            require_true(engine.read("IndexOnlyCount"s, active, documents));
            require_eq(engine.count("IndexOnlyCount"s, active), documents.get<object::array>().size());

            require_eq(engine.count("IndexOnlyCount"s, not_deleted), 3u);
        };

        section("CreateWriteFailureRollsBack") = []
        {
            const auto test_file = "./engine_create_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document = object{{"value"s, 1ll}},
                documents = object{};

            fail_next_write(engine);
            const auto result = engine.create("CreateFailure"s, document);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_false(engine.read("CreateFailure"s, object{}, documents));
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
            require_true(engine.create("UpdateFailure"s, document1).has_value());
            require_true(engine.create("UpdateFailure"s, document2).has_value());

            fail_next_write(engine);
            const auto result = engine.update("UpdateFailure"s, selector, updates);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_true(engine.read("UpdateFailure"s, selector, documents));
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
            require_true(engine.create("UpdateStatusFailure"s, document).has_value());
            const auto selector = object{{"_id"s, document["_id"s]}};
            const auto original_size = std::filesystem::file_size(test_file);

            fail_write(engine, 2);
            const auto result = engine.update("UpdateStatusFailure"s, selector, updates);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_eq(std::filesystem::file_size(test_file), original_size);
            require_true(engine.read("UpdateStatusFailure"s, selector, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["value"s], xson::integer_type{1});
            require_true(engine.history("UpdateStatusFailure"s, selector, history));
            require_eq(history.size(), 1u);
        };

        section("UpdateStatusRestoreFailureCommitsDurableUpdate") = []
        {
            // Successors are flushed, then status restore fails. Reporting
            // rollback_failure would claim the update failed while live reads
            // keep serving tombstoned pre-images and reopen flips to the new
            // value — commit the staged index so API and disk agree.
            const auto test_file = "./engine_update_status_restore_failure_test.db";
            const auto setup = fixture{test_file};
            const auto size_before_update = [&]
            {
                auto engine = yar::db::engine{test_file};
                auto document = object{{"value"s, 1ll}},
                    updates = object{{"value"s, 2ll}},
                    documents = object{};
                require_true(engine.create("UpdateStatusRestoreFailure"s, document).has_value());
                const auto selector = object{{"_id"s, document["_id"s]}};
                const auto size_before = std::filesystem::file_size(test_file);

                fail_write(engine, 2);
                fail_next_rollback_status(engine);
                const auto result = engine.update("UpdateStatusRestoreFailure"s, selector, updates);

                require_true(result.has_value());
                require_eq(result.value(), 1u);
                require_true(std::filesystem::file_size(test_file) > size_before);
                require_true(engine.read("UpdateStatusRestoreFailure"s, selector, documents));
                require_eq(documents.size(), 1u);
                require_eq(documents[0]["value"s], xson::integer_type{2});
                // Writes must remain enabled — durable update won.
                auto again = object{{"value"s, 3ll}};
                require_true(engine.update("UpdateStatusRestoreFailure"s, selector, again).has_value());
                return size_before;
            }();
            require_true(size_before_update > 0);

            auto reopened = yar::db::engine{test_file};
            auto documents = object{};
            require_true(reopened.read("UpdateStatusRestoreFailure"s, object{}, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["value"s], xson::integer_type{3});
        };

        section("DestroyWriteFailureRollsBack") = []
        {
            const auto test_file = "./engine_destroy_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document = object{{"value"s, 1ll}},
                selector = object{},
                documents = object{};
            require_true(engine.create("DestroyFailure"s, document).has_value());

            fail_next_write(engine);
            const auto result = engine.destroy("DestroyFailure"s, selector);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_true(engine.read("DestroyFailure"s, selector, documents));
            require_eq(documents.size(), 1u);
        };

        section("ReplaceStatusRestoreFailureCommitsDurableReplace") = []
        {
            // Replacement is flushed, then status restore fails. Reporting
            // rollback_failure would claim replace failed while live reads keep
            // serving the tombstoned pre-image — commit the staged index.
            const auto test_file = "./engine_replace_status_restore_failure_test.db";
            const auto setup = fixture{test_file};
            [&]
            {
                auto engine = yar::db::engine{test_file};
                auto document = object{{"value"s, 1ll}},
                    documents = object{};
                require_true(engine.create("ReplaceStatusRestoreFailure"s, document).has_value());
                const auto selector = object{{"_id"s, document["_id"s]}};
                auto replacement = object{{"value"s, 9ll}};

                fail_write(engine, 2);
                fail_next_rollback_status(engine);
                const auto result = engine.replace(
                    "ReplaceStatusRestoreFailure"s,
                    selector,
                    replacement);

                require_true(result.has_value());
                require_eq(result.value(), 1u);
                require_true(engine.read("ReplaceStatusRestoreFailure"s, selector, documents));
                require_eq(documents.size(), 1u);
                require_eq(documents[0]["value"s], xson::integer_type{9});
            }();

            auto reopened = yar::db::engine{test_file};
            auto documents = object{};
            require_true(reopened.read("ReplaceStatusRestoreFailure"s, object{}, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["value"s], xson::integer_type{9});
        };

        section("DestroyStatusRestoreFailureCommitsDurableDelete") = []
        {
            // Delete markers are flushed, then status restore fails. Reporting
            // rollback_failure would claim the delete failed while reopen skips
            // tombstones — commit the durable delete instead.
            const auto test_file = "./engine_destroy_status_restore_failure_test.db";
            const auto setup = fixture{test_file};
            const auto deleted_id = [&]
            {
                auto engine = yar::db::engine{test_file};
                auto document = object{{"value"s, 1ll}};
                require_true(engine.create("DestroyStatusRestoreFailure"s, document).has_value());
                const auto id = static_cast<xson::integer_type>(document["_id"s]);
                const auto selector = object{{"_id"s, document["_id"s]}};

                fail_write(engine, 1);
                fail_next_rollback_status(engine);
                const auto result = engine.destroy("DestroyStatusRestoreFailure"s, selector);

                require_true(result.has_value());
                require_eq(result.value(), 1u);
                auto documents = object{};
                require_false(engine.read("DestroyStatusRestoreFailure"s, selector, documents));
                return id;
            }();

            auto reopened = yar::db::engine{test_file};
            auto documents = object{};
            require_false(reopened.read(
                "DestroyStatusRestoreFailure"s,
                object{{"_id"s, deleted_id}},
                documents));
            auto successor = object{{"value"s, 2ll}};
            require_true(reopened.create(
                "DestroyStatusRestoreFailure"s,
                successor).has_value());
        };

        section("ReplaceWriteFailureRollsBack") = []
        {
            const auto test_file = "./engine_replace_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto original = object{{"value"s, 1ll}},
                replacement = object{{"value"s, 2ll}},
                documents = object{};
            require_true(engine.create("ReplaceFailure"s, original).has_value());
            const auto selector = object{{"_id"s, original["_id"s]}};
            replacement["_id"s] = original["_id"s];

            fail_next_write(engine);
            const auto result = engine.replace("ReplaceFailure"s, selector, replacement);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_true(engine.read("ReplaceFailure"s, selector, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["value"s], xson::integer_type{1});
        };

        section("IndexWriteFailureRollsBack") = []
        {
            const auto test_file = "./engine_index_failure_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};

            fail_next_write(engine);
            const auto result = engine.index("IndexFailure"s, {"status"s});

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::io_failure);
            require_true(engine.indexed_keys("IndexFailure"s).empty());
        };

        section("UpsertCreateReturnsCreatedDocument") = []
        {
            const auto test_file = "./engine_upsert_create_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document = object{{"name"s, "created"s}},
                documents = object{};

            const auto result = engine.upsert("UpsertCreate"s, object{{"_id"s, 42ll}}, document, documents);

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
            require_true(engine.index("Create2Keys", {"A", "B", "Z"}).has_value());
            require_true(engine.upsert("Create2Keys", document1, document1).has_value());
            require_true(engine.upsert("Create2Keys", document2, document2).has_value());
            require_true(engine.upsert("Create2Keys", document3, document3).has_value());
            dump(engine, "Create2Keys");
            require_true(engine.destroy("Create2Keys", selector, documents).has_value());
            dump(engine, "Create2Keys");
            require_true(engine.index("Create2Keys", {"D", "1", "2"}).has_value());
            require_true(engine.reindex().has_value());
        };

        section("ConcurrentReadersObserveStableCount") = []
        {
            const auto test_file = "./engine_concurrent_read_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            constexpr auto collection = "ConcurrentRead"s;
            for(auto i = 0; i < 10; ++i)
            {
                auto document = object{{"value"s, i}};
                require_true(engine.create(collection, document).has_value());
            }

            auto failures = std::atomic<int>{0};
            auto threads = std::vector<std::jthread>{};
            threads.reserve(8);
            for(auto t = 0; t < 8; ++t)
            {
                threads.emplace_back([&engine, &failures]
                {
                    for(auto i = 0; i < 50; ++i)
                    {
                        if(engine.count("ConcurrentRead"s, object{}) != 10u)
                            ++failures;
                    }
                });
            }
            threads.clear();
            require_eq(failures.load(), 0);
        };

        section("WritePreconditionRejectsStalePosition") = []
        {
            const auto test_file = "./engine_precondition_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            constexpr auto collection = "Precondition"s;
            auto document = object{{"name"s, "alpha"s}};
            require_true(engine.create(collection, document).has_value());
            auto position = engine.metadata_position(collection, object{});
            require_true(position.has_value());

            auto stale = yar::db::write_preconditions{};
            stale.if_match_position = *position - 1;
            auto updated = object{{"name"s, "beta"s}};
            auto result = engine.replace(collection, object{}, updated, stale);
            require_false(result.has_value());
            require_eq(result.error().code, yar::db::db_error_code::precondition_failed);
        };

        section("CreateRejectsDuplicateId") = []
        {
            const auto test_file = "./engine_duplicate_id_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            constexpr auto collection = "DuplicateId"s;

            auto original = object{{"name"s, "alice"s}};
            require_true(engine.create(collection, original).has_value());
            const auto id = static_cast<xson::integer_type>(original["_id"s]);

            auto duplicate = object{{"_id"s, id}, {"name"s, "eve"s}};
            auto result = engine.create(collection, duplicate);
            require_false(result.has_value());
            require_eq(result.error().code, yar::db::db_error_code::conflict);

            auto documents = object{};
            require_true(engine.read(collection, object{{"_id"s, id}}, documents));
            require_eq(documents.get<object::array>().size(), 1u);
            require_eq(static_cast<string>(documents[0]["name"s]), "alice"s);
        };

        section("CreateAllowsPreservedIdWhenAbsent") = []
        {
            const auto test_file = "./engine_preserved_id_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            constexpr auto collection = "PreservedId"s;

            auto document = object{{"_id"s, 42ll}, {"name"s, "imported"s}};
            require_true(engine.create(collection, document).has_value());

            auto documents = object{};
            require_true(engine.read(collection, object{{"_id"s, 42ll}}, documents));
            require_eq(documents.get<object::array>().size(), 1u);
            require_eq(static_cast<string>(documents[0]["name"s]), "imported"s);
        };

        section("UpdateRejectsMutableId") = []
        {
            const auto test_file = "./engine_immutable_id_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            constexpr auto collection = "ImmutableId"s;

            auto document = object{{"name"s, "alice"s}};
            require_true(engine.create(collection, document).has_value());
            const auto id = static_cast<xson::integer_type>(document["_id"s]);

            auto updates = object{{"_id"s, id + 1}, {"name"s, "mutated"s}};
            auto documents = object{};
            auto result = engine.update(collection, object{{"_id"s, id}}, updates, documents);
            require_false(result.has_value());
            require_eq(result.error().code, yar::db::db_error_code::conflict);

            documents = object{};
            require_true(engine.read(collection, object{{"_id"s, id}}, documents));
            require_eq(static_cast<string>(documents[0]["name"s]), "alice"s);
            require_false(engine.read(collection, object{{"_id"s, id + 1}}, documents));
        };
    };
    return true;
}

const auto test_registrar = test_set();

}
