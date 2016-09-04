#include "net/connector.hpp"
#include "xson/json.hpp"

using namespace std;
using namespace net;
using namespace xson;

// Currently supported shell commands are
// GET /collection/{id}
// EXIT

int main(int, char**)
try
{
    auto server = net::connect("localhost","2112");

    while(cin && server)
    {
        auto method = ""s, uri = ""s, version = "HTTP/1.1"s, reason = ""s;
        auto status = 0;

        cin >> method;

        if (method == "EXIT") break;

        cin >> uri;

        if(method == "GET"s)
            server << method << sp << uri << sp << version << crlf
                   << "Host: localhost:2112 "              << crlf
                   << "Accept: application/json"           << crlf
                   << crlf
                   << flush;
        else
            continue;

        server >> version >> status;
        getline(server, reason);
        trim(reason);

        clog << reason << endl;

        server >> ws;

        while(server && server.peek() != '\r')
        {
            auto name = ""s, value = ""s;
            getline(server, name, ':');
            trim(name);
            getline(server, value);
            trim(value);
        }

        if(status == 200 || status == 404)
            cout << json::stringify(json::parse(server)) << endl;
    }
    clog << "Server closed the connection" << endl;
    return 0;
}
catch(...)
{
    clog << "Shit hit the fan!" << endl;
    return 1;
}
