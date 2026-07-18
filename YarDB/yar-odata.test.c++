module yar;
import :odata;
import tester;
import std;
import net;
import xson;

namespace yar::odata_unit_test {

using namespace std;
using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace xson;
using namespace yar::http::odata;
using namespace tester::basic;
using namespace tester::assertions;

inline auto parse_and_filter(std::string_view expr)
{
    const auto parsed = parse_filter(expr);
    if(parsed.has_or())
        throw std::runtime_error{"expected AND-only filter expression"};
    return std::make_pair(parsed.and_selector, parsed.and_string_filters);
}

auto register_odata_tests()
{
    using namespace tester::bdd;

    // Test parse_metadata_level
    scenario("parse_metadata_level parses Accept header correctly, [yardb]") = []
    {
        given("Various Accept header values") = []
        {
            when("Accept header contains odata=fullmetadata") = []
            {
                auto headers = ::http::headers{};
                headers.set("accept"s, "application/json;odata=fullmetadata"s);
                
                then("Returns full metadata level") = [headers]
                {
                    const auto level = parse_metadata_level(headers);
                    require_eq(static_cast<int>(level), static_cast<int>(metadata_level::full));
                };
            };

            when("Accept header contains odata=minimalmetadata") = []
            {
                auto headers = ::http::headers{};
                headers.set("accept"s, "application/json;odata=minimalmetadata"s);
                
                then("Returns minimal metadata level") = [headers]
                {
                    const auto level = parse_metadata_level(headers);
                    require_eq(static_cast<int>(level), static_cast<int>(metadata_level::minimal));
                };
            };

            when("Accept header contains odata=nometadata") = []
            {
                auto headers = ::http::headers{};
                headers.set("accept"s, "application/json;odata=nometadata"s);
                
                then("Returns none metadata level") = [headers]
                {
                    const auto level = parse_metadata_level(headers);
                    require_eq(static_cast<int>(level), static_cast<int>(metadata_level::none));
                };
            };

            when("Accept header is missing") = []
            {
                auto headers = ::http::headers{};
                
                then("Returns none metadata level") = [headers]
                {
                    const auto level = parse_metadata_level(headers);
                    require_eq(static_cast<int>(level), static_cast<int>(metadata_level::none));
                };
            };

            when("Accept header has no odata parameter") = []
            {
                auto headers = ::http::headers{};
                headers.set("accept"s, "application/json"s);
                
                then("Returns none metadata level") = [headers]
                {
                    const auto level = parse_metadata_level(headers);
                    require_eq(static_cast<int>(level), static_cast<int>(metadata_level::none));
                };
            };
        };
    };

    // Test add_metadata
    scenario("add_metadata adds correct metadata based on level, [yardb]") = []
    {
        given("A document and collection name") = []
        {
            auto doc = xson::object{{"name"s, "Test"s}, {"value"s, 42}};
            const auto collection = "testitems"sv;
            
            when("Metadata level is none") = [doc, collection]
            {
                then("Document is unchanged") = [doc, collection]
                {
                    const auto result = add_metadata(doc, metadata_level::none, collection);
                    require_eq(result.size(), 2u);
                    require_false(result.has("@odata.context"s));
                };
            };

            when("Metadata level is minimal") = [doc, collection]
            {
                then("Document has @odata.context only") = [doc, collection]
                {
                    const auto result = add_metadata(doc, metadata_level::minimal, collection, 123);
                    require_true(result.has("@odata.context"s));
                    require_eq(result["@odata.context"s].get<string>(), "/$metadata#testitems/$entity"s);
                    require_false(result.has("@odata.id"s));
                };
            };

            when("Metadata level is full with ID") = [doc, collection]
            {
                then("Document has all metadata fields") = [doc, collection]
                {
                    const auto result = add_metadata(doc, metadata_level::full, collection, 123);
                    require_true(result.has("@odata.context"s));
                    require_eq(result["@odata.context"s].get<string>(), "/$metadata#testitems/$entity"s);
                    require_true(result.has("@odata.id"s));
                    require_eq(result["@odata.id"s].get<string>(), "/testitems/123"s);
                    require_true(result.has("@odata.editLink"s));
                    require_eq(result["@odata.editLink"s].get<string>(), "/testitems/123"s);
                };
            };

            when("Metadata level is full without ID") = [doc, collection]
            {
                then("Document has @odata.context but no ID fields") = [doc, collection]
                {
                    const auto result = add_metadata(doc, metadata_level::full, collection);
                    require_true(result.has("@odata.context"s));
                    require_eq(result["@odata.context"s].get<string>(), "/$metadata#testitems"s);
                    require_false(result.has("@odata.id"s));
                };
            };
        };
    };

    // Test add_metadata_to_array
    scenario("add_metadata_to_array wraps array with metadata, [yardb]") = []
    {
        given("An array of documents") = []
        {
            auto array = xson::object{xson::object::array{
                xson::object{{"name"s, "Item 1"s}, {"_id"s, 1}},
                xson::object{{"name"s, "Item 2"s}, {"_id"s, 2}}
            }};
            const auto collection = "testitems"sv;
            
            when("Metadata level is minimal") = [array, collection]
            {
                then("Array is wrapped with @odata.context and value") = [array, collection]
                {
                    const auto result = add_metadata_to_array(array, metadata_level::minimal, collection);
                    require_false(result.is_array());
                    require_true(result.has("@odata.context"s));
                    require_eq(result["@odata.context"s].get<string>(), "/$metadata#testitems"s);
                    require_true(result.has("value"s));
                    require_true(result["value"s].is_array());
                };
            };

            when("Metadata level is full") = [array, collection]
            {
                then("Array is wrapped and items have metadata") = [array, collection]
                {
                    const auto result = add_metadata_to_array(array, metadata_level::full, collection);
                    require_true(result.has("@odata.context"s));
                    require_true(result.has("value"s));
                    const auto& items = result["value"s].get<xson::object::array>();
                    require_eq(items.size(), 2u);
                    require_true(items[0].has("@odata.context"s));
                    require_true(items[0].has("@odata.id"s));
                };
            };
        };
    };

    // Test parse_filter
    scenario("parse_filter parses various filter expressions, [yardb]") = []
    {
        given("Various filter expressions") = []
        {
            when("Filter is 'field eq value'") = []
            {
                then("Returns correct selector") = []
                {
                    const auto [selector, filters] = parse_and_filter("name eq 'John'"sv);
                    require_true(selector.has("name"s));
                    require_eq(selector["name"s].get<string>(), "John"s);
                    require_true(filters.empty());
                };
            };

            when("Filter is 'field ne value'") = []
            {
                then("Returns selector with $ne operator") = []
                {
                    const auto [selector, filters] = parse_and_filter("status ne 'deleted'"sv);
                    require_true(selector.has("status"s));
                    require_true(selector["status"s].has("$ne"s));
                    require_eq(selector["status"s]["$ne"s].get<string>(), "deleted"s);
                    require_true(filters.empty());
                };
            };

            when("Filter is 'field ne number'") = []
            {
                then("Returns selector with numeric $ne operator") = []
                {
                    const auto [selector, filters] = parse_and_filter("age ne 25"sv);
                    require_true(selector.has("age"s));
                    const auto json_str = xson::json::stringify(selector);
                    require_true(json_str.find("\"$ne\"") != std::string::npos);
                    require_true(json_str.find("25") != std::string::npos);
                    require_true(filters.empty());
                };
            };

            when("Filter is 'field gt number'") = []
            {
                then("Returns selector with $gt operator") = []
                {
                    const auto [selector, filters] = parse_and_filter("age gt 25"sv);
                    require_true(selector.has("age"s));
                    // Verify the structure by checking JSON stringification
                    const auto json_str = xson::json::stringify(selector);
                    require_true(json_str.find("\"$gt\"") != std::string::npos);
                    require_true(json_str.find("25") != std::string::npos);
                    require_true(filters.empty());
                };
            };

            when("Filter is 'field ge number'") = []
            {
                then("Returns selector with $gte operator") = []
                {
                    const auto [selector, filters] = parse_and_filter("age ge 25"sv);
                    require_true(selector.has("age"s));
                    const auto json_str = xson::json::stringify(selector);
                    require_true(json_str.find("\"$gte\"") != std::string::npos);
                    require_true(json_str.find("25") != std::string::npos);
                    require_true(filters.empty());
                };
            };

            when("Filter is 'field lt number'") = []
            {
                then("Returns selector with $lt operator") = []
                {
                    const auto [selector, filters] = parse_and_filter("age lt 100"sv);
                    require_true(selector.has("age"s));
                    const auto json_str = xson::json::stringify(selector);
                    require_true(json_str.find("\"$lt\"") != std::string::npos);
                    require_true(json_str.find("100") != std::string::npos);
                    require_true(filters.empty());
                };
            };

            when("Filter is 'field le number'") = []
            {
                then("Returns selector with $lte operator") = []
                {
                    const auto [selector, filters] = parse_and_filter("age le 100"sv);
                    require_true(selector.has("age"s));
                    const auto json_str = xson::json::stringify(selector);
                    require_true(json_str.find("\"$lte\"") != std::string::npos);
                    require_true(json_str.find("100") != std::string::npos);
                    require_true(filters.empty());
                };
            };

            when("Filter is 'startswith(field, value)'") = []
            {
                then("Returns string filter") = []
                {
                    const auto [selector, filters] = parse_and_filter("startswith(name, 'John')"sv);
                    require_true(selector.empty());
                    require_eq(filters.size(), 1u);
                    require_eq(filters[0].function, "startswith"sv);
                    require_eq(filters[0].field, "name"sv);
                    require_eq(filters[0].value, "John"sv);
                };
            };

            when("Filter is 'contains(field, value)'") = []
            {
                then("Returns string filter") = []
                {
                    const auto [selector, filters] = parse_and_filter("contains(email, '@example')"sv);
                    require_true(selector.empty());
                    require_eq(filters.size(), 1u);
                    require_eq(filters[0].function, "contains"sv);
                    require_eq(filters[0].field, "email"sv);
                    require_eq(filters[0].value, "@example"sv);
                };
            };

            when("Filter is 'endswith(field, value)'") = []
            {
                then("Returns string filter") = []
                {
                    const auto [selector, filters] = parse_and_filter("endswith(path, '.pdf')"sv);
                    require_true(selector.empty());
                    require_eq(filters.size(), 1u);
                    require_eq(filters[0].function, "endswith"sv);
                    require_eq(filters[0].field, "path"sv);
                    require_eq(filters[0].value, ".pdf"sv);
                };
            };

            when("Filter uses 'and' operator") = []
            {
                then("Returns merged selector") = []
                {
                    const auto [selector, filters] = parse_and_filter("age gt 25 and status eq 'active'"sv);
                    require_true(selector.has("age"s));
                    require_true(selector.has("status"s));
                    const auto json_str = xson::json::stringify(selector);
                    require_true(json_str.find("\"$gt\"") != std::string::npos);
                    require_true(json_str.find("25") != std::string::npos);
                    require_true(json_str.find("\"status\"") != std::string::npos);
                    require_true(json_str.find("\"active\"") != std::string::npos);
                    require_true(filters.empty());
                };
            };

            when("Filter ANDs equality with a range on the same field") = []
            {
                then("Keeps both $eq and $gt instead of last-write-wins") = []
                {
                    const auto [selector, filters] = parse_and_filter("age eq 10 and age gt 5"sv);
                    require_true(filters.empty());
                    require_true(selector.has("age"s));
                    require_true(selector["age"s].has("$eq"s));
                    require_true(selector["age"s].has("$gt"s));
                    require_eq(static_cast<xson::integer_type>(selector["age"s]["$eq"s]), 10);
                    require_eq(static_cast<xson::integer_type>(selector["age"s]["$gt"s]), 5);

                    // Reverse order must also preserve both predicates.
                    const auto [selector2, filters2] = parse_and_filter("age gt 5 and age eq 10"sv);
                    require_true(filters2.empty());
                    require_true(selector2["age"s].has("$eq"s));
                    require_true(selector2["age"s].has("$gt"s));
                    require_eq(static_cast<xson::integer_type>(selector2["age"s]["$eq"s]), 10);
                    require_eq(static_cast<xson::integer_type>(selector2["age"s]["$gt"s]), 5);
                };
            };

            when("Filter ANDs contradictory equalities on the same field") = []
            {
                then("Builds an impossible predicate that matches nothing") = []
                {
                    const auto [selector, filters] = parse_and_filter(
                        "status eq 'a' and status eq 'b'"sv);
                    require_true(filters.empty());
                    require_true(selector.has("status"s));
                    require_true(selector["status"s].has("$eq"s));
                    require_true(selector["status"s].has("$ne"s));
                    require_eq(selector["status"s]["$eq"s].get<string>(), "a"s);
                    require_eq(selector["status"s]["$ne"s].get<string>(), "a"s);

                    require_false(object{{"status"s, "a"s}}.match(selector));
                    require_false(object{{"status"s, "b"s}}.match(selector));
                };
            };

            when("Filter ANDs the same range operator twice on one field") = []
            {
                then("Keeps the tighter bound instead of last-write-wins") = []
                {
                    // Weaker bound last must not loosen the earlier predicate.
                    const auto [selector, filters] = parse_and_filter("age gt 20 and age gt 10"sv);
                    require_true(filters.empty());
                    require_true(selector.has("age"s));
                    require_true(selector["age"s].has("$gt"s));
                    require_false(selector["age"s].has("$eq"s));
                    require_eq(static_cast<xson::integer_type>(selector["age"s]["$gt"s]), 20);

                    const auto [selector2, filters2] = parse_and_filter("age lt 5 and age lt 10"sv);
                    require_true(filters2.empty());
                    require_eq(static_cast<xson::integer_type>(selector2["age"s]["$lt"s]), 5);
                };
            };

            when("Filter uses 'or' operator") = []
            {
                then("Returns two OR branches") = []
                {
                    const auto parsed = parse_filter("age gt 25 or status eq 'active'"sv);
                    require_true(parsed.has_or());
                    require_eq(parsed.or_branches.size(), 2u);
                    require_true(parsed.or_branches[0].selector.has("age"s));
                    require_true(parsed.or_branches[1].selector.has("status"s));
                };
            };

            when("Filter uses OData precedence (and before or)") = []
            {
                then("Splits into (a and b) or c") = []
                {
                    const auto parsed = parse_filter("age gt 25 and status eq 'active' or age lt 10"sv);
                    require_true(parsed.has_or());
                    require_eq(parsed.or_branches.size(), 2u);
                    require_true(parsed.or_branches[0].selector.has("age"s));
                    require_true(parsed.or_branches[0].selector.has("status"s));
                    require_true(parsed.or_branches[1].selector.has("age"s));
                    require_false(parsed.or_branches[1].selector.has("status"s));
                };
            };

            when("Filter uses 'in' operator with strings") = []
            {
                then("Returns selector with $in map") = []
                {
                    const auto [selector, filters] = parse_and_filter("status in ('active','pending')"sv);
                    require_true(filters.empty());
                    require_true(selector.has("status"s));

                    const auto& status_selector = selector["status"s];
                    require_true(status_selector.has("$in"s));
                    const auto& in_map = status_selector["$in"s];
                    require_eq(in_map.size(), 2u);
                    require_eq(in_map["0"s].get<string>(), "active"s);
                    require_eq(in_map["1"s].get<string>(), "pending"s);
                };
            };

            when("Filter uses 'in' operator with spaced strings") = []
            {
                then("Returns selector with $in map") = []
                {
                    const auto [selector, filters] = parse_and_filter("status in ('active', 'pending')"sv);
                    require_true(filters.empty());
                    require_true(selector.has("status"s));
                    require_eq(selector["status"s]["$in"s].size(), 2u);
                };
            };

            when("Filter uses 'in' operator with numbers") = []
            {
                then("Returns selector with numeric $in map") = []
                {
                    const auto [selector, filters] = parse_and_filter("id in (1, 2, 3)"sv);
                    require_true(filters.empty());
                    require_true(selector.has("id"s));
                    require_eq(static_cast<xson::integer_type>(selector["id"s]["$in"s]["0"s]), 1ll);
                    require_eq(static_cast<xson::integer_type>(selector["id"s]["$in"s]["2"s]), 3ll);
                };
            };

            when("Filter combines 'and' with 'in' operator") = []
            {
                then("Returns merged selector") = []
                {
                    const auto [selector, filters] = parse_and_filter("age gt 25 and status in ('active','pending')"sv);
                    require_true(filters.empty());
                    require_true(selector.has("age"s));
                    require_true(selector.has("status"s));
                    require_true(selector["status"s].has("$in"s));
                    const auto json_str = xson::json::stringify(selector);
                    require_true(json_str.find("\"$gt\"") != std::string::npos);
                    require_true(json_str.find("\"$in\"") != std::string::npos);
                };
            };

            when("Filter ANDs two overlapping 'in' lists on one field") = []
            {
                then("Intersects membership instead of last-write-wins") = []
                {
                    const auto [selector, filters] =
                        parse_and_filter("status in ('a','b') and status in ('b','c')"sv);
                    require_true(filters.empty());
                    require_true(selector.has("status"s));
                    require_true(selector["status"s].has("$in"s));
                    const auto& in_map = selector["status"s]["$in"s];
                    require_eq(in_map.size(), 1u);
                    require_eq(in_map["0"s].get<string>(), "b"s);

                    require_true(object{{"status"s, "b"s}}.match(selector));
                    require_false(object{{"status"s, "a"s}}.match(selector));
                    require_false(object{{"status"s, "c"s}}.match(selector));

                    const auto [empty_sel, empty_filters] =
                        parse_and_filter("status in ('a') and status in ('b')"sv);
                    require_true(empty_filters.empty());
                    require_eq(empty_sel["status"s]["$in"s].size(), 0u);
                    require_false(object{{"status"s, "a"s}}.match(empty_sel));
                    require_false(object{{"status"s, "b"s}}.match(empty_sel));
                };
            };

            when("Filter ANDs two inequalities on one field") = []
            {
                then("Unions exclusions into $nin instead of last-write-wins") = []
                {
                    const auto [selector, filters] =
                        parse_and_filter("status ne 'a' and status ne 'b'"sv);
                    require_true(filters.empty());
                    require_true(selector.has("status"s));
                    require_true(selector["status"s].has("$nin"s));
                    require_false(selector["status"s].has("$ne"s));
                    const auto& nin_map = selector["status"s]["$nin"s];
                    require_eq(nin_map.size(), 2u);
                    require_eq(nin_map["0"s].get<string>(), "a"s);
                    require_eq(nin_map["1"s].get<string>(), "b"s);

                    require_true(object{{"status"s, "c"s}}.match(selector));
                    require_false(object{{"status"s, "a"s}}.match(selector));
                    require_false(object{{"status"s, "b"s}}.match(selector));

                    // A later $ne must not resurrect a contradictory eq pair by
                    // overwriting the sentinel $ne that encodes impossibility.
                    const auto [contra, contra_filters] =
                        parse_and_filter("status eq 'a' and status eq 'b' and status ne 'c'"sv);
                    require_true(contra_filters.empty());
                    require_true(contra["status"s].has("$eq"s));
                    require_true(contra["status"s].has("$nin"s));
                    require_false(contra["status"s].has("$ne"s));
                    require_false(object{{"status"s, "a"s}}.match(contra));
                    require_false(object{{"status"s, "b"s}}.match(contra));
                    require_false(object{{"status"s, "c"s}}.match(contra));
                };
            };

            when("Filter uses empty 'in' list") = []
            {
                then("Throws invalid_argument") = []
                {
                    require_throws([]
                    {
                        parse_filter("status in ()"sv);
                    });
                };
            };

            when("Filter is delimiter-only or") = []
            {
                then("Throws instead of matching all documents") = []
                {
                    require_throws([]
                    {
                        parse_filter(" or "sv);
                    });
                    require_throws([]
                    {
                        parse_filter(" or  or "sv);
                    });
                };
            };

            when("Filter is delimiter-only and") = []
            {
                then("Throws instead of matching all documents") = []
                {
                    require_throws([]
                    {
                        parse_filter(" and "sv);
                    });
                };
            };

            when("Filter uses mixed-type 'in' list") = []
            {
                then("Throws invalid_argument") = []
                {
                    require_throws([]
                    {
                        parse_filter("status in ('active', 1)"sv);
                    });
                };
            };

            when("Filter uses nested property path") = []
            {
                then("Returns nested selector") = []
                {
                    const auto [selector, filters] = parse_and_filter("Customer/Country eq 'USA'"sv);
                    require_true(filters.empty());
                    require_true(selector.has("Customer"s));
                    require_true(selector["Customer"s].has("Country"s));
                    require_eq(selector["Customer"s]["Country"s].get<string>(), "USA"s);
                };
            };

            when("Filter combines nested paths with and") = []
            {
                then("Deep-merges nested selector") = []
                {
                    const auto [selector, filters] = parse_and_filter(
                        "Customer/Country eq 'USA' and Customer/Name eq 'Acme'"sv);
                    require_true(filters.empty());
                    require_true(selector["Customer"s].has("Country"s));
                    require_true(selector["Customer"s].has("Name"s));
                    require_eq(selector["Customer"s]["Country"s].get<string>(), "USA"s);
                    require_eq(selector["Customer"s]["Name"s].get<string>(), "Acme"s);
                };
            };

            when("Filter uses nested path in startswith") = []
            {
                then("Returns string filter with path") = []
                {
                    const auto [selector, filters] = parse_and_filter("startswith(Customer/Name, 'Ac')"sv);
                    require_true(selector.empty());
                    require_eq(filters.size(), 1u);
                    require_eq(filters[0].field, "Customer/Name"sv);
                    require_eq(filters[0].value, "Ac"sv);
                };
            };

            when("Filter value contains and inside quotes") = []
            {
                then("Parses as a single eq comparison") = []
                {
                    const auto [selector, filters] = parse_and_filter("description eq 'rock and roll'"sv);
                    require_true(filters.empty());
                    require_true(selector.has("description"s));
                    require_eq(selector["description"s].get<string>(), "rock and roll"s);
                };
            };

            when("Filter combines quoted and with a second predicate") = []
            {
                then("Splits only on top-level and") = []
                {
                    const auto [selector, filters] = parse_and_filter(
                        "name eq 'Smith and Sons' and city eq 'Boston'"sv);
                    require_true(filters.empty());
                    require_true(selector.has("name"s));
                    require_true(selector.has("city"s));
                    require_eq(selector["name"s].get<string>(), "Smith and Sons"s);
                    require_eq(selector["city"s].get<string>(), "Boston"s);
                };
            };

            when("Filter uses or with quoted or inside contains") = []
            {
                then("Splits only on top-level or") = []
                {
                    const auto parsed = parse_filter(
                        "contains(message, 'error or warning') or level eq 'critical'"sv);
                    require_true(parsed.has_or());
                    require_eq(parsed.or_branches.size(), 2u);
                    require_eq(parsed.or_branches[0].string_filters.size(), 1u);
                    require_eq(parsed.or_branches[0].string_filters[0].value, "error or warning"sv);
                    require_true(parsed.or_branches[1].selector.has("level"s));
                    require_eq(parsed.or_branches[1].selector["level"s].get<string>(), "critical"s);
                };
            };

            when("Filter function argument contains a comma") = []
            {
                then("Parses contains value with comma") = []
                {
                    const auto [selector, filters] = parse_and_filter("contains(tag, 'a,b')"sv);
                    require_true(selector.empty());
                    require_eq(filters.size(), 1u);
                    require_eq(filters[0].function, "contains"sv);
                    require_eq(filters[0].field, "tag"sv);
                    require_eq(filters[0].value, "a,b"sv);
                };
            };

            when("Filter in list value contains and") = []
            {
                then("Parses both quoted list entries") = []
                {
                    const auto [selector, filters] = parse_and_filter(
                        "status in ('active and pending','done')"sv);
                    require_true(filters.empty());
                    require_true(selector.has("status"s));
                    const auto& in_map = selector["status"s]["$in"s];
                    require_eq(in_map.size(), 2u);
                    require_eq(in_map["0"s].get<string>(), "active and pending"s);
                    require_eq(in_map["1"s].get<string>(), "done"s);
                };
            };

            when("Filter comparison value contains eq token") = []
            {
                then("Finds operator outside quotes") = []
                {
                    const auto [selector, filters] = parse_and_filter("note eq 'a eq b'"sv);
                    require_true(filters.empty());
                    require_true(selector.has("note"s));
                    require_eq(selector["note"s].get<string>(), "a eq b"s);
                };
            };

            when("Filter value contains in marker inside quotes") = []
            {
                then("Parses eq instead of hijacking as in") = []
                {
                    const auto [selector, filters] = parse_and_filter("note eq 'x in (y)'"sv);
                    require_true(filters.empty());
                    require_true(selector.has("note"s));
                    require_eq(selector["note"s].get<string>(), "x in (y)"s);
                };

                then("Parses contains instead of hijacking as in") = []
                {
                    const auto [selector, filters] = parse_and_filter("contains(note, 'a in (b)')"sv);
                    require_true(selector.empty());
                    require_eq(filters.size(), 1u);
                    require_eq(filters[0].function, "contains"sv);
                    require_eq(filters[0].field, "note"s);
                    require_eq(filters[0].value, "a in (b)"s);
                };
            };

            when("Filter string literal uses OData doubled quotes") = []
            {
                then("Unescapes eq value") = []
                {
                    const auto [selector, filters] = parse_and_filter("name eq 'O''Brien'"sv);
                    require_true(filters.empty());
                    require_eq(selector["name"s].get<string>(), "O'Brien"s);
                };

                then("Unescapes in list value") = []
                {
                    const auto [selector, filters] = parse_and_filter("name in ('O''Brien','Smith')"sv);
                    require_true(filters.empty());
                    require_eq(selector["name"s]["$in"s]["0"s].get<string>(), "O'Brien"s);
                    require_eq(selector["name"s]["$in"s]["1"s].get<string>(), "Smith"s);
                };

                then("Unescapes contains value") = []
                {
                    const auto [selector, filters] = parse_and_filter("contains(name, 'O''Brien')"sv);
                    require_true(selector.empty());
                    require_eq(filters.size(), 1u);
                    require_eq(filters[0].value, "O'Brien"s);
                };
            };
        };
    };

    scenario("parse_filter nested paths match documents, [yardb]") = []
    {
        given("Documents with nested Customer object") = []
        {
            auto docs = object{object::array{
                object{{"name"s, "Alice"s}, {"Customer"s, object{{"Country"s, "USA"s}, {"Name"s, "Acme"s}}}},
                object{{"name"s, "Bob"s}, {"Customer"s, object{{"Country"s, "UK"s}, {"Name"s, "Beta"s}}}}
            }};

            when("Selector uses nested eq filter") = [docs]
            {
                then("Returns matching documents") = [docs]
                {
                    const auto [selector, filters] = parse_and_filter("Customer/Country eq 'USA'"sv);
                    require_true(filters.empty());

                    auto result = object::array{};
                    for(const auto& doc : docs.get<object::array>())
                    {
                        if(doc.match(selector))
                            result.push_back(doc);
                    }

                    require_eq(result.size(), 1u);
                    require_eq(result[0]["name"s].get<string>(), "Alice"s);
                };
            };

            when("String filter uses nested startswith") = [docs]
            {
                then("Returns matching documents") = [docs]
                {
                    const auto [selector, filters] = parse_and_filter("startswith(Customer/Name, 'Ac')"sv);
                    require_true(selector.empty());

                    const auto filtered = apply_string_filters(docs, filters);
                    require_eq(filtered.get<object::array>().size(), 1u);
                    require_eq(filtered[0]["name"s].get<string>(), "Alice"s);
                };
            };

            when("Scalar filter targets an object-valued field") = [docs]
            {
                then("Matches nothing and does not throw") = [docs]
                {
                    // Customer is a nested object; Customer eq/ne must not
                    // throw bad_variant_access or falsely match every document.
                    const auto eq = parse_filter("Customer eq 'Acme'"sv);
                    require_false(eq.has_or());

                    auto eq_matches = 0u;
                    for(const auto& doc : docs.get<object::array>())
                    {
                        if(doc.match(eq.and_selector))
                            ++eq_matches;
                    }
                    require_eq(eq_matches, 0u);

                    const auto ne = parse_filter("Customer ne 'Acme'"sv);
                    require_false(ne.has_or());

                    auto ne_matches = 0u;
                    for(const auto& doc : docs.get<object::array>())
                    {
                        if(doc.match(ne.and_selector))
                            ++ne_matches;
                    }
                    require_eq(ne_matches, 0u);
                };
            };
        };
    };

    scenario("parse_filter grouping parentheses match documents, [yardb]") = []
    {
        given("Documents with name and status fields") = []
        {
            auto docs = object{object::array{
                object{{"name"s, "Alice"s}, {"status"s, "active"s}, {"age"s, 30}},
                object{{"name"s, "Bob"s}, {"status"s, "inactive"s}, {"age"s, 20}}
            }};

            when("Filter wraps a leaf in parentheses") = [docs]
            {
                then("Matches the same documents as the ungrouped filter") = [docs]
                {
                    const auto [selector, filters] = parse_and_filter("(name eq 'Alice')"sv);
                    require_true(filters.empty());
                    require_true(selector.has("name"s));
                    require_eq(selector["name"s].get<string>(), "Alice"s);

                    auto result = object::array{};
                    for(const auto& doc : docs.get<object::array>())
                    {
                        if(doc.match(selector))
                            result.push_back(doc);
                    }

                    require_eq(result.size(), 1u);
                    require_eq(result[0]["name"s].get<string>(), "Alice"s);
                };
            };

            when("Filter wraps an AND group in parentheses") = [docs]
            {
                then("Applies both predicates") = [docs]
                {
                    const auto [selector, filters] = parse_and_filter("(name eq 'Alice' and age gt 25)"sv);
                    require_true(filters.empty());

                    auto result = object::array{};
                    for(const auto& doc : docs.get<object::array>())
                    {
                        if(doc.match(selector))
                            result.push_back(doc);
                    }

                    require_eq(result.size(), 1u);
                    require_eq(result[0]["name"s].get<string>(), "Alice"s);
                };
            };

            when("Filter parenthesizes each OR branch") = [docs]
            {
                then("Matches either branch") = [docs]
                {
                    const auto parsed = parse_filter("(name eq 'Alice') or (status eq 'inactive')"sv);
                    require_true(parsed.has_or());
                    require_eq(parsed.or_branches.size(), 2u);

                    auto result = object::array{};
                    for(const auto& doc : docs.get<object::array>())
                    {
                        auto matched = false;
                        for(const auto& branch : parsed.or_branches)
                        {
                            if(doc.match(branch.selector))
                            {
                                matched = true;
                                break;
                            }
                        }
                        if(matched)
                            result.push_back(doc);
                    }

                    require_eq(result.size(), 2u);
                };
            };

            when("Filter parenthesizes each AND operand") = [docs]
            {
                then("Applies both leaf predicates") = [docs]
                {
                    const auto [selector, filters] = parse_and_filter("(name eq 'Alice') and (status eq 'active')"sv);
                    require_true(filters.empty());

                    auto result = object::array{};
                    for(const auto& doc : docs.get<object::array>())
                    {
                        if(doc.match(selector))
                            result.push_back(doc);
                    }

                    require_eq(result.size(), 1u);
                    require_eq(result[0]["name"s].get<string>(), "Alice"s);
                };
            };

            when("Filter groups OR before AND with parentheses") = [docs]
            {
                then("Distributes to matching OR branches") = [docs]
                {
                    // (Alice or Bob) and age>25 → Alice(30) matches; Bob(20) does not
                    const auto parsed = parse_filter(
                        "(name eq 'Alice' or name eq 'Bob') and age gt 25"sv);
                    require_true(parsed.has_or());
                    require_eq(parsed.or_branches.size(), 2u);

                    auto result = object::array{};
                    for(const auto& doc : docs.get<object::array>())
                    {
                        auto matched = false;
                        for(const auto& branch : parsed.or_branches)
                        {
                            if(doc.match(branch.selector))
                            {
                                matched = true;
                                break;
                            }
                        }
                        if(matched)
                            result.push_back(doc);
                    }

                    require_eq(result.size(), 1u);
                    require_eq(result[0]["name"s].get<string>(), "Alice"s);
                };
            };

            when("Filter groups OR after AND with parentheses") = [docs]
            {
                then("Keeps the AND outside the OR group") = [docs]
                {
                    // age>25 and (Alice or inactive) → Alice(30) matches; Bob inactive but age 20 does not
                    const auto parsed = parse_filter(
                        "age gt 25 and (name eq 'Alice' or status eq 'inactive')"sv);
                    require_true(parsed.has_or());
                    require_eq(parsed.or_branches.size(), 2u);

                    auto result = object::array{};
                    for(const auto& doc : docs.get<object::array>())
                    {
                        auto matched = false;
                        for(const auto& branch : parsed.or_branches)
                        {
                            if(doc.match(branch.selector))
                            {
                                matched = true;
                                break;
                            }
                        }
                        if(matched)
                            result.push_back(doc);
                    }

                    require_eq(result.size(), 1u);
                    require_eq(result[0]["name"s].get<string>(), "Alice"s);
                };
            };

            when("Filter uses nested outer parentheses") = []
            {
                then("Strips them before parsing") = []
                {
                    const auto [selector, filters] = parse_and_filter("((name eq 'Alice'))"sv);
                    require_true(filters.empty());
                    require_true(selector.has("name"s));
                    require_eq(selector["name"s].get<string>(), "Alice"s);
                };
            };

            when("Filter is empty parentheses") = []
            {
                then("Throws invalid_argument") = []
                {
                    auto thrown = false;
                    try
                    {
                        parse_filter("()"sv);
                    }
                    catch(const std::invalid_argument&)
                    {
                        thrown = true;
                    }
                    require_true(thrown);
                };
            };
        };
    };

    scenario("parse_filter same-field AND keeps all predicates, [yardb]") = []
    {
        given("Documents with overlapping ages and statuses") = []
        {
            const auto test_file = "./odata_same_field_and_test.db"s;

            when("Equality is AND-combined with a range on age") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                require_true(engine.index("users"s, {"age"s}).has_value());

                auto young = object{{"name"s, "young"s}, {"age"s, 3ll}};
                auto exact = object{{"name"s, "exact"s}, {"age"s, 10ll}};
                auto older = object{{"name"s, "older"s}, {"age"s, 20ll}};
                require_true(engine.create("users"s, young).has_value());
                require_true(engine.create("users"s, exact).has_value());
                require_true(engine.create("users"s, older).has_value());

                // Last-write-wins would keep only age gt 5 and return exact+older.
                const auto parsed = parse_filter("age eq 10 and age gt 5"sv);
                const auto docs = read_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Returns only the equality match") = [docs, test_file]
                {
                    require_true(docs.is_array());
                    const auto& items = docs.get<object::array>();
                    require_eq(items.size(), 1u);
                    require_eq(items[0]["name"s].get<string>(), "exact"s);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };

            when("Contradictory equalities are AND-combined on status") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                require_true(engine.index("users"s, {"status"s}).has_value());

                auto a = object{{"name"s, "A"s}, {"status"s, "a"s}};
                auto b = object{{"name"s, "B"s}, {"status"s, "b"s}};
                require_true(engine.create("users"s, a).has_value());
                require_true(engine.create("users"s, b).has_value());

                // Last-write-wins would keep only status eq 'b' and return B.
                const auto parsed = parse_filter("status eq 'a' and status eq 'b'"sv);
                const auto docs = read_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Returns no documents") = [docs, test_file]
                {
                    require_true(docs.is_array());
                    require_eq(docs.get<object::array>().size(), 0u);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };

            when("Same range operator is AND-combined with a looser bound last") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                require_true(engine.index("users"s, {"age"s}).has_value());

                auto mid = object{{"name"s, "mid"s}, {"age"s, 15ll}};
                auto high = object{{"name"s, "high"s}, {"age"s, 25ll}};
                require_true(engine.create("users"s, mid).has_value());
                require_true(engine.create("users"s, high).has_value());

                // Last-write-wins would keep age gt 10 and return mid+high.
                const auto parsed = parse_filter("age gt 20 and age gt 10"sv);
                const auto docs = read_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Returns only ages above the tighter bound") = [docs, test_file]
                {
                    require_true(docs.is_array());
                    const auto& items = docs.get<object::array>();
                    require_eq(items.size(), 1u);
                    require_eq(items[0]["name"s].get<string>(), "high"s);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };

            when("Overlapping 'in' lists are AND-combined on status") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                require_true(engine.index("users"s, {"status"s}).has_value());

                auto a = object{{"name"s, "A"s}, {"status"s, "a"s}};
                auto b = object{{"name"s, "B"s}, {"status"s, "b"s}};
                auto c = object{{"name"s, "C"s}, {"status"s, "c"s}};
                require_true(engine.create("users"s, a).has_value());
                require_true(engine.create("users"s, b).has_value());
                require_true(engine.create("users"s, c).has_value());

                // Last-write-wins would keep only ('b','c') and return B+C.
                const auto parsed =
                    parse_filter("status in ('a','b') and status in ('b','c')"sv);
                const auto docs = read_with_parsed_filter(engine, "users"s, object{}, parsed);
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Returns only the intersection member") = [docs, count, test_file]
                {
                    require_eq(count, 1u);
                    require_true(docs.is_array());
                    const auto& items = docs.get<object::array>();
                    require_eq(items.size(), 1u);
                    require_eq(items[0]["name"s].get<string>(), "B"s);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };

            when("Multiple inequalities are AND-combined on status") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                require_true(engine.index("users"s, {"status"s}).has_value());

                auto a = object{{"name"s, "A"s}, {"status"s, "a"s}};
                auto b = object{{"name"s, "B"s}, {"status"s, "b"s}};
                auto c = object{{"name"s, "C"s}, {"status"s, "c"s}};
                require_true(engine.create("users"s, a).has_value());
                require_true(engine.create("users"s, b).has_value());
                require_true(engine.create("users"s, c).has_value());

                // Last-write-wins would keep only ne 'b' and incorrectly return A+C.
                const auto parsed = parse_filter("status ne 'a' and status ne 'b'"sv);
                const auto docs = read_with_parsed_filter(engine, "users"s, object{}, parsed);
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Returns only rows outside every excluded value") = [docs, count, test_file]
                {
                    require_eq(count, 1u);
                    require_true(docs.is_array());
                    const auto& items = docs.get<object::array>();
                    require_eq(items.size(), 1u);
                    require_eq(items[0]["name"s].get<string>(), "C"s);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };
        };
    };

    scenario("parse_filter or operator matches documents, [yardb]") = []
    {
        given("Documents with age and status fields") = []
        {
            auto docs = object{object::array{
                object{{"name"s, "Alice"s}, {"age"s, 30ll}, {"status"s, "active"s}},
                object{{"name"s, "Bob"s}, {"age"s, 20ll}, {"status"s, "active"s}},
                object{{"name"s, "Charlie"s}, {"age"s, 22ll}, {"status"s, "inactive"s}}
            }};

            when("Filter is age gt 25 or status eq 'active'") = [docs]
            {
                then("Returns union of matching documents") = [docs]
                {
                    const auto parsed = parse_filter("age gt 25 or status eq 'active'"sv);
                    require_true(parsed.has_or());

                    const auto result = filter_documents_by_or(docs, parsed.or_branches);
                    require_true(result.is_array());
                    const auto& items = result.get<object::array>();
                    require_eq(items.size(), 2u);
                    require_eq(items[0]["name"s].get<string>(), "Alice"s);
                    require_eq(items[1]["name"s].get<string>(), "Bob"s);
                };
            };
        };
    };

    scenario("parse_filter ne operator matches documents, [yardb]") = []
    {
        given("Documents with status field") = []
        {
            auto docs = object::array{
                object{{"name"s, "Alice"s}, {"status"s, "active"s}},
                object{{"name"s, "Bob"s}, {"status"s, "deleted"s}},
                object{{"name"s, "Charlie"s}, {"status"s, "active"s}}
            };

            when("Selector uses parsed ne filter") = [docs]
            {
                then("Excludes matching documents") = [docs]
                {
                    const auto [selector, filters] = parse_and_filter("status ne 'deleted'"sv);
                    require_true(filters.empty());

                    auto result = object::array{};
                    for(const auto& doc : docs)
                    {
                        if(doc.match(selector))
                            result.push_back(doc);
                    }

                    require_eq(result.size(), 2u);
                    require_eq(result[0]["name"s].get<string>(), "Alice"s);
                    require_eq(result[1]["name"s].get<string>(), "Charlie"s);
                };
            };
        };
    };

    scenario("parse_filter in operator matches documents, [yardb]") = []
    {
        given("Documents with status field") = []
        {
            auto docs = object::array{
                object{{"name"s, "Alice"s}, {"status"s, "active"s}},
                object{{"name"s, "Bob"s}, {"status"s, "pending"s}},
                object{{"name"s, "Charlie"s}, {"status"s, "inactive"s}}
            };

            when("Selector uses parsed in filter") = [docs]
            {
                then("Returns only matching documents") = [docs]
                {
                    const auto [selector, filters] = parse_and_filter("status in ('active','pending')"sv);
                    require_true(filters.empty());

                    auto result = object::array{};
                    for(const auto& doc : docs)
                    {
                        if(doc.match(selector))
                            result.push_back(doc);
                    }

                    require_eq(result.size(), 2u);
                    require_eq(result[0]["name"s].get<string>(), "Alice"s);
                    require_eq(result[1]["name"s].get<string>(), "Bob"s);
                };
            };
        };
    };

    // Test apply_string_filters
    scenario("apply_string_filters filters documents correctly, [yardb]") = []
    {
        given("An array of documents and string filters") = []
        {
            auto docs = xson::object{xson::object::array{
                xson::object{{"name"s, "Alice"s}, {"email"s, "alice@example.com"s}},
                xson::object{{"name"s, "Bob"s}, {"email"s, "bob@test.com"s}},
                xson::object{{"name"s, "Charlie"s}, {"email"s, "charlie@example.com"s}}
            }};
            
            when("Filter is startswith(name, 'A')") = [docs]
            {
                then("Returns only documents starting with 'A'") = [docs]
                {
                    auto filters = std::vector<string_filter>{
                        {"startswith"sv, "name"s, "A"s}
                    };
                    const auto result = apply_string_filters(docs, filters);
                    require_true(result.is_array());
                    const auto& items = result.get<xson::object::array>();
                    require_eq(items.size(), 1u);
                    require_eq(items[0]["name"s].get<string>(), "Alice"s);
                };
            };

            when("Filter is contains(email, '@example')") = [docs]
            {
                then("Returns only documents with '@example' in email") = [docs]
                {
                    auto filters = std::vector<string_filter>{
                        {"contains"sv, "email"s, "@example"s}
                    };
                    const auto result = apply_string_filters(docs, filters);
                    require_true(result.is_array());
                    const auto& items = result.get<xson::object::array>();
                    require_eq(items.size(), 2u);
                };
            };

            when("Filter is endswith(email, '.com')") = [docs]
            {
                then("Returns all documents ending with '.com'") = [docs]
                {
                    auto filters = std::vector<string_filter>{
                        {"endswith"sv, "email"s, ".com"s}
                    };
                    const auto result = apply_string_filters(docs, filters);
                    require_true(result.is_array());
                    const auto& items = result.get<xson::object::array>();
                    require_eq(items.size(), 3u);
                };
            };
        };
    };

    scenario("startswith lowers to indexed prefix range, [yardb]") = []
    {
        given("prefix_exclusive_upper_bound helper") = []
        {
            when("Prefix is a single letter") = []
            {
                then("Upper bound increments the last byte") = []
                {
                    const auto upper = prefix_exclusive_upper_bound("A"sv);
                    require_true(upper.has_value());
                    require_eq(*upper, "B"s);
                };
            };

            when("Prefix is a word") = []
            {
                then("Upper bound increments the final character") = []
                {
                    const auto upper = prefix_exclusive_upper_bound("John"sv);
                    require_true(upper.has_value());
                    require_eq(*upper, "Joho"s);
                };
            };
        };

        given("Collection with indexed name field") = []
        {
            const auto test_file = "./startswith_index_test.db"s;

            when("read_with_parsed_filter uses startswith on indexed field") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                                require_true(engine.index("users"s, {"name"s}).has_value());

                auto alice = object{{"name"s, "Alice"s}};
                auto bob = object{{"name"s, "Bob"s}};
                auto charlie = object{{"name"s, "Charlie"s}};
                require_true(engine.create("users"s, alice).has_value());
                require_true(engine.create("users"s, bob).has_value());
                require_true(engine.create("users"s, charlie).has_value());

                const auto parsed = parse_filter("startswith(name, 'A')"sv);
                auto documents = read_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Returns only names with prefix A") = [documents]
                {
                    require_true(documents.is_array());
                    const auto& items = documents.get<object::array>();
                    require_eq(items.size(), 1u);
                    require_eq(items[0]["name"s].get<string>(), "Alice"s);
                };
            };

            when("lower_startswith_filters skips nested paths") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                                require_true(engine.index("users"s, {"Customer"s}).has_value());

                auto selector = object{};
                auto filters = std::vector<string_filter>{
                    {"startswith"sv, "Customer/Name"s, "Ac"s}
                };
                lower_startswith_filters(engine, "users"s, selector, filters);

                then("Keeps post-filter and leaves selector empty") = [selector, filters]
                {
                    require_true(selector.empty());
                    require_eq(filters.size(), 1u);
                    require_eq(filters[0].function, "startswith"sv);
                };
            };

            when("lower_startswith_filters builds range selector") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                                require_true(engine.index("users"s, {"name"s}).has_value());

                auto selector = object{};
                auto filters = std::vector<string_filter>{
                    {"startswith"sv, "name"s, "Al"s}
                };
                lower_startswith_filters(engine, "users"s, selector, filters);

                then("Moves constraint into selector and clears string filter") = [selector, filters]
                {
                    require_true(filters.empty());
                    require_true(selector.has("name"s));
                    require_true(selector["name"s].has("$gte"s));
                    require_true(selector["name"s].has("$lt"s));
                    require_eq(selector["name"s]["$gte"s].get<string>(), "Al"s);
                    require_eq(selector["name"s]["$lt"s].get<string>(), "Am"s);
                };
            };
        };
    };

    scenario("count_with_parsed_filter counts documents correctly, [yardb]") = []
    {
        given("Collection with indexed age and name fields") = []
        {
            const auto test_file = "./count_parsed_filter_test.db"s;

            when("Filter uses OR branches") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                                require_true(engine.index("users"s, {"age"s, "status"s}).has_value());

                auto alice = object{{"name"s, "Alice"s}, {"age"s, 30ll}, {"status"s, "active"s}},
                    bob = object{{"name"s, "Bob"s}, {"age"s, 20ll}, {"status"s, "active"s}},
                    charlie = object{{"name"s, "Charlie"s}, {"age"s, 22ll}, {"status"s, "inactive"s}};
                require_true(engine.create("users"s, alice).has_value());
                require_true(engine.create("users"s, bob).has_value());
                require_true(engine.create("users"s, charlie).has_value());

                const auto parsed = parse_filter("age gt 25 or status eq 'active'"sv);
                require_true(parsed.has_or());
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Returns union count without reading all documents into caller") = [count]
                {
                    require_eq(count, 2u);
                };
            };

            when("Filter uses contains post-filter") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                                require_true(engine.index("users"s, {"email"s}).has_value());

                auto alice = object{{"name"s, "Alice"s}, {"email"s, "alice@example.com"s}},
                    bob = object{{"name"s, "Bob"s}, {"email"s, "bob@test.com"s}},
                    charlie = object{{"name"s, "Charlie"s}, {"email"s, "charlie@example.org"s}};
                require_true(engine.create("users"s, alice).has_value());
                require_true(engine.create("users"s, bob).has_value());
                require_true(engine.create("users"s, charlie).has_value());

                const auto parsed = parse_filter("contains(email, '@example')"sv);
                require_false(parsed.has_or());
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Counts only documents matching string filter") = [count]
                {
                    require_eq(count, 2u);
                };
            };

            when("Filter uses indexed startswith lowered to range") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                                require_true(engine.index("users"s, {"name"s}).has_value());

                auto alice = object{{"name"s, "Alice"s}},
                    bob = object{{"name"s, "Bob"s}},
                    charlie = object{{"name"s, "Charlie"s}};
                require_true(engine.create("users"s, alice).has_value());
                require_true(engine.create("users"s, bob).has_value());
                require_true(engine.create("users"s, charlie).has_value());

                const auto parsed = parse_filter("startswith(name, 'A')"sv);
                require_false(parsed.has_or());
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Uses engine.count on lowered selector") = [count]
                {
                    require_eq(count, 1u);
                };
            };

            when("Filter uses ne operator") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                                require_true(engine.index("users"s, {"status"s}).has_value());

                auto alice = object{{"name"s, "Alice"s}, {"status"s, "active"s}},
                    bob = object{{"name"s, "Bob"s}, {"status"s, "deleted"s}},
                    charlie = object{{"name"s, "Charlie"s}, {"status"s, "active"s}};
                require_true(engine.create("users"s, alice).has_value());
                require_true(engine.create("users"s, bob).has_value());
                require_true(engine.create("users"s, charlie).has_value());

                const auto parsed = parse_filter("status ne 'deleted'"sv);
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Falls back to scan count via engine.count") = [count]
                {
                    require_eq(count, 2u);
                };
            };

            when("Filter ANDs equality with an impossible range on an indexed field") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                require_true(engine.index("users"s, {"age"s}).has_value());

                auto young = object{{"name"s, "young"s}, {"age"s, 3ll}};
                auto exact = object{{"name"s, "exact"s}, {"age"s, 10ll}};
                require_true(engine.create("users"s, young).has_value());
                require_true(engine.create("users"s, exact).has_value());

                // Index-only $eq would count age==3; match requires age gt 5 too.
                const auto parsed = parse_filter("age eq 3 and age gt 5"sv);
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);
                const auto docs = read_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Count matches empty read instead of index-only $eq size") = [count, docs, test_file]
                {
                    require_eq(count, 0u);
                    require_true(docs.is_array());
                    require_eq(docs.get<object::array>().size(), 0u);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };

            when("Filter ANDs exclusive and inclusive lower bounds on an indexed field") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                require_true(engine.index("users"s, {"age"s}).has_value());

                auto mid = object{{"name"s, "mid"s}, {"age"s, 7ll}};
                auto high = object{{"name"s, "high"s}, {"age"s, 12ll}};
                require_true(engine.create("users"s, mid).has_value());
                require_true(engine.create("users"s, high).has_value());

                // Index-only $gt:5 would include age 7; match also requires age ge 10.
                const auto parsed = parse_filter("age gt 5 and age ge 10"sv);
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);
                const auto docs = read_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Count matches read and excludes ages between the bounds") = [count, docs, test_file]
                {
                    require_eq(count, 1u);
                    require_true(docs.is_array());
                    require_eq(docs.get<object::array>().size(), 1u);
                    require_eq(docs.get<object::array>()[0]["name"s].get<string>(), "high"s);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };

            when("Filter ANDs an inverted range on an indexed field") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                require_true(engine.index("users"s, {"age"s}).has_value());

                auto young = object{{"name"s, "young"s}, {"age"s, 3ll}};
                auto mid = object{{"name"s, "mid"s}, {"age"s, 10ll}};
                auto old = object{{"name"s, "old"s}, {"age"s, 20ll}};
                require_true(engine.create("users"s, young).has_value());
                require_true(engine.create("users"s, mid).has_value());
                require_true(engine.create("users"s, old).has_value());

                // Index view must treat {$gt:10,$lt:5} as empty (not UB / wild count).
                const auto parsed = parse_filter("age gt 10 and age lt 5"sv);
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);
                const auto docs = read_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Count and read are empty") = [count, docs, test_file]
                {
                    require_eq(count, 0u);
                    require_true(docs.is_array());
                    require_eq(docs.get<object::array>().size(), 0u);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };

            when("Filter ANDs an inverted primary-key range") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                auto a = object{{"name"s, "a"s}};
                auto b = object{{"name"s, "b"s}};
                auto c = object{{"name"s, "c"s}};
                require_true(engine.create("users"s, a).has_value());
                require_true(engine.create("users"s, b).has_value());
                require_true(engine.create("users"s, c).has_value());

                const auto parsed = parse_filter("_id gt 10 and _id lt 5"sv);
                const auto count = count_with_parsed_filter(engine, "users"s, object{}, parsed);
                const auto docs = read_with_parsed_filter(engine, "users"s, object{}, parsed);

                then("Count and read are empty") = [count, docs, test_file]
                {
                    require_eq(count, 0u);
                    require_true(docs.is_array());
                    require_eq(docs.get<object::array>().size(), 0u);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };
        };
    };

    // Test apply_select
    scenario("apply_select projects fields correctly, [yardb]") = []
    {
        given("An array of documents") = []
        {
            auto docs = xson::object{xson::object::array{
                xson::object{{"_id"s, 1}, {"name"s, "Alice"s}, {"email"s, "alice@example.com"s}, {"age"s, 30}},
                xson::object{{"_id"s, 2}, {"name"s, "Bob"s}, {"email"s, "bob@test.com"s}, {"age"s, 25}}
            }};
            
            when("Select is 'name,email'") = [docs]
            {
                then("Returns only name and email fields plus _id") = [docs]
                {
                    const auto result = apply_select(docs, "name,email"sv);
                    require_true(result.is_array());
                    const auto& items = result.get<xson::object::array>();
                    require_eq(items.size(), 2u);
                    require_true(items[0].has("_id"s));
                    require_true(items[0].has("name"s));
                    require_true(items[0].has("email"s));
                    require_false(items[0].has("age"s));
                };
            };

            when("Select is empty") = [docs]
            {
                then("Returns documents unchanged") = [docs]
                {
                    const auto result = apply_select(docs, ""sv);
                    require_true(result.is_array());
                    const auto& items = result.get<xson::object::array>();
                    require_eq(items.size(), 2u);
                    require_true(items[0].has("_id"s));
                    require_true(items[0].has("name"s));
                    require_true(items[0].has("email"s));
                    require_true(items[0].has("age"s));
                };
            };
        };
    };

    // Test metadata generation with engine
    scenario("OData metadata generation from collections, [yardb]") = []
    {
        given("Database with collections containing various field types") = []
        {
            const auto test_file = "./metadata_test.db"s;
            
            when("Metadata is generated from collections") = [test_file]
            {
                // Remove any existing test file
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());
                
                {
                    auto engine = yar::db::engine{test_file};
                    
                    // Create collection with various field types
                                        auto user_doc = xson::object{
                        {"_id"s, 1ll},
                        {"name"s, "John"s},
                        {"age"s, 30ll},
                        {"salary"s, 50000.5},
                        {"active"s, true}
                    };
                    require_true(engine.create("users"s, user_doc).has_value());
                    
                    // Create another collection
                                        auto order_doc = xson::object{
                        {"_id"s, 1ll},
                        {"userId"s, 1ll},
                        {"total"s, 99.99}
                    };
                    require_true(engine.create("orders"s, order_doc).has_value());
                    
                    // Generate metadata (no locking needed in single-threaded test)
                    auto metadata = yar::http::odata::generate_metadata(engine);
                    
                    then("Metadata contains correct structure") = [metadata]
                    {
                        // Check version and container
                        require_eq(metadata["$Version"s].template get<std::string>(), "4.01"s);
                        require_eq(metadata["$EntityContainer"s].template get<std::string>(), "DefaultContainer"s);
                        
                        // Check EntitySets
                        require_true(metadata.has("EntitySets"s));
                        const auto& entity_sets = metadata["EntitySets"s].get<xson::object::array>();
                        require_true(entity_sets.size() >= 2u); // users and orders (may have more)
                        
                        // Check EntityTypes
                        require_true(metadata.has("EntityTypes"s));
                        const auto& entity_types = metadata["EntityTypes"s].get<xson::object::array>();
                        require_true(entity_types.size() >= 2u); // users and orders
                        
                        // Find users EntityType
                        auto users_type = std::find_if(entity_types.begin(), entity_types.end(),
                            [](const auto& t) { return t["Name"s].template get<std::string>() == "users"s; });
                        require_true(users_type != entity_types.end());
                        
                        // Check users has Key
                        require_true((*users_type).has("Key"s));
                        
                        // Check users has Property array
                        require_true((*users_type).has("Property"s));
                        const auto& users_properties = (*users_type)["Property"s].get<xson::object::array>();
                        require_true(users_properties.size() >= 4u); // _id, name, age, salary, active (at least)
                        
                        // Verify _id is in properties and is non-nullable Int64
                        auto id_prop = std::find_if(users_properties.begin(), users_properties.end(),
                            [](const auto& p) { return p["Name"s].template get<std::string>() == "_id"s; });
                        require_true(id_prop != users_properties.end());
                        require_eq((*id_prop)["Type"s].template get<std::string>(), "Edm.Int64"s);
                        const bool nullable = (*id_prop)["Nullable"s];
                        require_eq(nullable, false);
                        
                        // Check name field type
                        auto name_prop = std::find_if(users_properties.begin(), users_properties.end(),
                            [](const auto& p) { return p["Name"s].template get<std::string>() == "name"s; });
                        require_true(name_prop != users_properties.end());
                        require_eq((*name_prop)["Type"s].template get<std::string>(), "Edm.String"s);
                        
                        // Check age field type
                        auto age_prop = std::find_if(users_properties.begin(), users_properties.end(),
                            [](const auto& p) { return p["Name"s].template get<std::string>() == "age"s; });
                        require_true(age_prop != users_properties.end());
                        require_eq((*age_prop)["Type"s].template get<std::string>(), "Edm.Int64"s);
                        
                        // Check salary field type
                        auto salary_prop = std::find_if(users_properties.begin(), users_properties.end(),
                            [](const auto& p) { return p["Name"s].template get<std::string>() == "salary"s; });
                        require_true(salary_prop != users_properties.end());
                        require_eq((*salary_prop)["Type"s].template get<std::string>(), "Edm.Double"s);
                        
                        // Check active field type
                        auto active_prop = std::find_if(users_properties.begin(), users_properties.end(),
                            [](const auto& p) { return p["Name"s].template get<std::string>() == "active"s; });
                        require_true(active_prop != users_properties.end());
                        require_eq((*active_prop)["Type"s].template get<std::string>(), "Edm.Boolean"s);
                    };
                }
                
                // Cleanup
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());
            };
        };
    };

    scenario("relationship naming helpers for $expand, [yardb]") = []
    {
        given("Plural collection names") = []
        {
            when("Singularizing common plurals") = []
            {
                then("ies and trailing s rules apply") = []
                {
                    check_eq(singularize_collection_name("customers"sv), "customer"s);
                    check_eq(singularize_collection_name("orders"sv), "order"s);
                    check_eq(singularize_collection_name("categories"sv), "category"s);
                    check_eq(singularize_collection_name("address"sv), "address"s);
                };
            };

            when("Pluralizing singular navigation names") = []
            {
                then("y→ies and append-s rules apply") = []
                {
                    check_eq(pluralize_collection_name("customer"sv), "customers"s);
                    check_eq(pluralize_collection_name("order"sv), "orders"s);
                    check_eq(pluralize_collection_name("category"sv), "categories"s);
                    check_eq(pluralize_collection_name("key"sv), "keys"s);
                };
            };

            when("Parsing $expand lists") = []
            {
                then("Comma-separated snake_case names are kept") = []
                {
                    const auto navs = parse_expand_navigations("customer, product"sv);
                    require_eq(navs.size(), 2u);
                    check_eq(navs[0], "customer"s);
                    check_eq(navs[1], "product"s);
                };
            };
        };
    };

    scenario("parse_orderby and apply_orderby sort by field name, [yardb]") = []
    {
        given("Orderby expressions and an out-of-id document array") = []
        {
            when("Parsing field desc and asc") = []
            {
                then("Field name and descending flag are extracted") = []
                {
                    const auto desc = parse_orderby("value desc"sv);
                    check_eq(desc.field_name, "value"s);
                    require_true(desc.descending);

                    const auto asc = parse_orderby("value asc"sv);
                    check_eq(asc.field_name, "value"s);
                    require_false(asc.descending);

                    const auto bare = parse_orderby("age"sv);
                    check_eq(bare.field_name, "age"s);
                    require_false(bare.descending);
                };
            };

            when("Sorting documents whose _id order differs from value order") = []
            {
                then("Ascending and descending follow the field") = []
                {
                    auto documents = xson::object{xson::object::array{
                        xson::object{{"_id"s, 1ll}, {"value"s, 30ll}},
                        xson::object{{"_id"s, 2ll}, {"value"s, 10ll}},
                        xson::object{{"_id"s, 3ll}, {"value"s, 20ll}}
                    }};

                    yar::db::apply_orderby(documents, "value"sv, false);
                    const auto& asc = documents.get<xson::object::array>();
                    require_eq(asc.size(), 3u);
                    check_eq(static_cast<xson::integer_type>(asc[0]["value"s]), 10);
                    check_eq(static_cast<xson::integer_type>(asc[1]["value"s]), 20);
                    check_eq(static_cast<xson::integer_type>(asc[2]["value"s]), 30);

                    yar::db::apply_orderby(documents, "value"sv, true);
                    const auto& desc = documents.get<xson::object::array>();
                    check_eq(static_cast<xson::integer_type>(desc[0]["value"s]), 30);
                    check_eq(static_cast<xson::integer_type>(desc[1]["value"s]), 20);
                    check_eq(static_cast<xson::integer_type>(desc[2]["value"s]), 10);
                };
            };

            when("OR filter merge applies orderby before top") = []
            {
                const auto test_file = "./odata_orderby_or_test.db"s;

                then("Highest value wins with $orderby desc and $top") = [test_file]
                {
                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());

                    auto engine = yar::db::engine{test_file};
                    auto a = xson::object{{"status"s, "a"s}, {"value"s, 30ll}};
                    auto b = xson::object{{"status"s, "b"s}, {"value"s, 10ll}};
                    auto c = xson::object{{"status"s, "a"s}, {"value"s, 20ll}};
                    require_true(engine.create("items"s, a).has_value());
                    require_true(engine.create("items"s, b).has_value());
                    require_true(engine.create("items"s, c).has_value());

                    const auto parsed = parse_filter("status eq 'a' or status eq 'b'"sv);
                    auto selector = xson::object{
                        {"$orderby"s, "value"s},
                        {"$desc"s, true},
                        {"$top"s, 2ll}
                    };
                    const auto docs = read_with_parsed_filter(engine, "items"s, selector, parsed);
                    require_true(docs.is_array());
                    const auto& items = docs.get<xson::object::array>();
                    require_eq(items.size(), 2u);
                    check_eq(static_cast<xson::integer_type>(items[0]["value"s]), 30);
                    check_eq(static_cast<xson::integer_type>(items[1]["value"s]), 20);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };

            when("contains post-filter runs before $top") = []
            {
                const auto test_file = "./odata_contains_top_test.db"s;

                then("Matching rows outside the unfiltered window are not dropped") = [test_file]
                {
                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());

                    auto engine = yar::db::engine{test_file};
                    // First two docs do not match contains; later ones do.
                    // If $top is applied before the string filter, $top=2 yields [].
                    auto skip1 = xson::object{{"email"s, "a@test.com"s}};
                    auto skip2 = xson::object{{"email"s, "b@test.com"s}};
                    auto hit1 = xson::object{{"email"s, "c@example.com"s}};
                    auto hit2 = xson::object{{"email"s, "d@example.org"s}};
                    require_true(engine.create("users"s, skip1).has_value());
                    require_true(engine.create("users"s, skip2).has_value());
                    require_true(engine.create("users"s, hit1).has_value());
                    require_true(engine.create("users"s, hit2).has_value());

                    const auto parsed = parse_filter("contains(email, '@example')"sv);
                    auto selector = xson::object{{"$top"s, 2ll}};
                    const auto docs = read_with_parsed_filter(engine, "users"s, selector, parsed);
                    require_true(docs.is_array());
                    const auto& items = docs.get<xson::object::array>();
                    require_eq(items.size(), 2u);
                    check_eq(static_cast<std::string>(items[0]["email"s]), "c@example.com"s);
                    check_eq(static_cast<std::string>(items[1]["email"s]), "d@example.org"s);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };
        };
    };

    scenario("apply_expand nests related documents by singular_id, [yardb]") = []
    {
        given("An engine with customers and orders") = []
        {
            const auto test_file = "./odata_expand_test.db"s;

            when("Expanding customer on orders") = [test_file]
            {
                std::remove(test_file.c_str());
                std::remove((test_file + ".pid").c_str());

                auto engine = yar::db::engine{test_file};
                auto customer = xson::object{{"name"s, "Ada"s}};
                require_true(engine.create("customers"s, customer).has_value());
                const auto customer_id = static_cast<xson::integer_type>(customer["_id"s]);

                auto order = xson::object{
                    {"customer_id"s, customer_id},
                    {"total"s, 19}
                };
                require_true(engine.create("orders"s, order).has_value());

                auto documents = xson::object{};
                require_true(engine.read("orders"s, xson::object{}, documents));
                const auto expanded = apply_expand(documents, "customer"sv, "orders"s, engine);

                then("Each order with customer_id nests the customer document") = [expanded, customer_id, test_file]
                {
                    require_true(expanded.is_array());
                    require_true(expanded.get<xson::object::array>().size() >= 1u);
                    const auto& doc = expanded.get<xson::object::array>()[0];
                    require_true(doc.has("customer"s));
                    require_true(doc["customer"s].is_object());
                    check_eq(static_cast<xson::integer_type>(doc["customer"s]["_id"s]), customer_id);
                    check_eq(static_cast<std::string>(doc["customer"s]["name"s]), "Ada"s);
                    check_eq(static_cast<xson::integer_type>(doc["customer_id"s]), customer_id);

                    std::remove(test_file.c_str());
                    std::remove((test_file + ".pid").c_str());
                };
            };
        };
    };

    return true;
}

const auto _ = register_odata_tests();

} // namespace yar::odata_unit_test

