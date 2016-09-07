#include "net/connector.hpp"
#include "xson/json.hpp"

using namespace std;
using namespace net;
using namespace string_literals;
using namespace xson;
using namespace json;

const auto usage = R"(
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

int main(int, char**)
try
{
    clog << usage << endl;

    auto server = connect("localhost","2112");

    while(cin && server)
    {
        auto method = ""s, uri = ""s, version = "HTTP/1.1"s, content = ""s, reason = ""s;
        auto status = 0;

        cin >> method;
        for (auto & c: method) c = std::toupper(c);

        if (method == "HELP")
        {
            clog << usage << endl;
            continue;
        }

        if (method == "EXIT")
        {
            break;
        }

        cin >> uri;

        if(method == "POST"s || method == "PUT"s || method == "PATCH"s)
            content = stringify(parse(cin));

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

        server >> ws;

        while(server && server.peek() != '\r')
        {
            auto name = ""s, value = ""s;
            getline(server, name, ':');
            trim(name);
            getline(server, value);
            trim(value);
            clog << name << ": " << value << endl;
        }

        if(status == 200 || status == 404)
            cout << stringify(parse(server)) << endl;
    }
    clog << "Server closed the connection" << endl;
    return 0;
}
catch(...)
{
    clog << "Shit hit the fan!" << endl;
    return 1;
}
