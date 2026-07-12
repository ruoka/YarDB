import yar;
import net;
import std;
import xson;

using namespace std;
using namespace utils;
using namespace net;
using namespace xson;

const auto usage = R"(usage: yarsh [--help] [URL])";

const auto help = R"(
Currently supported shell commands are:
POST /collection         aka Create (one-line JSON body follows)
PUT /collection/id       aka Replace (JSON body follows)
PATCH /collection/id     aka Update/Upsert (JSON body follows)
GET /collection/{id}     aka Read one document
GET /collection?...      aka Read with OData query parameters
HEAD /collection/{id}    aka Read headers only
DELETE /collection/id    aka Delete
HELP                     i.e. This text
EXIT                     i.e. Exit the shell

Collections and admin:
GET /                    List all collections
GET /_reindex            Reindex all collections
GET /$metadata           OData 4.01 JSON CSDL metadata
PUT /_db/collection      Configure secondary indexes (JSON body: {"keys":["field"]})
PATCH /_db/collection    Add secondary indexes incrementally

Optional request headers (before JSON body; one per line):
  @Accept: application/json;odata=minimalmetadata
  @If-Match: "etag-value"
  @If-None-Match: "etag-value"

OData query parameters:
  $top=n         Limit results (e.g. ?$top=10)
  $skip=n        Skip results (e.g. ?$skip=20)
  $orderby=field Sort (e.g. ?$orderby=age%20desc — URL-encode spaces)
  $filter=expr   Filter (eq, ne, gt, ge, lt, le, and, or, in, startswith, contains, endswith)
  $select=fields Project fields (e.g. ?$select=name,email)
  $count=true    Return count only (e.g. ?$count=true&$filter=age%20gt%2025)
  $expand=rel    Placeholder — returned as-is until relationship model lands

Examples:
  GET /users?$top=10&$filter=age%20gt%2025
  GET /users?$count=true&$filter=status%20ne%20'deleted'
  GET /users?$select=name,email&$orderby=name
  GET /users/1
  @If-None-Match: "12345"
  GET /users/1
)";

namespace {

void header_name_to_lower(string& name)
{
    for(auto& c : name)
    {
        if(c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
}

auto trim(string_view text) -> string
{
    auto begin = text.find_first_not_of(" \t\r\n");
    if(begin == string_view::npos)
        return ""s;
    auto end = text.find_last_not_of(" \t\r\n");
    return string{text.substr(begin, end - begin + 1)};
}

auto host_from_url(string_view url) -> string
{
    const uri endpoint{url};
    const auto host = string{endpoint.host};
    if(host.empty())
        return "localhost:2112"s;

    const auto port = string{endpoint.port};
    if(port.empty())
        return host;

    return host + ":" + port;
}

auto read_optional_headers() -> pair<string, vector<pair<string, string>>>
{
    auto accept = "application/json"s;
    auto headers = vector<pair<string, string>>{};

    while(cin.peek() == '@')
    {
        auto line = ""s;
        getline(cin, line);

        const auto colon = line.find(':');
        if(colon == string::npos)
            continue;

        auto name = trim(string_view{line}.substr(1, colon - 1));
        auto value = trim(string_view{line}.substr(colon + 1));
        header_name_to_lower(name);

        if(name == "accept"s)
            accept = std::move(value);
        else
            headers.emplace_back(std::move(name), std::move(value));
    }

    return {std::move(accept), std::move(headers)};
}

auto read_body(std::istream& stream, std::size_t length) -> string
{
    auto body = string(length, '\0');
    stream.read(body.data(), static_cast<std::streamsize>(length));
    return body;
}

void print_response_body(std::istream& stream, std::size_t content_length)
{
    if(content_length == 0)
        return;

    clog << "Response Body:" << newl;

    const auto body = read_body(stream, content_length);
    try
    {
        clog << json::stringify(json::parse(body)) << newl;
    }
    catch(const std::exception& e)
    {
        clog << body << newl;
        clog << "(body is not valid JSON: " << e.what() << ")" << newl;
    }
}

} // namespace

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

    auto server = connect(uri{url});
    const auto host_header = host_from_url(url);

    clog << help << endl;

    while(cin && server)
    {
        auto method = ""s, uri_path = ""s, version = "HTTP/1.1"s, content = ""s, reason = ""s;
        auto headers = http::headers{};
        auto status = 0u;

        clog << "Enter restful request: ";
        cin >> method;

        utils::ascii_to_upper(method);

        if(method == "HELP")
        {
            clog << help << endl;
            continue;
        }

        if(method == "EXIT")
        {
            clog << "Closing connection..." << endl;
            break;
        }

        cin >> ws;
        getline(cin, uri_path);
        while(!uri_path.empty() && (uri_path.back() == ' ' || uri_path.back() == '\t' || uri_path.back() == '\r' || uri_path.back() == '\n'))
            uri_path.pop_back();

        auto [accept_header, extra_headers] = read_optional_headers();

        if(method == "POST" || method == "PUT" || method == "PATCH")
        {
            try
            {
                auto body_line = ""s;
                getline(cin, body_line);
                content = json::stringify(json::parse(body_line), 0);
            }
            catch(const std::exception& e)
            {
                clog << "Invalid JSON body: " << e.what() << newl << endl;
                continue;
            }
        }

        clog << newl;

        clog << method << sp << uri_path << sp << version << newl
             << "Host: " << host_header << newl
             << "Accept: " << accept_header << newl;

        for(const auto& [name, value] : extra_headers)
            clog << name << ": " << value << newl;

        if(method == "POST" || method == "PUT" || method == "PATCH")
        {
            clog << "Content-Type: application/json" << newl
                 << "Content-Length: " << content.length() << newl;
        }

        clog << newl;
        if(!content.empty())
            clog << content << newl;
        clog << endl;

        server << method << sp << uri_path << sp << version << crlf
               << "Host: " << host_header << crlf
               << "Accept: " << accept_header << crlf;

        for(const auto& [name, value] : extra_headers)
            server << name << ": " << value << crlf;

        if(method == "POST" || method == "PUT" || method == "PATCH")
        {
            server << "Content-Type: application/json" << crlf
                   << "Content-Length: " << content.length() << crlf;
        }

        server << crlf;
        if(!content.empty())
            server << content;
        server << flush;

        server >> version >> status >> ws;
        getline(server,reason,'\r') >> ws >> headers >> crlf;

        clog << version << sp << status << sp << reason << newl;

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

        const auto content_length = headers.contains("content-length")
            ? static_cast<std::size_t>(std::stoull(headers["content-length"]))
            : 0uz;

        clog << newl;

        if(method != "HEAD")
            print_response_body(server, content_length);

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
    cerr << "Unexpected error occurred" << endl;
    return 1;
}