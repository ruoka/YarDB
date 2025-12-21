#!/bin/bash

# Test script for OData metadata support
# Make sure the server is running: ./build-darwin-debug/bin/yardb

PORT=2112
BASE_URL="http://localhost:${PORT}"

echo "=== Testing OData Metadata Support ==="
echo ""

# Create a test document
echo "1. Creating test document..."
DOC_ID=$(curl -X POST "${BASE_URL}/users" \
  -H "Content-Type: application/json" \
  -d '{"name": "Alice", "email": "alice@example.com", "age": 30}' \
  -s | python3 -c "import sys, json; print(json.load(sys.stdin)['_id'])")

echo "Created document with ID: ${DOC_ID}"
echo ""

# Test 1: No Accept header (plain JSON)
echo "2. GET /users/${DOC_ID} (no Accept header - plain JSON):"
curl -X GET "${BASE_URL}/users/${DOC_ID}" -s | python3 -m json.tool
echo ""

# Test 2: Minimal metadata
echo "3. GET /users/${DOC_ID} (Accept: application/json;odata=minimalmetadata):"
curl -X GET "${BASE_URL}/users/${DOC_ID}" \
  -H "Accept: application/json;odata=minimalmetadata" \
  -s | python3 -m json.tool
echo ""

# Test 3: Full metadata
echo "4. GET /users/${DOC_ID} (Accept: application/json;odata=fullmetadata):"
curl -X GET "${BASE_URL}/users/${DOC_ID}" \
  -H "Accept: application/json;odata=fullmetadata" \
  -s | python3 -m json.tool
echo ""

# Test 4: Collection query with minimal metadata
echo "5. GET /users (Accept: application/json;odata=minimalmetadata):"
curl -X GET "${BASE_URL}/users" \
  -H "Accept: application/json;odata=minimalmetadata" \
  -s | python3 -m json.tool
echo ""

# Test 5: Collection query with full metadata
echo "6. GET /users (Accept: application/json;odata=fullmetadata):"
curl -X GET "${BASE_URL}/users" \
  -H "Accept: application/json;odata=fullmetadata" \
  -s | python3 -m json.tool
echo ""

echo "=== Tests Complete ==="

