#include <iostream>
#include "xson/json.hpp"
#include "db/metadata.hpp"
#include "db/engine.hpp"

using namespace std;
using namespace std::string_literals;

db::engine::engine(const std::string file) : m_collection{u8"db"s}, m_index{}, m_storage{}
{
    m_storage.open(file, std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    if (!m_storage.is_open())
        throw std::invalid_argument("Cannot open file "s + file);
    reindex();
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

        if(m_storage.fail())
            break;

        auto& index = m_index[metadata.collection];

        index.update(document);

        if(metadata.status == metadata::deleted || metadata.status == metadata::updated)
            continue;

        index.insert(document, metadata.position);

        if(metadata.collection != "db"s)
            continue;

        auto collection = document["collection"s];
        std::vector<std::string> keys = document["keys"s];
        m_index[collection].add(keys);
    }
}

void db::engine::index(std::vector<std::string> keys)
{
    auto& index = m_index[m_collection];
    index.add(keys);
    auto selector = db::object{u8"collection"s, m_collection};
    auto document = db::object{selector, {u8"keys"s, index.keys()}};
    auto collection = u8"db"s;
    std::swap(collection, m_collection);
    update(selector, document, true);
    std::swap(collection, m_collection);
};

bool db::engine::create(db::object& document)
{
    auto match = true;
    auto& index = m_index[m_collection];
    auto metadata = db::metadata{m_collection};
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    index.update(document);
    index.insert(document, m_storage.tellp());
    m_storage << metadata << document << std::flush;
    return match;
}

bool db::engine::read(const db::object& selector, db::object& documents)
{
    auto match = false;
    auto& index = m_index[m_collection];
    documents.type(xson::type::array);
    for(const auto position : index.range(selector))
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        if(document.match(selector))
        {
            documents += document;
            match = true;
        }
    }
    return match;
}

bool db::engine::update(const db::object& selector, const db::object& updates, bool upsert)
{
    auto match = false;
    auto& index = m_index[m_collection];
    auto new_document = updates;
    for(const auto position : index.range(selector))
    {
        auto metadata = db::metadata{};
        auto old_document = db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> old_document;
        if(old_document.match(selector))
        {
            new_document = updates;
            new_document = new_document + old_document;

            m_storage.clear();
            m_storage.seekp(position, m_storage.beg);
            m_storage << updated << std::flush;

            m_storage.clear();
            m_storage.seekp(0, m_storage.end);
            index.update(new_document);
            index.insert(new_document, m_storage.tellp());
            m_storage << metadata << new_document << std::flush;

            match = true;
        }
    }
    if(!match && upsert)
        create(new_document);
    return match;
}

bool db::engine::upsert(const db::object& selector, const db::object& updates)
{
    return update(selector, updates, true);
}

bool db::engine::replace(const db::object& selector, db::object& updates)
{
    return destroy(selector) && create(updates);
}

bool db::engine::destroy(const db::object& selector)
{
    auto match = false;
    auto& index = m_index[m_collection];

    if(!index.primary_key(selector))
        throw std::invalid_argument("Expected primary key got "s + xson::json::stringify(selector, 0));

    auto position = index.position(selector); // FIXME If not key not found
    auto metadata = db::metadata{};
    auto document = db::object{};
    m_storage.clear();
    m_storage.seekp(position, m_storage.beg);
    m_storage << deleted << std::flush;
    m_storage.clear();
    m_storage.seekg(position, m_storage.beg);
    m_storage >> metadata >> document;
    index.erase(document);
    match = true;

    return match;
}

bool db::engine::history(const db::object& selector, db::object& documents)
{
    auto match = false;
    auto& index = m_index[m_collection];
    documents.type(xson::type::array);
    for(auto position : index.range(selector))
    while(position >= 0)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        documents += document;
        position = metadata.previous;
        match = true;
    }
    return match;
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
            std::clog << xson::json::stringify(
                            {{"collection"s, metadata.collection         },
                             {"action"s,     to_string(metadata.status)  },
                             {"position"s,   metadata.position           },
                             {"previous"s,   metadata.previous           },
                             {"document"s,   document                    }})
                      << ",\n";
    }
}
