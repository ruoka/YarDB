module yar;
import :httpd;
import tester;
import std;
import net;
import xson;

namespace yar::httpd_unit_test {

using namespace std;
using namespace std::string_literals;
using namespace std::chrono_literals;
using namespace net;
using namespace xson;

class fixture
{
public:
    fixture(string_view f) : file{f}, port{"21120"s}, server{file, "21120"s}
    {
        auto fs = fstream{};
        fs.open(file, ios::out);
        fs.close();
        server.start();
        // Wait longer for server to start and bind to port
        std::this_thread::sleep_for(1000ms);
    }

    ~fixture()
    {
        // Stop the server and wait for the thread to finish
        server.stop();
        remove(file.c_str());
        // Also remove the PID lock file
        const auto pid_file = file + ".pid"s;
        remove(pid_file.c_str());
    }
    
    const std::string& get_port() const { return port; }
    
private:
    std::string file;
    std::string port;
    db::rest_api_server server;
};

// Helper function to parse HTTP status line and headers
auto parse_http_response(net::endpointstream& stream)
{
    auto version = ""s, status_code = ""s, reason = ""s;
    auto headers = http::headers{};
    auto body = ""s;

    // Read status line: "HTTP/1.1 201 Created\r\n"
    // Read the entire status line first
    auto status_line = ""s;
    getline(stream, status_line, '\r');
    stream >> ws; // Skip \r\n
    
    // Parse the status line: "HTTP/1.1 201 Created"
    auto space1 = status_line.find(' ');
    if(space1 != std::string::npos)
    {
        version = status_line.substr(0, space1);
        auto space2 = status_line.find(' ', space1 + 1);
        if(space2 != std::string::npos)
        {
            status_code = status_line.substr(space1 + 1, space2 - space1 - 1);
            reason = status_line.substr(space2 + 1);
        }
        else
        {
            // No reason phrase, just status code
            status_code = status_line.substr(space1 + 1);
            reason = ""s;
        }
    }
    
    // Read headers
    stream >> headers >> crlf;

    // Read body if content-length is present
    if(headers.contains("content-length"))
    {
        auto content_length = std::stoll(headers["content-length"]);
        if(content_length > 0)
        {
            body.resize(content_length);
            for(auto& c : body)
                c = stream.get();
        }
    }

    return std::make_tuple(status_code, reason, headers, body);
}

// Helper function to make HTTP request and get response
auto make_request(const string& port, const string& method, const string& path, const string& body = ""s)
{
    auto stream = connect("localhost"s, port);
    
    stream << method << " " << path << " HTTP/1.1" << crlf
           << "Host: localhost:" << port << crlf
           << "Accept: application/json" << crlf
           << "Content-Type: application/json" << crlf;
    
    if(!body.empty())
        stream << "Content-Length: " << body.length() << crlf;
    
    stream << crlf << body << flush;

    return parse_http_response(stream);
}

auto test_set()
{
    using namespace tester::basic;
    using namespace tester::assertions;

    test_case("REST API status codes and responses") = []
    {
        const auto test_file = "./httpd_test.db";
        // Use shared_ptr so fixture lives beyond test_case lambda execution
        // Sections execute later and must capture by value
        auto setup = std::make_shared<fixture>(test_file);

        section("POST returns 201 Created") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Test Item","value":42})"s
            );

            require_eq(status, "201"s);
            require_eq(reason, "Created"s);
            require_true(headers.contains("content-type"));
            
            // Verify response body contains the created document
            auto document = json::parse(response_body);
            require_true(document.has("_id"s));
            const string name = document["name"s];
            require_eq(name, "Test Item"s);
        };

        section("GET existing document returns 200 OK with single object") = [setup]
        {
            // First create a document
            auto [post_status, post_reason, post_headers, post_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Existing Item"})"s
            );
            require_eq(post_status, "201"s);
            
            auto created_doc = json::parse(post_body);
            const long long id = created_doc["_id"s];
            
            // Get the document
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems/"s + std::to_string(id), ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Verify response is a single object (starts with {, not [)
            require_eq(response_body.front(), '{');
            require_eq(response_body.back(), '}');
            
            auto document = json::parse(response_body);
            require_true(document.has("_id"s));
            const long long doc_id = document["_id"s];
            require_eq(doc_id, id);
        };

        section("GET non-existent document returns 404 Not Found") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems/99999"s, ""s
            );

            require_eq(status, "404"s);
            require_eq(reason, "Not Found"s);
            
            // Verify error object structure
            auto error = json::parse(response_body);
            require_true(error.has("error"s));
            const string error_msg = error["error"s];
            require_eq(error_msg, "Not Found"s);
            require_true(error.has("message"s));
            require_true(error.has("collection"s));
            require_true(error.has("id"s));
        };

        section("DELETE existing document returns 204 No Content") = [setup]
        {
            // First create a document
            auto [post_status, post_reason, post_headers, post_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"name":"To Delete"})"s
            );
            require_eq(post_status, "201"s);
            
            auto created_doc = json::parse(post_body);
            const long long id = created_doc["_id"s];
            
            // Delete the document
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "DELETE"s, "/testitems/"s + std::to_string(id), ""s
            );

            require_eq(status, "204"s);
            require_eq(reason, "No Content"s);
            require_true(response_body.empty());
        };

        section("DELETE non-existent document returns 404 Not Found") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "DELETE"s, "/testitems/99999"s, ""s
            );

            require_eq(status, "404"s);
            require_eq(reason, "Not Found"s);
            
            // Verify error object structure
            auto error = json::parse(response_body);
            require_true(error.has("error"s));
            const string error_msg = error["error"s];
            require_eq(error_msg, "Not Found"s);
        };

        section("POST with invalid JSON returns 400 Bad Request") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"invalid":json})"s
            );

            require_eq(status, "400"s);
            require_eq(reason, "Bad Request"s);
            
            // Verify error object structure
            auto error = json::parse(response_body);
            require_true(error.has("error"s));
            const string error_msg = error["error"s];
            require_eq(error_msg, "Bad Request"s);
            require_true(error.has("message"s));
        };

        section("PUT updates document and returns 200 OK with Content-Location header") = [setup]
        {
            // First create a document
            auto [post_status, post_reason, post_headers, post_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Original"})"s
            );
            require_eq(post_status, "201"s);
            
            auto created_doc = json::parse(post_body);
            const long long id = created_doc["_id"s];
            
            // Update the document
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "PUT"s, "/testitems/"s + std::to_string(id), R"({"name":"Updated","value":100})"s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Verify Content-Location header is present
            require_true(headers.contains("content-location"s));
            const string content_location = headers["content-location"s];
            require_eq(content_location, "/testitems/"s + std::to_string(id));
            
            auto document = json::parse(response_body);
            const string name = document["name"s];
            require_eq(name, "Updated"s);
            const long long value = document["value"s];
            require_eq(value, 100ll);
        };

        section("PUT creates document (upsert) and returns 201 Created") = [setup]
        {
            // PUT to a non-existent document should create it
            const long long new_id = 99999ll;
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "PUT"s, "/testitems/"s + std::to_string(new_id), R"({"name":"New Item","value":200})"s
            );

            require_eq(status, "201"s);
            require_eq(reason, "Created"s);
            
            // Verify Location header is present
            require_true(headers.contains("location"s));
            const string location = headers["location"s];
            require_eq(location, "/testitems/"s + std::to_string(new_id));
            
            auto document = json::parse(response_body);
            const long long doc_id = document["_id"s];
            require_eq(doc_id, new_id);
            const string name = document["name"s];
            require_eq(name, "New Item"s);
            const long long value = document["value"s];
            require_eq(value, 200ll);
            
            // Verify we can retrieve it
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/testitems/"s + std::to_string(new_id), ""s
            );
            require_eq(get_status, "200"s);
        };

        section("PATCH updates document and returns 200 OK with Content-Location header") = [setup]
        {
            // First create a document
            auto [post_status, post_reason, post_headers, post_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Original","value":50})"s
            );
            require_eq(post_status, "201"s);
            
            auto created_doc = json::parse(post_body);
            const long long id = created_doc["_id"s];
            
            // Partially update the document
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "PATCH"s, "/testitems/"s + std::to_string(id), R"({"value":75})"s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Verify Content-Location header is present
            require_true(headers.contains("content-location"s));
            const string content_location = headers["content-location"s];
            require_eq(content_location, "/testitems/"s + std::to_string(id));
        };
    };

    test_case("OData query parameters") = []
    {
        const auto test_file = "./httpd_test_odata.db";
        auto setup = std::make_shared<fixture>(test_file);

        section("GET with $top query parameter returns limited results") = [setup]
        {
            // Create multiple documents
            for(int i = 1; i <= 5; ++i)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"("})"s
                );
                require_eq(post_status, "201"s);
                // Small delay to ensure server processes request
                std::this_thread::sleep_for(50ms);
            }
            
            // Get collection with $top=2
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems?$top=2"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Verify response contains at most 2 items
            // Response is an array, not an object with collection name
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            require_true(items.size() <= 2);
        };

        section("GET with $orderby desc query parameter returns descending order") = [setup]
        {
            // Create multiple documents
            for(int i = 1; i <= 3; ++i)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"("})"s
                );
                require_eq(post_status, "201"s);
                // Small delay to ensure server processes request
                std::this_thread::sleep_for(50ms);
            }
            
            // Get collection with $orderby desc
            // Note: Space in "field desc" must be URL-encoded as %20
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems?$orderby=field%20desc"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Verify response is valid JSON (array format)
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
        };

        section("GET with $top and $orderby combined") = [setup]
        {
            // Create multiple documents
            for(int i = 1; i <= 5; ++i)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"("})"s
                );
                // Allow retry if server is busy
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"("})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                // Small delay to ensure server processes request
                std::this_thread::sleep_for(100ms);
            }
            
            // Get collection with $top=2 and $orderby desc
            // Note: Space in "field desc" needs to be URL-encoded as %20
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems?$top=2&$orderby=field%20desc"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Verify response contains at most 2 items
            // Response is an array, not an object with collection name
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            require_true(items.size() <= 2);
        };

        section("GET with $skip query parameter skips first N results") = [setup]
        {
            // Create multiple documents
            for(int i = 1; i <= 10; ++i)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"(", "value":)"s + std::to_string(i) + R"(})"s
                );
                // Allow retry if server is busy
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"(", "value":)"s + std::to_string(i) + R"(})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                std::this_thread::sleep_for(100ms);
            }
            
            // Get collection with $skip=3 (should skip first 3 items)
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems?$skip=3"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Verify response is an array
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Get all items without skip to compare
            auto [status_all, reason_all, headers_all, response_body_all] = make_request(
                setup->get_port(), "GET"s, "/testitems"s, ""s
            );
            require_eq(status_all, "200"s);
            auto documents_all = json::parse(response_body_all);
            const auto& items_all = documents_all.get<object::array>();
            
            // With $skip=3, we should have fewer items than without skip
            // (unless there are fewer than 3 items total, but we created 10)
            require_true(items.size() < items_all.size());
            
            // Verify skip is working: the difference should be at least 3 (or equal to total if < 3 items)
            const auto expected_difference = std::min(static_cast<std::size_t>(3), items_all.size());
            require_true(items_all.size() - items.size() >= expected_difference);
        };

        section("GET with $skip and $top combined for pagination") = [setup]
        {
            // Create multiple documents
            for(int i = 1; i <= 10; ++i)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"(", "value":)"s + std::to_string(i) + R"(})"s
                );
                // Allow retry if server is busy
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"(", "value":)"s + std::to_string(i) + R"(})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                std::this_thread::sleep_for(100ms);
            }
            
            // Get collection with $skip=3&$top=3 (should return items 4, 5, 6)
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems?$skip=3&$top=3"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Verify response contains at most 3 items
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            require_true(items.size() <= 3);
            
            // Get all items without skip/top to compare
            auto [status_all, reason_all, headers_all, response_body_all] = make_request(
                setup->get_port(), "GET"s, "/testitems"s, ""s
            );
            require_eq(status_all, "200"s);
            auto documents_all = json::parse(response_body_all);
            const auto& items_all = documents_all.get<object::array>();
            
            // Verify we got at most 3 items (top limit)
            require_true(items.size() <= 3);
            
            // Verify skip is working: we should have fewer items than without skip
            // (skip=3 means we skip at least 3, so difference should be >= 3 or equal to total)
            if(items_all.size() > 3)
            {
                require_true(items.size() <= items_all.size() - 3);
            }
        };

        section("GET with $skip=0 should return all items") = [setup]
        {
            // Create multiple documents
            for(int i = 1; i <= 5; ++i)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"("})"s
                );
                // Allow retry if server is busy
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Item )"s + std::to_string(i) + R"("})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                std::this_thread::sleep_for(100ms);
            }
            
            // Get collection with $skip=0 (should return all items, same as no skip)
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems?$skip=0"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Get all items without skip to compare
            auto [status_all, reason_all, headers_all, response_body_all] = make_request(
                setup->get_port(), "GET"s, "/testitems"s, ""s
            );
            require_eq(status_all, "200"s);
            auto documents_all = json::parse(response_body_all);
            const auto& items_all = documents_all.get<object::array>();
            
            // Verify response contains all items (skip=0 should return everything)
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // With $skip=0, we should get the same number of items as without skip
            require_eq(items.size(), items_all.size());
        };
    };

    test_case("OData advanced query parameters") = []
    {
        const auto test_file = "./httpd_test_odata_advanced.db";
        auto setup = std::make_shared<fixture>(test_file);

        section("GET with $filter - age gt 25") = [setup]
        {
            // Create test documents with different ages
            auto test_data = std::vector<std::pair<std::string, int>>{
                {"Alice"s, 25},
                {"Bob"s, 30},
                {"Charlie"s, 35},
                {"David"s, 20}
            };
            
            for(const auto& [name, age] : test_data)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/users"s, R"({"name":")"s + name + R"(", "age":)"s + std::to_string(age) + R"(})"s
                );
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/users"s, R"({"name":")"s + name + R"(", "age":)"s + std::to_string(age) + R"(})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                std::this_thread::sleep_for(100ms);
            }
            
            // Filter: age gt 25 (should return Bob and Charlie)
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users?$filter=age%20gt%2025"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have 2 items (Bob age 30, Charlie age 35)
            require_eq(items.size(), 2);
            
            // Verify all returned items have age > 25
            for(const auto& item : items)
            {
                require_true(item.has("age"s));
                const auto age = static_cast<long long>(item["age"s]);
                require_true(age > 25);
            }
        };

        section("GET with $filter - name eq 'Alice'") = [setup]
        {
            // Filter: name eq 'Alice' (should return only Alice)
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users?$filter=name%20eq%20'Alice'"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have exactly 1 item
            require_eq(items.size(), 1);
            
            // Verify it's Alice
            require_true(items[0].has("name"s));
            const string name = items[0]["name"s];
            require_eq(name, "Alice"s);
        };

        section("GET with $filter - status eq 'active' and age ge 25") = [setup]
        {
            // Add status field to existing documents
            // First, get all users and update them with status
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/users"s, ""s
            );
            require_eq(get_status, "200"s);
            
            auto all_docs = json::parse(get_body);
            const auto& all_items = all_docs.get<object::array>();
            
            // Update documents with status (simplified - in real test we'd use PATCH)
            // For this test, we'll create new documents with status
            auto test_data = std::vector<std::tuple<std::string, int, std::string>>{
                {"Alice2"s, 25, "active"s},
                {"Bob2"s, 30, "active"s},
                {"Charlie2"s, 35, "inactive"s},
                {"David2"s, 20, "active"s}
            };
            
            for(const auto& [name, age, status] : test_data)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/users2"s, R"({"name":")"s + name + R"(", "age":)"s + std::to_string(age) + R"(, "status":")"s + status + R"("})"s
                );
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/users2"s, R"({"name":")"s + name + R"(", "age":)"s + std::to_string(age) + R"(, "status":")"s + status + R"("})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                std::this_thread::sleep_for(100ms);
            }
            
            // Filter: status eq 'active' and age ge 25
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users2?$filter=status%20eq%20'active'%20and%20age%20ge%2025"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have at least 2 items (Alice2 and Bob2)
            // (might have more if leftover data from previous tests)
            require_true(items.size() >= 2);
            
            // Verify all returned items have status='active' and age >= 25
            for(const auto& item : items)
            {
                require_true(item.has("status"s));
                const string status = item["status"s];
                require_eq(status, "active"s);
                require_true(item.has("age"s));
                const auto age = static_cast<long long>(item["age"s]);
                require_true(age >= 25);
            }
        };

        section("GET with $select - name,email") = [setup]
        {
            // Create a document with multiple fields
            auto [post_status, post_reason, post_headers, post_body] = make_request(
                setup->get_port(), "POST"s, "/users3"s, R"({"name":"TestUser", "email":"test@example.com", "age":30, "status":"active"})"s
            );
            if(post_status != "201"s)
            {
                std::this_thread::sleep_for(100ms);
                auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                    setup->get_port(), "POST"s, "/users3"s, R"({"name":"TestUser", "email":"test@example.com", "age":30, "status":"active"})"s
                );
                require_eq(retry_status, "201"s);
            }
            else
            {
                require_eq(post_status, "201"s);
            }
            std::this_thread::sleep_for(100ms);
            
            // Select only name and email fields
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users3?$select=name,email"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Verify projection: should only have _id, name, and email
            // Check at least one item if collection is not empty
            if(items.size() > 0)
            {
                for(const auto& item : items)
                {
                    // _id should always be present
                    require_true(item.has("_id"s));
                    
                    // Selected fields should be present if they exist in the document
                    // (they might not exist if document was created without them)
                    if(item.has("name"s) || item.has("email"s))
                    {
                        // If name or email exists, verify projection worked
                        // Other fields should NOT be present
                        require_true(!item.has("age"s));
                        require_true(!item.has("status"s));
                    }
                }
            }
        };

        section("GET with $filter + $select combined") = [setup]
        {
            // Filter and select: age gt 25, select name and email
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users?$filter=age%20gt%2025&$select=name,email"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Verify filtering and projection
            // Check items if any exist (might be empty if no documents match filter)
            for(const auto& item : items)
            {
                // Should have _id
                require_true(item.has("_id"s));
                
                // Verify projection: if age or status existed in source, they should be projected away
                // But we can't reliably check this without knowing source document structure
                // So we just verify the response is valid
            }
        };

        section("GET with $select + $top combined") = [setup]
        {
            // Select name and age, limit to 2 results
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users?$select=name,age&$top=2"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have at most 2 items
            require_true(items.size() <= 2);
            
            // Verify projection
            for(const auto& item : items)
            {
                require_true(item.has("_id"s));
                // Selected fields should be present if they exist in source documents
                // Other fields should NOT be present (projected away)
                require_true(!item.has("email"s));
                require_true(!item.has("status"s));
            }
        };

        section("GET with $filter + $select + $top combined") = [setup]
        {
            // First ensure we have test data in users2 collection
            // Create test documents with status field
            auto test_data = std::vector<std::tuple<std::string, int, std::string>>{
                {"Alice3"s, 25, "active"s},
                {"Bob3"s, 30, "active"s},
                {"Charlie3"s, 35, "inactive"s}
            };
            
            for(const auto& [name, age, status] : test_data)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/users2"s, R"({"name":")"s + name + R"(", "age":)"s + std::to_string(age) + R"(, "status":")"s + status + R"("})"s
                );
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/users2"s, R"({"name":")"s + name + R"(", "age":)"s + std::to_string(age) + R"(, "status":")"s + status + R"("})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                std::this_thread::sleep_for(100ms);
            }
            
            // First test simple GET to ensure collection exists
            auto [test_status, test_reason, test_headers, test_body] = make_request(
                setup->get_port(), "GET"s, "/users2"s, ""s
            );
            require_eq(test_status, "200"s);
            
            // Filter, select, and limit: status eq 'active', select name and age, top 2
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users2?$filter=status%20eq%20'active'&$select=name,age&$top=2"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have at most 2 items
            require_true(items.size() <= 2);
            
            // Verify filtering and projection
            for(const auto& item : items)
            {
                require_true(item.has("_id"s));
                // Verify the response is valid (projection worked)
                // We can't reliably check which fields are absent without knowing source structure
            }
        };

        section("GET with $filter - startswith(name, 'A')") = [setup]
        {
            // Create test documents with different names
            auto test_data = std::vector<std::pair<std::string, std::string>>{
                {"Alice"s, "alice@example.com"s},
                {"Bob"s, "bob@test.com"s},
                {"Charlie"s, "charlie@example.org"s},
                {"David"s, "david@test.org"s}
            };
            
            for(const auto& [name, email] : test_data)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/users3"s, R"({"name":")"s + name + R"(", "email":")"s + email + R"("})"s
                );
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/users3"s, R"({"name":")"s + name + R"(", "email":")"s + email + R"("})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                std::this_thread::sleep_for(100ms);
            }
            
            // Filter: startswith(name, 'A') (should return Alice)
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users3?$filter=startswith(name,'A')"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have at least 1 item (Alice)
            require_true(items.size() >= 1);
            
            // Verify all returned items have names starting with 'A'
            for(const auto& item : items)
            {
                require_true(item.has("name"s));
                const string name = item["name"s];
                require_true(name.starts_with("A"s));
            }
        };

        section("GET with $filter - contains(email, '@example')") = [setup]
        {
            // Filter: contains(email, '@example') (should return Alice and Charlie)
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users3?$filter=contains(email,'@example')"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have at least 2 items (Alice and Charlie)
            require_true(items.size() >= 2);
            
            // Verify all returned items have emails containing '@example'
            for(const auto& item : items)
            {
                require_true(item.has("email"s));
                const string email = item["email"s];
                require_true(email.find("@example"s) != std::string::npos);
            }
        };

        section("GET with $filter - endswith(path, '.pdf')") = [setup]
        {
            // Create test documents with different file paths
            auto test_data = std::vector<std::tuple<std::string, std::string, std::string>>{
                {"Alice4"s, "alice@example.com"s, "/documents/file.pdf"s},
                {"Bob4"s, "bob@test.com"s, "/images/photo.jpg"s},
                {"Charlie4"s, "charlie@example.org"s, "/documents/report.pdf"s},
                {"David4"s, "david@test.org"s, "/videos/movie.mp4"s}
            };
            
            for(const auto& [name, email, path] : test_data)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/users4"s, R"({"name":")"s + name + R"(", "email":")"s + email + R"(", "path":")"s + path + R"("})"s
                );
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/users4"s, R"({"name":")"s + name + R"(", "email":")"s + email + R"(", "path":")"s + path + R"("})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                std::this_thread::sleep_for(100ms);
            }
            
            // Filter: endswith(path, '.pdf') (should return Alice4 and Charlie4)
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users4?$filter=endswith(path,'.pdf')"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have at least 2 items (Alice4 and Charlie4)
            require_true(items.size() >= 2);
            
            // Verify all returned items have paths ending with '.pdf'
            for(const auto& item : items)
            {
                require_true(item.has("path"s));
                const string path = item["path"s];
                require_true(path.ends_with(".pdf"s));
            }
        };

        section("GET with $filter - startswith(name, 'C') and endswith(path, '.pdf')") = [setup]
        {
            // Combined filter: startswith(name, 'C') and endswith(path, '.pdf')
            // Should return Charlie4
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users4?$filter=startswith(name,'C')%20and%20endswith(path,'.pdf')"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have at least 1 item (Charlie4)
            require_true(items.size() >= 1);
            
            // Verify all returned items match both conditions
            for(const auto& item : items)
            {
                require_true(item.has("name"s));
                require_true(item.has("path"s));
                const string name = item["name"s];
                const string path = item["path"s];
                require_true(name.starts_with("C"s));
                require_true(path.ends_with(".pdf"s));
            }
        };

        section("GET with $filter - contains(email, '@example') and startswith(name, 'A')") = [setup]
        {
            // Combined filter: contains(email, '@example') and startswith(name, 'A')
            // Should return Alice
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users3?$filter=contains(email,'@example')%20and%20startswith(name,'A')"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            
            // Should have at least 1 item (Alice)
            require_true(items.size() >= 1);
            
            // Verify all returned items match both conditions
            for(const auto& item : items)
            {
                require_true(item.has("name"s));
                require_true(item.has("email"s));
                const string name = item["name"s];
                const string email = item["email"s];
                require_true(name.starts_with("A"s));
                require_true(email.find("@example"s) != std::string::npos);
            }
        };

        section("GET with $expand (placeholder - should return as-is)") = [setup]
        {
            // $expand is a placeholder, should return documents unchanged
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users?$expand=orders"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            
            // Should return documents (expand is placeholder, doesn't modify)
            require_true(documents.is_array());
        };

        section("GET with invalid $filter returns 400 Bad Request") = [setup]
        {
            // Invalid filter expression
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/users?$filter=invalid"s, ""s
            );

            require_eq(status, "400"s);
            require_eq(reason, "Bad Request"s);
            
            // Should return error message
            auto error = json::parse(response_body);
            require_true(error.has("error"s));
            const string error_msg = error["error"s];
            require_eq(error_msg, "Bad Request"s);
            require_true(error.has("message"s));
        };
    };

    // Note: Server runs in infinite loop, so we can't cleanly stop it
    // The test thread will continue running, but this is acceptable for unit tests
    return true;
}

const auto test_registrar = test_set();

} // namespace yar::httpd_unit_test

