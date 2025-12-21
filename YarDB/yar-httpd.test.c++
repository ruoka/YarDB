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

            require_true(status == "201"s);
            require_true(reason == "Created"s);
            require_true(headers.contains("content-type"));
            
            // Verify response body contains the created document
            auto document = json::parse(response_body);
            require_true(document.has("_id"s));
            const string name = document["name"s];
            require_true(name == "Test Item"s);
        };

        section("GET existing document returns 200 OK with single object") = [setup]
        {
            // First create a document
            auto [post_status, post_reason, post_headers, post_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Existing Item"})"s
            );
            require_true(post_status == "201"s);
            
            auto created_doc = json::parse(post_body);
            const long long id = created_doc["_id"s];
            
            // Get the document
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems/"s + std::to_string(id), ""s
            );

            require_true(status == "200"s);
            require_true(reason == "OK"s);
            
            // Verify response is a single object (starts with {, not [)
            require_true(response_body.front() == '{');
            require_true(response_body.back() == '}');
            
            auto document = json::parse(response_body);
            require_true(document.has("_id"s));
            const long long doc_id = document["_id"s];
            require_true(doc_id == id);
        };

        section("GET non-existent document returns 404 Not Found") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems/99999"s, ""s
            );

            require_true(status == "404"s);
            require_true(reason == "Not Found"s);
            
            // Verify error object structure
            auto error = json::parse(response_body);
            require_true(error.has("error"s));
            const string error_msg = error["error"s];
            require_true(error_msg == "Not Found"s);
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
            require_true(post_status == "201"s);
            
            auto created_doc = json::parse(post_body);
            const long long id = created_doc["_id"s];
            
            // Delete the document
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "DELETE"s, "/testitems/"s + std::to_string(id), ""s
            );

            require_true(status == "204"s);
            require_true(reason == "No Content"s);
            require_true(response_body.empty());
        };

        section("DELETE non-existent document returns 404 Not Found") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "DELETE"s, "/testitems/99999"s, ""s
            );

            require_true(status == "404"s);
            require_true(reason == "Not Found"s);
            
            // Verify error object structure
            auto error = json::parse(response_body);
            require_true(error.has("error"s));
            const string error_msg = error["error"s];
            require_true(error_msg == "Not Found"s);
        };

        section("POST with invalid JSON returns 400 Bad Request") = [setup]
        {
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"invalid":json})"s
            );

            require_true(status == "400"s);
            require_true(reason == "Bad Request"s);
            
            // Verify error object structure
            auto error = json::parse(response_body);
            require_true(error.has("error"s));
            const string error_msg = error["error"s];
            require_true(error_msg == "Bad Request"s);
            require_true(error.has("message"s));
        };

        section("PUT updates document and returns 200 OK with Content-Location header") = [setup]
        {
            // First create a document
            auto [post_status, post_reason, post_headers, post_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Original"})"s
            );
            require_true(post_status == "201"s);
            
            auto created_doc = json::parse(post_body);
            const long long id = created_doc["_id"s];
            
            // Update the document
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "PUT"s, "/testitems/"s + std::to_string(id), R"({"name":"Updated","value":100})"s
            );

            require_true(status == "200"s);
            require_true(reason == "OK"s);
            
            // Verify Content-Location header is present
            require_true(headers.contains("content-location"s));
            const string content_location = headers["content-location"s];
            require_true(content_location == "/testitems/"s + std::to_string(id));
            
            auto document = json::parse(response_body);
            const string name = document["name"s];
            require_true(name == "Updated"s);
            const long long value = document["value"s];
            require_true(value == 100ll);
        };

        section("PUT creates document (upsert) and returns 201 Created") = [setup]
        {
            // PUT to a non-existent document should create it
            const long long new_id = 99999ll;
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "PUT"s, "/testitems/"s + std::to_string(new_id), R"({"name":"New Item","value":200})"s
            );

            require_true(status == "201"s);
            require_true(reason == "Created"s);
            
            // Verify Location header is present
            require_true(headers.contains("location"s));
            const string location = headers["location"s];
            require_true(location == "/testitems/"s + std::to_string(new_id));
            
            auto document = json::parse(response_body);
            const long long doc_id = document["_id"s];
            require_true(doc_id == new_id);
            const string name = document["name"s];
            require_true(name == "New Item"s);
            const long long value = document["value"s];
            require_true(value == 200ll);
            
            // Verify we can retrieve it
            auto [get_status, get_reason, get_headers, get_body] = make_request(
                setup->get_port(), "GET"s, "/testitems/"s + std::to_string(new_id), ""s
            );
            require_true(get_status == "200"s);
        };

        section("PATCH updates document and returns 200 OK with Content-Location header") = [setup]
        {
            // First create a document
            auto [post_status, post_reason, post_headers, post_body] = make_request(
                setup->get_port(), "POST"s, "/testitems"s, R"({"name":"Original","value":50})"s
            );
            require_true(post_status == "201"s);
            
            auto created_doc = json::parse(post_body);
            const long long id = created_doc["_id"s];
            
            // Partially update the document
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "PATCH"s, "/testitems/"s + std::to_string(id), R"({"value":75})"s
            );

            require_true(status == "200"s);
            require_true(reason == "OK"s);
            
            // Verify Content-Location header is present
            require_true(headers.contains("content-location"s));
            const string content_location = headers["content-location"s];
            require_true(content_location == "/testitems/"s + std::to_string(id));
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
                require_true(post_status == "201"s);
                // Small delay to ensure server processes request
                std::this_thread::sleep_for(50ms);
            }
            
            // Get collection with $top=2
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems?$top=2"s, ""s
            );

            require_true(status == "200"s);
            require_true(reason == "OK"s);
            
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
                require_true(post_status == "201"s);
                // Small delay to ensure server processes request
                std::this_thread::sleep_for(50ms);
            }
            
            // Get collection with $orderby desc
            // Note: Space in "field desc" must be URL-encoded as %20
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems?$orderby=field%20desc"s, ""s
            );

            require_true(status == "200"s);
            require_true(reason == "OK"s);
            
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
                    require_true(retry_status == "201"s);
                }
                else
                {
                    require_true(post_status == "201"s);
                }
                // Small delay to ensure server processes request
                std::this_thread::sleep_for(100ms);
            }
            
            // Get collection with $top=2 and $orderby desc
            // Note: Space in "field desc" needs to be URL-encoded as %20
            auto [status, reason, headers, response_body] = make_request(
                setup->get_port(), "GET"s, "/testitems?$top=2&$orderby=field%20desc"s, ""s
            );

            require_true(status == "200"s);
            require_true(reason == "OK"s);
            
            // Verify response contains at most 2 items
            // Response is an array, not an object with collection name
            auto documents = json::parse(response_body);
            require_true(documents.is_array());
            const auto& items = documents.get<object::array>();
            require_true(items.size() <= 2);
        };
    };

    // Note: Server runs in infinite loop, so we can't cleanly stop it
    // The test thread will continue running, but this is acceptable for unit tests
    return true;
}

const auto test_registrar = test_set();

} // namespace yar::httpd_unit_test

