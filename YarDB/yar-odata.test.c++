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
                    engine.collection("users"s);
                    auto user_doc = xson::object{
                        {"_id"s, 1ll},
                        {"name"s, "John"s},
                        {"age"s, 30ll},
                        {"salary"s, 50000.5},
                        {"active"s, true}
                    };
                    engine.create(user_doc);
                    
                    // Create another collection
                    engine.collection("orders"s);
                    auto order_doc = xson::object{
                        {"_id"s, 1ll},
                        {"userId"s, 1ll},
                        {"total"s, 99.99}
                    };
                    engine.create(order_doc);
                    
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

    return true;
}

const auto _ = register_odata_tests();

} // namespace yar::odata_unit_test

