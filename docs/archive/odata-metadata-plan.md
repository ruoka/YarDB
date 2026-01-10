# OData /$metadata Endpoint Implementation Plan

**Status**: ✅ **COMPLETED** (December 2025)

## Overview

This document describes the implementation of the OData `/$metadata` service endpoint in YarDB that generates OData 4.01 JSON CSDL (Common Schema Definition Language) metadata based on existing documents in the database.

**Reference**: [OData 4.01 Specification](https://www.odata.org/documentation/) and [OData JSON Format](https://www.odata.org/documentation/)

## Implementation Summary

1. ✅ Implemented `GET /$metadata` endpoint that returns OData 4.01 JSON CSDL metadata
2. ✅ Generates metadata by inferring schemas from existing documents in collections
3. ✅ Uses the most recent document in each collection to infer the schema
4. ✅ Returns proper `application/json` content type for JSON CSDL format
5. ✅ Maps field types to EDM types (Edm.String, Edm.Int64, Edm.Double, Edm.Boolean)
6. ✅ Complex types (objects/arrays) are serialized as Edm.String
7. ✅ `_id` field is always included as non-nullable Edm.Int64 key for all entity types
8. ✅ Excludes internal collections (e.g., `_db`) from metadata

## Original Goals (Historical Reference)

1. Implement `GET /$metadata` endpoint that returns OData 4.01 JSON CSDL metadata
2. Generate metadata by inferring schemas from existing documents in collections
3. Use the most recent document version in each collection to infer the schema
4. Return proper `application/json` content type for JSON CSDL format

## OData 4.01 JSON CSDL Format

According to OData 4.01 specifications, the JSON CSDL format has the following structure:

```json
{
  "$Version": "4.01",
  "$EntityContainer": "DefaultContainer",
  "EntitySets": [
    {
      "Name": "CollectionName",
      "EntityType": "Default.CollectionName"
    }
  ],
  "EntityTypes": [
    {
      "Name": "CollectionName",
      "Key": [
        {
          "PropertyRef": [
            {
              "Name": "_id"
            }
          ]
        }
      ],
      "Property": [
        {
          "Name": "_id",
          "Type": "Edm.Int64",
          "Nullable": false
        },
        {
          "Name": "fieldName",
          "Type": "Edm.String",  // or Edm.Int64, Edm.Double, Edm.Boolean, etc.
          "Nullable": true
        }
      ]
    }
  ]
}
```

### Key Components

1. **$Version**: OData version (always "4.01" for our implementation)
2. **$EntityContainer**: Container name (we'll use "DefaultContainer")
3. **EntitySets**: Array of collections, each mapped to an EntityType
4. **EntityTypes**: Array of type definitions with:
   - **Name**: Collection name (same as EntitySet name)
   - **Key**: Primary key property (always `_id` for YarDB)
   - **Property**: Array of property definitions with:
     - **Name**: Field name
     - **Type**: EDM type (Edm.String, Edm.Int64, Edm.Double, Edm.Boolean, Edm.DateTimeOffset, etc.)
     - **Nullable**: Whether the field can be null (optional, defaults to true)

## Implementation Strategy

### Phase 1: Schema Inference

Create functions to analyze documents and infer EDM (Entity Data Model) types:

**File**: `YarDB/yar-odata.c++m` (add new functions)

```cpp
// Infer EDM type from xson::object value
inline auto infer_edm_type(const xson::object& value) -> std::string
{
    if(value.is_null()) return "Edm.String"; // Default for null
    
    if(value.is_string()) return "Edm.String";
    if(value.is_integer()) return "Edm.Int64";
    if(value.is_double() || value.is_float()) return "Edm.Double";
    if(value.is_boolean()) return "Edm.Boolean";
    if(value.is_object() || value.is_array()) return "Edm.String"; // Serialize as JSON string
    
    // Fallback
    return "Edm.String";
}

// Infer schema from a single document
inline auto infer_document_schema(const xson::object& document) -> xson::object
{
    auto properties = xson::object::array{};
    
    // Always include _id as non-nullable Int64
    properties.push_back(xson::object{
        {"Name", "_id"},
        {"Type", "Edm.Int64"},
        {"Nullable", false}
    });
    
    // Analyze other fields (skip _id which is already added)
    for(const auto& [key, value] : document.get<xson::object::map>())
    {
        if(key == "_id") continue; // Already added
        
        properties.push_back(xson::object{
            {"Name", key},
            {"Type", infer_edm_type(value)},
            {"Nullable", true} // All fields except _id are nullable
        });
    }
    
    return xson::object{
        {"Property", properties}
    };
}

// Infer schema from collection (use most recent document)
inline auto infer_collection_schema(const std::string& collection_name, yar::db::engine& engine) -> xson::object
{
    auto guard = std::lock_guard{engine};
    engine.collection(collection_name);
    
    // Get the most recent document (last one in storage)
    // We'll read with $top=1 and $orderby to get latest, or just get first document as approximation
    auto selector = xson::object{"$top", 1ll};
    auto documents = xson::object{};
    
    if(engine.read(selector, documents) && documents.is_array() && !documents.get<xson::object::array>().empty())
    {
        const auto& doc = documents.get<xson::object::array>()[0];
        return infer_document_schema(doc);
    }
    
    // No documents in collection - return empty schema with just _id
    return xson::object{
        {"Property", xson::object::array{
            xson::object{
                {"Name", "_id"},
                {"Type", "Edm.Int64"},
                {"Nullable", false}
            }
        }}
    };
}
```

### Phase 2: Metadata Generation

Create function to generate full JSON CSDL metadata document:

**File**: `YarDB/yar-odata.c++m` (add new function)

```cpp
// Generate OData 4.01 JSON CSDL metadata
inline auto generate_metadata(yar::db::engine& engine) -> xson::object
{
    auto entity_sets = xson::object::array{};
    auto entity_types = xson::object::array{};
    
    // Get all collections
    auto guard = std::lock_guard{engine};
    const auto collections = engine.collections();
    
    for(const auto& collection_name : collections.get<xson::object::array>())
    {
        const auto collection_str = collection_name.get<std::string>();
        
        // Skip internal collections
        if(collection_str == "_db") continue;
        
        // Create EntitySet
        entity_sets.push_back(xson::object{
            {"Name", collection_str},
            {"EntityType", "Default." + collection_str}
        });
        
        // Infer schema and create EntityType
        engine.collection(collection_str);
        auto schema = infer_collection_schema(collection_str, engine);
        
        entity_types.push_back(xson::object{
            {"Name", collection_str},
            {"Key", xson::object::array{
                xson::object{
                    {"PropertyRef", xson::object::array{
                        xson::object{
                            {"Name", "_id"}
                        }
                    }}
                }
            }},
            {"Property", schema["Property"]}
        });
    }
    
    // Build complete metadata document
    return xson::object{
        {"$Version", "4.01"},
        {"$EntityContainer", "DefaultContainer"},
        {"EntitySets", entity_sets},
        {"EntityTypes", entity_types}
    };
}
```

### Phase 3: Route Handler

Add GET /$metadata route handler to `yar-httpd.c++m`:

**Location**: Inside `setup_routes()` function, after the list collections handler

```cpp
// GET /$metadata - OData metadata endpoint
auto metadata_handler = [this]([[maybe_unused]] ::http::request_view request, 
                                [[maybe_unused]] ::http::body_view body, 
                                [[maybe_unused]] ::http::headers& headers)
{
    // Generate metadata from existing collections
    auto metadata = yar::http::odata::generate_metadata(m_engine);
    
    // Return as JSON
    return response_with_headers{
        status_ok, 
        xson::json::stringify(metadata), 
        std::optional<::http::headers>{}
    };
};

auto metadata_middlewares = build_middleware_chain(
    method_get, 
    "GET_METADATA"sv, 
    "GET_METADATA_ERROR"sv, 
    false
);
m_server.get("/\\$metadata"s).response_with_headers(
    "application/json"sv,
    ::http::middleware::wrap(metadata_handler, metadata_middlewares)
);
```

**Note**: The route pattern `"/\\$metadata"s` escapes the `$` character in the regex pattern. The HTTP server uses regex for route matching, so we need to escape special characters.

## Implementation Details

### Schema Inference Strategy

1. **Document Selection**: Use the most recent document in each collection to infer schema
   - Get first document with `$top=1` (or iterate and find latest by timestamp if needed)
   - If collection is empty, return schema with only `_id` field

2. **Type Inference**: Map xson::object types to EDM types:
   - `string` → `Edm.String`
   - `integer` → `Edm.Int64`
   - `double/float` → `Edm.Double`
   - `boolean` → `Edm.Boolean`
   - `object/array` → `Edm.String` (serialize as JSON string)
   - `null` → `Edm.String` (default, nullable)

3. **Field Handling**:
   - Always include `_id` as non-nullable `Edm.Int64` key
   - All other fields are nullable by default
   - Field names are taken directly from document keys

### Limitations

1. **Schema Evolution**: If documents in a collection have different schemas, only the first/latest document's schema is used
2. **Nested Objects**: Complex objects and arrays are serialized as JSON strings (Edm.String)
3. **Type Precision**: Type inference is based on runtime values, not declared types
4. **Collections**: Empty collections return minimal schema with only `_id`

### Future Enhancements

1. **Union Schemas**: Merge schemas from multiple documents in a collection
2. **Type Annotations**: Add more precise type information (e.g., Edm.DateTimeOffset, Edm.Guid)
3. **Navigation Properties**: Support for relationships between collections
4. **Actions and Functions**: Add support for custom OData actions and functions
5. **Complex Types**: Proper support for nested object structures
6. **Annotations**: Add OData annotations for documentation, validation, etc.

## Testing Strategy

### Unit Tests

1. **Schema Inference Tests**:
   - Test `infer_edm_type()` with various xson::object types
   - Test `infer_document_schema()` with sample documents
   - Test `infer_collection_schema()` with empty and non-empty collections

2. **Metadata Generation Tests**:
   - Test `generate_metadata()` with multiple collections
   - Test metadata structure matches OData 4.01 JSON CSDL format
   - Test that internal collections (like `_db`) are excluded

### Integration Tests

1. **Endpoint Tests**:
   - Test `GET /$metadata` returns 200 OK
   - Test response Content-Type is `application/json`
   - Test response body is valid JSON CSDL
   - Test metadata reflects actual collections and schemas

2. **End-to-End Tests**:
   - Create documents in multiple collections
   - Query `/$metadata` and verify schemas match documents
   - Test with empty collections
   - Test with collections containing different field types

## Implementation Status

All tasks have been completed:

1. ✅ **YarDB/yar-odata.c++m**: Added schema inference and metadata generation functions
2. ✅ **YarDB/yar-httpd.c++m**: Added `/$metadata` route handler in `setup_routes()`
3. ✅ **YarDB/yar-odata.test.c++**: Added tests for metadata generation
4. ✅ **YarDB/yar-httpd.test.c++**: Added tests for `/$metadata` endpoint
5. ✅ **docs/README.md**: Updated documentation with `/$metadata` endpoint
6. ✅ **docs/programs.md**: Added `/$metadata` to API documentation

## Files Modified (Historical Reference)

1. **YarDB/yar-odata.c++m**: Add schema inference and metadata generation functions
2. **YarDB/yar-httpd.c++m**: Add `/$metadata` route handler in `setup_routes()`
3. **YarDB/yar-odata.test.c++**: Add tests for metadata generation
4. **YarDB/yar-httpd.test.c++**: Add tests for `/$metadata` endpoint
5. **docs/README.md**: Update documentation with `/$metadata` endpoint
6. **docs/programs.md**: Add `/$metadata` to API documentation

## Example Response

```json
{
  "$Version": "4.01",
  "$EntityContainer": "DefaultContainer",
  "EntitySets": [
    {
      "Name": "users",
      "EntityType": "Default.users"
    },
    {
      "Name": "orders",
      "EntityType": "Default.orders"
    }
  ],
  "EntityTypes": [
    {
      "Name": "users",
      "Key": [
        {
          "PropertyRef": [
            {
              "Name": "_id"
            }
          ]
        }
      ],
      "Property": [
        {
          "Name": "_id",
          "Type": "Edm.Int64",
          "Nullable": false
        },
        {
          "Name": "name",
          "Type": "Edm.String",
          "Nullable": true
        },
        {
          "Name": "age",
          "Type": "Edm.Int64",
          "Nullable": true
        },
        {
          "Name": "active",
          "Type": "Edm.Boolean",
          "Nullable": true
        }
      ]
    },
    {
      "Name": "orders",
      "Key": [
        {
          "PropertyRef": [
            {
              "Name": "_id"
            }
          ]
        }
      ],
      "Property": [
        {
          "Name": "_id",
          "Type": "Edm.Int64",
          "Nullable": false
        },
        {
          "Name": "userId",
          "Type": "Edm.Int64",
          "Nullable": true
        },
        {
          "Name": "total",
          "Type": "Edm.Double",
          "Nullable": true
        }
      ]
    }
  ]
}
```

## References

- [OData 4.01 Documentation](https://www.odata.org/documentation/)
- [OData JSON Format Specification](https://www.odata.org/documentation/odata-version-4-01-json-format/)
- [OData CSDL JSON Representation](https://www.odata.org/documentation/odata-version-4-01-json-format/) (Part 4: Common Schema Definition Language (CSDL) JSON Representation)
