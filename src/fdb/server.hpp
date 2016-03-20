#pragma once

#include "net/acceptor.hpp"
#include "fdb/engine.hpp"

namespace fdb {

    class server
    {
    public:

        explicit server(fdb::engine&);

        void start();

    private:

        void handle_create(net::endpointstream& client);

        void handle_read(net::endpointstream& client);

        void handle_update(net::endpointstream& client);

        void handle_destroy(net::endpointstream& client);

        void handle_error(net::endpointstream& client);

        net::acceptor m_acceptor;

        fdb::engine& m_engine;
    };

} // namespace fdb
