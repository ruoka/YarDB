#include "fdb/metadata.hpp"
#include "fdb/engine.hpp"

using namespace std::string_literals;

fdb::engine::engine() : m_storage{}
{
    m_storage.open("fdb.fson", std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    if (!m_storage.is_open())
        throw "Perkele!";

    while(m_storage)
    {
        fdb::metadata metadata;
        fdb::object document;
        m_storage >> metadata >> document;
        if(m_storage && !metadata.deleted)
            m_index[metadata.index] = metadata.position;
    }
}

bool fdb::engine::create(fdb::object& document)
{
    if(!document.has("_id"))
        document["_id"s].value((std::int64_t)m_index.size()); // FIXME

    m_storage.clear();
    m_storage.seekp(0, m_storage.end);

    fdb::metadata metadata;
    metadata.index = document["_id"s];
    metadata.position = m_storage.tellp();

    m_storage << metadata << document << std::flush;
    if(m_storage)
        m_index[metadata.index] = metadata.position;

    return true;
}

bool fdb::engine::read(fdb::object& selector, std::vector<fdb::object>& result) const
{
    m_storage.clear();

    if(selector.has("_id"s)) // Can we use index?
    {
        for(auto itr = m_index.find(selector["_id"s]); itr != m_index.cend(); itr = m_index.cend())
        {
            const auto position = itr->second;
            m_storage.seekp(position, m_storage.beg);

            fdb::metadata metadata; fdb::object document;
            m_storage >> metadata >> document;
            if(m_storage)
                result.emplace_back(document);
        }
    }
    else // Fallback to full table scan :-(
    {
        for(auto itr = m_index.cbegin(); itr != m_index.cend(); ++itr)
        {
            const auto position = itr->second;
            m_storage.seekp(position, m_storage.beg);

            fdb::metadata metadata; fdb::object document;
            m_storage >> metadata >> document;
            if(m_storage)
                result.emplace_back(document);
        }
    }
    return true;
}

bool fdb::engine::update(fdb::object& selector, fdb::object& document)
{
    if(selector.has("_id"s)) // Can we use index?
    {
        destroy(selector);
        create(document);
        return true;
    }
    return false;
}

bool fdb::engine::destroy(fdb::object& selector)
{
    if(selector.has("_id"s)) // Can we use index?
    {
        for(auto itr = m_index.find(selector["_id"s]); itr != m_index.end(); itr = m_index.end())
        {
            const auto position = itr->second;
            m_storage.seekp(position, m_storage.beg);

            fdb::metadata metadata;
            metadata.deleted = true;
            m_storage << metadata;

            if(m_storage)
                itr = m_index.erase(itr);

            return true;
        }
    }
    return false;
}
