# Test File Refactoring Proposal

## Status: ✅ COMPLETED (Dec 2025)

## Overview

This document describes the refactoring of test files to follow the recommended tester framework conventions. The project should serve as an example of how to use the tester framework correctly.

**All refactoring has been completed and verified:**
- ✅ `net-http_server.test.c++` - Refactored to proper BDD structure (9 scenarios)
- ✅ `net-http_headers.test.c++` - Refactored to use `test_case` with `section`
- ✅ All tests pass (226/226 tests, 702/702 assertions)

## Current Issues

### 1. `net-http_server.test.c++`
**Problem**: Uses plain functions (`test_route_registration()`, `test_server_start_stop()`, etc.) that are wrapped in BDD scenarios, but the scenarios don't have proper BDD nesting structure.

**Current Pattern**:
```cpp
void test_route_registration() { /* assertions */ }

auto register_server_tests() {
    tester::bdd::scenario("Route registration, [net]") = [] {
        test_route_registration();  // ❌ Not proper BDD structure
    };
}
```

**Required Pattern**: BDD tests must use nested structure: `scenario` -> `given` -> (`when` -> `then`)

### 2. `net-http_headers.test.c++`
**Problem**: Uses plain code blocks in registration function without any test structure (no `test_case` or BDD `scenario`).

**Current Pattern**:
```cpp
auto register_malformed_headers_tests() {
    {  // ❌ Plain code block, no test structure
        auto raw = "...";
        check_eq(hs["valid-header"], "correct");
    }
    return true;
}
```

**Required Pattern**: Should use either:
- `tester::basic::test_case` with `section` for subtests, OR
- `tester::bdd::scenario` with `given` -> `when` -> `then` structure

## Valid Test Patterns

The tester framework supports two patterns:

### Pattern 1: Basic Unit Tests (`tester::basic::test_case`)
```cpp
auto register_tests() {
    test_case("Description, [tag]") = [] {
        section("Subtest 1") = [] {
            require_eq(expected, actual);
        };
        section("Subtest 2") = [] {
            check_true(condition);
        };
    };
    return true;
}
```

### Pattern 2: BDD Scenarios (`tester::bdd::scenario`)
```cpp
auto register_tests() {
    // Simple: scenario -> given
    tester::bdd::scenario("Simple test, [tag]") = [] {
        tester::bdd::given("Context") = [] {
            check_eq(expected, actual);
        };
    };

    // Full: scenario -> given -> when -> then
    tester::bdd::scenario("Complex test, [tag]") = [] {
        tester::bdd::given("Initial setup") = [] {
            tester::bdd::when("Action is performed") = [] {
                tester::bdd::then("Expected result occurs") = [] {
                    check_eq(expected, actual);
                };
            };
        };
    };
    return true;
}
```

## Refactoring Plan

### File 1: `net-http_server.test.c++`
**Strategy**: Refactor plain functions into proper BDD scenarios with `given` -> `when` -> `then` structure.

**Example Transformation**:
```cpp
// Before:
void test_route_registration() {
    auto handler = [](...) { return {...}; };
    http::server server{};
    server.get("/").html(view::index);
    check_eq(s1, "200 OK");
}

tester::bdd::scenario("Route registration, [net]") = [] {
    test_route_registration();
};

// After:
tester::bdd::scenario("Route registration and rendering, [net]") = [] {
    tester::bdd::given("An HTTP server with registered routes") = [] {
        auto handler = [](...) { return {...}; };
        http::server server{};
        server.get("/").html(view::index);
        
        tester::bdd::when("Routes are rendered") = [] {
            http::headers hs{};
            auto [s1, c1, t1, h1] = server.get("/").render("/", "", hs);
            
            tester::bdd::then("Routes return correct status and content type") = [] {
                check_eq(s1, "200 OK");
                check_eq(t1, "text/html");
            };
        };
    };
};
```

### File 2: `net-http_headers.test.c++`
**Strategy**: Convert plain code blocks to `tester::basic::test_case` with `section` for each test case.

**Example Transformation**:
```cpp
// Before:
auto register_malformed_headers_tests() {
    {
        auto raw = "...";
        check_eq(hs["valid-header"], "correct");
    }
    return true;
}

// After:
auto register_malformed_headers_tests() {
    tester::basic::test_case("Malformed HTTP headers parsing, [net]") = [] {
        section("Parsing broken header lines") = [] {
            auto raw = "...";
            auto is = std::istringstream{raw};
            http::headers hs;
            is >> hs;
            
            check_eq(hs["valid-header"], "correct");
            check_false(hs.contains("no-colon-line"));
        };
        
        section("Line ending variations") = [] {
            // Test cases...
        };
    };
    return true;
}
```

## Files Reviewed and Status

1. ✅ `net-uuid.test.c++` - Already follows BDD pattern correctly
2. ✅ `net-http_server.test.c++` - **REFACTORED** - Now uses proper BDD nesting (9 scenarios)
3. ✅ `net-http_headers.test.c++` - **REFACTORED** - Now uses `test_case` with `section`
4. ✅ `net-acceptor.test.c++` - Already follows BDD pattern correctly
5. ⚠️ Other files - May need review in future

## Implementation Steps (Completed)

1. ✅ Refactored `net-http_server.test.c++` to use proper BDD nesting
2. ✅ Refactored `net-http_headers.test.c++` to use `test_case` with `section`
3. ✅ Fixed variable capture issues using `std::shared_ptr` for shared state
4. ✅ Verified all tests pass (226/226 tests, 702/702 assertions)
5. ✅ Updated documentation with lessons learned

## Notes

- Network tests that check `network_tests_enabled()` should remain in `given` blocks
- Complex setup code can remain in `given` blocks
- Assertions should be in `then` blocks (or directly in `given` for simple tests)
- All tests must maintain their current functionality - only structure changes

## Key Lessons Learned

### Variable Capture in Nested Lambdas
- **Critical**: Nested lambdas execute **after** the parent lambda returns
- **Solution**: Use `std::shared_ptr` for objects that need to be shared:
  ```cpp
  auto server = std::make_shared<http::server>();
  tester::bdd::when("Action") = [server] {
      server->method();  // Use -> operator for shared_ptr
  };
  ```
- **Never capture by reference** (`[&]`) in nested lambdas - causes dangling references

### Test Pattern Selection
- **BDD scenarios** (`tester::bdd::scenario`) - Best for behavior-driven tests with clear setup/action/assertion phases
- **Basic test cases** (`tester::basic::test_case`) - Best for simple unit tests with multiple related subtests using `section`

## Verification Results

**All tests verified passing:**
- Tests: 226/226 passed (100.0%)
- Assertions: 702/702 passed (100.0%)
- All refactored scenarios executing correctly
- No functionality lost during refactoring

