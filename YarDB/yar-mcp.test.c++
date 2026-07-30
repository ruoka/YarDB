// Copyright (c) 2025-2026 Kaius Ruokonen. All rights reserved.
// SPDX-License-Identifier: MIT
// See the LICENSE file in the project root for full license text.

module yar;
import :mcp;
import :engine;
import tester;
import std;
import xson;

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

using tester::assertions::check_false;
using tester::assertions::check_true;
using tester::assertions::check_contains;
using tester::assertions::require_eq;
using tester::assertions::require_false;
using tester::assertions::require_true;

class fixture
{
public:
    explicit fixture(std::string_view f) : file{f}
    {
        auto pid_file = file + ".pid";
        std::remove(pid_file.c_str());
        auto fs = std::fstream{};
        fs.open(file, std::ios::out);
        fs.close();
    }

    ~fixture()
    {
        std::remove(file.c_str());
        auto pid_file = file + ".pid";
        std::remove(pid_file.c_str());
    }

private:
    std::string file;
};

auto ready = [] { return "ready"s; };

auto register_mcp_tests()
{
    using namespace tester::basic;

    test_case("native MCP tool specs and health/ready, [yardb]") = []
    {
        section("tool_specs includes core tools") = []
        {
            const auto specs = yar::mcp::tool_specs();
            check_true(specs.size() >= 10);
            check_true(std::ranges::any_of(specs, [](const auto& t) { return t.name == "health"; }));
            check_true(std::ranges::any_of(specs, [](const auto& t) { return t.name == "query_collection"; }));
            check_true(std::ranges::any_of(specs, [](const auto& t) { return t.name == "replace_document"; }));
        };

        section("health and list_collections via engine") = []
        {
            const auto setup = fixture{"./mcp_tools_test.db"};
            auto engine = yar::db::engine{"./mcp_tools_test.db"};
            const auto health = yar::mcp::call_tool(engine, ready, "health", "{}");
            check_false(health.is_error);
            check_contains(health.text, "ok");

            auto doc = xson::object{{"name"s, "a"s}};
            require_true(static_cast<bool>(engine.create("items", doc)));
            const auto listed = yar::mcp::call_tool(engine, ready, "list_collections", "{}");
            check_false(listed.is_error);
            check_contains(listed.text, "items");
        };

        section("create_document rejects reserved _db collection name") = []
        {
            // HTTP validates collection names; MCP must too — otherwise
            // create_document("_db", {...}) can poison reopen/index metadata.
            const auto setup = fixture{"./mcp_db_reject_test.db"};
            auto engine = yar::db::engine{"./mcp_db_reject_test.db"};
            const auto poisoned = yar::mcp::call_tool(
                engine,
                ready,
                "create_document",
                R"({"collection":"_db","document":{"collection":"x","keys":"not-an-array"}})");
            check_true(poisoned.is_error);
            check_contains(poisoned.text, "must start with a lowercase letter");
        };
    };

    test_case("native MCP document CRUD and replace identity, [yardb]") = []
    {
        section("create get update delete roundtrip") = []
        {
            const auto setup = fixture{"./mcp_crud_test.db"};
            auto engine = yar::db::engine{"./mcp_crud_test.db"};

            const auto created = yar::mcp::call_tool(
                engine,
                ready,
                "create_document",
                R"({"collection":"docs","document":{"name":"Ada","role":"dev"}})");
            require_false(created.is_error);
            const auto created_doc = xson::json::parse(created.text);
            require_true(created_doc.has("_id"s));
            const auto id = static_cast<xson::integer_type>(created_doc["_id"s]);
            const auto id_json = std::to_string(id);

            const auto fetched = yar::mcp::call_tool(
                engine,
                ready,
                "get_document",
                R"({"collection":"docs","document_id":")"s + id_json + R"("})");
            require_false(fetched.is_error);
            const auto fetched_doc = xson::json::parse(fetched.text);
            require_eq(fetched_doc["name"s].get<std::string>(), "Ada"s);

            const auto updated = yar::mcp::call_tool(
                engine,
                ready,
                "update_document",
                R"({"collection":"docs","document_id":")"s + id_json
                    + R"(","patch":{"role":"lead"}})");
            require_false(updated.is_error);
            const auto updated_doc = xson::json::parse(updated.text);
            require_eq(updated_doc["role"s].get<std::string>(), "lead"s);
            require_eq(static_cast<xson::integer_type>(updated_doc["_id"s]), id);

            const auto deleted = yar::mcp::call_tool(
                engine,
                ready,
                "delete_document",
                R"({"collection":"docs","document_id":")"s + id_json + R"("})");
            require_false(deleted.is_error);
            check_contains(deleted.text, "deleted");

            const auto missing = yar::mcp::call_tool(
                engine,
                ready,
                "get_document",
                R"({"collection":"docs","document_id":")"s + id_json + R"("})");
            require_true(missing.is_error);
            check_contains(missing.text, "Not Found");
        };

        section("replace_document upsert honors document_id when body omits _id") = []
        {
            // Regression for #69: MCP replace upserts used to mint a fresh _id
            // when the body omitted it, so retries duplicated rows.
            const auto setup = fixture{"./mcp_replace_omit_id_test.db"};
            auto engine = yar::db::engine{"./mcp_replace_omit_id_test.db"};

            const auto first = yar::mcp::call_tool(
                engine,
                ready,
                "replace_document",
                R"({"collection":"items","document_id":"42","document":{"name":"Ada"}})");
            require_false(first.is_error);
            const auto first_doc = xson::json::parse(first.text);
            require_eq(static_cast<xson::integer_type>(first_doc["_id"s]), 42ll);
            require_eq(first_doc["name"s].get<std::string>(), "Ada"s);

            const auto retry = yar::mcp::call_tool(
                engine,
                ready,
                "replace_document",
                R"({"collection":"items","document_id":"42","document":{"name":"Ada Lovelace"}})");
            require_false(retry.is_error);
            const auto retry_doc = xson::json::parse(retry.text);
            require_eq(static_cast<xson::integer_type>(retry_doc["_id"s]), 42ll);
            require_eq(retry_doc["name"s].get<std::string>(), "Ada Lovelace"s);
            require_eq(engine.count("items"s, xson::object{}), 1u);

            const auto fetched = yar::mcp::call_tool(
                engine,
                ready,
                "get_document",
                R"({"collection":"items","document_id":"42"})");
            require_false(fetched.is_error);
            require_eq(xson::json::parse(fetched.text)["name"s].get<std::string>(), "Ada Lovelace"s);
        };

        section("replace_document forces document_id over mismatched body _id") = []
        {
            const auto setup = fixture{"./mcp_replace_body_id_test.db"};
            auto engine = yar::db::engine{"./mcp_replace_body_id_test.db"};

            const auto replaced = yar::mcp::call_tool(
                engine,
                ready,
                "replace_document",
                R"({"collection":"items","document_id":"7","document":{"_id":99,"name":"Bob"}})");
            require_false(replaced.is_error);
            const auto doc = xson::json::parse(replaced.text);
            require_eq(static_cast<xson::integer_type>(doc["_id"s]), 7ll);
            require_eq(engine.count("items"s, xson::object{}), 1u);

            const auto by_selector = yar::mcp::call_tool(
                engine,
                ready,
                "get_document",
                R"({"collection":"items","document_id":"7"})");
            require_false(by_selector.is_error);

            const auto by_body_id = yar::mcp::call_tool(
                engine,
                ready,
                "get_document",
                R"({"collection":"items","document_id":"99"})");
            require_true(by_body_id.is_error);
        };

        section("document_id rejects trailing garbage instead of truncating") = []
        {
            // std::stoll accepts a numeric prefix ("12abc" → 12). MCP must
            // match HTTP's utils::stoll full-string parse so malformed ids
            // cannot silently delete/update/replace the wrong document.
            const auto setup = fixture{"./mcp_id_strict_test.db"};
            auto engine = yar::db::engine{"./mcp_id_strict_test.db"};

            const auto seeded = yar::mcp::call_tool(
                engine,
                ready,
                "replace_document",
                R"({"collection":"items","document_id":"12","document":{"name":"keep"}})");
            require_false(seeded.is_error);

            const auto bad_delete = yar::mcp::call_tool(
                engine,
                ready,
                "delete_document",
                R"({"collection":"items","document_id":"12abc"})");
            require_true(bad_delete.is_error);
            check_contains(bad_delete.text, "error:");

            const auto bad_update = yar::mcp::call_tool(
                engine,
                ready,
                "update_document",
                R"({"collection":"items","document_id":"12.5","patch":{"name":"gone"}})");
            require_true(bad_update.is_error);

            const auto bad_replace = yar::mcp::call_tool(
                engine,
                ready,
                "replace_document",
                R"({"collection":"items","document_id":"1e2","document":{"name":"wrong"}})");
            require_true(bad_replace.is_error);

            const auto bad_get = yar::mcp::call_tool(
                engine,
                ready,
                "get_document",
                R"({"collection":"items","document_id":" 12"})");
            require_true(bad_get.is_error);

            const auto still_there = yar::mcp::call_tool(
                engine,
                ready,
                "get_document",
                R"({"collection":"items","document_id":"12"})");
            require_false(still_there.is_error);
            require_eq(xson::json::parse(still_there.text)["name"s].get<std::string>(), "keep"s);
            require_eq(engine.count("items"s, xson::object{}), 1u);
        };
    };

    test_case("native MCP query validation and index tools, [yardb]") = []
    {
        section("query_collection honors $top and leading '?'") = []
        {
            const auto setup = fixture{"./mcp_query_test.db"};
            auto engine = yar::db::engine{"./mcp_query_test.db"};
            for(auto i = 0; i < 3; ++i)
            {
                auto doc = xson::object{{"n"s, static_cast<xson::integer_type>(i)}};
                require_true(static_cast<bool>(engine.create("nums", doc)));
            }

            const auto queried = yar::mcp::call_tool(
                engine,
                ready,
                "query_collection",
                R"({"collection":"nums","odata_query":"?$top=2"})");
            require_false(queried.is_error);
            const auto rows = xson::json::parse(queried.text);
            require_true(rows.is_array());
            require_eq(rows.get<xson::object::array>().size(), 2u);
        };

        section("query_collection keeps & inside quoted $filter literals") = []
        {
            const auto setup = fixture{"./mcp_query_amp_test.db"};
            auto engine = yar::db::engine{"./mcp_query_amp_test.db"};
            auto match = xson::object{{"name"s, "AT&T"s}};
            auto other = xson::object{{"name"s, "Verizon"s}};
            require_true(static_cast<bool>(engine.create("vendors", match)));
            require_true(static_cast<bool>(engine.create("vendors", other)));

            // MCP odata_query is a JSON string, not a URL — clients write raw '&'
            // in literals. Naive '&' split used to truncate the filter to
            // `name eq 'AT` and return an empty set while still applying $top.
            const auto queried = yar::mcp::call_tool(
                engine,
                ready,
                "query_collection",
                R"({"collection":"vendors","odata_query":"$filter=name eq 'AT&T'&$top=10"})");
            require_false(queried.is_error);
            const auto rows = xson::json::parse(queried.text);
            require_true(rows.is_array());
            require_eq(rows.get<xson::object::array>().size(), 1u);
            check_contains(queried.text, "AT&T");
        };

        section("configure_indexes accepts keys array") = []
        {
            const auto setup = fixture{"./mcp_index_test.db"};
            auto engine = yar::db::engine{"./mcp_index_test.db"};
            auto doc = xson::object{{"name"s, "a"s}};
            require_true(static_cast<bool>(engine.create("people", doc)));

            const auto indexed = yar::mcp::call_tool(
                engine,
                ready,
                "configure_indexes",
                R"({"collection":"people","keys":["name"]})");
            require_false(indexed.is_error);
            check_contains(indexed.text, "name");
            const auto keys = engine.indexed_keys("people"s);
            require_true(std::ranges::contains(keys, "name"s));
        };

        section("unknown tool and invalid arguments return is_error") = []
        {
            const auto setup = fixture{"./mcp_errors_test.db"};
            auto engine = yar::db::engine{"./mcp_errors_test.db"};

            const auto unknown = yar::mcp::call_tool(engine, ready, "no_such_tool", "{}");
            require_true(unknown.is_error);
            check_contains(unknown.text, "unknown tool");

            const auto bad_args = yar::mcp::call_tool(
                engine,
                ready,
                "get_document",
                R"({"collection":"items"})");
            require_true(bad_args.is_error);
            check_contains(bad_args.text, "error:");

            const auto not_found = yar::mcp::call_tool(
                engine,
                ready,
                "update_document",
                R"({"collection":"items","document_id":"1","patch":{"name":"x"}})");
            require_true(not_found.is_error);
            check_contains(not_found.text, "Not Found");
        };
    };

    return true;
}

const auto mcp_test_registrar = register_mcp_tests();

} // namespace
