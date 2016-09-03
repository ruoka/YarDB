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
        auto endpoint = acceptor{"localhost"s, serice_or_port};
        endpoint.timeout(1h);
        while(true)
        {
            auto connection = endpoint.accept();
            auto worker = thread{[&](){handle(move(connection));}};
            worker.detach();
        }
    }

private:

    void handle(endpointstream connection)
    {
        while(connection)
        {
            auto method = ""s, path = ""s, option = ""s;
            connection >> method >> path;
            slog << debug << method << ' ' << path << ' ' << flush;

            while(connection && option.length() > 2)
            {
                getline(connection, option);
                slog << debug << option << flush;
            }

            auto body = json::object{};
            connection >> body;
            slog << debug << body << flush;

            const auto content = render(method, path, body);

            if(content.length() > 0)
                connection << "HTTP/1.1 200 OK\r\n"
                       << "Date: " << to_rfc1123(system_clock::now()) << "\r\n"
                       << "Server: http://localhost:8080" << path << "\r\n" // FIXME
                       << "Access-Control-Allow-Origin: *\r\n"
                       << "Access-Control-Allow-Methods: POST, GET, PUT, PATCH, DELETE\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << content.length() << "\r\n"
                       << "\r\n"
                       << content;
            else
                connection << "HTTP/1.1 404 Not Found"s;

            connection.flush();
        }
    }

    string render(const string& method, const string& path, json::object& body)
    {
        const auto uri = parse(path);
        const auto collection = get<0>(uri);
        const auto selector = get<1>(uri);

        m_engine.collection(collection);

        if(method == "PUSH"s)
            m_engine.create(body);
        else if(method == "GET"s)
            m_engine.read(selector, body);
        else if(method == "PUT"s)
            m_engine.replace(body, body);
        else if(method == "PATCH"s)
            m_engine.upsert(body, body);
        else if(method == "DELETE"s)
            m_engine.destroy(body);

        return json::stringify(body);
    }

    tuple<string,json::object> parse(const string& path)
    {
        auto ss = stringstream{path};
        auto collection = ""s, selector = ""s;
        getline(ss, collection, '/');
        getline(ss, selector);
        return std::make_tuple(collection,json::object{u8"_id"s, selector});
    }

    db::engine& m_engine;
};

} // namespace db::rest
