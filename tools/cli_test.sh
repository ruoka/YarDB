#!/bin/bash
# Test script for yarsh CLI improvements
# Tests HEAD method, query parameters, and response headers display

set -e

# Determine build directory based on OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    BUILD_DIR="build-darwin-debug"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    BUILD_DIR="build-linux-debug"
else
    BUILD_DIR="build-debug"
fi

CLI="./${BUILD_DIR}/bin/yarsh"
SERVER="${1:-http://localhost:2112}"
TEST_COLLECTION="clitest_$(date +%s)"

echo "=== Testing CLI Improvements ==="
echo "Using CLI: $CLI"
echo "Server: $SERVER"
echo "Test collection: $TEST_COLLECTION"
echo ""

# Change to script directory
cd "$(dirname "$0")/.."

# Test 1: POST to create a document and verify headers
echo "=== Test 1: POST to create document (check Location header) ==="
cat <<EOF | $CLI "$SERVER" 2>&1 | grep -A 10 "Response Headers:"
POST /${TEST_COLLECTION}
{"name":"Test Document","age":30,"status":"active"}
EXIT
EOF

echo ""
echo "=== Test 2: GET document (check ETag and Last-Modified headers) ==="
cat <<EOF | $CLI "$SERVER" 2>&1 | grep -A 15 "Response Headers:"
GET /${TEST_COLLECTION}/1
EXIT
EOF

echo ""
echo "=== Test 3: HEAD method (headers only, no body) ==="
cat <<EOF | $CLI "$SERVER" 2>&1 | grep -A 10 "HEAD /${TEST_COLLECTION}/1" | head -15
HEAD /${TEST_COLLECTION}/1
EXIT
EOF

echo ""
echo "=== Test 4: GET with \$top query parameter ==="
# Create multiple documents first
for i in 1 2 3; do
    cat <<DOCEOF | $CLI "$SERVER" > /dev/null 2>&1
POST /${TEST_COLLECTION}
{"name":"Doc $i","value":$i}
EXIT
DOCEOF
    sleep 0.1
done

cat <<EOF | $CLI "$SERVER" 2>&1 | grep -A 5 "\$top"
GET /${TEST_COLLECTION}?\$top=2
EXIT
EOF

echo ""
echo "=== Test 5: GET with \$orderby query parameter ==="
cat <<EOF | $CLI "$SERVER" 2>&1 | grep -A 5 "\$orderby"
GET /${TEST_COLLECTION}?\$orderby=value
EXIT
EOF

echo ""
echo "=== Test 6: PUT update (check Content-Location and ETag change) ==="
cat <<EOF | $CLI "$SERVER" 2>&1 | grep -A 10 "PUT /${TEST_COLLECTION}/1"
PUT /${TEST_COLLECTION}/1
{"name":"Updated Document","age":31,"status":"inactive"}
EXIT
EOF

echo ""
echo "=== Test 7: Verify ETag changed after update ==="
cat <<EOF | $CLI "$SERVER" 2>&1 | grep -A 10 "Response Headers:"
GET /${TEST_COLLECTION}/1
EXIT
EOF

echo ""
echo "=== Tests completed ==="
echo "Note: Query parameters with spaces need URL encoding (e.g., \$filter=age%20gt%2025)"
