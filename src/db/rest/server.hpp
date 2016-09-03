#include <thread>
#include "std/extension.hpp"
#include "xson/json.hpp"
#include "net/syslogstream.hpp"
#include "net/acceptor.hpp"
#include "db/engine.hpp"

namespace db::rest {

using namespace std;
using namespace std::chrono;
using namespace std::string_literals;
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
            worker.detach();
        }
    }

private:

    void handle(endpointstream connection)
    {
        while(connection)
        {
            slog << debug << "Reading HTTP request" << flush;
            auto method = ""s, path = ""s, protocol = ""s;
            connection >> method >> path >> protocol >> ws;
            slog << info << method << ' ' << path << ' ' << protocol << flush;

            if(protocol.empty())
            {
                slog << debug << "Connection closed" << flush;
                break;
            }

            slog << debug << "Read HTTP request" << flush;

            slog << debug << "Reading HTTP headers" << flush;
            while(connection && connection.peek() != '\r')
            {
                auto name = ""s, value = ""s;
                connection >> name >> ws;
                getline(connection, value);
                slog << info << name << ' ' << value << flush;
            }
            slog << debug << "Read HTTP headers" << flush;

            if(path == R"(/)"s || path == R"(/favicon.ico)"s)
            {
                slog << debug << "Path " << path << " is ignored" << flush;
                connection << "HTTP/1.1 404 Not Found"s;
                continue;
            }

            const auto uri = parse(path);
            const auto collection = get<0>(uri);
            const auto selector = get<1>(uri);

            m_engine.collection(collection);
            auto body = json::object{};

            if(method == "GET"s || method == "HEAD"s)
            {
                m_engine.read(selector, body);
            }
            else
            {
                slog << debug << "Reading content" << flush;
                body = json::parse(connection);
                slog << debug << "Read content" << flush;
                if(method == "POST"s)
                    m_engine.create(body);
                else if(method == "PUT"s)
                    m_engine.replace(selector, body);
                else if(method == "PATCH"s)
                    m_engine.upsert(selector, body);
                else if(method == "DELETE"s)
                    m_engine.destroy(selector);
            }

            const auto content = json::stringify({"data"s, body});

            slog << debug << "Sending HTTP response" << flush;

            if(content.length() > 0)
                connection << "HTTP/1.1 200 OK\r\n"
                       << "Date: " << to_rfc1123(system_clock::now()) << "\r\n"
                       << "Server: http://localhost:2112" << path << "\r\n" // FIXME
                       << "Access-Control-Allow-Origin: *\r\n"
                       << "Access-Control-Allow-Methods: HEAD, GET, POST, PUT, PATCH, DELETE\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << content.length() << "\r\n"
                       << "\r\n"
                       << (method != "HEAD"s ? content : ""s);
            else
                connection << "HTTP/1.1 404 Not Found"s;

            connection << flush;

            slog << debug << "Sent HTTP response" << flush;
        }
    }

    tuple<string,json::object> parse(const string& path)
    {
        auto ss = stringstream{path};
        auto slash = ""s, collection = ""s, selector = ""s;
        getline(ss, slash, '/');
        getline(ss, collection, '/');
        getline(ss, selector);
//        return std::make_tuple(collection,json::object{u8"_id"s, selector});
        return std::make_tuple(collection,json::object{});
    }

    db::engine& m_engine;
};

} // namespace db::rest
