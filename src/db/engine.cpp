#include "db/metadata.hpp"
#include "db/engine.hpp"

using namespace std::string_literals;

db::engine::engine(const std::string file) : m_collection{u8"db"s}, m_index{}, m_storage{}
{
    m_storage.open(file, std::ios::out | std::ios::in | std::ios::binary);
    if (!m_storage.is_open())
        m_storage.open(file, std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if (!m_storage.is_open())
        throw std::invalid_argument("Cannot open file "s + file);
    reindex();
    reindex(); // Intentional double
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
    upsert(selector, document);
    std::swap(collection, m_collection);
};

bool db::engine::create(db::object& document)
{
    auto& index = m_index[m_collection];
    auto metadata = db::metadata{m_collection};
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    index.update(document);
    index.insert(document, m_storage.tellp());
    m_storage << metadata << document;
    return true;
}

bool db::engine::read(const db::object& selector, db::object& documents)
{
    documents.type(xson::type::array);

    auto top = std::numeric_limits<sequence_type>::max();
    if(selector.has(u8"$top"s))
        top = selector[u8"$top"s];

    auto success = false;
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
            documents += std::move(document);
            success = true;
            if(--top == 0) break;
        }
    }

    return success;
}

bool db::engine::update(const db::object& selector, db::object& updates, db::object& documents)
{
    documents.type(xson::type::array);
    
    auto success = false;
    auto& index = m_index[m_collection];

    for(const auto position : index.range(selector))
    {
        auto metadata = db::metadata{};
        auto old_document = db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> old_document;
        if(old_document.match(selector))
        {
            m_storage.clear();
            m_storage.seekp(position, m_storage.beg);
            m_storage << updated;

            auto new_document = updates;
            new_document += std::move(old_document);

            m_storage.clear();
            m_storage.seekp(0, m_storage.end);
            index.update(new_document);
            index.insert(new_document, m_storage.tellp());
            m_storage << metadata << new_document;

            documents += std::move(new_document);
            success = true;
        }
    }

    return success;
}

bool db::engine::destroy(const db::object& selector, db::object& documents)
{
    documents.type(xson::type::array);

    auto success = false;
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
            m_storage.clear();
            m_storage.seekp(position, m_storage.beg);
            m_storage << deleted;
            documents += std::move(document);
            success = true;
        }
    }

    for(const auto document : documents)
        index.erase(document.second);

    return success;
}

bool db::engine::history(const db::object& selector, db::object& documents)
{
    documents.type(xson::type::array);

    auto success = false;
    auto& index = m_index[m_collection];

    for(auto position : index.range(selector))
        while(position >= 0)
        {
            auto metadata = db::metadata{};
            auto document = db::object{};
            m_storage.clear();
            m_storage.seekg(position, m_storage.beg);
            m_storage >> metadata >> document;
            position = metadata.previous;
            documents += std::move(document);
            success = true;
        }

    return success;
}
