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
        auto header = fdb::header{};
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
    auto request = fdb::create{};
    client >> request;
    if(client) {
        auto reply = fdb::reply{};
        m_engine.create(request.document);
        reply.document = {"ack", true};
        client << reply << net::flush;
    }
}

void fdb::server::handle_read(net::endpointstream& client)
{
    std::clog << "fdb::server::handle_read" << std::endl;
    auto request = fdb::read{};
    client >> request;
    if(client) {
        auto result = std::vector<object>{};
        auto reply = fdb::reply{};
        m_engine.read(request.selector, result);
        reply.document = {"result", result};
        client << reply << net::flush;
    }
}

void fdb::server::handle_update(net::endpointstream& client)
{
    std::clog << "fdb::server::handle_update" << std::endl;
    auto request = fdb::update{};
    client >> request;
    if(client) {
        auto reply = fdb::reply{};
        m_engine.update(request.selector, request.document);
        reply.document = {"ack", true};
        client << reply << net::flush;
    }
}

void fdb::server::handle_destroy(net::endpointstream& client)
{
    std::clog << "fdb::server::handle_destroy" << std::endl;
    auto request = fdb::destroy{};
    client >> request;
    if(client) {
        auto reply = fdb::reply{};
        m_engine.destroy(request.selector);
        reply.document = {"ack", true};
        client << reply << net::flush;
    }
}

void fdb::server::handle_error(net::endpointstream& client)
{
    std::clog << "fdb::server::handle_error" << std::endl;
}
