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

    m_index.create("A"s);

    while(m_storage)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage >> metadata >> document;
        if(m_storage && metadata.valid())
            m_index.insert(metadata.id, metadata.position, document);
    }
}

void db::engine::create(db::object& document)
{
    m_storage.clear();

    auto metadata = db::metadata{};

    if(document.has(u8"_id"s))
        metadata.id = document[u8"_id"s]; // FIXME Validate if key already exists
    else
        metadata.id = document[u8"_id"s] = m_index.sequence();

    m_storage.seekp(0, m_storage.end);
    metadata.position = m_storage.tellp();

    m_storage << metadata << document << std::flush;
    m_index.insert(metadata.id, metadata.position, document);
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

    if(m_index.primary_key(selector) && replace) // Fast track
        for(const auto position : m_index.range(selector))
        {
            auto metadata = db::metadata{};
            metadata.id = selector[u8"_id"s];
            m_storage.seekp(0, m_storage.end);
            metadata.position = m_storage.tellp();
            metadata.previous = position;

            auto new_document = changes;
            new_document[u8"_id"s] = metadata.id;

            m_storage << metadata << new_document << std::flush;
            m_index.insert(metadata.id, metadata.position, new_document);

            m_storage.seekp(position, m_storage.beg);
            m_storage << updated;
        }
    else
        for(const auto position : m_index.range(selector))
        {
            auto metadata = db::metadata{};
            auto document = db::object{};
            m_storage.seekg(position, m_storage.beg);
            m_storage >> metadata >> document;

            if(document.has(selector))
            {
                metadata.id = document[u8"_id"s];
                m_storage.seekp(0, m_storage.end);
                metadata.position = m_storage.tellp();
                metadata.previous = position;

                auto new_document = changes;
                new_document[u8"_id"s] = metadata.id;

                if(!replace)
                    new_document = new_document + document;

                m_storage << metadata << new_document << std::flush;
                m_index.insert(metadata.id, metadata.position, new_document);

                m_storage.seekp(position, m_storage.beg);
                m_storage << updated;
            }
        }
}

void db::engine::destroy(const db::object& selector)
{
    if(!m_index.primary_key(selector))
        throw std::invalid_argument("Expected primary key got "s + xson::json::stringify(selector, 0));

    m_storage.clear();

    const auto range = m_index.range(selector);
    if(range.begin() != range.end())
    {
        auto position = *range.begin();
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        m_index.erase(metadata.id, metadata.position, document);

        m_storage.seekp(position, m_storage.beg);
        m_storage << deleted;
    }
}

void db::engine::history(const db::object& selector, std::vector<db::object>& results)
{
    if(!m_index.primary_key(selector))
        throw std::invalid_argument("Expected primary key got "s + xson::json::stringify(selector, 0));

    m_storage.clear();

    const auto range = m_index.range(selector);
    if(range.begin() != range.end())
    {
        auto position = *range.begin();
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
}
