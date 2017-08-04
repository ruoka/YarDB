#include <string_view>
#include "gsl/span.hpp"
#include "net/connector.hpp"
#include "xson/json.hpp"

using namespace std;
using namespace string_literals;
using namespace gsl;
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
GET /collection/{id}     aka Raad
DELETE /collection/id    aka Delete
HELP                     i.e. This text
EXIT                     i.e. Exit the shell
)";

int main(int argc, char** argv)
try
{
    const auto arguments = make_span(argv,argc).subspan(1);
    auto url = "http://localhost:2112"s;

    for(const string_view option : arguments)
    {
        if(option.find("--help") == 0)
        {
            clog << usage << endl;
            return 0;
        }
        else if(option.find("-") == 0)
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
        auto status = 0u;

        clog << "Enter HTTP request or command: ";
        cin >> method;
        if(method.empty()) continue;

        for(auto& c : method) c = std::toupper(c);

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
        if(uri.empty()) continue;

        if(method == "POST" || method == "PUT" || method == "PATCH")
            content = json::stringify(json::parse(cin));

        clog << newl;

        clog << method << sp << uri << sp << version     << newl
               << "Host: localhost:2112 "                << newl
               << "Accept: application/json"             << newl
               << "Content-Type: application/json"       << newl
               << "Content-Length: " << content.length() << newl
               << newl
               << content
               << flush;

        server.clear();

        server << method << sp << uri << sp << version   << crlf
               << "Host: localhost:2112 "                << crlf
               << "Accept: application/json"             << crlf
               << "Content-Type: application/json"       << crlf
               << "Content-Length: " << content.length() << crlf
               << crlf
               << content
               << flush;

        server >> version >> status;
        getline(server, reason);
        trim(reason);

        clog << version << sp << status << sp << reason << endl;

        server >> ws;  // Skip all whitespaces

        auto content_length = 0ll;

        while(server && server.peek() != '\r')
        {
            auto header = ""s, value = ""s;
            getline(server, header, ':');
            trim(header);
            getline(server, value);
            trim(value);
            clog << header << ": " << value << newl;

            if(header == "Content-Length")
                content_length = stoll(value);
        }
        // assert(server.get() == '\r');
        // assert(server.get() == '\n');
        server.ignore(2);

        if(method != "HEAD" && (status == 200 /* || status == 404 */ ))
        {
            // server.ignore(content_length);
            clog << json::stringify(json::parse(server)) << newl;
        }
        clog << newl;
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
    cerr << "Exception: " << e.what() << endl;
    return 1;
}
catch(...)
{
    cerr << "Shit hit the fan!" << endl;
    return 1;
}
