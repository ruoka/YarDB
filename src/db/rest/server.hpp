#include <mutex>
#include <set>
#include "xson/json.hpp"
#include "net/endpointstream.hpp"
#include "db/engine.hpp"

namespace db::rest {

using string_view = std::experimental::string_view;

class server
{
public:

    const std::set<std::string> methods = {"HEAD", "GET", "POST", "PUT", "PATCH", "DELETE"};

    server(db::engine& engine);

    server(server&&);

    void start(const std::string& serice_or_port = "http"s);

protected:

    virtual std::tuple<string_view,xson::json::object> convert(string_view request_uri);

private:

    void handle(net::endpointstream client);

    db::engine& m_engine;

    std::mutex m_mutex;
};

} // namespace db::rest
