#include <thread>
#include <set>
#include "std/extension.hpp"
#include "xson/json.hpp"
#include "net/syslogstream.hpp"
#include "net/acceptor.hpp"
#include "net/uri.hpp"
#include "db/engine.hpp"

namespace db::rest {

using namespace std::string_literals;
using namespace std::chrono_literals;
using namespace net;
using namespace xson;

using std::string;
using std::thread;
using std::stringstream;
using std::chrono::system_clock;
using std::set;
using std::tuple;
using std::get;
using std::ws;

class server
{
public:

    server(db::engine& engine) :
    m_engine{engine}
    {}

    void start(const string& serice_or_port = "http"s)
    {
        slog << notice << "Starting server" << flush;
        auto endpoint = acceptor{"localhost"s, serice_or_port};
        endpoint.timeout(1h);
        slog << notice << "Started server" << flush;
        while(true)
        {
            slog << notice << "Accepting connections" << flush;
            auto client = endpoint.accept();
            slog << info << "Accepted connection" << flush;
            auto worker = thread{[&](){handle(move(client));}};
            std::this_thread::sleep_for(1s);
            worker.detach();
        }
    }

private:

    void handle(endpointstream client)
    {
        while(client)
        {
            slog << debug << "Reading HTTP request" << flush;
            auto method = ""s, uri = ""s, version = ""s;
            client >> method >> uri >> version;
            slog << info << method << ' ' << uri << ' ' << version << flush;

            if(version != "HTTP/1.1")
                break;

            slog << debug << "Read HTTP request" << flush;

            slog << debug << "Reading HTTP headers" << flush;

            client >> ws;

            while(client && client.peek() != '\r')
            {
                auto name = ""s, value = ""s;
                getline(client, name, ':');
                trim(name);
                getline(client, value);
                trim(value);
                slog << info << name << ": " << value << flush;
            }

            slog << debug << "Read HTTP headers" << flush;

            if(!methods.count(method))
            {
                slog << warning << "Method " << method << " is ignored" << flush;
                client << "HTTP/1.1 400 Bad Request"                  << crlf
                       << "Date: " << to_rfc1123(system_clock::now()) << crlf
                       << "Server: YarDB/0.1"                         << crlf
                       << "Content-Length: 0"                         << crlf
                       << crlf
                       << flush;
                continue;
            }

            if(uri == R"(/)"s || uri == R"(/favicon.ico)"s)
            {
                slog << debug << "URI " << uri << " is ignored" << flush;
                client << "HTTP/1.1 204 No Content"                   << crlf
                       << "Date: " << to_rfc1123(system_clock::now()) << crlf
                       << "Server: YarDB/0.1"                         << crlf
                       << "Content-Length: 0"                         << crlf
                       << crlf
                       << flush;
                continue;
            }

            slog << debug << "URI " << uri << " is OK" << flush;

            auto uri2 = net::uri{uri};
            auto collection = uri2.path[1];
            auto key = uri2.path[2];
            auto query = uri2.query[0];

            auto selector = xson::object{};

            if(!key.empty() && std::numeric(key))
                selector = {"id"s, stoll(key)};
            else if(!key.empty() && !query.empty())
                selector = {to_string(key), make_object(uri2.query[0])};

            m_engine.collection(to_string(collection));

            auto found = false;
            auto body = json::object{};

            if(method == "GET"s || method == "HEAD"s)
            {
                slog << debug << "Reading " << collection << json::stringify(selector,0) << " from DB" << flush;
                found = m_engine.read(selector, body);
                slog << debug << "Read "   << collection << json::stringify(selector,0)  << " from DB" << flush;
            }
            else if(method == "DELETE")
            {
                slog << debug << "Deleting " << collection << json::stringify(selector,0) << " in DB"  << flush;
                found = m_engine.destroy(selector, body);
                slog << debug << "Deleted "  << collection << json::stringify(selector,0) << " in DB"  << flush;
            }
            else if(method == "POST" || method == "PUT" || method == "PATCH")
            {
                slog << debug << "Reading content" << flush;
                body = json::parse(client);
                slog << debug << "Read content " << json::stringify(body,0) << flush;
                slog << debug << "Updating " << collection << json::stringify(selector,0) << " in DB" << flush;
                if(method == "POST"s)
                    found = m_engine.create(body);
                else if(method == "PUT"s)
                    found = m_engine.replace(selector, body);
                else if(method == "PATCH"s)
                    found = m_engine.upsert(selector, body);
                slog << debug << "Updated "  << collection << json::stringify(selector,0) << " in DB" << flush;
            }

            slog << debug << "Sending HTTP response" << flush;

            const auto content = json::stringify(body);

            if(found)
            {
                // const auto content = json::stringify({"success"s, body});
                client << "HTTP/1.1 200 OK"                                                   << crlf
                       << "Date: " << to_rfc1123(system_clock::now())                         << crlf
                       << "Server: YarDB/0.1"                                              << crlf
                       << "Access-Control-Allow-Origin: *"                                    << crlf
                       << "Access-Control-Allow-Methods: HEAD, GET, POST, PUT, PATCH, DELETE" << crlf
                       << "Accept: application/json"                                          << crlf
                       << "Content-Type: application/json"                                    << crlf
                       << "Content-Length: " << content.length()                              << crlf
                       << crlf
                       << (method != "HEAD"s ? content : ""s) << flush;
            }
            else
            {
                // const auto content = json::stringify({"error"s, body});
                client << "HTTP/1.1 404 Not Found"                                            << crlf
                       << "Date: " << to_rfc1123(system_clock::now())                         << crlf
                       << "Server: YarDB/0.1"                                              << crlf
                       << "Access-Control-Allow-Origin: *"                                    << crlf
                       << "Access-Control-Allow-Methods: HEAD, GET, POST, PUT, PATCH, DELETE" << crlf
                       << "Accept: application/json"                                          << crlf
                       << "Content-Type: application/json"                                    << crlf
                       << "Content-Length: " << content.length()                              << crlf
                       << crlf
                       << (method != "HEAD"s ? content : ""s) << flush;
            }
            slog << debug << "Sent HTTP response" << flush;
        }
        slog << debug << "client closed" << flush;
    }

    xson::object make_object(const string_view query)
    {
        auto pos = query.find_first_of('=');
        auto name = to_string(query.substr(0,pos));
        auto value = stoll(query.substr(pos+1,query.length()));
        return {"$"s + name, value};
    }

    const set<string> methods = {"HEAD", "GET", "POST", "PUT", "PATCH", "DELETE"};

    db::engine& m_engine;
};

} // namespace db::rest
