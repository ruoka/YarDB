#include <iostream>
#include "fdb/metadata.hpp"
#include "fdb/engine.hpp"

using namespace std::string_literals;

fdb::engine::engine() : m_storage{}
{
    m_storage.open("fdb.fson", std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    if (!m_storage.is_open())
        throw "Perkele!";

    m_storage.clear();
    while(m_storage)
    {
        fdb::metadata metadata;
        fdb::object document;
        m_storage >> metadata >> document;
        if(m_storage && metadata.valid)
        {
            std::clog << metadata.valid << ':' << metadata.index << ':' << metadata.position << std::endl;
            m_index[metadata.index] = metadata.position;
        }
    }
}

void fdb::engine::create(fdb::object& document)
{
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    fdb::metadata metadata{true};
    metadata.index = document["_id"s].value(m_index.sequence());
    metadata.position = m_storage.tellp();
    m_storage << metadata << document << std::flush;
    m_index[metadata.index] = metadata.position;
}

void fdb::engine::read(fdb::object& selector, std::vector<fdb::object>& result)
{
    for(const auto position : m_index.range(selector))
    {
        m_storage.clear();
        m_storage.seekp(position, m_storage.beg);
        fdb::metadata metadata;
        fdb::object document;
        m_storage >> metadata >> document;
        result.emplace_back(document);
    }
}

void fdb::engine::update(fdb::object& selector, fdb::object& changes)
{
    std::vector<fdb::object> array;
    read(selector, array);
    destroy(selector);
    for(auto document : array)
        create(document + changes);
}

void fdb::engine::destroy(fdb::object& selector)
{
    auto range = m_index.range(selector);
    for(auto itr = range.begin(); itr != range.end(); ++itr)
    {
        const auto position = *itr;
        m_storage.clear();
        m_storage.seekp(position, m_storage.beg);
        fdb::metadata metadata{false};
        m_storage << metadata;
        range.destroy(itr);
    }
}
