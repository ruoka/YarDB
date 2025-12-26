# HTTP API Proposal: Add Secondary Indexes

## ✅ FULLY IMPLEMENTED - December 2025

This proposal has been **fully implemented**. The REST API for secondary indexes is now available:

- ✅ `PUT /_db/{collection_name}` - Create/update index configuration
- ✅ `PATCH /_db/{collection_name}` - Incremental index updates
- ✅ `GET /_reindex` - Reindex all collections (added Dec 2025)

**Index configuration** is stored in the `_db` system collection and **automatic reindexing** occurs after configuration changes.

---

## Historical Context (Pre-Implementation)

## Endpoint Design

### PUT `/_db/{collection_name}`
Creates or updates the index configuration for a collection and triggers reindexing.

**Request:**
- **Method**: `PUT`
- **Path**: `/_db/{collection_name}`
- **Content-Type**: `application/json`
- **Body**:
```json
{
  "keys": ["field1", "field2", "field3"]
}
```

**Response:**
- **200 OK** (if index configuration updated)
- **201 Created** (if new index configuration created)
- **400 Bad Request** (if invalid keys or collection name)
- **Body**: Updated index configuration document
```json
{
  "collection": "users",
  "keys": ["field1", "field2", "field3"],
  "_id": 1
}
```

## Implementation Details

1. **Validate collection name** - Must match collection name pattern
2. **Parse request body** - Extract `keys` array
3. **Validate keys** - Ensure all keys are valid field names
4. **Switch to target collection** - Set engine collection context
5. **Add keys to index** - Call `index.add(keys)`
6. **Update `_db` document** - Upsert document in `_db` collection with:
   - `{"collection": collection_name}`
   - `{"keys": [list of all keys]}`
7. **Trigger reindex** - Call `reindex()` to populate new indexes
8. **Return response** - Return the updated `_db` document

## Example Usage

```bash
# Add secondary indexes to 'users' collection
curl -X PUT http://localhost:8080/_db/users \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  -d '{"keys": ["email", "status"]}'
```

## Alternative Design: PATCH

Alternatively, we could use `PATCH` to allow partial updates (adding keys incrementally):

### PATCH `/_db/{collection_name}`
Adds additional keys to existing index configuration.

**Request:**
- **Method**: `PATCH`
- **Path**: `/_db/{collection_name}`
- **Body**: Same as PUT

**Behavior:**
- Merges new keys with existing keys
- Triggers reindex after update

## Route Pattern

The route should be registered **before** the generic collection routes to ensure it matches first:

```cpp
// PUT /_db/{collection_name} - Add/update secondary indexes
m_server.put("/_db/[a-z][a-z0-9]*"s).response_with_headers(
    "application/json"sv,
    [this](std::string_view request, std::string_view body, const ::http::headers& headers)
    {
        // Implementation
    });
```

## Error Handling

- **Invalid collection name**: Return 400 Bad Request
- **Invalid keys format**: Return 400 Bad Request
- **Empty keys array**: Return 400 Bad Request (or allow clearing indexes?)
- **Database errors**: Return 500 Internal Server Error

## Notes

- The `_db` collection is a special system collection that stores metadata
- After updating indexes, `reindex()` must be called to populate the new indexes
- This operation may take time for large collections (consider async or progress reporting)

