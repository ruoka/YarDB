#include <iostream>
#include "xson/json.hpp"
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

    auto metadata = db::metadata{};

    if(document.has(u8"_id"s))
        metadata.index = document[u8"_id"s]; // FIXME Validate if key already exists
    else
        metadata.index = document[u8"_id"s] = m_index.sequence();

    m_storage.seekp(0, m_storage.end);
    metadata.position = m_storage.tellp();
    m_index[metadata.index] = metadata.position;

    m_storage << metadata << document << std::flush;
}

void db::engine::read(const db::object& selector, std::vector<db::object>& results)
{
    m_storage.clear();

    for(const auto position : m_index.range(selector))
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        if(document.has(selector))
            results.emplace_back(document);
    }
}

void db::engine::update(const db::object& selector, const db::object& changes, bool replace)
{
    m_storage.clear();

    for(const auto position : m_index.range(selector))
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;

        if(document.has(selector))
        {
            m_storage.seekp(position, m_storage.beg);
            m_storage << destroyed;

            metadata.index = document[u8"_id"s];
            m_storage.seekp(0, m_storage.end);
            metadata.position = m_storage.tellp();
            metadata.previous = position;
            m_index[metadata.index] = metadata.position;

            auto new_document = changes;
            new_document[u8"_id"s] = metadata.index;
            if(replace)
                m_storage << metadata << new_document << std::flush;
            else
                m_storage << metadata << new_document + document << std::flush;
        }
    }
}

void db::engine::destroy(const db::object& selector)
{
    m_storage.clear();

    const auto range = m_index.range(selector);
    auto itr = range.begin();

    if(m_index.seek(selector))
        while(itr != range.end())
        {
            const auto position = *itr;

            m_storage.seekp(position, m_storage.beg);
            m_storage << destroyed;
            itr = m_index.erase(itr);
        }

    else if(m_index.scan(selector))
        while(itr != range.end())
        {
            const auto position = *itr;

            auto metadata = db::metadata{};
            auto document = db::object{};
            m_storage.seekg(position, m_storage.beg);
            m_storage >> metadata >> document;

            if(document.has(selector))
            {
                m_storage.seekp(position, m_storage.beg);
                m_storage << destroyed;
                itr = m_index.erase(itr);
            }
            else
                ++itr;
        }
}

void db::engine::history(const db::object& selector, std::vector<db::object>& results)
{
    if(!m_index.primary_key(selector))
        throw std::invalid_argument("Cannot find primary key in "s + xson::json::stringify(selector));

    m_storage.clear();

    auto position = m_index[selector[u8"_id"s]];

    while(position >= 0)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        results.emplace_back(document);
        position = metadata.previous;
    }
}
