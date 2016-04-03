#include <iostream>
#include "db/metadata.hpp"
#include "db/engine.hpp"

using namespace std::string_literals;

db::engine::engine(const std::string file) : m_storage{}
{
    m_storage.open(file, std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    if (!m_storage.is_open())
        throw "Perkele!";
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
        }
        std::clog << metadata.valid << ':' << metadata.index << ':' << metadata.position << std::endl;
    }
}

void db::engine::create(db::object& document)
{
    auto metadata = db::metadata{true};
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    if(document.has("_id"s))
        metadata.index = document["_id"s]; // Validate if key already exists
    else
        metadata.index = document["_id"s].value(m_index.sequence());
    metadata.position = m_storage.tellp();
    m_storage << metadata << document << std::flush;
    m_index[metadata.index] = metadata.position;
}

void db::engine::read(db::object& selector, std::vector<db::object>& result)
{
    m_storage.clear();
    for(const auto position : m_index.range(selector))
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        result.emplace_back(document);
    }
}

void db::engine::update(db::object& selector, db::object& changes)
{
    auto array = std::vector<db::object>{};
    read(selector, array);
    destroy(selector);
    for(auto& document : array)
        create(document + changes);
}

void db::engine::destroy(db::object& selector)
{
    const auto range = m_index.range(selector);
    m_storage.clear();
    for(auto itr = range.begin(); itr != range.end(); itr = m_index.erase(itr))
    {
        const auto position = *itr;
        const auto metadata = db::metadata{false};
        m_storage.seekp(position, m_storage.beg);
        m_storage << metadata;
    }
}
