#include <iostream>
#include "db/engine.hpp"
#include "db/messages.hpp"
#include "db/server.hpp"

db::server::server(db::engine& e, const std::string& service_or_port) :
m_acceptor{"localhost", service_or_port}, m_engine{e}
{}

void db::server::start() // FIXME Support multiple connections!
{
    auto client = m_acceptor.accept();
    while(client)
    {
        auto header = msg::header{};
        client >> header;
        switch (header.operaion) {
            case msg::header::create:
            handle_create(client);
            break;
            case msg::header::read:
            handle_read(client);
            break;
            case msg::header::update:
            handle_update(client);
            break;
            case msg::header::destroy:
            handle_destroy(client);
            break;
            default:
            handle_error(client);
            break;
        }
    }
}

void db::server::handle_create(net::endpointstream& client)
{
    std::clog << "db::server::handle_create" << std::endl;
    auto request = msg::create{};
    client >> request;
    if(client) {
        auto reply = msg::reply{};
        m_engine.create(request.document);
        reply.document = {u8"ack"s, true};
        client << reply << net::flush;
    }
}

void db::server::handle_read(net::endpointstream& client)
{
    std::clog << "db::server::handle_read" << std::endl;
    auto request = msg::read{};
    client >> request;
    if(client) {
        auto result = std::vector<db::object>{};
        auto reply = msg::reply{};
        m_engine.read(request.selector, result);
        reply.document = {u8"result"s, result};
        client << reply << net::flush;
    }
}

void db::server::handle_update(net::endpointstream& client)
{
    std::clog << "db::server::handle_update" << std::endl;
    auto request = msg::update{};
    client >> request;
    if(client) {
        auto reply = msg::reply{};
        m_engine.update(request.selector, request.document);
        reply.document = {u8"ack"s, true};
        client << reply << net::flush;
    }
}

void db::server::handle_destroy(net::endpointstream& client)
{
    std::clog << "db::server::handle_destroy" << std::endl;
    auto request = msg::destroy{};
    client >> request;
    if(client) {
        auto reply = msg::reply{};
        m_engine.destroy(request.selector);
        reply.document = {u8"ack"s, true};
        client << reply << net::flush;
    }
}

void db::server::handle_error(net::endpointstream& client)
{
    std::clog << "db::server::handle_error" << std::endl;
}
