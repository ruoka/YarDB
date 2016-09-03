#pragma once

#include "net/acceptor.hpp"

namespace db {

    class engine;

namespace fast {

    class server
    {
    public:

        server(db::engine& engine, const std::string& service_or_port);

        void start();

    private:

        void handle_create(net::endpointstream& client);

        void handle_read(net::endpointstream& client);

        void handle_update(net::endpointstream& client);

        void handle_destroy(net::endpointstream& client);

        void handle_error(net::endpointstream& client);

        db::engine& m_engine;

        net::acceptor m_acceptor;
    };

} // namespace fast
} // namespace db
