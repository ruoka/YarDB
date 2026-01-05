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
                    const auto [selector, filters] = parse_filter("name eq 'John'"sv);
                    require_true(selector.has("name"s));
                    require_eq(selector["name"s].get<string>(), "John"s);
                    require_true(filters.empty());
                };
            };

            when("Filter is 'field gt number'") = []
            {
                then("Returns selector with $gt operator") = []
                {
                    const auto [selector, filters] = parse_filter("age gt 25"sv);
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
                    const auto [selector, filters] = parse_filter("age ge 25"sv);
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
                    const auto [selector, filters] = parse_filter("age lt 100"sv);
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
                    const auto [selector, filters] = parse_filter("age le 100"sv);
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
                    const auto [selector, filters] = parse_filter("startswith(name, 'John')"sv);
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
                    const auto [selector, filters] = parse_filter("contains(email, '@example')"sv);
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
                    const auto [selector, filters] = parse_filter("endswith(path, '.pdf')"sv);
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
                    const auto [selector, filters] = parse_filter("age gt 25 and status eq 'active'"sv);
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

            when("Filter uses 'or' operator") = []
            {
                then("Throws invalid_argument") = []
                {
                    require_throws([]
                    {
                        parse_filter("age gt 25 or status eq 'active'"sv);
                    });
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
                        {"startswith"sv, "name"sv, "A"sv}
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
                        {"contains"sv, "email"sv, "@example"sv}
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
                        {"endswith"sv, "email"sv, ".com"sv}
                    };
                    const auto result = apply_string_filters(docs, filters);
                    require_true(result.is_array());
                    const auto& items = result.get<xson::object::array>();
                    require_eq(items.size(), 3u);
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

    return true;
}

const auto _ = register_odata_tests();

} // namespace yar::odata_unit_test

