#pragma once

#include <fstream>
#include "db/index.hpp"

namespace db {

class engine
{
public:

    engine(const std::string file = "./yar.db"s);

//  Indexing

    void index(std::vector<std::string> keys);

    void reindex();

//  CRUD

    bool create(db::object& document);

    bool read(const db::object& selector, db::object& documents);

    bool update(const db::object& selector, db::object& updates);

    bool destroy(const db::object& selector, db::object& documents);

//  Chain of updates

    bool history(const db::object& selector, db::object& documents);

//  For convenience

    bool upsert(const db::object& selector, db::object& updates)
    {
        return update(selector, updates) || create(updates);
    }

    bool replace(const db::object& selector, db::object& updates)
    {
        return destroy(selector) && create(updates);
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

private:

    std::string m_collection;

    std::map<std::string,db::index> m_index;

    std::fstream m_storage;
};

} // namespace db
