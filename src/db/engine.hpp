#pragma once

#include <fstream>
#include "db/index.hpp"

namespace db {

class engine
{
public:

    engine(const std::string file = "./fson.db"s);

    void rebuild_indexes(std::initializer_list<std::string> keys = {});

    void create(db::object& document);

    void read(const db::object& selector, std::vector<db::object>& results);

    void update(const db::object& selector, const db::object& changes);

    void destroy(const db::object& selector);

    void history(const db::object& selector, std::vector<db::object>& results);

private:

    db::index m_index;

    std::fstream m_storage;
};

} // namespace db
