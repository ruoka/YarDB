#pragma once

#include <fstream>
#include "db/index.hpp"

namespace db {

class engine
{
public:

    engine(const std::string file = "./fson.db"s);

    void collection(const std::string& collection);

    void create_index(const std::initializer_list<std::string> keys);

    void reindex();

    void create(db::object& document);

    void read(const db::object& selector, std::vector<db::object>& documents);

    void update(const db::object& selector, const db::object& changes);

    void destroy(const db::object& selector);

    void history(const db::object& selector, std::vector<db::object>& updates);

    void dump();

private:

    std::string m_collection;

    std::map<std::string,db::index> m_index;

    std::fstream m_storage;
};

} // namespace db
