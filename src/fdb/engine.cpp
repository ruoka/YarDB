#include <iostream>
#include "fdb/metadata.hpp"
#include "fdb/engine.hpp"

using namespace std::string_literals;

fdb::engine::engine(const std::string file) : m_storage{}
{
    m_storage.open(file, std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    if (!m_storage.is_open())
        throw "Perkele!";

    rebuild_indexes();
}

void fdb::engine::rebuild_indexes()
{
    m_storage.clear();
    m_storage.seekg(0, m_storage.beg);
    while(m_storage)
    {
        fdb::metadata metadata;
        fdb::object document;
        m_storage >> metadata >> document;
        if(m_storage && metadata.valid)
        {
            m_index[metadata.index] = metadata.position;
        }
        std::clog << metadata.valid << ':' << metadata.index << ':' << metadata.position << std::endl;
    }
}

void fdb::engine::create(fdb::object& document)
{
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    fdb::metadata metadata{true};
    if(document.has("_id"s))
        metadata.index = document["_id"s]; // Validate if key already exists
    else
        metadata.index = document["_id"s].value(m_index.sequence());
    metadata.position = m_storage.tellp();
    m_storage << metadata << document << std::flush;
    m_index[metadata.index] = metadata.position;
}

void fdb::engine::read(fdb::object& selector, std::vector<fdb::object>& result)
{
    m_storage.clear();
    for(const auto position : m_index.range(selector))
    {
        m_storage.seekg(position, m_storage.beg);
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
        create(document);
}

void fdb::engine::destroy(fdb::object& selector)
{
    m_storage.clear();
    auto range = m_index.range(selector);
    for(auto itr = range.begin(); itr != range.end(); itr = range.destroy(itr))
    {
        const auto position = *itr;
        m_storage.seekp(position, m_storage.beg);
        fdb::metadata metadata{false};
        m_storage << metadata;
    }
}
