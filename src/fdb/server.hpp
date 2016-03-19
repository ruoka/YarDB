#pragma once

#include <iostream>
#include <fstream>
#include "net/acceptor.hpp"
#include "fdb/messages.hpp"

namespace fdb {

    class server
    {
    public:
        server() : m_data{}, m_acceptor{"localhost", "50888"}
        {
            m_data.open("fdb.fson", std::ios_base::out | std::ios_base::in | std::ios_base::binary);
            if (!m_data.is_open())
                throw "Perkele!";
        }
        void start()
        {
            auto client = m_acceptor.accept();

            while(client)
            {
                header hdr;
                client >> hdr;

                switch (hdr.operaion) {
                case header::create:
                {
                    std::clog << "header::create" << std::endl;
                    m_data.clear();
                    m_data.seekp(0, m_data.end);
                    create crt;
                    client >> crt;
                    if(client)
                        m_data << crt.document << std::flush;
                }
                break;
                case header::query:
                {
                    std::clog << "header::query" << std::endl;
                    std::vector<object> result;
                    m_data.clear();
                    m_data.seekp(0, m_data.beg);
                    while(m_data)
                    {
                        object document;
                        m_data >> document;
                        if(m_data)
                            result.emplace_back(document);
                    }
                    reply rpl;
                    rpl.document = {"result", result};
                    client << rpl << std::flush;
                }
                break;
                default:
                {
                    std::clog << "header::?" << std::endl;
                }
                break;
                }
            }
        }
    private:
        std::fstream m_data;
        net::acceptor m_acceptor;
    };

} // namespace fdb
