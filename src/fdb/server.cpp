#include <iostream>
#include "fdb/messages.hpp"
#include "fdb/server.hpp"

fdb::server::server(fdb::engine& e) : m_acceptor{"localhost", "50888"}, m_engine{e}
{}

void fdb::server::start()
{
    auto client = m_acceptor.accept();
    while(client)
    {
        fdb::header header;
        client >> header;
        m_engine.collection(header.collection);
        switch (header.operaion) {
            case header::create:
            handle_create(client);
            break;
            case header::read:
            handle_read(client);
            break;
            case header::update:
            handle_update(client);
            break;
            case header::destroy:
            handle_destroy(client);
            break;
            default:
            handle_error(client);
            break;
        }
    }
}

void fdb::server::handle_create(net::endpointstream& client)
{
    std::clog << "fdb::server::handle_create" << std::endl;
    fdb::create request;
    client >> request;
    if(client) {
        m_engine.create(request.document);
        fdb::reply reply;
        reply.document = {"ack", true};
        client << reply << net::flush;
    }
}

void fdb::server::handle_read(net::endpointstream& client)
{
    std::clog << "fdb::server::handle_read" << std::endl;
    fdb::read request;
    client >> request;
    if(client) {
        std::vector<object> result;
        m_engine.read(request.selector, result);
        fdb::reply reply;
        reply.document = {"result", result};
        client << reply << net::flush;
    }
}

void fdb::server::handle_update(net::endpointstream& client)
{
    std::clog << "fdb::server::handle_update" << std::endl;
    fdb::update request;
    client >> request;
    if(client) {
        m_engine.update(request.selector, request.document);
        fdb::reply reply;
        reply.document = {"ack", true};
        client << reply << net::flush;
    }
}

void fdb::server::handle_destroy(net::endpointstream& client)
{
    std::clog << "fdb::server::handle_destroy" << std::endl;
    fdb::destroy request;
    client >> request;
    if(client) {
        m_engine.destroy(request.selector);
        fdb::reply reply;
        reply.document = {"ack", true};
        client << reply << net::flush;
    }
}

void fdb::server::handle_error(net::endpointstream& client)
{
    std::clog << "fdb::server::handle_error" << std::endl;
}
