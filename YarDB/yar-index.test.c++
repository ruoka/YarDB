// Copyright (c) 2025-2026 Kaius Ruokonen. All rights reserved.
// SPDX-License-Identifier: MIT
// See the LICENSE file in the project root for full license text.

module yar;
import :index;
import :metadata;
import tester;
import std;
import xson;

namespace yar::index_unit_test {

using namespace std;
using namespace std::string_literals;
using namespace xson;
using namespace tester::basic;
using namespace tester::assertions;

class index_fixture
{
public:
    explicit index_fixture(string_view f) : m_file{f}
    {
        remove(m_file.c_str());
        remove((m_file + ".pid"s).c_str());
        m_storage.open(m_file, ios::out | ios::in | ios::binary | ios::trunc);
    }

    ~index_fixture()
    {
        m_storage.close();
        remove(m_file.c_str());
        remove((m_file + ".pid"s).c_str());
    }

    yar::db::index& index() { return m_index; }

    void add_keys(std::initializer_list<string> keys)
    {
        m_index.add(vector<string>{keys});
    }

    void insert(object doc)
    {
        using xson::fson::operator<<;

        auto metadata = yar::db::metadata{"index_test"s};
        m_index.update(doc);
        m_storage.seekp(0, ios::end);
        const auto position = m_storage.tellp();
        m_index.insert(doc, position);
        m_storage << metadata << doc;
        m_storage.flush();
    }

    std::size_t count(const object& selector)
    {
        return m_index.count(m_storage, selector);
    }

    // Stores the next version of the document with doc's _id.
    void update(object doc)
    {
        using xson::fson::operator<<;

        auto metadata = yar::db::metadata{"index_test"s};
        const auto prior = stored(*m_index.position(static_cast<xson::integer_type>(doc["_id"s])));
        m_storage.seekp(0, ios::end);
        const auto position = m_storage.tellp();
        m_index.replace(prior, doc, position);
        m_storage << metadata << doc;
        m_storage.flush();
    }

    // Open's path: stored in file order, indexed by finish_load().
    void load(object doc)
    {
        using xson::fson::operator<<;

        auto metadata = yar::db::metadata{"index_test"s};
        m_index.update(doc);
        m_storage.seekp(0, ios::end);
        const auto position = m_storage.tellp();
        m_index.load(doc, position);
        m_storage << metadata << doc;
        m_storage.flush();
    }

    std::size_t view_count(const object& selector)
    {
        return std::ranges::distance(m_index.view(selector));
    }

    std::vector<yar::db::position_type> positions(const object& selector)
    {
        return m_index.view(selector) | std::ranges::to<std::vector<yar::db::position_type>>();
    }

    // The field of each stored document the view walks, in walk order.
    std::vector<xson::integer_type> walk(const object& selector, const string& field = "_id"s)
    {
        auto values = std::vector<xson::integer_type>{};
        for(const auto position : m_index.view(selector))
            values.push_back(static_cast<xson::integer_type>(stored(position)[field]));
        return values;
    }

    object stored(yar::db::position_type position)
    {
        using xson::fson::operator>>;

        auto metadata = yar::db::metadata{};
        auto doc = object{};
        m_storage.clear();
        m_storage.seekg(position, ios::beg);
        m_storage >> metadata >> doc;
        return doc;
    }

private:
    std::string m_file;
    std::fstream m_storage;
    yar::db::index m_index;
};

auto test_set()
{
    test_case("index count and view behavior, [yardb]") = []
    {
        section("CountEmptySelectorUsesPrimaryIndexSize") = []
        {
            index_fixture fixture{"./index_count_empty.db"s};
            fixture.add_keys({"status"s});
            fixture.insert(object{{"status"s, "active"s}});
            fixture.insert(object{{"status"s, "pending"s}});
            fixture.insert(object{{"status"s, "active"s}});

            require_eq(fixture.count(object{}), 3u);
            require_eq(fixture.view_count(object{}), 3u);
        };

        section("CountEqualityOnIndexedFieldIsIndexOnly") = []
        {
            index_fixture fixture{"./index_count_eq.db"s};
            fixture.add_keys({"status"s});
            fixture.insert(object{{"status"s, "active"s}});
            fixture.insert(object{{"status"s, "pending"s}});
            fixture.insert(object{{"status"s, "active"s}});

            const auto active = object{{"status"s, "active"s}};
            require_eq(fixture.count(active), 2u);
            require_eq(fixture.view_count(active), 2u);
        };

        section("CountRangeOnIndexedFieldIsIndexOnly") = []
        {
            index_fixture fixture{"./index_count_range.db"s};
            fixture.add_keys({"age"s});
            fixture.insert(object{{"age"s, 20ll}, {"name"s, "twenty"s}});
            fixture.insert(object{{"age"s, 30ll}, {"name"s, "thirty"s}});
            fixture.insert(object{{"age"s, 40ll}, {"name"s, "forty"s}});

            const auto over_25 = object{{"age"s, object{{"$gt"s, 25ll}}}};
            require_eq(fixture.count(over_25), 2u);
            require_eq(fixture.view_count(over_25), 2u);
        };

        section("IntegerRangesUseNumericOrdering") = []
        {
            index_fixture fixture{"./index_integer_order.db"s};
            fixture.add_keys({"value"s});
            fixture.insert(object{{"value"s, 2ll}});
            fixture.insert(object{{"value"s, 9ll}});
            fixture.insert(object{{"value"s, 10ll}});
            fixture.insert(object{{"value"s, 100ll}});

            const auto over_9 = object{{"value"s, object{{"$gt"s, 9ll}}}},
                under_10 = object{{"value"s, object{{"$lt"s, 10ll}}}};
            require_eq(fixture.count(over_9), 2u);
            require_eq(fixture.view_count(over_9), 2u);
            require_eq(fixture.count(under_10), 2u);
            require_eq(fixture.view_count(under_10), 2u);
        };

        section("FloatingPointRangesUseNumericOrdering") = []
        {
            index_fixture fixture{"./index_number_order.db"s};
            fixture.add_keys({"value"s});
            fixture.insert(object{{"value"s, 2.0}});
            fixture.insert(object{{"value"s, 9.0}});
            fixture.insert(object{{"value"s, 10.0}});
            fixture.insert(object{{"value"s, 100.0}});

            const auto over_9 = object{{"value"s, object{{"$gt"s, 9.0}}}};
            require_eq(fixture.count(over_9), 2u);
            require_eq(fixture.view_count(over_9), 2u);
        };

        section("SecondaryKeysPreservePrimitiveTypes") = []
        {
            index_fixture fixture{"./index_typed_keys.db"s};
            fixture.add_keys({"value"s});
            fixture.insert(object{{"value"s, 1ll}});
            fixture.insert(object{{"value"s, 1.0}});
            fixture.insert(object{{"value"s, "1"s}});
            fixture.insert(object{{"value"s, true}});

            // integer_type and number_type with the same numeric value share a
            // secondary key so OData integer filters match JSON doubles.
            require_eq(fixture.count(object{{"value"s, 1ll}}), 2u);
            require_eq(fixture.count(object{{"value"s, 1.0}}), 2u);
            require_eq(fixture.count(object{{"value"s, "1"s}}), 1u);
            require_eq(fixture.count(object{{"value"s, true}}), 1u);
        };

        section("SecondaryNumericCrossTypeRanges") = []
        {
            index_fixture fixture{"./index_numeric_cross_type.db"s};
            fixture.add_keys({"score"s});
            auto high = object{};
            high["score"s] = 100.0;
            fixture.insert(high);
            fixture.insert(object{{"score"s, 3ll}});

            const auto under_9 = object{{"score"s, object{{"$lt"s, 9ll}}}};
            const auto over_0 = object{{"score"s, object{{"$gt"s, 0ll}}}};
            require_eq(fixture.count(under_9), 1u);
            require_eq(fixture.view_count(under_9), 1u);
            require_eq(fixture.count(over_0), 2u);
            require_eq(fixture.view_count(over_0), 2u);
        };

        section("CountPrimaryKeyRangeIsIndexOnly") = []
        {
            index_fixture fixture{"./index_count_id_range.db"s};
            fixture.insert(object{{"name"s, "first"s}});
            fixture.insert(object{{"name"s, "second"s}});
            fixture.insert(object{{"name"s, "third"s}});

            const auto after_first = object{{"_id"s, object{{"$gt"s, 1ll}}}};
            require_eq(fixture.count(after_first), 2u);
            require_eq(fixture.view_count(after_first), 2u);
        };

        section("InvertedPrimaryRangeIsEmpty") = []
        {
            // AND-merged OData can produce {_id:{$gt:10,$lt:5}}; walking that
            // iterator pair without clamping is undefined behavior on flat_map.
            index_fixture fixture{"./index_inverted_id_range.db"s};
            fixture.insert(object{{"name"s, "first"s}});
            fixture.insert(object{{"name"s, "second"s}});
            fixture.insert(object{{"name"s, "third"s}});

            const auto inverted = object{{"_id"s, object{{"$gt"s, 10ll}, {"$lt"s, 5ll}}}};
            require_eq(fixture.view_count(inverted), 0u);
            require_eq(fixture.count(inverted), 0u);
        };

        section("InvertedSecondaryRangeIsEmpty") = []
        {
            index_fixture fixture{"./index_inverted_age_range.db"s};
            fixture.add_keys({"age"s});
            fixture.insert(object{{"age"s, 20ll}});
            fixture.insert(object{{"age"s, 30ll}});
            fixture.insert(object{{"age"s, 40ll}});

            const auto inverted = object{{"age"s, object{{"$gt"s, 35ll}, {"$lt"s, 25ll}}}};
            require_eq(fixture.view_count(inverted), 0u);
            require_eq(fixture.count(inverted), 0u);
        };

        section("CountNeUsesScanFallback") = []
        {
            index_fixture fixture{"./index_count_ne.db"s};
            fixture.add_keys({"status"s});
            fixture.insert(object{{"status"s, "active"s}});
            fixture.insert(object{{"status"s, "deleted"s}});
            fixture.insert(object{{"status"s, "pending"s}});

            const auto not_deleted = object{{"status"s, object{{"$ne"s, "deleted"s}}}};
            require_eq(fixture.count(not_deleted), 2u);
            // View still narrows by status key presence; count verifies via document.match
            require_eq(fixture.view_count(not_deleted), 3u);
        };

        section("CountMultiFieldAndIntersectsPostings") = []
        {
            index_fixture fixture{"./index_count_multi.db"s};
            fixture.add_keys({"status"s, "age"s});
            fixture.insert(object{{"status"s, "active"s}, {"age"s, 30ll}});
            fixture.insert(object{{"status"s, "active"s}, {"age"s, 20ll}});
            fixture.insert(object{{"status"s, "pending"s}, {"age"s, 40ll}});

            const auto selector = object{
                {"status"s, "active"s},
                {"age"s, object{{"$gt"s, 25ll}}}
            };
            require_eq(fixture.count(selector), 1u);
            auto empty = std::stringstream{};
            require_eq(fixture.index().count(empty, selector), 1u);
        };

        section("InsertSkipsObjectAndArraySecondaryValues") = []
        {
            // Nested values must not throw via make_secondary_key (bad_variant_access).
            index_fixture fixture{"./index_nested_secondary.db"s};
            fixture.add_keys({"address"s, "tags"s, "city"s});
            fixture.insert(object{
                {"address"s, object{{"city"s, "NYC"s}}},
                {"tags"s, object{object::array{object{{"n"s, 1ll}}, object{{"n"s, 2ll}}}}},
                {"city"s, "NYC"s}
            });
            fixture.insert(object{{"address"s, "plain"s}, {"city"s, "LA"s}});

            require_eq(fixture.count(object{}), 2u);
            require_eq(fixture.count(object{{"city"s, "NYC"s}}), 1u);
            require_eq(fixture.count(object{{"city"s, "LA"s}}), 1u);
            // Only primitive address values are secondary-indexed.
            require_eq(fixture.count(object{{"address"s, "plain"s}}), 1u);
            require_eq(fixture.view_count(object{{"address"s, "plain"s}}), 1u);
        };

        section("NestedPathSelectorFallsBackWhenParentIsIndexed") = []
        {
            // Indexing the parent object field must not make Customer/Country
            // queries false-empty: object values are skipped on insert, so the
            // secondary map cannot answer nested document selectors.
            index_fixture fixture{"./index_nested_path_fallback.db"s};
            fixture.add_keys({"Customer"s});
            fixture.insert(object{
                {"Customer"s, object{{"Country"s, "USA"s}, {"Name"s, "Acme"s}}}
            });
            fixture.insert(object{
                {"Customer"s, object{{"Country"s, "UK"s}, {"Name"s, "Beta"s}}}
            });

            const auto nested_usa = object{
                {"Customer"s, object{{"Country"s, "USA"s}}}
            };
            require_eq(fixture.view_count(nested_usa), 2u);
            require_eq(fixture.count(nested_usa), 1u);

            const auto nested_uk = object{
                {"Customer"s, object{{"Country"s, "UK"s}}}
            };
            require_eq(fixture.count(nested_uk), 1u);
        };

        section("CompoundEqualityCountDoesNotReadStorage") = []
        {
            index_fixture fixture{"./index_compound_count.db"s};
            fixture.add_keys({"service"s, "type"s, "status"s, "service+type+status"s});
            fixture.insert(object{{"service"s, "oms"s}, {"type"s, "oms.capture"s}, {"status"s, "DONE"s}});
            fixture.insert(object{{"service"s, "oms"s}, {"type"s, "oms.capture"s}, {"status"s, "DONE"s}});
            fixture.insert(object{{"service"s, "oms"s}, {"type"s, "oms.capture"s}, {"status"s, "PENDING"s}});
            fixture.insert(object{{"service"s, "tms"s}, {"type"s, "tms.instruct"s}, {"status"s, "DONE"s}});

            const auto done = object{
                {"service"s, object{{"$eq"s, "oms"s}}},
                {"type"s, object{{"$eq"s, "oms.capture"s}}},
                {"status"s, object{{"$eq"s, "DONE"s}}}
            };
            // An empty stream fails a document scan. The compound count is the
            // posting list's size and does not read it.
            auto empty = std::stringstream{};
            require_eq(fixture.index().count(empty, done), 2u);
            require_eq(fixture.index().count(empty, object{
                {"service"s, "oms"s},
                {"type"s, "oms.place-order"s},
                {"status"s, "DONE"s}
            }), 0u);

            auto doc = object{{"service"s, "oms"s}, {"type"s, "oms.capture"s}, {"status"s, "DONE"s}};
            auto undo = yar::db::index_undo{};
            fixture.index().update(doc, &undo);
            fixture.index().insert(doc, 900001, &undo);
            require_eq(fixture.index().count(empty, done), 3u);
            undo.apply(fixture.index());
            require_eq(fixture.index().count(empty, done), 2u);

            fixture.index().update(doc);
            fixture.index().insert(doc, 900002);
            fixture.index().erase(doc);
            doc["status"s] = "PENDING"s;
            fixture.index().insert(doc, 900002);
            require_eq(fixture.index().count(empty, done), 2u);
            require_eq(fixture.index().count(empty, object{
                {"service"s, "oms"s},
                {"type"s, "oms.capture"s},
                {"status"s, "PENDING"s}
            }), 2u);
        };

        section("PrimaryRangeIntersectSecondaryEq") = []
        {
            index_fixture fixture{"./index_primary_intersect.db"s};
            fixture.add_keys({"status"s});
            fixture.insert(object{{"status"s, "A"s}, {"tag"s, 1ll}});
            fixture.insert(object{{"status"s, "A"s}, {"tag"s, 2ll}});
            fixture.insert(object{{"status"s, "B"s}, {"tag"s, 3ll}});
            fixture.insert(object{{"status"s, "B"s}, {"tag"s, 4ll}});

            const auto both = object{
                {"_id"s, object{{"$gt"s, 1ll}}},
                {"status"s, "A"s}};
            require_eq(fixture.view_count(both), 1u);
            require_eq(fixture.count(both), 1u);

            const auto all_a = object{
                {"_id"s, object{{"$gt"s, 0ll}}},
                {"status"s, "A"s}};
            require_eq(fixture.view_count(all_a), 2u);
        };

        section("ViewUsesTightestExactSecondarySpan") = []
        {
            // service sorts ahead of updatedMs. The day's bound is the smaller span.
            index_fixture fixture{"./index_tight_span.db"s};
            fixture.add_keys({"service"s, "status"s, "updatedMs"s});
            for(auto i = 0; i < 5; ++i)
                fixture.insert(object{{"service"s, "oms"s}, {"status"s, "DONE"s}, {"updatedMs"s, 1ll}});
            fixture.insert(object{{"service"s, "oms"s}, {"status"s, "DONE"s}, {"updatedMs"s, 100ll}});

            const auto day = object{
                {"service"s, "oms"s},
                {"status"s, "DONE"s},
                {"updatedMs"s, object{{"$gte"s, 100ll}, {"$lt"s, 101ll}}}
            };
            require_eq(fixture.view_count(day), 1u);
            require_eq(fixture.count(day), 1u);
        };
    };

    test_case("index posting lists follow _id, [yardb]") = []
    {
        section("InsertKeepsEachPostingListInIdOrder") = []
        {
            index_fixture fixture{"./index_posting_order.db"s};
            fixture.add_keys({"status"s});
            for(const auto id : {5ll, 3ll, 9ll, 4ll})
                fixture.insert(object{{"_id"s, id}, {"status"s, "A"s}});

            // Appending in write order would walk 5, 3, 9, 4.
            require_container_eq(fixture.walk(object{{"status"s, "A"s}}), std::vector<xson::integer_type>{3, 4, 5, 9});
        };

        section("AnUpdatedDocumentKeepsItsPlace") = []
        {
            index_fixture fixture{"./index_posting_update.db"s};
            fixture.add_keys({"status"s});
            fixture.insert(object{{"status"s, "A"s}, {"n"s, 1ll}});
            fixture.insert(object{{"status"s, "A"s}, {"n"s, 2ll}});
            fixture.insert(object{{"status"s, "A"s}, {"n"s, 3ll}});
            fixture.update(object{{"_id"s, 1ll}, {"status"s, "A"s}, {"n"s, 10ll}});

            const auto active = object{{"status"s, "A"s}};
            require_container_eq(fixture.walk(active), std::vector<xson::integer_type>{1, 2, 3});
            require_container_eq(fixture.walk(active, "n"s), std::vector<xson::integer_type>{10, 2, 3});

            fixture.update(object{{"_id"s, 2ll}, {"status"s, "B"s}, {"n"s, 2ll}});
            require_container_eq(fixture.walk(active), std::vector<xson::integer_type>{1, 3});
            require_container_eq(fixture.walk(object{{"status"s, "B"s}}), std::vector<xson::integer_type>{2});
        };

        section("EqualitiesIntersectWithoutReadingTheFile") = []
        {
            index_fixture fixture{"./index_intersect_eq.db"s};
            fixture.add_keys({"status"s, "type"s, "venue"s});
            fixture.insert(object{{"status"s, "A"s}, {"type"s, "x"s}, {"venue"s, "XHEL"s}});
            fixture.insert(object{{"status"s, "A"s}, {"type"s, "y"s}, {"venue"s, "XHEL"s}});
            fixture.insert(object{{"status"s, "B"s}, {"type"s, "x"s}, {"venue"s, "XHEL"s}});
            fixture.insert(object{{"status"s, "A"s}, {"type"s, "x"s}, {"venue"s, "XSTO"s}});
            fixture.insert(object{{"status"s, "A"s}, {"type"s, "x"s}, {"venue"s, "XHEL"s}});

            const auto two = object{{"status"s, "A"s}, {"type"s, "x"s}};
            const auto three = object{{"status"s, "A"s}, {"type"s, object{{"$eq"s, "x"s}}}, {"venue"s, "XHEL"s}};
            require_container_eq(fixture.walk(two), std::vector<xson::integer_type>{1, 4, 5});
            require_container_eq(fixture.walk(three), std::vector<xson::integer_type>{1, 5});

            // An empty stream fails a document scan, so these counts come from
            // the posting lists alone.
            auto empty = std::stringstream{};
            require_eq(fixture.index().count(empty, two), 3u);
            require_eq(fixture.index().count(empty, three), 2u);
            require_eq(fixture.index().count(empty, object{{"status"s, "B"s}, {"type"s, "y"s}}), 0u);
            require_eq(fixture.index().count(empty, object{{"status"s, "C"s}, {"type"s, "x"s}}), 0u);
        };

        section("AShortListGallopsThroughALongOne") = []
        {
            index_fixture fixture{"./index_intersect_skewed.db"s};
            fixture.add_keys({"status"s, "type"s});
            for(auto i = 1; i <= 1000; ++i)
                fixture.insert(object{{"status"s, "A"s}, {"type"s, i % 100 == 0 ? "rare"s : "common"s}});

            const auto rare = object{{"status"s, "A"s}, {"type"s, "rare"s}};
            auto expected = std::vector<xson::integer_type>{};
            for(auto id = 100ll; id <= 1000; id += 100)
                expected.push_back(id);
            require_container_eq(fixture.walk(rare), expected);

            auto empty = std::stringstream{};
            require_eq(fixture.index().count(empty, rare), 10u);

            auto backward = rare;
            backward["$desc"s] = true;
            std::ranges::reverse(expected);
            require_container_eq(fixture.walk(backward), expected);
        };

        section("ARangeMergesItsKeysIntoTheIntersection") = []
        {
            index_fixture fixture{"./index_intersect_range.db"s};
            fixture.add_keys({"age"s, "status"s});
            for(auto i = 1ll; i <= 20; ++i)
                fixture.insert(object{{"age"s, i % 7}, {"status"s, i % 2 == 0 ? "A"s : "B"s}});

            const auto window = object{{"status"s, "A"s}, {"age"s, object{{"$gte"s, 2ll}, {"$lt"s, 5ll}}}};
            require_container_eq(fixture.walk(window), std::vector<xson::integer_type>{2, 4, 10, 16, 18});

            auto empty = std::stringstream{};
            require_eq(fixture.index().count(empty, window), 5u);
        };

        section("ALoneRangeWalksItsKeysAndAPagedOneFollowsId") = []
        {
            index_fixture fixture{"./index_lone_range.db"s};
            fixture.add_keys({"age"s});
            fixture.insert(object{{"age"s, 15ll}});
            fixture.insert(object{{"age"s, 12ll}});
            fixture.insert(object{{"age"s, 18ll}});
            fixture.insert(object{{"age"s, 30ll}});

            const auto teens = object{{"age"s, object{{"$gte"s, 10ll}, {"$lt"s, 20ll}}}};
            require_container_eq(fixture.walk(teens), std::vector<xson::integer_type>{2, 1, 3});

            auto paged = teens;
            paged["$top"s] = 2ll;
            require_container_eq(fixture.walk(paged), std::vector<xson::integer_type>{1, 2, 3});
        };

        section("AWideBoundJoinsOnlyWhenItDrives") = []
        {
            index_fixture fixture{"./index_seek_guard.db"s};
            fixture.add_keys({"age"s, "status"s});
            for(auto age = 0ll; age < 200; ++age)
                fixture.insert(object{{"age"s, age}, {"status"s, age % 10 == 0 ? "A"s : "B"s}});

            // 150 keys of age against 20 postings of A: A drives and age is
            // left to document.match.
            const auto wide = object{{"status"s, "A"s}, {"age"s, object{{"$lt"s, 150ll}}}};
            require_eq(fixture.view_count(wide), 20u);
            require_eq(fixture.count(wide), 15u);

            auto empty = std::stringstream{};
            const auto narrow = object{{"status"s, "A"s}, {"age"s, object{{"$lt"s, 50ll}}}};
            require_eq(fixture.view_count(narrow), 5u);
            require_eq(fixture.index().count(empty, narrow), 5u);

            const auto driving = object{{"status"s, "B"s}, {"age"s, object{{"$lt"s, 100ll}}}};
            require_eq(fixture.view_count(driving), 90u);
            require_eq(fixture.index().count(empty, driving), 90u);
        };

        section("AnIdBoundTrimsEachPostingList") = []
        {
            index_fixture fixture{"./index_id_bound.db"s};
            fixture.add_keys({"status"s});
            for(auto n = 1ll; n <= 8; ++n)
                fixture.insert(object{{"status"s, n % 2 == 0 ? "A"s : "B"s}, {"n"s, n}});
            fixture.update(object{{"_id"s, 2ll}, {"status"s, "A"s}, {"n"s, 20ll}});
            fixture.update(object{{"_id"s, 6ll}, {"status"s, "A"s}, {"n"s, 60ll}});

            const auto after = [](xson::integer_type cursor)
            {
                return object{{"_id"s, object{{"$gt"s, cursor}}}, {"status"s, "A"s}};
            };
            require_container_eq(fixture.walk(after(0)), std::vector<xson::integer_type>{2, 4, 6, 8});
            require_container_eq(fixture.walk(after(3), "n"s), std::vector<xson::integer_type>{4, 60, 8});
            require_container_eq(fixture.walk(after(8)), std::vector<xson::integer_type>{});

            auto backward = after(3);
            backward["$desc"s] = true;
            require_container_eq(fixture.walk(backward), std::vector<xson::integer_type>{8, 6, 4});

            auto empty = std::stringstream{};
            require_eq(fixture.index().count(empty, after(3)), 3u);
            const auto span = object{{"_id"s, object{{"$gte"s, 4ll}, {"$lte"s, 6ll}}}, {"status"s, "A"s}};
            require_eq(fixture.index().count(empty, span), 2u);
        };

        section("APureInBecomesDiscreteRuns") = []
        {
            index_fixture fixture{"./index_in_runs.db"s};
            fixture.add_keys({"status"s, "type"s});
            fixture.insert(object{{"status"s, "active"s}, {"type"s, "x"s}});
            fixture.insert(object{{"status"s, "pending"s}, {"type"s, "x"s}});
            fixture.insert(object{{"status"s, "closed"s}, {"type"s, "y"s}});
            fixture.insert(object{{"status"s, "active"s}, {"type"s, "y"s}});
            fixture.insert(object{{"status"s, "archived"s}, {"type"s, "x"s}});

            const auto three = object{
                {"status"s, object{{"$in"s, object{object::array{object{"active"s}, object{"pending"s}, object{"closed"s}}}}}}};
            require_container_eq(fixture.walk(three), std::vector<xson::integer_type>{1, 2, 3, 4});

            const auto one = object{
                {"status"s, object{{"$in"s, object{object::array{object{"pending"s}}}}}}};
            require_container_eq(fixture.walk(one), std::vector<xson::integer_type>{2});

            const auto none = object{
                {"status"s, object{{"$in"s, object{object::array{object{"missing"s}, object{"gone"s}}}}}}};
            require_container_eq(fixture.walk(none), std::vector<xson::integer_type>{});

            const auto empty_list = object{
                {"status"s, object{{"$in"s, object{object::array{}}}}}};
            require_container_eq(fixture.walk(empty_list), std::vector<xson::integer_type>{});

            // OData stores membership as a map of indexed values.
            const auto as_map = object{
                {"status"s, object{{"$in"s, object{{"0"s, "active"s}, {"1"s, "closed"s}}}}}};
            require_container_eq(fixture.walk(as_map), std::vector<xson::integer_type>{1, 3, 4});

            auto empty = std::stringstream{};
            require_eq(fixture.index().count(empty, three), 4u);
            require_eq(fixture.index().count(empty, none), 0u);
            require_eq(fixture.index().count(empty, empty_list), 0u);
        };

        section("AnInIntersectsAnotherExactBound") = []
        {
            index_fixture fixture{"./index_in_intersect.db"s};
            fixture.add_keys({"status"s, "type"s});
            fixture.insert(object{{"status"s, "A"s}, {"type"s, "x"s}});
            fixture.insert(object{{"status"s, "B"s}, {"type"s, "x"s}});
            fixture.insert(object{{"status"s, "A"s}, {"type"s, "y"s}});
            fixture.insert(object{{"status"s, "C"s}, {"type"s, "x"s}});

            const auto both = object{
                {"status"s, object{{"$in"s, object{object::array{object{"A"s}, object{"B"s}}}}}},
                {"type"s, "x"s}};
            require_container_eq(fixture.walk(both), std::vector<xson::integer_type>{1, 2});

            auto empty = std::stringstream{};
            require_eq(fixture.index().count(empty, both), 2u);
        };

        section("AnInTrimsByIdRange") = []
        {
            index_fixture fixture{"./index_in_id.db"s};
            fixture.add_keys({"status"s});
            fixture.insert(object{{"status"s, "A"s}});
            fixture.insert(object{{"status"s, "B"s}});
            fixture.insert(object{{"status"s, "A"s}});
            fixture.insert(object{{"status"s, "B"s}});
            fixture.insert(object{{"status"s, "C"s}});

            const auto after = object{
                {"_id"s, object{{"$gt"s, 2ll}}},
                {"status"s, object{{"$in"s, object{object::array{object{"A"s}, object{"B"s}}}}}}};
            require_container_eq(fixture.walk(after), std::vector<xson::integer_type>{3, 4});

            auto empty = std::stringstream{};
            require_eq(fixture.index().count(empty, after), 2u);
        };

        section("AWideInFallsBackAndStillMatches") = []
        {
            index_fixture fixture{"./index_in_wide.db"s};
            fixture.add_keys({"status"s});
            fixture.insert(object{{"status"s, "keep"s}});
            fixture.insert(object{{"status"s, "other"s}});

            auto members = object::array{};
            for(auto i = 0; i < 65; ++i)
                members.push_back(object{"v"s + std::to_string(i)});
            members.push_back(object{"keep"s});

            const auto wide = object{{"status"s, object{{"$in"s, object{std::move(members)}}}}};
            // Over the run cap the planner falls back to a loose walk; match()
            // still keeps the membership correct on count.
            require_eq(fixture.view_count(wide), 2u);
            require_eq(fixture.count(wide), 1u);
        };

        section("AnInOnAnUnindexedFieldScans") = []
        {
            index_fixture fixture{"./index_in_unindexed.db"s};
            fixture.add_keys({"type"s});
            fixture.insert(object{{"status"s, "A"s}, {"type"s, "x"s}});
            fixture.insert(object{{"status"s, "B"s}, {"type"s, "x"s}});
            fixture.insert(object{{"status"s, "A"s}, {"type"s, "y"s}});

            const auto by_status = object{
                {"status"s, object{{"$in"s, object{object::array{object{"A"s}}}}}}};
            require_eq(fixture.count(by_status), 2u);
            require_eq(fixture.view_count(by_status), 3u);
        };

        section("UndoPutsInsertedMovedAndRemovedPostingsBack") = []
        {
            index_fixture fixture{"./index_posting_undo.db"s};
            fixture.add_keys({"status"s});
            fixture.insert(object{{"_id"s, 2ll}, {"status"s, "A"s}});
            fixture.insert(object{{"_id"s, 4ll}, {"status"s, "A"s}});
            fixture.insert(object{{"_id"s, 6ll}, {"status"s, "B"s}});

            const auto a = object{{"status"s, "A"s}};
            const auto b = object{{"status"s, "B"s}};
            const auto a_before = fixture.positions(a);
            const auto b_before = fixture.positions(b);

            auto undo = yar::db::index_undo{};
            auto middle = object{{"_id"s, 3ll}, {"status"s, "A"s}};
            fixture.index().insert(middle, 9001, &undo);
            auto gone = object{{"_id"s, 4ll}, {"status"s, "A"s}};
            fixture.index().erase(gone, &undo);
            auto moved = object{{"_id"s, 6ll}, {"status"s, "B"s}};
            fixture.index().replace(moved, moved, 9002, &undo);
            auto changed = object{{"_id"s, 2ll}, {"status"s, "B"s}};
            fixture.index().replace(object{{"_id"s, 2ll}, {"status"s, "A"s}}, changed, 9003, &undo);

            require_container_eq(fixture.positions(a), std::vector<yar::db::position_type>{9001});
            require_container_eq(fixture.positions(b), std::vector<yar::db::position_type>{9003, 9002});

            undo.apply(fixture.index());
            require_container_eq(fixture.positions(a), a_before);
            require_container_eq(fixture.positions(b), b_before);
            require_eq(fixture.count(object{}), 3u);
        };

        section("LoadOrdersPostingsAndKeepsTheLaterLiveVersion") = []
        {
            index_fixture fixture{"./index_posting_load.db"s};
            fixture.add_keys({"status"s});
            fixture.load(object{{"_id"s, 5ll}, {"status"s, "A"s}});
            fixture.load(object{{"_id"s, 3ll}, {"status"s, "A"s}});
            // A crash between a successor's append and its prior's tombstone
            // leaves two live records for _id 5.
            fixture.load(object{{"_id"s, 5ll}, {"status"s, "B"s}});
            fixture.load(object{{"_id"s, 1ll}, {"status"s, "A"s}});
            fixture.index().finish_load();

            require_container_eq(fixture.walk(object{{"status"s, "A"s}}), std::vector<xson::integer_type>{1, 3});
            require_container_eq(fixture.walk(object{{"status"s, "B"s}}), std::vector<xson::integer_type>{5});
            require_eq(fixture.count(object{}), 3u);
        };
    };

    return true;
}

const auto test_registrar = test_set();

} // namespace yar::index_unit_test
