#include <thread>
#include <sstream>
#include "net/acceptor.hpp"
#include "net/syslogstream.hpp"
#include "net/uri.hpp"
#include "db/rest/server.hpp"

using namespace std;
using namespace string_literals;
using namespace chrono_literals;
using namespace chrono;
using namespace this_thread;
using namespace net;
using namespace xson;
using namespace ext;

namespace
{

inline auto iscsv(string_view value)
{
    return value.find_first_of(',') != value.npos;
}

inline json::object make_array(string_view values)
{
    std::stringstream ss;
    ss << '[' << values << ']';
    auto array = json::parse(ss);
    return array;
}

inline json::object make_object(string_view name, string_view value)
{
    if(isnumeric(value))
        return {string{name}, stoll(value)};
    else if(value == "true")
        return {string{name}, true};
    else if(value == "false")
        return {string{name}, false};
    else if(value == "null")
        return {string{name}, nullptr};
    else if(iscsv(value))
        return {string{name}, db::object{"$in"s, make_array(value)}};
    else
        return {string{name}, string(value)};
}

inline json::object make_operator(string_view query)
{
    auto pos   = query.find_first_of('=');
    auto name  = string(query.substr(0,pos));
    auto value = query.substr(pos+1);
    if(isnumeric(value))
        return {"$"s + name, stoll(value)};
    else if(value == "true")
        return {"$"s + name, true};
    else if(value == "false")
        return {"$"s + name, false};
    else if(value == "null")
        return {"$"s + name, nullptr};
    else
        return {"$"s + name, string(value)};
}

template<typename T>
json::object make_filter(const T& query)
{
    auto filter = db::object{};
    for(auto q : query)
        if(q.rfind("top") != 0)
            filter += make_operator(q);
    return filter;
}

template<typename T>
json::object make_order(const T& query)
{
    auto filter = db::object{};
    for(auto q : query)
        if(q.rfind("desc") == 0)
            filter += make_operator(q);
    return filter;
}

template<typename T>
json::object make_top(const T& query)
{
    auto filter = db::object{};
    for(auto q : query)
        if(q.rfind("top") == 0)
            filter += make_operator(q);
    return filter;
}

template<typename T1, typename T2, typename T3>
json::object make_selector(const T1& name, const T2& value, const T3& query)
{
    auto selector = db::object{};
    if(value.empty())                                            // collection/id?$head=10
        selector += {string{name}, make_filter(query)};
    else                                                         // collection/id/123 or collection/id/1,2,3
        selector += make_object(name, value);
    return selector;
}

} // namespace

db::rest::server::server(std::string_view file) :
m_engine{file}, m_hook{m_engine}
{}

void db::rest::server::start(std::string_view service_or_port)
{
    slog << notice << "Starting up at " << service_or_port << flush;
    auto endpoint = net::acceptor{"localhost"s, service_or_port};
    endpoint.timeout(24h);
    slog << info << "Started up at " << endpoint.host() << ":" << endpoint.service_or_port() << flush;

    while(true)
    {
        slog << notice << "Accepting connections at " << endpoint.host() << ":" << endpoint.service_or_port() << flush;
        auto host = ""s, port = ""s;
        auto client = endpoint.accept(host, port);
        slog << info << "Accepted connection from " << host << ":" << port << flush;
        auto worker = thread{[this,&client](){handle(move(client));}};
        sleep_for(1s);
        worker.detach();
        handle(move(client));
    }
}

void db::rest::server::handle(net::endpointstream client)
{
    while(client)
    {
        auto c = client.peek();

        slog << debug << "Reading HTTP request message" << flush;

        slog << debug << "Reading HTTP request line" << flush;

        auto method = ""s, request_uri = ""s, http_version = ""s;
        client >> method >> request_uri >> http_version;
        slog << info << "Received request \"" << method << ' ' << request_uri << ' ' << http_version << "\"" << flush;

        slog << debug << "Read HTTP request line" << flush;

        if(!methods.count(method) || (request_uri == "/" && method != "GET"))
        {
            slog << info << "HTTP " << method << " requests message with URI " << request_uri << " was rejected" << flush;
            client << "HTTP/1.1 400 Bad Request"                                                    << crlf
                   << "Date: " << to_rfc1123(system_clock::now())                                   << crlf
                   << "Server: YarDB/0.2"                                                           << crlf
                   << "Access-Control-Allow-Origin: *"                                              << crlf
                   << "Access-Control-Allow-Methods: OPTIONS, HEAD, GET, POST, PUT, PATCH, DELETE"  << crlf
                   << "Accept: application/json"                                                    << crlf
                   << "Content-Type: application/json"                                              << crlf
                   << "Content-Length: 0"                                                           << crlf
                   << crlf
                   << flush;
            break; // reject with break
        }

        if(request_uri == "/favicon.ico")
        {
            slog << info << "HTTP \"" << method << "\" requests message with URI \"" << request_uri << "\" was ignored" << flush;
            client << "HTTP/1.1 404 Not Found"                    << crlf
                   << "Date: " << to_rfc1123(system_clock::now()) << crlf
                   << "Server: YarDB/0.2"                         << crlf
                   << "Content-Length: 0"                         << crlf
                   << crlf
                   << flush;
            break; // ignore with break
        }

        if(http_version != "HTTP/1.1")
        {
            slog << warning << "HTTP \"" << method << "\" requests message with HTTP version \"" << http_version << "\" was rejected" << flush;
            client << "HTTP/1.1 505 HTTP Version Not Supported"   << crlf
                   << "Date: " << to_rfc1123(system_clock::now()) << crlf
                   << "Server: YarDB/0.2"                         << crlf
                   << "Content-Length: 0"                         << crlf
                   << crlf
                   << flush;
            break; // reject with break
        }

        slog << debug << "Reading HTTP request headers" << flush;

        auto content_type = "text/html"s, authorization = ""s;

        client >> ws; // Skip all whitespaces

        while(client && client.peek() != '\r')
        {
            auto request_header = ""s, value = ""s;
            getline(client, request_header, ':');
            trim(request_header);
            getline(client, value);
            trim(value);
            slog << info << "With request header \"" << request_header << "\": \"" << value << "\"" << flush;

            if(request_header == "Accept")
                content_type = value;

            if(request_header == "Authorization")
                authorization = value;
        }
        // assert(client.get() == '\r');
        // assert(client.get() == '\n');
        client.ignore(2);

        slog << debug << "Read HTTP request headers" << flush;

        slog << debug << "Read HTTP request message" << flush;

        if(method != "OPTIONS" && (authorization.empty() || authorization == "Basic Og=="))
        {
            slog << info << "HTTP \"" << method << "\" requests message with URI \"" << request_uri << "\" was unauthorized" << flush;
            client << "HTTP/1.1 401 Unauthorized status"                       << net::crlf
                   << "Date: " << ext::to_rfc1123(chrono::system_clock::now()) << net::crlf
                   << "Server: net4cpp/1.1"                                    << net::crlf
                   << "WWW-Authenticate: Basic realm=\"User Visible Realm\""   << net::crlf
                   << "Content-Type: " << content_type                         << net::crlf
                   << "Content-Length: 0"                                      << net::crlf
                   << net::crlf << net::flush;
            continue;
        }

        slog << debug << "HTTP \"" << method << "\" request with URI \"" << request_uri << "\" was accepted" << flush;

        auto found = false;
        auto request_body  = db::object{},
             response_body = db::object{};
        request_body.type(xson::type::array);
        response_body.type(xson::type::array);

        try
        {
            // auto[collection,selector] = convert(request_uri);
            auto collection = string_view{};
            auto selector = db::object{};
            std::tie(collection,selector) = convert(request_uri);

            const auto lock = make_lock(m_engine);
            m_engine.collection(string(collection));

            if(collection == "_reindex")
            {
                m_engine.reindex();
                found = true;
            }
            else if(collection.empty())
            {
                response_body = {"collections", m_engine.collections()};
                found = true;
            }
            else if(method == "GET" || method == "HEAD")
            {
                slog << debug << "Reading from collection \"" << collection << "\" with selector "<< json::stringify(selector,0) << flush;
                found = m_engine.read(selector, response_body);
                slog << info << "Read from collection \"" << collection << "\" with selector " << json::stringify(selector,0) << flush;
            }
            else if(method == "DELETE")
            {
                slog << debug << "Deleting from collection \"" << collection <<  "\" with selector " << json::stringify(selector,0) << flush;
                found = m_engine.destroy(selector, response_body);
                slog << info << "Deleted from collection \""  << collection <<  "\" with selector " << json::stringify(selector,0) << flush;
            }
            else if(method == "POST" || method == "PUT" || method == "PATCH")
            {
                slog << debug << "Reading HTTP request body" << flush;
                request_body = json::parse(client);
                slog << debug << "Read HTTP request body " << json::stringify(request_body,0) << flush;
                slog << debug << "Updating collection \"" << collection <<  "\" with selector " << json::stringify(selector,0) << flush;
                if(method == "POST"s)
                {
                    m_hook.pre_post(request_body);
                    found = m_engine.create(request_body);
                }
                else if(method == "PUT"s)
                    found = m_engine.replace(selector, request_body);
                else if(method == "PATCH"s)
                    found = m_engine.upsert(selector, request_body, response_body);
                slog << info << "Updated collection \""  << collection <<  "\" with selector " << json::stringify(selector,0) << flush;
            }
        }
        catch(const std::exception& e)
        {
            slog << warning << "Exception from the DB " << e.what() << flush;
        }

        auto content = ""s;
        if(method == "POST" || method == "PUT")
            content = json::stringify(request_body);
        else if(method != "OPTIONS")
            content = json::stringify(response_body);
        else
            found = true;

        slog << debug << "Sending HTTP response message" << flush;

        if(found)
        {
            // const auto content = json::stringify({"success"s, body});
            client << "HTTP/1.1 200 OK"                                                            << crlf
                   << "Date: " << to_rfc1123(system_clock::now())                                  << crlf
                   << "Server: YarDB/0.2"                                                          << crlf
                   << "Access-Control-Allow-Origin: http://localhost:8080"                         << crlf
                   << "Access-Control-Allow-Methods: OPTIONS, HEAD, GET, POST, PUT, PATCH, DELETE" << crlf
                   << "Access-Control-Allow-Headers: authorization,content-type"                   << crlf
                   << "Access-Control-Allow-Credentials: true"                                     << crlf
                   << "Accept: application/json"                                                   << crlf
                   << "Content-Type: application/json"                                             << crlf
                   << "Content-Length: " << content.length()                                       << crlf
                   << crlf
                   << (method != "HEAD"s ? content : ""s) << flush;
        }
        else
        {
            // const auto content = json::stringify({"error"s, body});
            client << "HTTP/1.1 404 Not Found"                                                     << crlf
                   << "Date: " << to_rfc1123(system_clock::now())                                  << crlf
                   << "Server: YarDB/0.2"                                                          << crlf
                   << "Access-Control-Allow-Origin: http://localhost:8080"                         << crlf
                   << "Access-Control-Allow-Methods: OPTIONS, HEAD, GET, POST, PUT, PATCH, DELETE" << crlf
                   << "Access-Control-Allow-Headers: authorization,content-type"                   << crlf
                   << "Access-Control-Allow-Credentials: true"                                     << crlf
                   << "Accept: application/json"                                                   << crlf
                   << "Content-Type: application/json"                                             << crlf
                   << "Content-Length: " << content.length()                                       << crlf
                   << crlf
                   << (method != "HEAD"s ? content : ""s) << flush;
        }
        slog << debug << "Sent HTTP response message" << flush;
    }
    slog << debug << "Connection was closed" << flush;
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

    if(key == "id"s)                                     // collection/id?$head=10, collection/id/123 or collection/id/1,2,3
        selector = make_selector(u8"_id"s, value, query);

    else if(key.empty() || isnumeric(key) || iscsv(key)) // collection?lte=4?desc, collection/123 or collection/1,2,3
        selector = make_selector(u8"_id"s, key, query);

    else                                                 // collection/field?eq=value?desc or collection/field/value
         selector = make_selector(key, value, query);

    for(auto i = 4; !components.path[i].empty(); i+=2)
         selector += make_selector(components.path[i], components.path[i+1], query);

    selector += make_top(query) += make_order(query);

    return std::make_tuple(collection,selector);
}
