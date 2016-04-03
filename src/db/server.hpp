#pragma once

#include "net/acceptor.hpp"
#include "db/engine.hpp"

namespace db {

    class server
    {
    public:

        explicit server(db::engine& engine, const std::string& service_or_port);

        void start();

    private:

        void handle_create(net::endpointstream& client);

        void handle_read(net::endpointstream& client);

        void handle_update(net::endpointstream& client);

        void handle_destroy(net::endpointstream& client);

        void handle_error(net::endpointstream& client);

        net::acceptor m_acceptor;

        db::engine& m_engine;
    };

} // namespace db
