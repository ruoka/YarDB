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

    const std::set<std::string_view> methods = {"HEAD", "GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};

    server(std::string_view file = "./yar.db"s);

    void start(std::string_view serice_or_port = "http"s);

protected:

    virtual std::tuple<std::string_view,xson::json::object> convert(std::string_view request_uri);

private:

    void handle(net::endpointstream client);

    ext::lockable<db::engine> m_engine;
};

} // namespace db::rest
