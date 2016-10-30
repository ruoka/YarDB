#pragma once

#include <set>
#include "std/lockable.hpp"
#include "xson/json.hpp"
#include "net/endpointstream.hpp"
#include "db/engine.hpp"

namespace db::rest {

class server
{
public:

    const std::set<std::string> methods = {"HEAD", "GET", "POST", "PUT", "PATCH", "DELETE"};

    server(const std::string& file = "./yar.db"s);

    void start(const std::string& serice_or_port = "http"s);

protected:

    virtual std::tuple<std::string_view,xson::json::object> convert(std::string_view request_uri);

private:

    void handle(net::endpointstream client);

    ext::lockable<db::engine> m_engine;
};

} // namespace db::rest
