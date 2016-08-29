#include <iostream>
#include "xson/json.hpp"
#include "db/metadata.hpp"
#include "db/engine.hpp"

using namespace std::string_literals;

db::engine::engine(const std::string file) : m_sequence{0}, m_index{}, m_storage{}
{
    m_storage.open(file, std::ios_base::out | std::ios_base::in | std::ios_base::binary);

    if (!m_storage.is_open())
        throw std::invalid_argument("Cannot open file "s + file);

    rebuild_indexes({u8"_id"s});
}

void db::engine::rebuild_indexes(std::initializer_list<std::string> keys)
{
    m_storage.clear();
    m_storage.seekg(0, m_storage.beg);

    for(auto& key : keys)
        m_index.add(key);

    while(m_storage)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage >> metadata >> document;
        if(m_storage && metadata.valid())
        {
            m_index.insert(document[u8"_id"s], metadata.position, document);
            m_sequence = std::max(m_sequence, metadata.id);
        }
    }
}

void db::engine::create(db::object& document)
{
    m_storage.clear();

    auto metadata = db::metadata{};
    if(document.has(u8"_id"s))
        metadata.id = document[u8"_id"s];
    else
        metadata.id = document[u8"_id"s] = ++m_sequence;
    m_storage.seekp(0, m_storage.end);
    metadata.position = m_storage.tellp();
    m_storage << metadata << document << std::flush;
    m_index.insert(document[u8"_id"s], metadata.position, document);
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
        if(document.match(selector))
            results.emplace_back(document);
    }
}

void db::engine::update(const db::object& selector, const db::object& changes)
{
    m_storage.clear();

    for(const auto position : m_index.range(selector))
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;

        if(document.match(selector))
        {
            m_storage.seekp(0, m_storage.end);
            metadata.position = m_storage.tellp();
            metadata.previous = position;

            auto new_document = changes;
            new_document = new_document + document;

            m_storage << metadata << new_document << std::flush;
            m_index.insert(document[u8"_id"s], metadata.position, new_document);

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
        m_index.erase(document[u8"_id"s], document);

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
