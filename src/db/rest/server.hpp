#include <thread>
#include "std/extension.hpp"
#include "xson/json.hpp"
#include "net/syslogstream.hpp"
#include "net/acceptor.hpp"
#include "db/engine.hpp"

namespace db::rest {

using namespace std;
using namespace std::string_literals;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace xson;
using namespace net;

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
            auto connection = endpoint.accept();
            slog << info << "Accepted connection" << flush;
            auto worker = thread{[&](){handle(move(connection));}};
            this_thread::sleep_for(1s);
            worker.detach();
        }
    }

private:

    void handle(endpointstream connection)
    {
        while(connection)
        {
            slog << debug << "Reading HTTP request" << flush;
            auto method = ""s, uri = ""s, version = ""s;
            connection >> method >> uri >> version;
            slog << info << method << ' ' << uri << ' ' << version << flush;

            if(version != "HTTP/1.1")
                break;

            slog << debug << "Read HTTP request" << flush;

            slog << debug << "Reading HTTP headers" << flush;

            connection >> ws;

            while(connection && connection.peek() != '\r')
            {
                auto name = ""s, value = ""s;
                getline(connection, name, ':');
                trim(name);
                getline(connection, value);
                trim(value);
                slog << info << name << ": " << value << flush;
            }

            slog << debug << "Read HTTP headers" << flush;

            if(uri == R"(/)"s || uri == R"(/favicon.ico)"s)
            {
                slog << debug << "URI " << uri << " is ignored" << flush;
                connection << "HTTP/1.1 204 No Content"                   << crlf
                           << "Date: " << to_rfc1123(system_clock::now()) << crlf
                           << "Server: YARESTDB/0.1"                      << crlf
                           << "Content-Length: 0"                         << crlf
                           << crlf
                           << flush;
                continue;
            }

            const auto tmp = parse(uri);
            const auto collection = get<0>(tmp);
            const auto selector = get<1>(tmp);
            auto found = false;

            m_engine.collection(collection);

            auto body = json::object{};
            body.type(xson::type::array);

            if(method == "GET"s || method == "HEAD"s)
            {
                slog << debug << "Reading " << collection << json::stringify(selector,0) << " from DB" << flush;
                found = m_engine.read(selector, body);
                slog << debug << "Read "   << collection << json::stringify(selector,0)  << " from DB" << flush;
            }
            else if(method == "DELETE")
            {
                slog << debug << "Deleting " << collection << json::stringify(selector,0) << " in DB"  << flush;
                found = m_engine.destroy(selector);
                slog << debug << "Deleted "  << collection << json::stringify(selector,0) << " in DB"  << flush;
            }
            else
            {
                slog << debug << "Reading content" << flush;
                body = json::parse(connection);
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

            const auto content = json::stringify({"data"s, body});

            if(found)
                connection << "HTTP/1.1 200 OK"                                                   << crlf
                           << "Date: " << to_rfc1123(system_clock::now())                         << crlf
                           << "Server: YARESTDB/0.1"                                              << crlf
                           << "Access-Control-Allow-Origin: *"                                    << crlf
                           << "Access-Control-Allow-Methods: HEAD, GET, POST, PUT, PATCH, DELETE" << crlf
                           << "Accept: application/json"                                          << crlf
                           << "Content-Type: application/json"                                    << crlf
                           << "Content-Length: " << content.length()                              << crlf
                           << crlf
                           << (method != "HEAD"s ? content : ""s) << flush;
            else
                connection << "HTTP/1.1 404 Not Found"                                            << crlf
                           << "Date: " << to_rfc1123(system_clock::now())                         << crlf
                           << "Server: YARESTDB/0.1"                                              << crlf
                           << "Access-Control-Allow-Origin: *"                                    << crlf
                           << "Access-Control-Allow-Methods: HEAD, GET, POST, PUT, PATCH, DELETE" << crlf
                           << "Accept: application/json"                                          << crlf
                           << "Content-Type: application/json"                                    << crlf
                           << "Content-Length: " << content.length()                              << crlf
                           << crlf
                           << (method != "HEAD"s ? content : ""s) << flush;

            slog << debug << "Sent HTTP response" << flush;
        }
        slog << debug << "Connection closed" << flush;
    }

    tuple<string,json::object> parse(const string& uri)
    {
        auto ss = stringstream{uri};
        auto slash = ""s, collection = ""s, id = ""s, query = ""s;
        getline(ss, slash, '/');
        getline(ss, collection, '/');
        getline(ss, id, '?');
        getline(ss, query);
        auto selector = object{};
        if(!id.empty())
            selector = {"_id",id};
        return std::make_tuple(collection,selector);
    }

    db::engine& m_engine;
};

} // namespace db::rest
