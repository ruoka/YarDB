#include <set>
#include "net/syslogstream.hpp"
#include "xson/fson.hpp"
#include "db/metadata.hpp"
#include "db/engine.hpp"

namespace {

using namespace std::string_literals;

auto locks = std::set<std::string>{};

inline void unlock()
{
    for(const auto& lock : locks)
        std::remove(lock.c_str());
    locks.clear();
}

inline void unlock(std::string_view db)
{
    const auto lock = std::string{db} + ".pid"s;
    std::remove(lock.c_str());
    locks.erase(lock);
}

inline void lock(std::string_view db)
{
    const auto lock = std::string{db} + ".pid"s;
    auto file = std::fstream{lock, std::ios::in};
    if(file.is_open())
    {
        auto pid = ""s;
        file >> pid;
        throw std::runtime_error{"DB "s + lock + " is already in use by PID "s + pid};
    }
    file.open(lock, std::ios::out | std::ios::trunc);
    if(!file.is_open())
        throw std::runtime_error{"Failed to create DB lock "s + lock};
    file << net::syslog::getpid() << std::endl;
    locks.emplace(lock);
    std::atexit(unlock);
}

} // namespace

db::engine::engine(std::string_view db) :
    m_db{db},
    m_collection{"_db"s},
    m_index{},
    m_storage{}
{
    ::lock(m_db);
    m_storage.open(m_db, std::ios::out | std::ios::in | std::ios::binary);
    if(!m_storage.is_open())
        m_storage.open(m_db, std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if(!m_storage.is_open())
        throw std::runtime_error{"Failed to open/create DB "s + m_db};
    reindex();
    reindex(); // Intentional double
}

db::engine::engine(db::engine&& e) :
    m_db{std::move(e.m_db)},
    m_collection{std::move(e.m_collection)},
    m_index{std::move(e.m_index)},
    m_storage{std::move(e.m_storage)}
{}

db::engine::~engine()
{
    ::unlock(m_db);
}

void db::engine::reindex()
{
    using namespace xson::fson;

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

        if(metadata.collection != "_db"s)
            continue;

        auto collection = document["collection"s];
        auto keys = document["keys"s];
        auto temp = std::vector<std::string>{};
        for(const auto& k : keys.get<object::array>())
            temp.push_back(k);

        m_index[collection].add(temp);
    }
}

void db::engine::index(std::vector<std::string> keys)
{
    auto& index = m_index[m_collection];
    index.add(keys);
    auto selector = db::object{"collection"s, m_collection};
    auto document = db::object{selector, {"keys"s, index.keys()}};
    auto collection = "_db"s;
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
    m_storage.flush();
    return true;
}

bool db::engine::read(const db::object& selector, db::object& documents)
{
    documents = db::object::array{};

    auto top = std::numeric_limits<sequence_type>::max();
    if(selector.has("$top"s))
        top = selector["$top"s];

    auto success = false;
    const auto& index = m_index[m_collection];

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

bool db::engine::update(const db::object& selector, const db::object& updates, db::object& documents)
{
    documents = db::object::array{};

    auto success = false;
    auto& index = m_index[m_collection];

    for(const auto position : index.range(selector))
    {
        using namespace xson::fson;

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

    if(success)
        m_storage.flush();

    return success;
}

bool db::engine::destroy(const db::object& selector, db::object& documents)
{
    documents = db::object::array{};

    auto top = std::numeric_limits<sequence_type>::max();
    if(selector.has("$top"s))
        top = selector["$top"s];

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

            if(--top == 0) break;
        }
    }

    if(success)
        m_storage.flush();

    for(const auto& document : documents.get<object::array>())
        index.erase(document);

    return success;
}

bool db::engine::history(const db::object& selector, db::object& documents)
{
    documents = db::object::array{};

    auto success = false;
    const auto& index = m_index[m_collection];

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
