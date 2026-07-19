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
        if(not m_index.update(doc))
            throw std::logic_error("index::update failed to assign _id");
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

    std::size_t view_count(const object& selector)
    {
        return std::ranges::distance(m_index.view(selector));
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

        section("CountMultiFieldAndUsesScanFallback") = []
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

        section("UpdateRejectsAutoIdWhenSequenceIsExhausted") = []
        {
            // A client-supplied _id of INT64_MAX advances m_sequence to the
            // limit. The next auto-id assignment must fail instead of
            // overflowing signed ++m_sequence.
            auto index = yar::db::index{};
            auto max_id = object{{"_id"s, std::numeric_limits<xson::integer_type>::max()}};
            require_true(index.update(max_id));

            auto auto_id = object{{"name"s, "next"s}};
            require_false(index.update(auto_id));
            require_false(auto_id.has("_id"s));
        };
    };

    return true;
}

const auto test_registrar = test_set();

} // namespace yar::index_unit_test