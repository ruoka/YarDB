import yar;
import net;
import std;
import xson;

using namespace std;
using namespace string_view_literals;
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
DELETE /collection/id    aka Delete
HELP                     i.e. This text
EXIT                     i.e. Exit the shell
GET /                    i.e. Returns all the collections
GET /_reindex            i.e. Reindex all the collections
)";

int main(int argc, char** argv)
try
{
    const auto arguments = span(argv,argc).subspan(1);
    auto url = "http://localhost:2112"sv;

    for(const string_view option : arguments)
    {
        if(option.starts_with("--help"))
        {
            clog << usage << endl;
            return 0;
        }
        else if(option.starts_with("-"))
        {
            clog << usage << endl;
            return 1;
        }
        else
        {
            url = option;
        }
    }

    auto server = connect(url);

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

        cin >> uri;

        if(method == "POST" || method == "PUT" || method == "PATCH")
            content = json::stringify(json::parse(cin),0);

        clog << newl;

        clog << method << sp << uri << sp << version     << newl
               << "Host: localhost:2112 "                << newl
               << "Accept: application/json"             << newl
               << "Content-Type: application/json"       << newl
               << "Content-Length: " << content.length() << newl
               << newl
               << content
               << newl;

        clog << endl;

        server << method << sp << uri << sp << version   << crlf
               << "Host: localhost:2112 "                << crlf
               << "Accept: application/json"             << crlf
               << "Content-Type: application/json"       << crlf
               << "Content-Length: " << content.length() << crlf
               << crlf
               << content
               << flush;

        server >> version >> status >> ws;
        getline(server,reason,'\r') >> ws >> headers >> crlf;

        clog << version << sp << status << sp << reason << newl;

        auto content_length = headers.contains("content-length") ? std::stoll(headers["content-length"]) : 0ull;

        clog << newl;

        // only {} and [] are valid, complete JSON strings in parsers and stringifiers
        if(content_length > 1)
            clog << json::stringify(json::parse(server)) << newl;

        clog << endl;
    }
    clog << "See you latter - bye!" << newl;
    return 0;
}
catch(const system_error& e)
{
    cerr << "System error with code: \"" << e.code() << "\" \"" << e.what() << "\"" << endl;
    return 1;
}
catch(const std::exception& e)
{
    cerr << "Exception: \"" << e.what() << "\"" << endl;
    return 1;
}
catch(...)
{
    cerr << "Shit hit the fan!" << endl;
    return 1;
}
