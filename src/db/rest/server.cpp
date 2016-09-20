#include <thread>
#include "net/acceptor.hpp"
#include "net/syslogstream.hpp"
#include "net/uri.hpp"
#include "db/rest/server.hpp"

using namespace std::string_literals;
using namespace std::chrono_literals;
using namespace net;
using namespace xson;
using namespace ext;

using std::chrono::system_clock;
using std::thread;
using std::ws;

namespace
{

inline json::object to_object(string_view name, string_view value)
{
    if(numeric(value))
        return {to_string(name), stoi(value)};
    else if(value == "true")
        return {to_string(name), true};
    else if(value == "false")
        return {to_string(name), false};
    else if(value == "null")
        return {to_string(name), nullptr};
    else
        return {to_string(name), to_string(value)};
}

inline json::object to_operator(string_view query)
{
    auto pos   = query.find_first_of('=');
    auto name  = to_string(query.substr(0,pos));
    auto value = query.substr(pos+1);
    if(numeric(value))
        return {"$"s + name, stoi(value)};
    else if(value == "true")
        return {"$"s + name, true};
    else if(value == "false")
        return {"$"s + name, false};
    else if(value == "null")
        return {"$"s + name, nullptr};
    else
        return {"$"s + name, to_string(value)};
}

template<typename T>
json::object to_filter(const T& query)
{
    auto filter = json::object{};
    for(auto q : query)
        if(q.rfind("top") != 0)
            filter += to_operator(q);
    return filter;
}

template<typename T>
json::object to_top(const T& query)
{
    auto filter = json::object{};
    for(auto q : query)
        if(q.rfind("top") == 0)
            filter += to_operator(q);
    return filter;
}

} // namespace

db::rest::server::server(db::engine& engine) :
m_engine{engine}
{}

void db::rest::server::start(const std::string& serice_or_port)
{
    slog << notice << "Starting server" << flush;
    auto endpoint = net::acceptor{"localhost"s, serice_or_port};
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

void db::rest::server::handle(net::endpointstream client)
{
    while(client)
    {
        slog << debug << "Reading HTTP request message" << flush;

        slog << debug << "Reading HTTP request line" << flush;

        auto method = ""s, request_uri = ""s, http_version = ""s;
        client >> method >> request_uri >> http_version;
        slog << info << "Received request " << method << ' ' << request_uri << ' ' << http_version << flush;

        slog << debug << "Read HTTP request line" << flush;

        slog << debug << "Reading HTTP request headers" << flush;

        client >> ws;

        while(client && client.peek() != '\r')
        {
            auto request_header = ""s, value = ""s;
            getline(client, request_header, ':');
            trim(request_header);
            getline(client, value);
            trim(value);
            slog << info << "With request header " << request_header << ": " << value << flush;
        }

        slog << debug << "Read HTTP request headers" << flush;

        slog << debug << "Read HTTP request message" << flush;

        if(!methods.count(method))
        {
            slog << notice << "HTTP requests message with method " << method << " was ignored" << flush;
            client << "HTTP/1.1 400 Bad Request"                  << crlf
                   << "Date: " << to_rfc1123(system_clock::now()) << crlf
                   << "Server: YarDB/0.1"                         << crlf
                   << "Content-Length: 0"                         << crlf
                   << crlf
                   << flush;
            continue; // ignore with continue
        }

        if(request_uri == "/" || request_uri == "/favicon.ico")
        {
            slog << info << "HTTP requests message with request URI " << request_uri << " was ignored" << flush;
            client << "HTTP/1.1 404 Not Found"                    << crlf
                   << "Date: " << to_rfc1123(system_clock::now()) << crlf
                   << "Server: YarDB/0.1"                         << crlf
                   << "Content-Length: 0"                         << crlf
                   << crlf
                   << flush;
            continue; // ignore with continue
        }

        if(http_version != "HTTP/1.1")
        {
            slog << warning << "HTTP requests message with HTTP version " << http_version << " was rejected" << flush;
            client << "HTTP/1.1 505 HTTP Version Not Supported"   << crlf
                   << "Date: " << to_rfc1123(system_clock::now()) << crlf
                   << "Server: YarDB/0.1"                         << crlf
                   << "Content-Length: 0"                         << crlf
                   << crlf
                   << flush;
            break; // reject with break
        }

        slog << debug << "HTTP request with request URI " << request_uri << " was OK" << flush;

        auto found = false;
        auto request_body  = json::object{},
             response_body = json::object{};

        try
        {
            auto collection = string_view{};
            auto selector = json::object{};
            std::tie(collection,selector) = convert(request_uri);

            const auto lock = std::unique_lock<db::engine>{m_engine};
            m_engine.collection(to_string(collection));

            if(method == "GET" || method == "HEAD")
            {
                slog << debug << "Reading from collection " << collection << " with selector "<< json::stringify(selector,0) << flush;
                found = m_engine.read(selector, response_body);
                slog << info << "Read from collection " << collection << " with selector " << json::stringify(selector,0) << flush;
            }
            else if(method == "DELETE")
            {
                slog << debug << "Deleting from collection " << collection <<  " with selector " << json::stringify(selector,0) << flush;
                found = m_engine.destroy(selector, response_body);
                slog << info << "Deleted from collection "  << collection <<  " with selector " << json::stringify(selector,0) << flush;
            }
            else if(method == "POST" || method == "PUT" || method == "PATCH")
            {
                slog << debug << "Reading HTTP request body" << flush;
                request_body = json::parse(client);
                slog << debug << "Read  HTTP request body " << json::stringify(request_body,0) << flush;
                slog << debug << "Updating collection " << collection <<  " with selector " << json::stringify(selector,0) << flush;
                if(method == "POST"s)
                    found = m_engine.create(request_body);
                else if(method == "PUT"s)
                    found = m_engine.replace(selector, request_body);
                else if(method == "PATCH"s)
                    found = m_engine.upsert(selector, request_body, response_body);
                slog << info << "Updated collection "  << collection <<  " with selector " << json::stringify(selector,0) << flush;
            }
        }
        catch(const std::exception& e)
        {
            slog << warning << "Exception from the DB " << e.what() << flush;
        }

        auto content = ""s;
        if(method == "POST" || method == "PUT")
            content = json::stringify(request_body);
        else
            content = json::stringify(response_body);

        slog << debug << "Sending HTTP response message" << flush;

        if(found)
        {
            // const auto content = json::stringify({"success"s, body});
            client << "HTTP/1.1 200 OK"                                                   << crlf
                   << "Date: " << to_rfc1123(system_clock::now())                         << crlf
                   << "Server: YarDB/0.1"                                                 << crlf
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
                   << "Server: YarDB/0.1"                                                 << crlf
                   << "Access-Control-Allow-Origin: *"                                    << crlf
                   << "Access-Control-Allow-Methods: HEAD, GET, POST, PUT, PATCH, DELETE" << crlf
                   << "Accept: application/json"                                          << crlf
                   << "Content-Type: application/json"                                    << crlf
                   << "Content-Length: " << content.length()                              << crlf
                   << crlf
                   << (method != "HEAD"s ? content : ""s) << flush;
        }
        slog << debug << "Sent HTTP response message" << flush;
    }
    slog << debug << "Client closed connection" << flush;
}

std::tuple<string_view,json::object> db::rest::server::convert(string_view request_uri)
{
    auto components = net::uri{request_uri};
    auto root       = components.path[0];                // Should be /
    auto collection = components.path[1];
    auto key        = components.path[2];
    auto value      = components.path[3];
    auto query      = components.query;
    auto selector   = xson::object{};

    if(key.empty())                                      // collection?lte=4?desc
        selector += {"_id"s, to_filter(query)};

    else if(numeric(key))                                // collection/123
        selector += {"_id"s, stoll(key)};

    else if(key == "id"s && value.empty())              // collection/id?$head=10
        selector += {"_id"s, to_filter(query)};

    else if(key == "id"s && numeric(value))              // collection/id/123
        selector += {"_id"s, stoll(value)};

    else if(value.empty())                               // collection/field?eq=value?desc
        selector += {to_string(key), to_filter(query)};

    else                                                 // collection/field/value
        selector += to_object(key,value) += to_filter(query);

    selector += to_top(query);

    return std::make_tuple(collection,selector);
}
