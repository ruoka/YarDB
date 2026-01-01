# HTTP Handler Refactoring Proposal

## Analysis of Handler Patterns

After analyzing all HTTP handlers in `yar-httpd.c++m`, we found these common patterns:

### Common Patterns (100% of handlers):
1. **Accept header validation** - 11 handlers check `accepts_json(headers)`
2. **Exception handling** - 18 handlers have identical try-catch blocks
3. **Debug logging** - All handlers log request with `slog << debug`

### Common Patterns (Most handlers):
4. **URI parsing** - `auto uri = ::http::uri{request};`
5. **Collection validation** - `validate_collection_name(uri.path[1])`
6. **Lock guard** - `const auto guard = std::lock_guard{m_engine};`
7. **OData metadata** - Check and add metadata if requested
8. **Error response formatting** - Standardized error object structure

### Handler Types:
- **Simple handlers** (DELETE, some GETs): Basic pattern only
- **Document handlers** (GET by ID, PUT, PATCH): + conditional requests, ETag, Last-Modified
- **Collection handlers** (GET collection, POST): + OData query params, metadata arrays
- **Index management** (PUT/PATCH /_db/*): Custom validation logic

## Proposed Solution: Handler Wrapper Template

### 1. Core Wrapper Template

```cpp
// In yar-helpers.c++m

namespace yar::http::helpers {

// Type aliases matching framework expectations
using handler_result = std::tuple<std::string, std::string>;
using handler_result_with_headers = std::tuple<std::string, std::string, std::optional<::http::headers>>;

// Handler function type
template<typename Result>
using handler_fn = std::function<Result(std::string_view, std::string_view, const ::http::headers&)>;

// Wrapper that handles common concerns
template<typename Result>
auto wrap_handler(handler_fn<Result> handler) -> handler_fn<Result>
{
    return [handler = std::move(handler)](
        std::string_view request,
        std::string_view body,
        const ::http::headers& headers
    ) -> Result {
        // Check Accept header
        if(!accepts_json(headers))
        {
            if constexpr(std::is_same_v<Result, handler_result_with_headers>)
                return not_acceptable_response_with_headers();
            else
                return not_acceptable_response();
        }
        
        // Execute handler with exception handling
        try
        {
            return handler(request, body, headers);
        }
        catch(const std::exception& e)
        {
            auto error = xson::object{
                {"error", "Bad Request"s},
                {"message", std::string{e.what()}}
            };
            
            if constexpr(std::is_same_v<Result, handler_result_with_headers>)
                return std::make_tuple("400 Bad Request"s, xson::json::stringify(error), std::optional<::http::headers>{});
            else
                return std::make_tuple("400 Bad Request"s, xson::json::stringify(error));
        }
    };
}

// Helper for creating error responses (for custom status codes like 404, 412)
template<typename Result>
auto make_error_response(const std::string& status, const std::string& error_type, const std::string& message) -> Result
{
    auto error = xson::object{
        {"error", error_type},
        {"message", message}
    };
    
    if constexpr(std::is_same_v<Result, handler_result_with_headers>)
        return std::make_tuple(status, xson::json::stringify(error), std::optional<::http::headers>{});
    else
        return std::make_tuple(status, xson::json::stringify(error));
}

} // namespace yar::http::helpers
```

### 2. Usage Example - Simple Handler

**Before:**
```cpp
m_server.post("/[a-z][a-z0-9]*"s).response_with_headers(
    "application/json"sv,
    [this](std::string_view request, std::string_view body, const ::http::headers& headers)
    {
        slog << debug << "POST /[a-z]+: "<< request << " <- " << body << flush;
        
        // Check Accept header
        if(!accepts_json(headers))
            return not_acceptable_response_with_headers();
        
        try
        {
            auto uri = ::http::uri{request};
            auto document = xson::json::parse(body);
            const auto guard = std::lock_guard{m_engine};
            auto collection = validate_collection_name(uri.path[1]);
            m_engine.collection(collection);
            m_engine.create(document);
            
            auto response_headers = ::http::headers{};
            const auto id = static_cast<long long>(document["_id"s]);
            response_headers.set("location"s, "/"s + collection + "/"s + std::to_string(id));
            
            auto metadata_level = yar::http::odata::parse_metadata_level(headers);
            if(metadata_level != yar::http::odata::metadata_level::none)
                document = yar::http::odata::add_metadata(document, metadata_level, collection, id);
            
            return std::make_tuple("201 Created"s, xson::json::stringify(document), std::make_optional(response_headers));
        }
        catch(const std::exception& e)
        {
            auto error = xson::object{
                {"error", "Bad Request"s},
                {"message", std::string{e.what()}}
            };
            return std::make_tuple("400 Bad Request"s, xson::json::stringify(error), std::optional<::http::headers>{});
        }
    });
```

**After:**
```cpp
m_server.post("/[a-z][a-z0-9]*"s).response_with_headers(
    "application/json"sv,
    wrap_handler<handler_result_with_headers>([this](
        std::string_view request,
        std::string_view body,
        const ::http::headers& headers
    ) -> handler_result_with_headers {
        slog << debug << "POST /[a-z]+: "<< request << " <- " << body << flush;
        
        auto uri = ::http::uri{request};
        auto document = xson::json::parse(body);
        const auto guard = std::lock_guard{m_engine};
        auto collection = validate_collection_name(uri.path[1]);
        m_engine.collection(collection);
        m_engine.create(document);
        
        auto response_headers = ::http::headers{};
        const auto id = static_cast<long long>(document["_id"s]);
        response_headers.set("location"s, "/"s + collection + "/"s + std::to_string(id));
        
        auto metadata_level = yar::http::odata::parse_metadata_level(headers);
        if(metadata_level != yar::http::odata::metadata_level::none)
            document = yar::http::odata::add_metadata(document, metadata_level, collection, id);
        
        return std::make_tuple("201 Created"s, xson::json::stringify(document), std::make_optional(response_headers));
    }));
```

### 3. Usage Example - Handler with Custom Error (404)

**Before:**
```cpp
// GET by ID handler - returns 404 if not found
m_server.get("/[a-z][a-z0-9]*/[0-9]+"s).response_with_headers(
    "application/json"sv,
    [this](std::string_view request, std::string_view body, const ::http::headers& headers)
    {
        // ... Accept check ...
        try
        {
            // ... handler logic ...
            if(!found || documents.get<xson::object::array>().empty())
            {
                auto error = xson::object{
                    {"error", "Not Found"s},
                    {"message", "Document not found"s},
                    {"collection", collection},
                    {"id", id}
                };
                return std::make_tuple("404 Not Found"s, xson::json::stringify(error), std::optional<::http::headers>{});
            }
            // ... success case ...
        }
        catch(const std::exception& e) { /* ... */ }
    });
```

**After:**
```cpp
m_server.get("/[a-z][a-z0-9]*/[0-9]+"s).response_with_headers(
    "application/json"sv,
    wrap_handler<handler_result_with_headers>([this](
        std::string_view request,
        std::string_view body,
        const ::http::headers& headers
    ) -> handler_result_with_headers {
        // ... handler logic ...
        if(!found || documents.get<xson::object::array>().empty())
        {
            auto error = xson::object{
                {"error", "Not Found"s},
                {"message", "Document not found"s},
                {"collection", collection},
                {"id", id}
            };
            return std::make_tuple("404 Not Found"s, xson::json::stringify(error), std::optional<::http::headers>{});
        }
        // ... success case ...
    }));
```

Note: Custom error responses (404, 412, etc.) still need to be handled in the handler, but the wrapper handles the common Accept check and exception handling.

### 4. Additional Helper Functions

We can also extract common operations:

```cpp
// Collection validation and setup helper
struct handler_context {
    ::http::uri uri;
    std::string collection;
    std::lock_guard<ext::lockable<yar::db::engine>> guard;
    
    handler_context(std::string_view request, ext::lockable<yar::db::engine>& engine)
        : uri{request}
        , collection{validate_collection_name(uri.path[1])}
        , guard{engine}
    {
        engine.collection(collection);
    }
};

// OData metadata helper
inline auto add_metadata_if_requested(
    xson::object& document,
    const ::http::headers& headers,
    const std::string& collection,
    long long id
) -> void
{
    auto metadata_level = yar::http::odata::parse_metadata_level(headers);
    if(metadata_level != yar::http::odata::metadata_level::none)
        document = yar::http::odata::add_metadata(document, metadata_level, collection, id);
}
```

### 5. Benefits

1. **Eliminates ~200+ lines** of duplicated code
2. **Consistent error handling** across all handlers
3. **Single point of change** for Accept header logic
4. **Cleaner handler code** - focuses on business logic
5. **Type-safe** - template ensures correct return types
6. **Zero runtime overhead** - templates are compile-time

### 6. Migration Strategy

1. **Phase 1**: Add wrapper functions to `yar-helpers.c++m`
2. **Phase 2**: Migrate simplest handlers first (DELETE, simple GETs)
3. **Phase 3**: Migrate handlers with custom errors (404, 412)
4. **Phase 4**: Migrate complex handlers (conditional requests, OData)
5. **Phase 5**: Extract additional helpers (context, metadata)

### 7. Estimated Impact

- **Lines removed**: ~200-250 lines
- **Lines added**: ~50 lines (wrapper + helpers)
- **Net reduction**: ~150-200 lines
- **Maintainability**: Significantly improved
- **Test coverage**: Easier to test wrapper logic once

## Recommendation

**Yes, there are enough similarities to justify refactoring.** The template wrapper approach is ideal because:

1. ✅ Handlers share identical patterns (Accept check, exception handling)
2. ✅ Framework accepts function objects (perfect for templates)
3. ✅ Zero runtime overhead (compile-time templates)
4. ✅ Type-safe (compile-time type checking)
5. ✅ Flexible (handlers can still return custom errors)

Start with the wrapper template, then gradually extract more helpers as patterns emerge.

