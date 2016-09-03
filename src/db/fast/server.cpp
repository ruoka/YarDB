#include <iostream>
#include "db/engine.hpp"
#include "db/fast/messages.hpp"
#include "db/fast/server.hpp"

namespace db::fast {

server::server(db::engine& engine, const std::string& service_or_port) :
m_engine{engine}, m_acceptor{"localhost", service_or_port}
{}

void server::start() // FIXME Support multiple connections!
{
    auto client = m_acceptor.accept();
    while(client)
    {
        auto header = fast::header{};
        client >> header;
        switch (header.operaion) {
            case fast::header::create:
            handle_create(client);
            break;
            case fast::header::read:
            handle_read(client);
            break;
            case fast::header::update:
            handle_update(client);
            break;
            case fast::header::destroy:
            handle_destroy(client);
            break;
            default:
            handle_error(client);
            break;
        }
    }
}

void server::handle_create(net::endpointstream& client)
{
    std::clog << "db::fast::server::handle_create" << std::endl;
    auto request = fast::create{};
    client >> request;
    if(client) {
        auto reply = fast::reply{};
        m_engine.create(request.document);
        reply.document = {u8"ack"s, true};
        client << reply << net::flush;
    }
}

void server::handle_read(net::endpointstream& client)
{
    std::clog << "db::fast::server::handle_read" << std::endl;
    auto request = fast::read{};
    client >> request;
    if(client) {
        auto result = db::object{};
        auto reply = fast::reply{};
        m_engine.read(request.selector, result);
        reply.document = {u8"result"s, result};
        client << reply << net::flush;
    }
}

void server::handle_update(net::endpointstream& client)
{
    std::clog << "db::fast::server::handle_update" << std::endl;
    auto request = fast::update{};
    client >> request;
    if(client) {
        auto reply = fast::reply{};
        m_engine.update(request.selector, request.document);
        reply.document = {u8"ack"s, true};
        client << reply << net::flush;
    }
}

void server::handle_destroy(net::endpointstream& client)
{
    std::clog << "db::fast::server::handle_destroy" << std::endl;
    auto request = fast::destroy{};
    client >> request;
    if(client) {
        auto reply = fast::reply{};
        m_engine.destroy(request.selector);
        reply.document = {u8"ack"s, true};
        client << reply << net::flush;
    }
}

void server::handle_error(net::endpointstream& client)
{
    std::clog << "db::fast::server::handle_error" << std::endl;
}

} // db::fast
