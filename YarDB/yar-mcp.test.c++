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

using tester::assertions::check_eq;
using tester::assertions::check_false;
using tester::assertions::check_true;
using tester::assertions::check_contains;
using tester::assertions::require_true;

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
        };

        section("health and list_collections via engine") = []
        {
            auto engine = yar::db::engine{"./mcp_tools_test.db"};
            auto ready = [] { return "ready"s; };
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
            auto engine = yar::db::engine{"./mcp_db_reject_test.db"};
            auto ready = [] { return "ready"s; };
            const auto poisoned = yar::mcp::call_tool(
                engine,
                ready,
                "create_document",
                R"({"collection":"_db","document":{"collection":"x","keys":"not-an-array"}})");
            check_true(poisoned.is_error);
            check_contains(poisoned.text, "must start with a lowercase letter");
        };
    };

    return true;
}

const auto mcp_test_registrar = register_mcp_tests();

} // namespace
