#include "fdb/metadata.hpp"
#include "fdb/engine.hpp"

fdb::engine::engine() : m_storage{}
{
    m_storage.open("fdb.fson", std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    if (!m_storage.is_open())
        throw "Perkele!";
}

bool fdb::engine::create(const fdb::object& document)
{
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    fdb::metadata metadata;
    m_storage << metadata << document << std::flush;
    return true;
}

bool fdb::engine::read(const fdb::object& selector, std::vector<fdb::object>& result)
{
    m_storage.clear();
    m_storage.seekp(0, m_storage.beg);
    while(m_storage)
    {
        fdb::metadata metadata;
        fdb::object document;
        m_storage >> metadata >> document;
        if(m_storage)
            result.emplace_back(document);
    }
    return true;
}

bool fdb::engine::update(const fdb::object& selector, const fdb::object& document)
{
    return true;
}

bool fdb::engine::destroy(const fdb::object& selector)
{
    return true;
}
