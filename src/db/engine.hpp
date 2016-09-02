#pragma once

#include <fstream>
#include "db/index.hpp"

namespace db {

class engine
{
public:

    engine(const std::string file = "./fson.db"s);

    void collection(const std::string& collection);

    void create_index(std::vector<std::string> keys);

    void reindex();

    bool create(db::object& document);

    bool read(const db::object& selector, std::vector<db::object>& documents);

    bool update(const db::object& selector, const db::object& updates, bool upsert = false);

    bool destroy(const db::object& selector);

    bool history(const db::object& selector, std::vector<db::object>& updates);

    void dump();

private:

    std::string m_collection;

    std::map<std::string,db::index> m_index;

    std::fstream m_storage;
};

} // namespace db
