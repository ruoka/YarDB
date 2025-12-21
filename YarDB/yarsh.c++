import yar;
import net;
import std;
import xson;

using namespace std;
using namespace ext;
using namespace net;
using namespace xson;

const auto usage = R"(usage: yarsh [--help] [URL])";

const auto help = R"(
Currently supported shell commands are:
POST /collection         aka Create
JSON
PUT /collection/id       aka Replace
JSON
PATCH /collection/id     aka Update/Upsert
JSON
GET /collection/{id}     aka Read
GET /collection?$top=10  aka Read with OData query parameters
HEAD /collection/{id}    aka Read headers only
DELETE /collection/id    aka Delete
HELP                     i.e. This text
EXIT                     i.e. Exit the shell
GET /                    i.e. Returns all the collections
GET /_reindex            i.e. Reindex all the collections

OData Query Parameters:
  $top=n         Limit results (e.g., ?$top=10)
  $skip=n        Skip results (e.g., ?$skip=20)
  $orderby=field Sort results (e.g., ?$orderby=age desc)
  $filter=expr   Filter documents (e.g., ?$filter=age gt 25)
  $select=fields Project fields (e.g., ?$select=name,email)
  $expand=rel    Expand related entities (e.g., ?$expand=orders)

Examples:
  GET /users?$top=10&$filter=age gt 25
  GET /users?$select=name,email&$orderby=name
)";

int main(int argc, char** argv)
try
{
    const auto arguments = span(argv,argc).subspan(1);
    auto url = "http://localhost:2112"sv;

    for(const string_view option : arguments)
    {
        if(option == "--help")
        {
            clog << usage << endl;
            return 0;
        }

        if(option.starts_with("-"))
        {
            clog << "Error: unknown option " << option << endl;
            clog << usage << endl;
            return 1;
        }

        url = option;
    }

    auto server = connect(url);
    
    // Extract host from URL for Host header
    auto host_header = ""s;
    if(url.starts_with("http://"))
    {
        auto host_start = url.substr(7); // Skip "http://"
        auto path_pos = host_start.find('/');
        if(path_pos != string_view::npos)
            host_header = string{host_start.substr(0, path_pos)};
        else
            host_header = string{host_start};
    }
    else
    {
        host_header = "localhost:2112"s;
    }
    
    clog << help << endl;

    while(cin && server)
    {
        auto method = ""s, uri = ""s, version = "HTTP/1.1"s, content = ""s, reason = ""s;
        auto headers = http::headers{};
        auto status = 0u;

        clog << "Enter restful request: ";
        cin >> method;

        ext::to_upper(method);

        if (method == "HELP")
        {
            clog << help << endl;
            continue;
        }

        if (method == "EXIT")
        {
            clog << "Closing connection..." << endl;
            break;
        }

        // Read URI (may include query parameters, so read rest of line)
        cin >> ws; // Skip whitespace
        getline(cin, uri);
        // Trim trailing whitespace from URI
        while(!uri.empty() && (uri.back() == ' ' || uri.back() == '\t' || uri.back() == '\r' || uri.back() == '\n'))
            uri.pop_back();

        // Read JSON body for methods that need it
        if(method == "POST" || method == "PUT" || method == "PATCH")
            content = json::stringify(json::parse(cin),0);

        clog << newl;

        // Display request
        clog << method << sp << uri << sp << version << newl
             << "Host: " << host_header << newl
             << "Accept: application/json" << newl;
        
        if(method == "POST" || method == "PUT" || method == "PATCH")
        {
            clog << "Content-Type: application/json" << newl
                 << "Content-Length: " << content.length() << newl;
        }
        
        clog << newl;
        if(!content.empty())
            clog << content << newl;
        clog << endl;

        // Send request
        server << method << sp << uri << sp << version << crlf
               << "Host: " << host_header << crlf
               << "Accept: application/json" << crlf;
        
        if(method == "POST" || method == "PUT" || method == "PATCH")
        {
            server << "Content-Type: application/json" << crlf
                   << "Content-Length: " << content.length() << crlf;
        }
        
        server << crlf;
        if(!content.empty())
            server << content;
        server << flush;

        // Read response
        server >> version >> status >> ws;
        getline(server,reason,'\r') >> ws >> headers >> crlf;

        // Display response status
        clog << version << sp << status << sp << reason << newl;

        // Display important headers
        clog << newl << "Response Headers:" << newl;
        if(headers.contains("etag"))
            clog << "  ETag: " << headers["etag"] << newl;
        if(headers.contains("last-modified"))
            clog << "  Last-Modified: " << headers["last-modified"] << newl;
        if(headers.contains("location"))
            clog << "  Location: " << headers["location"] << newl;
        if(headers.contains("content-location"))
            clog << "  Content-Location: " << headers["content-location"] << newl;
        if(headers.contains("content-type"))
            clog << "  Content-Type: " << headers["content-type"] << newl;
        if(headers.contains("content-length"))
            clog << "  Content-Length: " << headers["content-length"] << newl;

        auto content_length = headers.contains("content-length") ? std::stoll(headers["content-length"]) : 0ull;

        clog << newl;

        // Read and display body (skip for HEAD requests)
        if(method != "HEAD" && content_length > 1)
        {
            clog << "Response Body:" << newl;
            // only {} and [] are valid, complete JSON strings in parsers and stringifiers
            clog << json::stringify(json::parse(server)) << newl;
        }

        clog << endl;
    }
    clog << "See you later - bye!" << newl;
    return 0;
}
catch(const system_error& e)
{
    cerr << "System error with code " << e.code() << " aka " << quoted(e.what()) << endl;
    return 1;
}
catch(const std::exception& e)
{
    cerr << "Exception: " << quoted(e.what()) << endl;
    return 1;
}
catch(...)
{
    cerr << "Shit hit the fan!" << endl;
    return 1;
}
