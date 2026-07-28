// Copyright (c) 2025-2026 Kaius Ruokonen. All rights reserved.
// SPDX-License-Identifier: MIT
// See the LICENSE file in the project root for full license text.

module yar;
import :engine;
import :metadata;
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
            require_true(documents.is_array());
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

        section("HistoryFiltersUnindexedSelector") = []
        {
            // Without a secondary index, view() scans every primary key. history()
            // must still match() the live row or unrelated version chains leak.
            const auto test_file = "./engine_history_unindexed_selector_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto ada = object{{"name"s, "Ada"s}, {"role"s, "admin"s}},
                bob = object{{"name"s, "Bob"s}, {"role"s, "user"s}},
                history = object{};
            require_true(engine.create("HistoryFilter"s, ada).has_value());
            require_true(engine.create("HistoryFilter"s, bob).has_value());
            require_true(
                engine.update(
                    "HistoryFilter"s,
                    object{{"_id"s, ada["_id"s]}},
                    object{{"name"s, "Ada"s}, {"role"s, "owner"s}}).value() > 0);

            require_true(engine.history("HistoryFilter"s, object{{"name"s, "Ada"s}}, history));
            require_eq(history.size(), 2u);
            for(const auto& item : history.get<object::array>())
                require_eq(item["name"s].get<string>(), "Ada"s);

            history = object{};
            require_true(engine.history("HistoryFilter"s, object{{"name"s, "Bob"s}}, history));
            require_eq(history.size(), 1u);
            require_eq(history[0]["name"s].get<string>(), "Bob"s);
            require_eq(history[0]["role"s].get<string>(), "user"s);
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

        section("DestroyLargeInt64DoesNotCollapseNeighbors") = [test_file]
        {
            // int64 equality via double made 2^53 and 2^53+1 match, so a
            // destroy/update selector for one value could hit both documents.
            constexpr auto exact = 9007199254740992ll; // 2^53
            constexpr auto next = exact + 1;

            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"serial"s, exact}, {"name"s, "exact"s}};
            auto document2 = object{{"serial"s, next}, {"name"s, "next"s}};
            auto documents = object{};
            require_true(engine.create("LargeInt64Destroy"s, document1).has_value());
            require_true(engine.create("LargeInt64Destroy"s, document2).has_value());

            require_eq(
                engine.destroy("LargeInt64Destroy"s, object{{"serial"s, exact}}, documents).value(),
                1u);
            require_eq(documents.get<object::array>().size(), 1u);
            require_eq(documents[0]["name"s].get<string>(), "exact"s);

            documents = object{};
            require_true(engine.read("LargeInt64Destroy"s, object{}, documents));
            require_eq(documents.get<object::array>().size(), 1u);
            require_eq(static_cast<xson::integer_type>(documents[0]["serial"s]), next);
            require_eq(documents[0]["name"s].get<string>(), "next"s);
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

        section("ReplacePreservesIdWhenOmitted") = []
        {
            // Replacement bodies without _id used to get a fresh sequence id from
            // index::update, silently moving the resource off its original key.
            const auto test_file = "./engine_replace_omit_id_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto original = object{{"name"s, "before"s}},
                replacement = object{{"name"s, "after"s}},
                documents = object{};
            require_true(engine.create("ReplaceOmitId"s, original).has_value());
            const auto id = static_cast<xson::integer_type>(original["_id"s]);
            const auto selector = object{{"_id"s, id}};

            require_true(engine.replace("ReplaceOmitId"s, selector, replacement).value() > 0);
            require_eq(static_cast<xson::integer_type>(replacement["_id"s]), id);
            require_true(engine.read("ReplaceOmitId"s, selector, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["name"s].get<string>(), "after"s);
            require_eq(static_cast<xson::integer_type>(documents[0]["_id"s]), id);
            require_eq(engine.count("ReplaceOmitId"s, object{}), 1u);
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

        section("ReplaceRejectsPrimaryKeyMutation") = []
        {
            // Changing _id on replace tombstones the matched row and inserts
            // under a new key — the original id disappears.
            const auto test_file = "./engine_replace_id_mutation_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto original = object{{"name"s, "before"s}};
            require_true(engine.create("ReplaceIdMut"s, original).has_value());
            const auto id = static_cast<xson::integer_type>(original["_id"s]);
            const auto selector = object{{"_id"s, id}};

            auto moved = object{{"_id"s, id + 100}, {"name"s, "after"s}};
            const auto result = engine.replace("ReplaceIdMut"s, selector, moved);
            require_false(result.has_value());
            require_eq(result.error().code, yar::db::db_error_code::conflict);

            auto documents = object{};
            require_true(engine.read("ReplaceIdMut"s, selector, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["name"s].get<string>(), "before"s);
            require_false(engine.read("ReplaceIdMut"s, object{{"_id"s, id + 100}}, documents));
        };

        section("PutCreateHonorsSelectorId") = []
        {
            // put() create used to auto-assign _id when the body omitted it,
            // ignoring selector {"_id": N}. Retries then created duplicates
            // while N stayed empty (MCP replace_document hit this).
            const auto test_file = "./engine_put_selector_id_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document = object{{"name"s, "Ada"s}};
            const auto selector = object{{"_id"s, 42ll}};

            const auto outcome = engine.put("PutSelId"s, selector, document);
            require_true(outcome.has_value());
            require_true(*outcome == yar::db::put_outcome::created);
            require_eq(static_cast<xson::integer_type>(document["_id"s]), 42ll);

            auto documents = object{};
            require_true(engine.read("PutSelId"s, selector, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["name"s].get<string>(), "Ada"s);

            auto retry = object{{"name"s, "Ada"s}};
            const auto again = engine.put("PutSelId"s, selector, retry);
            require_true(again.has_value());
            require_true(*again == yar::db::put_outcome::replaced);
            require_eq(engine.count("PutSelId"s, object{}), 1u);
        };

        section("PutCreateOverwritesMismatchedBodyId") = []
        {
            // Body _id must not win over the PUT selector identity.
            const auto test_file = "./engine_put_body_id_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document = object{{"_id"s, 99ll}, {"name"s, "Bob"s}};
            const auto selector = object{{"_id"s, 7ll}};

            const auto outcome = engine.put("PutBodyId"s, selector, document);
            require_true(outcome.has_value());
            require_true(*outcome == yar::db::put_outcome::created);
            require_eq(static_cast<xson::integer_type>(document["_id"s]), 7ll);

            auto documents = object{};
            require_true(engine.read("PutBodyId"s, selector, documents));
            require_eq(documents.size(), 1u);
            require_false(engine.read("PutBodyId"s, object{{"_id"s, 99ll}}, documents));
        };

        section("ReplaceRejectsMultiMatchSelector") = []
        {
            // replace appends one successor. A broad selector must not tombstone
            // every match while leaving only a single replacement document.
            const auto test_file = "./engine_replace_multi_match_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto first = object{{"name"s, "alpha"s}},
                second = object{{"name"s, "beta"s}},
                documents = object{};
            require_true(engine.create("ReplaceMulti"s, first).has_value());
            require_true(engine.create("ReplaceMulti"s, second).has_value());

            auto replacement = object{{"name"s, "only-one"s}};
            const auto result = engine.replace("ReplaceMulti"s, object{}, replacement);

            require_false(result.has_value());
            require_true(result.error().code == yar::db::db_error_code::conflict);
            require_true(engine.read("ReplaceMulti"s, object{}, documents));
            require_eq(documents.size(), 2u);
            require_true(engine.read("ReplaceMulti"s, object{{"_id"s, first["_id"s]}}, documents));
            require_eq(documents[0]["name"s].get<string>(), "alpha"s);
            require_true(engine.read("ReplaceMulti"s, object{{"_id"s, second["_id"s]}}, documents));
            require_eq(documents[0]["name"s].get<string>(), "beta"s);
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

        section("IndexPopulatesExistingDocumentsWithoutReindex") = []
        {
            // index() used to publish empty secondary maps and rely on a later
            // reindex(). Concurrent readers (and a failed reindex) then saw
            // false-empty results for selectors on the new key.
            const auto test_file = "./engine_index_populate_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            auto document1 = object{{"tag"s, "alpha"s}, {"value"s, 1ll}},
                document2 = object{{"tag"s, "beta"s}, {"value"s, 2ll}},
                selector = object{{"tag"s, "alpha"s}},
                documents = object{};
            require_true(engine.create("IndexPopulate"s, document1).has_value());
            require_true(engine.create("IndexPopulate"s, document2).has_value());
            require_true(engine.index("IndexPopulate"s, {"tag"s}).has_value());

            require_true(engine.read("IndexPopulate"s, selector, documents));
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

        section("NestedPathReadWorksWhenParentObjectIsIndexed") = []
        {
            // Customer/Country selectors must not choose the empty Customer
            // secondary index when Customer values are nested objects.
            const auto test_file = "./engine_nested_path_indexed_parent.db";
            std::remove(test_file);
            std::remove((std::string{test_file} + ".pid").c_str());

            auto engine = yar::db::engine{test_file};
            auto usa = object{
                {"Customer"s, object{{"Country"s, "USA"s}, {"Name"s, "Acme"s}}}
            };
            auto uk = object{
                {"Customer"s, object{{"Country"s, "UK"s}, {"Name"s, "Beta"s}}}
            };
            require_true(engine.create("NestedPathIdx"s, usa).has_value());
            require_true(engine.create("NestedPathIdx"s, uk).has_value());
            require_true(engine.index("NestedPathIdx"s, {"Customer"s}).has_value());

            auto by_usa = object{{"Customer"s, object{{"Country"s, "USA"s}}}};
            auto documents = object{};
            require_true(engine.read("NestedPathIdx"s, by_usa, documents));
            require_eq(documents.get<object::array>().size(), 1u);
            require_eq(documents[0]["Customer"s]["Name"s].get<string>(), "Acme"s);
            require_eq(engine.count("NestedPathIdx"s, by_usa), 1u);

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
            require_eq(static_cast<xson::integer_type>(document["_id"s]), 42ll);
            require_eq(static_cast<xson::integer_type>(documents[0]["_id"s]), 42ll);
            require_true(documents[0].match(document));
            require_true(engine.read("UpsertCreate"s, object{{"_id"s, 42ll}}, documents));
            require_eq(documents.size(), 1u);
            require_eq(documents[0]["name"s].get<string>(), "created"s);

            // Idempotent retry must update the same row, not mint another id.
            auto retry = object{{"name"s, "updated"s}};
            documents = object{};
            require_eq(engine.upsert("UpsertCreate"s, object{{"_id"s, 42ll}}, retry, documents).value(), 1u);
            require_eq(engine.count("UpsertCreate"s, object{}), 1u);
            require_true(engine.read("UpsertCreate"s, object{{"_id"s, 42ll}}, documents));
            require_eq(documents.size(), 1u);
            require_eq(static_cast<xson::integer_type>(documents[0]["_id"s]), 42ll);
            require_eq(documents[0]["name"s].get<string>(), "updated"s);
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

        section("ReadRevisionMatchesReturnedBodyUnderConcurrentReplace") = []
        {
            // Separate read() + metadata_position() can observe body from v1 and
            // position from v2. Atomic read(..., revision) must keep them aligned
            // so If-Match cannot be minted for a body the client never saw.
            const auto test_file = "./engine_revision_toctou_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            constexpr auto collection = "RevisionToctou"s;

            auto seed = object{{"value"s, 0ll}};
            auto create_revision = yar::db::document_revision{};
            require_true(engine.create(collection, seed, &create_revision).has_value());
            require_true(create_revision.position.has_value());
            const auto id = static_cast<xson::integer_type>(seed["_id"s]);
            const auto selector = object{{"_id"s, id}};

            auto mismatches = std::atomic<int>{0};
            auto stop = std::atomic<bool>{false};
            auto writer = std::jthread{[&]
            {
                auto next = 1ll;
                while(not stop.load(std::memory_order_relaxed))
                {
                    auto replacement = object{{"_id"s, id}, {"value"s, next}};
                    if(engine.replace(collection, selector, replacement).has_value())
                        ++next;
                }
            }};

            for(auto i = 0; i < 400; ++i)
            {
                auto documents = object{};
                auto revision = yar::db::document_revision{};
                require_true(engine.read(collection, selector, documents, revision));
                require_true(revision.position.has_value());
                require_false(documents.get<object::array>().empty());
                const auto returned_value =
                    static_cast<xson::integer_type>(documents[0]["value"s]);

                using xson::fson::operator >>;
                auto storage = std::ifstream{test_file, std::ios::binary};
                require_true(storage.is_open());
                storage.seekg(*revision.position, storage.beg);
                auto metadata = yar::db::metadata{};
                auto on_disk = object{};
                storage >> metadata >> on_disk;
                require_false(storage.fail());
                if(static_cast<xson::integer_type>(on_disk["value"s]) != returned_value)
                    ++mismatches;
            }

            stop.store(true, std::memory_order_relaxed);
            writer.join();
            require_eq(mismatches.load(), 0);
        };

        section("WriteRevisionMatchesCreatedAndReplacedPositions") = []
        {
            const auto test_file = "./engine_write_revision_test.db";
            const auto setup = fixture{test_file};
            auto engine = yar::db::engine{test_file};
            constexpr auto collection = "WriteRevision"s;

            auto document = object{{"name"s, "alpha"s}};
            auto created_revision = yar::db::document_revision{};
            require_true(engine.create(collection, document, &created_revision).has_value());
            require_true(created_revision.position.has_value());
            require_true(created_revision.timestamp.has_value());
            const auto id = static_cast<xson::integer_type>(document["_id"s]);
            const auto selector = object{{"_id"s, id}};
            require_eq(*created_revision.position, *engine.metadata_position(collection, selector));

            auto replacement = object{{"_id"s, id}, {"name"s, "beta"s}};
            auto replaced_revision = yar::db::document_revision{};
            require_true(
                engine.replace(collection, selector, replacement, {}, &replaced_revision).has_value());
            require_true(replaced_revision.position.has_value());
            require_true(*replaced_revision.position != *created_revision.position);
            require_eq(*replaced_revision.position, *engine.metadata_position(collection, selector));

            auto documents = object{};
            auto updated_revision = yar::db::document_revision{};
            auto updates = object{{"name"s, "gamma"s}};
            require_true(
                engine.update(
                    collection, selector, updates, documents, {}, &updated_revision).has_value());
            require_true(updated_revision.position.has_value());
            require_eq(*updated_revision.position, *engine.metadata_position(collection, selector));
            require_eq(static_cast<string>(documents[0]["name"s]), "gamma"s);
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

        section("DualLiveCrashRecoverySupersedesStalePreImage") = []
        {
            // Update appends a successor then tombstones the prior row. A crash
            // between those steps leaves two status=created records for one _id.
            // Reopen must serve only the later version and drop stale secondary
            // hits (and heal the prior status so yarexport cannot resurrect it).
            const auto test_file = "./engine_dual_live_recovery_test.db";
            const auto setup = fixture{test_file};
            constexpr auto collection = "DualLive"s;
            const auto prior = [&]
            {
                auto engine = yar::db::engine{test_file};
                require_true(engine.index(collection, {"name"s}).has_value());
                auto document = object{{"name"s, "alice"s}};
                require_true(engine.create(collection, document).has_value());
                const auto id = static_cast<xson::integer_type>(document["_id"s]);
                const auto selector = object{{"_id"s, id}};
                const auto position = engine.metadata_position(collection, selector);
                require_true(position.has_value());

                auto updates = object{{"name"s, "bob"s}};
                require_true(engine.update(collection, selector, updates).has_value());
                return std::pair{*position, id};
            }();

            {
                auto storage = std::fstream{
                    test_file,
                    std::ios::out | std::ios::in | std::ios::binary};
                require_true(storage.is_open());
                storage.seekp(prior.first, storage.beg);
                storage << yar::db::metadata{yar::db::metadata::created};
                require_false(storage.fail());
                storage.flush();
            }

            {
                auto reopened = yar::db::engine{test_file};
                auto by_id = object{};
                require_true(reopened.read(collection, object{{"_id"s, prior.second}}, by_id));
                require_eq(by_id.size(), 1u);
                require_eq(static_cast<string>(by_id[0]["name"s]), "bob"s);

                auto stale = object{};
                require_false(reopened.read(collection, object{{"name"s, "alice"s}}, stale));

                auto current = object{};
                require_true(reopened.read(collection, object{{"name"s, "bob"s}}, current));
                require_eq(current.size(), 1u);
            }

            // Second open must still see one live row after on-disk status heal.
            auto again = yar::db::engine{test_file};
            auto all = object{};
            require_true(again.read(collection, object{}, all));
            require_eq(all.size(), 1u);
            require_eq(static_cast<string>(all[0]["name"s]), "bob"s);
        };

        section("DualLiveYarexportLiveOmitsStalePreImage") = []
        {
            // Same crash window as DualLiveCrashRecovery, but export --live
            // without a prior engine reopen. Raw status=created scanning would
            // emit both alice and bob; engine-backed --live must emit only bob.
            const auto test_file = "./engine_dual_live_yarexport_test.db";
            const auto export_file = "./engine_dual_live_yarexport_test.live.jsonl";
            const auto setup = fixture{test_file};
            filesystem::remove(export_file);

            constexpr auto collection = "DualLiveExport"s;
            const auto prior_position = [&]
            {
                auto engine = yar::db::engine{test_file};
                require_true(engine.index(collection, {"name"s}).has_value());
                auto document = object{{"name"s, "alice"s}};
                require_true(engine.create(collection, document).has_value());
                const auto id = static_cast<xson::integer_type>(document["_id"s]);
                const auto selector = object{{"_id"s, id}};
                const auto position = engine.metadata_position(collection, selector);
                require_true(position.has_value());

                auto updates = object{{"name"s, "bob"s}};
                require_true(engine.update(collection, selector, updates).has_value());
                return *position;
            }();

            {
                auto storage = std::fstream{
                    test_file,
                    std::ios::out | std::ios::in | std::ios::binary};
                require_true(storage.is_open());
                storage.seekp(prior_position, storage.beg);
                storage << yar::db::metadata{yar::db::metadata::created};
                require_false(storage.fail());
                storage.flush();
            }

            // Confirm the crash window is still dual-live on disk before export.
            {
                using xson::fson::operator >>;
                auto created = 0;
                auto storage = std::ifstream{test_file, std::ios::binary};
                require_true(storage.is_open());
                while(storage.peek() != std::char_traits<char>::eof())
                {
                    auto metadata = yar::db::metadata{};
                    auto document = object{};
                    storage >> metadata >> document;
                    if(not storage)
                        break;
                    if(metadata.collection == collection
                        and metadata.status == yar::db::metadata::created)
                        ++created;
                }
                require_eq(created, 2);
            }

            auto yarexport_bin = optional<string>{};
            if(const auto* env = getenv("YAREXPORT_BIN"); env != nullptr and *env != '\0')
                yarexport_bin = string{env};
            else
            {
                for(const auto candidate : {
                        "./build-linux-debug/bin/yarexport"sv,
                        "./build-darwin-debug/bin/yarexport"sv,
                        "./build-linux-release/bin/yarexport"sv,
                        "./build-darwin-release/bin/yarexport"sv})
                {
                    error_code ec{};
                    if(filesystem::exists(candidate, ec))
                    {
                        yarexport_bin = string{candidate};
                        break;
                    }
                }
            }
            require_true(yarexport_bin.has_value());

            const auto command =
                *yarexport_bin
                + " --file="s + test_file
                + " --live > "s + export_file;
            require_eq(std::system(command.c_str()), 0);

            auto live_rows = 0;
            auto saw_bob = false;
            auto saw_alice = false;
            auto input = std::ifstream{export_file};
            require_true(input.is_open());
            for(auto line = string{}; std::getline(input, line);)
            {
                if(line.empty())
                    continue;
                const auto row = json::parse(line);
                require_true(row.has("collection"s));
                require_true(row.has("document"s));
                if(static_cast<string>(row["collection"s]) != collection)
                    continue;
                ++live_rows;
                const auto name = static_cast<string>(row["document"s]["name"s]);
                if(name == "bob"s)
                    saw_bob = true;
                if(name == "alice"s)
                    saw_alice = true;
            }

            require_eq(live_rows, 1);
            require_true(saw_bob);
            require_false(saw_alice);
            filesystem::remove(export_file);
        };
    };

    return true;
}

const auto test_registrar = test_set();

}
