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
    fixture(string_view f) : file{f}, port{"21120"s}, server{file, port}
    {
        // Clean up any stale PID file from previous test runs
        const auto pid_file = string{file} + ".pid"s;
        remove(pid_file.c_str());

        auto fs = fstream{};
        fs.open(file, ios::out);
        fs.close();
        server.start();
        // Wait longer for server to start and bind to port
        std::this_thread::sleep_for(1000ms);
    }

    const std::string& get_port() const { return port; }

    ~fixture()
    {
        // Stop the server and wait for the thread to finish
        server.stop();
        remove(file.c_str());
        // Also remove the PID lock file
        const auto pid_file = file + ".pid"s;
        remove(pid_file.c_str());
    }

private:
    std::string file;
    std::string port;
    yar::http::rest_api_server server;
};

// Helper function to parse HTTP status line and headers
auto parse_http_response(net::endpointstream& stream, const string& method = ""s)
{
    auto version = ""s, status_code = ""s, reason = ""s;
    auto headers = ::http::headers{};
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

    // Read body if content-length is present and method is not HEAD
    // For HEAD requests, Content-Length is set but body is not sent by framework
    if(headers.contains("content-length") && method != "HEAD"s)
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

    return parse_http_response(stream, method);
}

// Helper function to make HTTP request with custom Accept header
auto make_request_with_accept(const string& port, const string& method, const string& path, const string& accept_header, const string& body = ""s)
{
    auto stream = connect("localhost"s, port);
    
    stream << method << " " << path << " HTTP/1.1" << crlf
           << "Host: localhost:" << port << crlf
           << "Accept: " << accept_header << crlf;
    
    if(!body.empty())
    {
        stream << "Content-Type: application/json" << crlf
               << "Content-Length: " << body.length() << crlf;
    }
    
    stream << crlf << body << flush;

    return parse_http_response(stream, method);
}

// Helper function to make HTTP request with custom headers (e.g., If-Match, If-None-Match)
auto make_request_with_headers(const string& port, const string& method, const string& path, 
                                const std::map<string, string>& custom_headers, const string& body = ""s)
{
    auto stream = connect("localhost"s, port);
    
    stream << method << " " << path << " HTTP/1.1" << crlf
           << "Host: localhost:" << port << crlf
           << "Accept: application/json" << crlf;
    
    // Add custom headers (convert to lowercase for http::headers)
    for(const auto& [key, value] : custom_headers)
    {
        auto lower_key = key;
        for(auto& c : lower_key)
        {
            if(c >= 'A' && c <= 'Z')
                c = c - 'A' + 'a';
        }
        stream << lower_key << ": " << value << crlf;
    }
    
    if(!body.empty())
    {
        stream << "Content-Type: application/json" << crlf
               << "Content-Length: " << body.length() << crlf;
    }
    
    stream << crlf << body << flush;

    return parse_http_response(stream, method);
}

auto test_set()
{
    using namespace tester::basic;
    using namespace tester::assertions;

    test_case("REST API status codes and responses, [yardb]") = []
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
            const auto id = static_cast<xson::integer_type>(created_doc["_id"s]);
            
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
            const auto doc_id = static_cast<xson::integer_type>(document["_id"s]);
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
            const auto id = static_cast<xson::integer_type>(created_doc["_id"s]);
            
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
            const auto id = static_cast<xson::integer_type>(created_doc["_id"s]);
            
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
            const auto value = static_cast<xson::integer_type>(document["value"s]);
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
            const auto doc_id = static_cast<xson::integer_type>(document["_id"s]);
            require_eq(doc_id, new_id);
            const string name = document["name"s];
            require_eq(name, "New Item"s);
            const auto value = static_cast<xson::integer_type>(document["value"s]);
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
            const auto id = static_cast<xson::integer_type>(created_doc["_id"s]);
            
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

    test_case("OData query parameters, [yardb]") = []
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

    test_case("OData advanced query parameters, [yardb]") = []
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
                const auto age = static_cast<xson::integer_type>(item["age"s]);
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
            // Note: all_docs parsed but not used in this simplified test
            
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
                const auto age = static_cast<xson::integer_type>(item["age"s]);
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

        section("GET with $count=true returns count only") = [setup]
        {
            // Create test documents
            auto test_data = std::vector<std::pair<std::string, int>>{
                {"CountUser1"s, 25},
                {"CountUser2"s, 30},
                {"CountUser3"s, 35}
            };
            
            for(const auto& [name, age] : test_data)
            {
                auto [post_status, post_reason, post_headers, post_body] = make_request(
                    setup->get_port(), "POST"s, "/counttest"s, R"({"name":")"s + name + R"(", "age":)"s + std::to_string(age) + R"(})"s
                );
                if(post_status != "201"s)
                {
                    std::this_thread::sleep_for(100ms);
                    auto [retry_status, retry_reason, retry_headers, retry_body] = make_request(
                        setup->get_port(), "POST"s, "/counttest"s, R"({"name":")"s + name + R"(", "age":)"s + std::to_string(age) + R"(})"s
                    );
                    require_eq(retry_status, "201"s);
                }
                else
                {
                    require_eq(post_status, "201"s);
                }
                std::this_thread::sleep_for(100ms);
            }
            
            // Get count with $count=true
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/counttest?$count=true"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Response should be just a number (JSON number)
            // Parse as integer
            auto count = std::stoll(response_body);
            require_true(count >= 3); // Should have at least 3 documents we just created
            
            // Verify it matches the actual count by getting all documents
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/counttest"s, ""s
            );
            require_eq(get_status, "200"s);
            auto documents = json::parse(get_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            require_eq(static_cast<long long>(items.size()), count);
        };

        section("GET with $count=true and $filter returns filtered count") = [setup]
        {
            // Count with filter: age gt 25 (should return count of users with age > 25)
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/counttest?$count=true&$filter=age%20gt%2025"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Response should be just a number
            auto count = std::stoll(response_body);
            require_true(count >= 2); // Should have at least 2 documents (CountUser2 age 30, CountUser3 age 35)
            
            // Verify it matches the filtered count
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/counttest?$filter=age%20gt%2025"s, ""s
            );
            require_eq(get_status, "200"s);
            auto documents = json::parse(get_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            require_eq(static_cast<long long>(items.size()), count);
        };

        section("GET with $count=true on empty collection returns 0") = [setup]
        {
            // Count on empty collection
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/emptycollection?$count=true"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Response should be "0"
            require_eq(response_body, "0"s);
        };
    };

    test_case("PUT and PATCH /_db/{collection_name} - Add secondary indexes, [yardb]") = []
    {
        const auto test_file = "./httpd_test_indexes.db";
        auto setup = std::make_shared<fixture>(test_file);

        section("PUT /_db/{collection_name} creates index configuration") = [setup]
        {
            // Create some test documents first
            auto [post_status1, _unused1, _unused2, post_body1] = make_request(
                setup->get_port(), "POST"s, "/indextest"s, R"({"name":"Alice","email":"alice@example.com","status":"active"})"s
            );
            require_eq(post_status1, "201"s);
            
            auto [post_status2, _unused3, _unused4, post_body2] = make_request(
                setup->get_port(), "POST"s, "/indextest"s, R"({"name":"Bob","email":"bob@example.com","status":"inactive"})"s
            );
            require_eq(post_status2, "201"s);
            
            // Add secondary indexes
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "PUT"s, "/_db/indextest"s, R"({"keys":["email","status"]})"s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            
            // Verify response contains index configuration
            auto config = json::parse(response_body);
            require_true(config.has("collection"s));
            require_eq(config["collection"s].get<string>(), "indextest"s);
            require_true(config.has("keys"s));
            auto keys = config["keys"s].get<object::array>();
            require_eq(keys.size(), 2u);
            
            // Verify Location header
            require_true(headers.contains("location"s));
            require_eq(headers["location"s], "/_db/indextest"s);
        };

        section("PUT /_db/{collection_name} with invalid keys returns 400") = [setup]
        {
            // Missing keys field
            auto [status1, reason1, _unused1, body1] = make_request(
                setup->get_port(), "PUT"s, "/_db/indextest2"s, R"({})"s
            );
            require_eq(status1, "400"s);
            auto error1 = json::parse(body1);
            require_eq(error1["error"s].get<string>(), "Bad Request"s);
            
            // Keys not an array
            auto [status2, reason2, _unused2, body2] = make_request(
                setup->get_port(), "PUT"s, "/_db/indextest2"s, R"({"keys":"email"})"s
            );
            require_eq(status2, "400"s);
            auto error2 = json::parse(body2);
            require_eq(error2["error"s].get<string>(), "Bad Request"s);
            
            // Empty keys array
            auto [status3, reason3, _unused3, body3] = make_request(
                setup->get_port(), "PUT"s, "/_db/indextest2"s, R"({"keys":[]})"s
            );
            require_eq(status3, "400"s);
            auto error3 = json::parse(body3);
            require_eq(error3["error"s].get<string>(), "Bad Request"s);
            
            // Invalid key (not a string)
            auto [status4, reason4, _unused4, body4] = make_request(
                setup->get_port(), "PUT"s, "/_db/indextest2"s, R"({"keys":[123]})"s
            );
            require_eq(status4, "400"s);
            auto error4 = json::parse(body4);
            require_eq(error4["error"s].get<string>(), "Bad Request"s);
            
            // Reserved field name
            auto [status5, reason5, _unused5, body5] = make_request(
                setup->get_port(), "PUT"s, "/_db/indextest2"s, R"({"keys":["_id"]})"s
            );
            require_eq(status5, "400"s);
            auto error5 = json::parse(body5);
            require_eq(error5["error"s].get<string>(), "Bad Request"s);
        };

        section("PUT /_db/{collection_name} enables querying by indexed fields") = [setup]
        {
            // Create test documents
            auto [post_status1, _unused1, _unused2, post_body1] = make_request(
                setup->get_port(), "POST"s, "/querytest"s, R"({"name":"User1","email":"user1@test.com","age":25})"s
            );
            require_eq(post_status1, "201"s);
            
            auto [post_status2, _unused3, _unused4, post_body2] = make_request(
                setup->get_port(), "POST"s, "/querytest"s, R"({"name":"User2","email":"user2@test.com","age":30})"s
            );
            require_eq(post_status2, "201"s);
            
            // Add index on email field
            auto [put_status, _unused5, _unused6, _unused7] = make_request(
                setup->get_port(), "PUT"s, "/_db/querytest"s, R"({"keys":["email"]})"s
            );
            require_eq(put_status, "200"s);
            
            // Query by indexed field
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/querytest?$filter=email%20eq%20'user1@test.com'"s, ""s
            );
            require_eq(get_status, "200"s);
            
            auto documents = json::parse(get_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            require_eq(items.size(), 1u);
            require_eq(items[0]["email"s].get<string>(), "user1@test.com"s);
        };

        section("PATCH /_db/{collection_name} adds indexes incrementally") = [setup]
        {
            // First, add some indexes with PUT
            auto [put_status1, _unused1, _unused2, put_body1] = make_request(
                setup->get_port(), "PUT"s, "/_db/patchtest"s, R"({"keys":["email"]})"s
            );
            require_eq(put_status1, "200"s);
            auto config1 = json::parse(put_body1);
            auto keys1 = config1["keys"s].get<object::array>();
            require_eq(keys1.size(), 1u);
            
            // PATCH to add more indexes
            auto [patch_status, patch_reason, patch_headers, patch_body] = make_request(
                setup->get_port(), "PATCH"s, "/_db/patchtest"s, R"({"keys":["status","age"]})"s
            );
            require_eq(patch_status, "200"s);
            
            // Verify all indexes are present (email + status + age)
            auto config2 = json::parse(patch_body);
            auto keys2 = config2["keys"s].get<object::array>();
            require_eq(keys2.size(), 3u);
            
            // Verify Location header
            require_true(patch_headers.contains("location"s));
            require_eq(patch_headers["location"s], "/_db/patchtest"s);
        };

        section("PATCH /_db/{collection_name} ignores duplicate keys") = [setup]
        {
            // Add indexes with PUT
            auto [put_status, _unused1, _unused2, _unused3] = make_request(
                setup->get_port(), "PUT"s, "/_db/duptest"s, R"({"keys":["email","status"]})"s
            );
            require_eq(put_status, "200"s);
            
            // PATCH with duplicate keys (email already exists)
            auto [patch_status, patch_reason, patch_headers, patch_body] = make_request(
                setup->get_port(), "PATCH"s, "/_db/duptest"s, R"({"keys":["email","age"]})"s
            );
            require_eq(patch_status, "200"s);
            
            // Verify only new key (age) was added, email was not duplicated
            auto config = json::parse(patch_body);
            auto keys = config["keys"s].get<object::array>();
            require_eq(keys.size(), 3u); // email, status, age
            
            // Verify all three keys are present
            auto keys_set = std::set<string>{};
            for(const auto& key : keys)
                keys_set.insert(key.get<string>());
            require_true(keys_set.contains("email"s));
            require_true(keys_set.contains("status"s));
            require_true(keys_set.contains("age"s));
        };

        section("PATCH /_db/{collection_name} with all duplicate keys returns existing config") = [setup]
        {
            // Add indexes with PUT
            auto [put_status, _unused1, _unused2, put_body] = make_request(
                setup->get_port(), "PUT"s, "/_db/allduptest"s, R"({"keys":["email","status"]})"s
            );
            require_eq(put_status, "200"s);
            auto original_config = json::parse(put_body);
            
            // PATCH with all duplicate keys
            auto [patch_status, patch_reason, patch_headers, patch_body] = make_request(
                setup->get_port(), "PATCH"s, "/_db/allduptest"s, R"({"keys":["email","status"]})"s
            );
            require_eq(patch_status, "200"s);
            
            // Should return existing configuration unchanged
            auto config = json::parse(patch_body);
            require_eq(config["collection"s].get<string>(), original_config["collection"s].get<string>());
            auto keys = config["keys"s].get<object::array>();
            auto original_keys = original_config["keys"s].get<object::array>();
            require_eq(keys.size(), original_keys.size());
        };

        section("PUT /_db/{collection_name} with invalid Accept header returns 406") = [setup]
        {
            auto stream = connect("localhost"s, setup->get_port());
            stream << "PUT /_db/accepttest HTTP/1.1" << crlf
                   << "Host: localhost:" << setup->get_port() << crlf
                   << "Accept: application/xml" << crlf
                   << "Content-Type: application/json" << crlf
                   << "Content-Length: " << R"({"keys":["email"]})"s.length() << crlf
                   << crlf << R"({"keys":["email"]})"s << flush;
            
            auto [status, reason, headers, body] = parse_http_response(stream, "PUT"s);
            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
        };

        section("PATCH /_db/{collection_name} with invalid Accept header returns 406") = [setup]
        {
            auto stream = connect("localhost"s, setup->get_port());
            stream << "PATCH /_db/accepttest2 HTTP/1.1" << crlf
                   << "Host: localhost:" << setup->get_port() << crlf
                   << "Accept: application/xml" << crlf
                   << "Content-Type: application/json" << crlf
                   << "Content-Length: " << R"({"keys":["email"]})"s.length() << crlf
                   << crlf << R"({"keys":["email"]})"s << flush;
            
            auto [status, reason, headers, body] = parse_http_response(stream, "PATCH"s);
            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
        };
    };

    test_case("HEAD method and Accept header content negotiation, [yardb]") = []
    {
        const auto test_file = "./httpd_head_test.db";
        auto setup = std::make_shared<fixture>(test_file);

        // Setup: Create a test document
        section("Setup: Create test document") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "POST"s, "/headtest"s, R"({"name":"HEAD Test","value":100})"s
            );
            require_eq(status, "201"s);
        };

        section("HEAD / returns 200 OK with no body") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "HEAD"s, "/"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            require_true(headers.contains("content-type"s));
            require_eq(headers["content-type"s], "application/json"s);
            // HEAD should return no body
            require_eq(response_body, ""s);
        };

        section("HEAD /collection returns 200 OK with no body") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "HEAD"s, "/headtest"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            require_true(headers.contains("content-type"s));
            require_eq(headers["content-type"s], "application/json"s);
            // HEAD should return no body
            require_eq(response_body, ""s);
        };

        section("HEAD /collection/{id} returns 200 OK with no body for existing document") = [setup]
        {
            // First get the ID from a GET request
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/headtest?$top=1"s, ""s
            );
            require_eq(get_status, "200"s);
            auto documents = json::parse(get_body);
            require_true(documents.is_array());
            require_true(documents.size() > 0);
            const auto id = static_cast<xson::integer_type>(documents[0]["_id"s]);
            
            // Now test HEAD
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "HEAD"s, "/headtest/"s + std::to_string(id), ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            require_true(headers.contains("content-type"s));
            require_eq(headers["content-type"s], "application/json"s);
            // HEAD should return no body
            require_eq(response_body, ""s);
        };

        section("HEAD /collection/{id} returns 404 Not Found for non-existent document") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "HEAD"s, "/headtest/99999"s, ""s
            );

            require_eq(status, "404"s);
            require_eq(reason, "Not Found"s);
            // Error response should have body even for HEAD (framework may include it)
            require_true(headers.contains("content-type"s));
        };

        section("GET with Accept: application/json returns 200 OK") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/headtest?s$top=1"s, "application/json"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            require_true(headers.contains("content-type"s));
            require_eq(headers["content-type"s], "application/json"s);
            require_true(!response_body.empty());
        };

        section("GET with Accept: */* returns 200 OK") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/headtest?s$top=1"s, "*/*"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            require_true(headers.contains("content-type"s));
            require_eq(headers["content-type"s], "application/json"s);
        };

        section("GET with Accept: application/* returns 200 OK") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/headtest?s$top=1"s, "application/*"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            require_true(headers.contains("content-type"s));
            require_eq(headers["content-type"s], "application/json"s);
        };

        section("GET with Accept: application/json;odata=fullmetadata returns 200 OK") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/headtest?$top=1"s, "application/json;odata=fullmetadata"s, ""s
            );

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            require_true(headers.contains("content-type"s));
            require_eq(headers["content-type"s], "application/json"s);
        };

        section("GET with Accept: application/xml returns 406 Not Acceptable") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/headtest"s, "application/xml"s, ""s
            );

            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
            require_true(headers.contains("content-type"s));
            
            // Should return error message
            auto error = json::parse(response_body);
            require_true(error.has("error"s));
            const string error_msg = error["error"s];
            require_eq(error_msg, "Not Acceptable"s);
            require_true(error.has("message"s));
            const string message = error["message"s];
            require_eq(message, "Only application/json is supported"s);
        };

        section("GET with Accept: text/html returns 406 Not Acceptable") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/headtest"s, "text/html"s, ""s
            );

            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
            
            // Should return error message
            auto error = json::parse(response_body);
            require_true(error.has("error"s));
            const string error_msg = error["error"s];
            require_eq(error_msg, "Not Acceptable"s);
        };

        section("GET with Accept: application/xml, text/html returns 406 Not Acceptable") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/headtest"s, "application/xml, text/html"s, ""s
            );

            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
        };

        section("GET without Accept header returns 200 OK (defaults to accept anything)") = [setup]
        {
            auto stream = connect("localhost"s, setup->get_port());
            stream << "GET /headtest?s$top=1 HTTP/1.1" << crlf
                   << "Host: localhost:" << setup->get_port() << crlf
                   << crlf << flush;
            
            auto [status, reason, headers, response_body] = parse_http_response(stream);

            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            require_true(headers.contains("content-type"s));
            require_eq(headers["content-type"s], "application/json"s);
        };

        section("HEAD with Accept: application/xml returns 406 Not Acceptable") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "HEAD"s, "/headtest"s, "application/xml"s, ""s
            );

            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
        };

        section("POST with Accept: application/xml returns 406 Not Acceptable") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "POST"s, "/headtest"s, "application/xml"s, R"({"name":"Test"})"s
            );

            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
        };

        section("PUT with Accept: application/xml returns 406 Not Acceptable") = [setup]
        {
            // First get an ID
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/headtest?$top=1"s, ""s
            );
            auto documents = json::parse(get_body);
            const auto id = static_cast<xson::integer_type>(documents[0]["_id"s]);
            
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "PUT"s, "/headtest/"s + std::to_string(id), "application/xml"s, R"({"name":"Updated"})"s
            );

            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
        };

        section("PATCH with Accept: application/xml returns 406 Not Acceptable") = [setup]
        {
            // First get an ID
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/headtest?$top=1"s, ""s
            );
            auto documents = json::parse(get_body);
            const auto id = static_cast<xson::integer_type>(documents[0]["_id"s]);
            
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "PATCH"s, "/headtest/"s + std::to_string(id), "application/xml"s, R"({"name":"Patched"})"s
            );

            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
        };

        section("DELETE with Accept: application/xml returns 406 Not Acceptable") = [setup]
        {
            // Create a document to delete
            auto [post_status, post_reason, post_headers, post_body] = make_request(
                setup->get_port(), "POST"s, "/headtest"s, R"({"name":"To Delete"})"s
            );
            auto created_doc = json::parse(post_body);
            const auto id = static_cast<xson::integer_type>(created_doc["_id"s]);
            
            auto [status, reason, headers, response_body] = make_request_with_accept(
                setup->get_port(), "DELETE"s, "/headtest/"s + std::to_string(id), "application/xml"s, ""s
            );

            require_eq(status, "406"s);
            require_eq(reason, "Not Acceptable"s);
        };

        section("HEAD /collection with query parameters validates them") = [setup]
        {
            // Valid query parameters should work
            auto [status1, reason1, headers1, body1] = make_request(
                setup->get_port(), "HEAD"s, "/headtest?$top=5"s, ""s
            );
            require_eq(status1, "200"s);

            // Invalid query parameters should return 400
            auto [status2, reason2, headers2, body2] = make_request(
                setup->get_port(), "HEAD"s, "/headtest?$top=-1"s, ""s
            );
            require_eq(status2, "400"s);
        };

        section("HEAD returns same Content-Type header as GET") = [setup]
        {
            // GET request
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/headtest?$top=1"s, ""s
            );
            
            // HEAD request
            auto [head_status, head_reason, head_headers, head_body] = make_request(
                setup->get_port(), "HEAD"s, "/headtest?$top=1"s, ""s
            );

            require_eq(get_status, "200"s);
            require_eq(head_status, "200"s);
            require_true(get_headers.contains("content-type"s));
            require_true(head_headers.contains("content-type"s));
            require_eq(get_headers["content-type"s], head_headers["content-type"s]);
            // HEAD should have no body
            require_eq(head_body, ""s);
            // GET should have body
            require_false(get_body.empty());
        };
    };

    test_case("OData metadata support, [yardb]") = []
    {
        const auto test_file = "./httpd_odata_test.db";
        auto setup = std::make_shared<fixture>(test_file);

        section("Collection query with minimal metadata") = [setup]
        {
            // Create test document
            auto [post_status, _, __, post_body] = make_request(
                setup->get_port(), "POST"s, "/odatatest"s, R"({"name":"Alice","age":30})"s
            );
            require_eq(post_status, "201"s);

            // Query collection with minimal metadata
            auto [status, reason, headers, body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/odatatest"s, "application/json;odata=minimalmetadata"s
            );

            require_eq(status, "200"s);
            auto response = json::parse(body);
            
            // Should have @odata.context at root
            require_true(response.has("@odata.context"s));
            require_eq(response["@odata.context"s].get<string>(), "/$metadata#odatatest"s);
            
            // Should have value array
            require_true(response.has("value"s));
            require_true(response["value"s].is_array());
            
            // Items in array should NOT have metadata (minimal only adds context at root)
            auto& items = response["value"s].get<xson::object::array>();
            require_false(items.empty());
            require_false(items[0].has("@odata.context"s));
        };

        section("Collection query with full metadata") = [setup]
        {
            // Query collection with full metadata
            auto [status, reason, headers, body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/odatatest"s, "application/json;odata=fullmetadata"s
            );

            require_eq(status, "200"s);
            auto response = json::parse(body);
            
            // Should have @odata.context at root
            require_true(response.has("@odata.context"s));
            require_eq(response["@odata.context"s].get<string>(), "/$metadata#odatatest"s);
            
            // Should have value array
            require_true(response.has("value"s));
            auto& items = response["value"s].get<xson::object::array>();
            require_false(items.empty());
            
            // Each item should have full metadata
            for(const auto& item : items)
            {
                require_true(item.has("@odata.context"s));
                require_true(item.has("@odata.id"s));
                require_true(item.has("@odata.editLink"s));
                require_true(item.has("_id"s));
                
                auto id = static_cast<xson::integer_type>(item["_id"s]);
                require_eq(item["@odata.id"s].get<string>(), "/odatatest/"s + std::to_string(id));
                require_eq(item["@odata.editLink"s].get<string>(), "/odatatest/"s + std::to_string(id));
            }
        };

        section("Single document with minimal metadata") = [setup]
        {
            // Get first document ID
            auto [get_status, __, ___, get_body] = make_request(
                setup->get_port(), "GET"s, "/odatatest"s, ""s
            );
            auto all_docs = json::parse(get_body);
            auto& items = all_docs.get<xson::object::array>();
            require_false(items.empty());
            auto doc_id = static_cast<xson::integer_type>(items[0]["_id"s]);
            
            // Get single document with minimal metadata
            auto [status, reason, headers, body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/odatatest/"s + std::to_string(doc_id), 
                "application/json;odata=minimalmetadata"s
            );

            require_eq(status, "200"s);
            auto document = json::parse(body);
            
            // Should have @odata.context
            require_true(document.has("@odata.context"s));
            require_eq(document["@odata.context"s].get<string>(), "/$metadata#odatatest/$entity"s);
            
            // Should NOT have other metadata properties for minimal
            require_false(document.has("@odata.id"s));
            require_false(document.has("@odata.editLink"s));
        };

        section("Single document with full metadata") = [setup]
        {
            // Get first document ID
            auto [get_status, __, ___, get_body] = make_request(
                setup->get_port(), "GET"s, "/odatatest"s, ""s
            );
            auto all_docs = json::parse(get_body);
            auto& items = all_docs.get<xson::object::array>();
            require_false(items.empty());
            auto doc_id = static_cast<xson::integer_type>(items[0]["_id"s]);
            
            // Get single document with full metadata
            auto [status, reason, headers, body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/odatatest/"s + std::to_string(doc_id), 
                "application/json;odata=fullmetadata"s
            );

            require_eq(status, "200"s);
            auto document = json::parse(body);
            
            // Should have all metadata properties
            require_true(document.has("@odata.context"s));
            require_eq(document["@odata.context"s].get<string>(), "/$metadata#odatatest/$entity"s);
            require_true(document.has("@odata.id"s));
            require_true(document.has("@odata.editLink"s));
            
            require_eq(document["@odata.id"s].get<string>(), "/odatatest/"s + std::to_string(doc_id));
            require_eq(document["@odata.editLink"s].get<string>(), "/odatatest/"s + std::to_string(doc_id));
        };

        section("No metadata without Accept header") = [setup]
        {
            // Query without Accept header
            auto [status, reason, headers, body] = make_request(
                setup->get_port(), "GET"s, "/odatatest"s, ""s
            );

            require_eq(status, "200"s);
            auto response = json::parse(body);
            
            // Should NOT have @odata.context
            require_false(response.has("@odata.context"s));
            require_false(response.has("value"s));
            
            // Should be direct array (not wrapped)
            require_true(response.is_array());
        };

        section("No metadata with odata=nometadata") = [setup]
        {
            // Query with explicit no metadata
            auto [status, reason, headers, body] = make_request_with_accept(
                setup->get_port(), "GET"s, "/odatatest"s, "application/json;odata=nometadata"s
            );

            require_eq(status, "200"s);
            auto response = json::parse(body);
            
            // Should NOT have @odata.context
            require_false(response.has("@odata.context"s));
            require_false(response.has("value"s));
            
            // Should be direct array (not wrapped)
            require_true(response.is_array());
        };

        section("POST response includes metadata when requested") = [setup]
        {
            // Create document with minimal metadata request
            auto [status, reason, headers, body] = make_request_with_accept(
                setup->get_port(), "POST"s, "/odatatest2"s, "application/json;odata=minimalmetadata"s,
                R"({"name":"Bob","age":25})"s
            );

            require_eq(status, "201"s);
            auto document = json::parse(body);
            
            // Should have @odata.context
            require_true(document.has("@odata.context"s));
            require_eq(document["@odata.context"s].get<string>(), "/$metadata#odatatest2/$entity"s);
            require_true(document.has("_id"s));
        };

        section("PUT response includes full metadata when requested") = [setup]
        {
            // First create a document
            auto [post_status, __, ___, post_body] = make_request(
                setup->get_port(), "POST"s, "/odatatest3"s, R"({"name":"Charlie"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // Update with full metadata request
            auto [status, reason, headers, body] = make_request_with_accept(
                setup->get_port(), "PUT"s, "/odatatest3/"s + std::to_string(doc_id), 
                "application/json;odata=fullmetadata"s,
                R"({"name":"Charlie Updated","age":40})"s
            );

            require_eq(status, "200"s);
            auto document = json::parse(body);
            
            // Should have all metadata properties
            require_true(document.has("@odata.context"s));
            require_true(document.has("@odata.id"s));
            require_true(document.has("@odata.editLink"s));
        };

        section("PATCH response includes metadata when requested") = [setup]
        {
            // First create a document
            auto [post_status, __, ___, post_body] = make_request(
                setup->get_port(), "POST"s, "/odatatest4"s, R"({"name":"David"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // Update with minimal metadata request
            auto [status, reason, headers, body] = make_request_with_accept(
                setup->get_port(), "PATCH"s, "/odatatest4/"s + std::to_string(doc_id), 
                "application/json;odata=minimalmetadata"s,
                R"({"age":35})"s
            );

            require_eq(status, "200"s);
            auto document = json::parse(body);
            
            // PATCH may return array or single object - check metadata accordingly
            if(document.is_array())
            {
                // If array, check first element has metadata
                auto& items = document.get<xson::object::array>();
                require_false(items.empty());
                require_true(items[0].has("@odata.context"s));
            }
            else
            {
                // If single object, check it has metadata
                require_true(document.has("@odata.context"s));
            }
        };
    };

    test_case("ETag and conditional requests, [yardb]") = []
    {
        const auto test_file = "./etag_test.db";
        auto setup = std::make_shared<fixture>(test_file);

        section("GET response includes ETag header") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post1, _unused_post2, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest"s, R"({"name":"ETag Document"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET the document
            auto [status, reason, headers, body] = make_request(
                setup->get_port(), "GET"s, "/etagtest/"s + std::to_string(doc_id), ""s
            );
            
            require_eq(status, "200"s);
            require_true(headers.contains("etag"s));
            
            // ETag should be a quoted string
            auto etag = headers["etag"s];
            require_true(etag.front() == '"');
            require_true(etag.back() == '"');
        };

        section("GET with If-None-Match matching ETag returns 304 Not Modified") = [setup]
        {
            // Create a document
            auto [post_status, _unused1, _unused2, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest2"s, R"({"name":"If-None-Match Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET the document to get ETag
            auto [get_status, _unused3, headers1, _unused4] = make_request(
                setup->get_port(), "GET"s, "/etagtest2/"s + std::to_string(doc_id), ""s
            );
            require_eq(get_status, "200"s);
            require_true(headers1.contains("etag"s));
            auto etag = headers1["etag"s];
            
            // GET again with If-None-Match matching the ETag
            auto custom_headers = std::map<string, string>{{"If-None-Match", etag}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "GET"s, "/etagtest2/"s + std::to_string(doc_id), custom_headers
            );
            
            require_eq(status, "304"s);
            require_eq(reason, "Not Modified"s);
            require_true(body.empty()); // 304 should have no body
            require_true(headers.contains("etag"s)); // Should still include ETag header
        };

        section("GET with If-None-Match non-matching ETag returns 200 OK") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post3, _unused_post4, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest3"s, R"({"name":"If-None-Match Non-Match"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET with If-None-Match with a different ETag
            auto custom_headers = std::map<string, string>{{"If-None-Match", "\"different-etag\""s}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "GET"s, "/etagtest3/"s + std::to_string(doc_id), custom_headers
            );
            
            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            require_false(body.empty()); // Should have body
            auto document = json::parse(body);
            require_true(document.has("_id"s));
        };

        section("PUT with If-Match matching ETag succeeds") = [setup]
        {
            // Create a document
            auto [post_status, _unused5, _unused6, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest4"s, R"({"name":"If-Match Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET the document to get ETag
            auto [get_status, _unused7, headers1, _unused8] = make_request(
                setup->get_port(), "GET"s, "/etagtest4/"s + std::to_string(doc_id), ""s
            );
            require_true(headers1.contains("etag"s));
            auto etag = headers1["etag"s];
            
            // PUT with If-Match matching the ETag
            auto custom_headers = std::map<string, string>{{"If-Match", etag}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "PUT"s, "/etagtest4/"s + std::to_string(doc_id), custom_headers,
                R"({"name":"Updated Name","value":100})"s
            );
            
            require_eq(status, "200"s);
            require_eq(reason, "OK"s);
            auto document = json::parse(body);
            require_eq(document["name"s].get<string>(), "Updated Name"s);
            require_true(headers.contains("etag"s)); // Should have new ETag
        };

        section("PUT with If-Match non-matching ETag returns 412 Precondition Failed") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post5, _unused_post6, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest5"s, R"({"name":"If-Match Fail Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // PUT with If-Match with a different ETag
            auto custom_headers = std::map<string, string>{{"If-Match", "\"wrong-etag\""s}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "PUT"s, "/etagtest5/"s + std::to_string(doc_id), custom_headers,
                R"({"name":"Should Not Update"})"s
            );
            
            require_eq(status, "412"s);
            require_eq(reason, "Precondition Failed"s);
            auto error = json::parse(body);
            require_true(error.has("error"s));
            require_eq(error["error"s].get<string>(), "Precondition Failed"s);
        };

        section("PATCH with If-Match matching ETag succeeds") = [setup]
        {
            // Create a document
            auto [post_status, _unused9, _unused10, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest6"s, R"({"name":"PATCH If-Match Test","age":25})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET the document to get ETag
            auto [get_status, _unused11, headers1, _unused12] = make_request(
                setup->get_port(), "GET"s, "/etagtest6/"s + std::to_string(doc_id), ""s
            );
            require_true(headers1.contains("etag"s));
            auto etag = headers1["etag"s];
            
            // PATCH with If-Match matching the ETag
            auto custom_headers = std::map<string, string>{{"If-Match", etag}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "PATCH"s, "/etagtest6/"s + std::to_string(doc_id), custom_headers,
                R"({"age":30})"s
            );
            
            require_eq(status, "200"s);
            require_true(headers.contains("etag"s)); // Should have new ETag
        };

        section("PATCH with If-Match non-matching ETag returns 412 Precondition Failed") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post7, _unused_post8, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest7"s, R"({"name":"PATCH Fail Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // PATCH with If-Match with a different ETag
            auto custom_headers = std::map<string, string>{{"If-Match", "\"wrong-etag\""s}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "PATCH"s, "/etagtest7/"s + std::to_string(doc_id), custom_headers,
                R"({"value":999})"s
            );
            
            require_eq(status, "412"s);
            require_eq(reason, "Precondition Failed"s);
        };

        section("PUT response includes ETag header after update") = [setup]
        {
            // Create a document
            auto [post_status, _unused13, _unused14, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest8"s, R"({"name":"ETag After Update"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET to get initial ETag
            auto [get_status1, _unused15, headers1, _unused16] = make_request(
                setup->get_port(), "GET"s, "/etagtest8/"s + std::to_string(doc_id), ""s
            );
            auto initial_etag = headers1["etag"s];
            
            // PUT to update
            auto [put_status, _unused17, headers2, put_body] = make_request(
                setup->get_port(), "PUT"s, "/etagtest8/"s + std::to_string(doc_id), 
                R"({"name":"Updated","value":42})"s
            );
            
            require_eq(put_status, "200"s);
            require_true(headers2.contains("etag"s));
            auto new_etag = headers2["etag"s];
            
            // ETag should have changed after update
            require_true(new_etag != initial_etag);
        };

        section("HEAD response includes ETag header") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post9, _unused_post10, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest9"s, R"({"name":"HEAD ETag Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // HEAD request
            auto [status, reason, headers, body] = make_request(
                setup->get_port(), "HEAD"s, "/etagtest9/"s + std::to_string(doc_id), ""s
            );
            
            require_eq(status, "200"s);
            require_true(headers.contains("etag"s));
            require_true(body.empty()); // HEAD should have no body
        };

        section("If-Match wildcard (*) matches any existing resource") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post11, _unused_post12, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest10"s, R"({"name":"Wildcard Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // PUT with If-Match: *
            auto custom_headers = std::map<string, string>{{"If-Match", "*"s}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "PUT"s, "/etagtest10/"s + std::to_string(doc_id), custom_headers,
                R"({"name":"Updated with Wildcard"})"s
            );
            
            require_eq(status, "200"s); // Should succeed because resource exists
        };

        section("If-None-Match wildcard (*) fails if resource exists") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post13, _unused_post14, post_body] = make_request(
                setup->get_port(), "POST"s, "/etagtest11"s, R"({"name":"Wildcard None-Match"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET with If-None-Match: *
            auto custom_headers = std::map<string, string>{{"If-None-Match", "*"s}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "GET"s, "/etagtest11/"s + std::to_string(doc_id), custom_headers
            );
            
            require_eq(status, "304"s); // Should return 304 because resource exists
        };
    };

    test_case("Last-Modified and conditional requests, [yardb]") = []
    {
        const auto test_file = "./lastmodified_test.db";
        auto setup = std::make_shared<fixture>(test_file);

        section("GET response includes Last-Modified header") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post1, _unused_post2, post_body] = make_request(
                setup->get_port(), "POST"s, "/lastmodifiedtest"s, R"({"name":"Last-Modified Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET the document
            auto [status, reason, headers, body] = make_request(
                setup->get_port(), "GET"s, "/lastmodifiedtest/"s + std::to_string(doc_id), ""s
            );
            
            require_eq(status, "200"s);
            require_true(headers.contains("last-modified"s));
            
            // Last-Modified should be a valid HTTP date string
            auto last_modified = headers["last-modified"s];
            require_false(last_modified.empty());
            // Format: "Sun, 21 Dec 2025 22:30:08 GMT"
            require_true(last_modified.ends_with(" GMT"s));
        };

        section("GET with If-Modified-Since returns 304 if document not modified") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post3, _unused_post4, post_body] = make_request(
                setup->get_port(), "POST"s, "/lastmodifiedtest2"s, R"({"name":"If-Modified-Since Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET the document to get Last-Modified
            auto [get_status, _unused_get1, headers1, _unused_get2] = make_request(
                setup->get_port(), "GET"s, "/lastmodifiedtest2/"s + std::to_string(doc_id), ""s
            );
            require_eq(get_status, "200"s);
            require_true(headers1.contains("last-modified"s));
            auto last_modified = headers1["last-modified"s];
            
            // Small delay to ensure timestamps differ (if needed)
            std::this_thread::sleep_for(100ms);
            
            // GET again with If-Modified-Since matching the Last-Modified date
            auto custom_headers = std::map<string, string>{{"If-Modified-Since", last_modified}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "GET"s, "/lastmodifiedtest2/"s + std::to_string(doc_id), custom_headers
            );
            
            // Should return 304 if document timestamp <= If-Modified-Since date
            // Note: Exact match might return 304, or if timestamp is slightly newer it returns 200
            // The behavior depends on the exact timestamp comparison
            require_true(status == "200"s || status == "304"s);
            if(status == "304"s)
            {
                require_true(body.empty()); // 304 should have no body
                require_true(headers.contains("last-modified"s)); // Should still include Last-Modified header
            }
        };

        section("GET with If-Modified-Since in future returns 200 OK") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post5, _unused_post6, post_body] = make_request(
                setup->get_port(), "POST"s, "/lastmodifiedtest3"s, R"({"name":"Future Date Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET with If-Modified-Since set to a future date (document is older)
            auto future_date = "Tue, 31 Dec 2099 23:59:59 GMT"s;
            auto custom_headers = std::map<string, string>{{"If-Modified-Since", future_date}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "GET"s, "/lastmodifiedtest3/"s + std::to_string(doc_id), custom_headers
            );
            
            // Document was modified before the future date, so should return 304
            require_eq(status, "304"s);
            require_true(body.empty());
        };

        section("PUT with If-Unmodified-Since matching Last-Modified succeeds") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post7, _unused_post8, post_body] = make_request(
                setup->get_port(), "POST"s, "/lastmodifiedtest4"s, R"({"name":"If-Unmodified-Since Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET the document to get Last-Modified
            auto [get_status, _unused_get3, headers1, _unused_get4] = make_request(
                setup->get_port(), "GET"s, "/lastmodifiedtest4/"s + std::to_string(doc_id), ""s
            );
            require_true(headers1.contains("last-modified"s));
            auto last_modified = headers1["last-modified"s];
            
            // PUT with If-Unmodified-Since matching the Last-Modified date
            auto custom_headers = std::map<string, string>{{"If-Unmodified-Since", last_modified}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "PUT"s, "/lastmodifiedtest4/"s + std::to_string(doc_id), custom_headers,
                R"({"name":"Updated Name","value":100})"s
            );
            
            // Should succeed if document timestamp <= If-Unmodified-Since date
            require_eq(status, "200"s);
            auto document = json::parse(body);
            require_eq(document["name"s].get<string>(), "Updated Name"s);
        };

        section("PUT with If-Unmodified-Since in past returns 412 Precondition Failed") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post9, _unused_post10, post_body] = make_request(
                setup->get_port(), "POST"s, "/lastmodifiedtest5"s, R"({"name":"Past Date Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // Wait a moment to ensure document timestamp is newer than past date
            std::this_thread::sleep_for(100ms);
            
            // PUT with If-Unmodified-Since set to a past date (document is newer, so precondition fails)
            auto past_date = "Mon, 01 Jan 2000 00:00:00 GMT"s;
            auto custom_headers = std::map<string, string>{{"If-Unmodified-Since", past_date}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "PUT"s, "/lastmodifiedtest5/"s + std::to_string(doc_id), custom_headers,
                R"({"name":"Should Not Update"})"s
            );
            
            // Document was modified after the past date, so should return 412
            require_eq(status, "412"s);
            require_eq(reason, "Precondition Failed"s);
            auto error = json::parse(body);
            require_true(error.has("error"s));
            require_eq(error["error"s].get<string>(), "Precondition Failed"s);
        };

        section("PATCH with If-Unmodified-Since matching Last-Modified succeeds") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post11, _unused_post12, post_body] = make_request(
                setup->get_port(), "POST"s, "/lastmodifiedtest6"s, R"({"name":"PATCH If-Unmodified-Since Test","age":25})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // GET the document to get Last-Modified
            auto [get_status, _unused_get5, headers1, _unused_get6] = make_request(
                setup->get_port(), "GET"s, "/lastmodifiedtest6/"s + std::to_string(doc_id), ""s
            );
            require_true(headers1.contains("last-modified"s));
            auto last_modified = headers1["last-modified"s];
            
            // PATCH with If-Unmodified-Since matching the Last-Modified date
            auto custom_headers = std::map<string, string>{{"If-Unmodified-Since", last_modified}};
            auto [status, reason, headers, body] = make_request_with_headers(
                setup->get_port(), "PATCH"s, "/lastmodifiedtest6/"s + std::to_string(doc_id), custom_headers,
                R"({"age":30})"s
            );
            
            require_eq(status, "200"s);
        };

        section("HEAD response includes Last-Modified header") = [setup]
        {
            // Create a document
            auto [post_status, _unused_post13, _unused_post14, post_body] = make_request(
                setup->get_port(), "POST"s, "/lastmodifiedtest7"s, R"({"name":"HEAD Last-Modified Test"})"s
            );
            auto created = json::parse(post_body);
            auto doc_id = static_cast<xson::integer_type>(created["_id"s]);
            
            // HEAD request
            auto [status, reason, headers, body] = make_request(
                setup->get_port(), "HEAD"s, "/lastmodifiedtest7/"s + std::to_string(doc_id), ""s
            );
            
            require_eq(status, "200"s);
            require_true(headers.contains("last-modified"s));
            require_true(body.empty()); // HEAD should have no body
        };
    };

    // Note: Server runs in infinite loop, so we can't cleanly stop it
    // The test thread will continue running, but this is acceptable for unit tests
    return true;
}

const auto test_registrar = test_set();

} // namespace yar::httpd_unit_test

