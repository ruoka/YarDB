#include <iostream>
#include "xson/json.hpp"
#include "db/metadata.hpp"
#include "db/engine.hpp"

using namespace std::string_literals;

db::engine::engine(const std::string file) : m_collection{u8"db"s}, m_index{}, m_storage{}
{
    m_storage.open(file, std::ios_base::out | std::ios_base::in | std::ios_base::binary);

    if (!m_storage.is_open())
        throw std::invalid_argument("Cannot open file "s + file);

    reindex();
}

void db::engine::reindex()
{
    m_storage.clear();
    m_storage.seekg(0, m_storage.beg);

    while(m_storage)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage >> metadata >> document;
        if(m_storage && metadata.valid())
            m_index[metadata.collection].insert(document, metadata.position);
    }
}

void db::engine::collection(const std::string& collection)
{
    m_collection = collection;
};

void db::engine::create_index(const std::initializer_list<std::string> keys)
{
    auto& index = m_index[m_collection];

    index.add(keys);
    auto document = db::object{{u8"collection"s, m_collection}, {u8"keys"s, index.keys()}};
    auto collection = u8"db"s;
    std::swap(collection, m_collection);
    create(document);
    std::swap(collection, m_collection);
};

void db::engine::create(db::object& document)
{
    auto& index = m_index[m_collection];

    auto metadata = db::metadata{m_collection};
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    index.insert(document, m_storage.tellp());
    m_storage << metadata << document << std::flush;
}

void db::engine::read(const db::object& selector, std::vector<db::object>& documents)
{
    auto& index = m_index[m_collection];

    for(const auto position : index.range(selector))
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        if(document.match(selector))
            documents.emplace_back(document);
    }
}

void db::engine::update(const db::object& selector, const db::object& changes)
{
    auto& index = m_index[m_collection];

    for(const auto position : index.range(selector))
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;

        if(document.match(selector))
        {
            auto new_document = changes;
            new_document = new_document + document;

            m_storage.clear();
            m_storage.seekp(position, m_storage.beg);
            m_storage << updated << std::flush;

            m_storage.clear();
            m_storage.seekp(0, m_storage.end);
            index.insert(new_document, m_storage.tellp());
            m_storage << metadata << new_document;
        }
    }
}

void db::engine::destroy(const db::object& selector)
{
    auto& index = m_index[m_collection];

    if(!index.primary_key(selector))
        throw std::invalid_argument("Expected primary key got "s + xson::json::stringify(selector, 0));

    auto position = index.position(selector);
    auto metadata = db::metadata{};
    auto document = db::object{};
    m_storage.clear();
    m_storage.seekp(position, m_storage.beg);
    m_storage << deleted << std::flush;
    m_storage.clear();
    m_storage.seekg(position, m_storage.beg);
    m_storage >> metadata >> document;
    index.erase(document);
}

void db::engine::history(const db::object& selector, std::vector<db::object>& updates)
{
    const auto& index = m_index[m_collection];

    if(!index.primary_key(selector))
        throw std::invalid_argument("Expected primary key got "s + xson::json::stringify(selector, 0));

    auto position = index.position(selector);
    while(position >= 0)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        updates.emplace_back(document);
        position = metadata.previous;
    }
}

void db::engine::dump()
{
    m_storage.clear();
    m_storage.seekg(0, m_storage.beg);
    std::clog << "dump: " << '\n';
    while(m_storage)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage >> metadata >> document;
        if(m_storage)
            std::clog << xson::json::stringify(document) << '\n';
    }
}
