#include "fdb/metadata.hpp"
#include "fdb/engine.hpp"

using namespace std::string_literals;

fdb::engine::engine() : m_storage{}
{
    m_storage.open("fdb.fson", std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    if (!m_storage.is_open())
        throw "Perkele!";
}

bool fdb::engine::create(fdb::object& document)
{
    if(!document.has("_id"))
        document["_id"s].value(++s_sequence); // FIXME: Use someting else than static!

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

bool fdb::engine::read(fdb::object& selector, std::vector<fdb::object>& result)
{
    m_storage.clear();

    if(selector.has("_id")) // Can we use index?
    {
        std::streamoff position{m_index[selector["_id"s]]};
        m_storage.seekp(position, m_storage.beg);
        fdb::metadata metadata;
        fdb::object document;
        m_storage >> metadata >> document;
        if(m_storage)
            result.emplace_back(document);
    }
    else // Fallback to full table scan :-(
    {
        std::streamoff position{0};
        m_storage.seekp(position, m_storage.beg);
        while(m_storage)
        {
            fdb::metadata metadata;
            fdb::object document;
            m_storage >> metadata >> document;
            if(m_storage && !metadata.deleted) // FIXME: Implement proper filtering!
                result.emplace_back(document);
        }
    }
    return true;
}

bool fdb::engine::update(fdb::object& selector, fdb::object& document)
{
    return true;
}

bool fdb::engine::destroy(fdb::object& selector)
{
    return true;
}
