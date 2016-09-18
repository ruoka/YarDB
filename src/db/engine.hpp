#pragma once

#include <mutex>
#include <fstream>
#include "db/index.hpp"

namespace db {

class engine
{
public:

    engine(const std::string& db = "./yar.db"s);

    engine(engine&&);

    ~engine();

//  Indexing

    void index(std::vector<std::string> keys);

    void reindex();

//  CRUD

    bool create(db::object& document);

    bool read(const db::object& selector, db::object& documents);

    bool update(const db::object& selector, const db::object& updates, db::object& documents);

    bool destroy(const db::object& selector, db::object& documents);

//  Chain of updates

    bool history(const db::object& selector, db::object& documents);

//  For convenience

    bool upsert(const db::object& selector, db::object& updates, db::object& documents)
    {
        return update(selector, updates, documents) || create(updates);
    }

    bool upsert(const db::object& selector, db::object& updates)
    {
        return update(selector, updates) || create(updates);
    }

    bool replace(const db::object& selector, db::object& document)
    {
        return destroy(selector) && create(document);
    }

    bool update(const db::object& selector, const db::object& updates)
    {
        auto documents = db::object{};
        return update(selector, updates, documents);
    }

    bool destroy(const db::object& selector)
    {
        auto documents = db::object{};
        return destroy(selector, documents);
    }

//  Getters & setters

    const auto& collection() const
    {
        return m_collection;
    };

    void collection(const std::string& in_use)
    {
        m_collection = in_use;
    }

//  The lockable concept

    void lock()
    {
        m_mutex.lock();
    }

    bool try_lock()
    {
        return m_mutex.try_lock();
    }

    void unlock()
    {
        m_mutex.unlock();
    }

private:

    std::string m_db;

    std::string m_collection;

    std::map<std::string,db::index> m_index;

    std::fstream m_storage;

    std::mutex m_mutex;
};

} // namespace db
