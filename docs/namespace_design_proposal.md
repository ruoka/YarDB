# Namespace Design Proposal for YarDB

## Current State Analysis

### Current Namespaces
```
db                          - Core database (engine, index, metadata, constants) AND HTTP server ❌
db::http                    - HTTP helper utilities
db::http::odata             - OData-specific functionality
```

### Problems with Current Design
1. **Mixed Concerns**: `db` namespace contains both database engine AND HTTP server
2. **Confusing Naming**: `rest_api_server` in `db` namespace suggests it's database functionality
3. **Poor Separation**: HTTP helpers nested under `db::http` implies they're database-related
4. **Inconsistency**: Some modules export `namespace db`, others just use it

## Proposed Design

### Option 1: Clear Separation (RECOMMENDED) ⭐

```
yar::db                     - Core database functionality
  ├── engine                - Database engine class
  ├── index                 - Indexing system
  ├── metadata              - Document metadata
  └── constants             - Database constants

yar::http                   - HTTP/REST API functionality
  ├── server                - REST API server (rest_api_server)
  ├── helpers               - HTTP utility functions
  └── odata                 - OData protocol support
```

**Module → Namespace Mapping:**
- `yar:engine` → `yar::db` namespace
- `yar:index` → `yar::db` namespace  
- `yar:metadata` → `yar::db` namespace
- `yar:constants` → `yar::db` namespace
- `yar:httpd` → `yar::http::server` namespace (or just `yar::http`)
- `yar:helper` → `yar::http::helpers` namespace
- `yar:odata` → `yar::http::odata` namespace

**Benefits:**
- ✅ Clear separation: database vs. API concerns
- ✅ Intuitive: `yar::http::server` clearly indicates HTTP server
- ✅ Scalable: Easy to add `yar::http::auth`, `yar::http::metrics`, etc.
- ✅ Consistent with module naming (`yar:*`)
- ✅ Follows P1204R0: namespaces match module structure

### Option 2: Single `yar` Top-Level with Sub-namespaces

```
yar                         - Top-level namespace
  ├── db                    - Database core
  │   ├── engine
  │   ├── index
  │   ├── metadata
  │   └── constants
  └── api                   - REST API (or "http")
      ├── server
      ├── helpers
      └── odata
```

**Module → Namespace Mapping:**
- All modules use `yar` as top-level
- Same as Option 1 but with explicit `yar` prefix

**Benefits:**
- ✅ Clear top-level namespace
- ✅ Still separates concerns
- ❌ More verbose (`yar::db::engine` vs `db::engine`)

### Option 3: Keep Current but Fix Inconsistencies

```
db                          - Core database (engine, index, metadata, constants)
yar::http                   - HTTP/REST API (separate from db)
  ├── server
  ├── helpers  
  └── odata
```

**Benefits:**
- ✅ Minimal changes needed
- ✅ Keeps familiar `db` namespace
- ❌ Still mixed concerns in `db`
- ❌ Inconsistent top-level namespaces

## Recommendation: Option 1

### Implementation Plan

#### Phase 1: Rename Core Database Namespaces
```cpp
// yar-engine.c++m
export namespace yar::db {
    class engine { ... };
}

// yar-index.c++m  
namespace yar::db {
    class index { ... };
}

// yar-metadata.c++m
export namespace yar::db {
    struct metadata { ... };
}

// yar-constants.c++m
export namespace yar::db {
    // constants...
}
```

#### Phase 2: Rename HTTP/API Namespaces
```cpp
// yar-httpd.c++m
namespace yar::http {
    class rest_api_server { ... };
}

// yar-helpers.c++m
namespace yar::http::helpers {
    // helper functions...
}

// yar-odata.c++m
namespace yar::http::odata {
    // OData functions...
}
```

#### Phase 3: Update Using Declarations
```cpp
// In files using both:
using namespace yar::db;
using namespace yar::http;
using namespace yar::http::helpers;
using namespace yar::http::odata;
```

### Migration Strategy

1. **Add aliases for backward compatibility** (temporary):
   ```cpp
   namespace db = yar::db;
   namespace http = yar::http;
   ```

2. **Gradually update all code** to use new namespaces

3. **Remove aliases** once migration complete

### Code Examples

#### Before (Current):
```cpp
// yar-httpd.c++m
namespace db {
    class rest_api_server {  // Confusing - not a database class!
        db::engine m_engine;  // OK
    };
}

// Usage:
db::rest_api_server server;  // Looks like database class
```

#### After (Proposed):
```cpp
// yar-httpd.c++m
namespace yar::http {
    class rest_api_server {  // Clear - HTTP server
        yar::db::engine m_engine;  // Uses database engine
    };
}

// Usage:
yar::http::rest_api_server server;  // Clear intent
```

### Type Aliases for Convenience

For commonly used types, we can add aliases:
```cpp
// yar.c++m
export module yar;
export namespace yar {
    namespace db = yar::db;
    namespace http = yar::http;
    
    // Common type aliases
    using object = xson::object;  // Already exists as db::object
}
```

## Alternative: Option 2 with Convenience Aliases

If we want to keep shorter names while maintaining clarity:

```cpp
// yar.c++m
export module yar;
export namespace yar {
    namespace db = ::yar::db;      // Allow yar::db::engine
    namespace http = ::yar::http;  // Allow yar::http::server
}

// Usage can be:
using namespace yar;
db::engine eng;        // Short but clear
http::rest_api_server server;  // Short but clear
```

## Summary

**Recommended**: Option 1 with `yar::db` and `yar::http` namespaces

**Key Principles**:
1. Separate database concerns from API concerns
2. Use consistent `yar::` prefix matching module names
3. Nest appropriately (helpers and odata under http)
4. Keep it intuitive and self-documenting

**Migration**: Can be done gradually with backward-compatibility aliases

