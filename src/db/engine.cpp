#include <iostream>
#include "xson/json.hpp"
#include "db/metadata.hpp"
#include "db/engine.hpp"

using namespace std::string_literals;

db::engine::engine(const std::string file) : m_index{}, m_storage{}
{
    m_storage.open(file, std::ios_base::out | std::ios_base::in | std::ios_base::binary);

    if (!m_storage.is_open())
        throw std::invalid_argument("Cannot open file "s + file);

    rebuild_indexes();
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
            m_index.insert(document, metadata.position);
    }
}

void db::engine::create(db::object& document)
{
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    m_index.insert(document, m_storage.tellp());
    auto metadata = db::metadata{};
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

            auto new_document = changes;
            new_document = new_document + document;

            m_index.insert(new_document, m_storage.tellp());
            m_storage << metadata << new_document << std::flush;

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

    auto position = m_index.position(selector);
    auto metadata = db::metadata{};
    auto document = db::object{};
    m_storage.seekg(position, m_storage.beg);
    m_storage >> metadata >> document;
    m_index.erase(document);
    m_storage.seekp(position, m_storage.beg);
    m_storage << deleted;
}

void db::engine::history(const db::object& selector, std::vector<db::object>& results)
{
    if(!m_index.primary_key(selector))
        throw std::invalid_argument("Expected primary key got "s + xson::json::stringify(selector, 0));

    m_storage.clear();

    auto position = m_index.position(selector);
    while(position >= 0)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        results.emplace_back(document);
        position = metadata.previous;
        std::clog << position << std::endl;
    }
}
