#include <iostream>
#include "db/metadata.hpp"
#include "db/engine.hpp"

using namespace std::string_literals;

db::engine::engine(const std::string file) : m_storage{}
{
    m_storage.open(file, std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    if (!m_storage.is_open())
        throw std::invalid_argument("Cannot open file "s + file);
    rebuild_indexes();
}

void db::engine::rebuild_indexes()
{
    m_storage.clear();
    m_storage.seekg(0, m_storage.beg);
    while(m_storage)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage >> metadata >> document;
        if(m_storage && metadata.valid)
        {
            m_index[metadata.index] = metadata.position;
            // Debug dump
            std::clog << metadata.valid << ':' << metadata.index << ':' << metadata.position << std::endl;
        }
    }
}

void db::engine::create(db::object& document)
{
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);

    auto metadata = db::metadata{true};
    if(document.has(u8"_id"s))
        metadata.index = document[u8"_id"s]; // FIXME Validate if key already exists
    else
        metadata.index = document[u8"_id"s].value(m_index.sequence());

    metadata.position = m_storage.tellp();
    m_index[metadata.index] = metadata.position;
    m_storage << metadata << document << std::flush;
}

void db::engine::read(const db::object& selector, std::vector<db::object>& result)
{
    m_storage.clear();

    auto seek = m_index.seek(selector);
    for(const auto position : m_index.range(selector))
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        if(seek || document.has(selector))
            result.emplace_back(document);
    }
}

void db::engine::update(const db::object& selector, const db::object& changes, bool replace)
{
    auto array = std::vector<db::object>{};
    read(selector, array);
    for(const auto& document : array)
    {
        destroy(document);
        auto updates = changes;
        updates[u8"_id"s] = document[u8"_id"s];
        if(replace)
            create(updates);
        else
            create(updates + document);
    }
}

void db::engine::destroy(const db::object& selector)
{
    m_storage.clear();

    const auto range = m_index.range(selector);

    if(m_index.seek(selector))
        for(auto itr = range.begin(); itr != range.end(); itr = m_index.erase(itr))
        {
            const auto position = *itr;
            const auto metadata = db::metadata{false};
            m_storage.seekp(position, m_storage.beg);
            m_storage << metadata;
        }

    if(m_index.scan(selector))
        for(auto itr = range.begin(); itr != range.end();)
        {
            const auto position = *itr;

            auto metadata = db::metadata{};
            auto document = db::object{};
            m_storage.seekg(position, m_storage.beg);
            m_storage >> metadata >> document;

            if(document.has(selector))
            {
                m_storage.seekp(position, m_storage.beg);
                m_storage << db::metadata{false};
                itr = m_index.erase(itr);
            }
            else
                ++itr;
        }
}
