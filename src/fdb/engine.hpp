#pragma once

#include <fstream>
#include "fdb/index.hpp"

namespace fdb {

using object = xson::fson::object;

class engine
{
public:

    engine(const std::string file = "./fson.db"s);

    void collection(const std::string& col)
    {
        m_collection = col;
    }

    void create(fdb::object& document);

    void read(fdb::object& selector, std::vector<fdb::object>& result);

    void update(fdb::object& selector, fdb::object& document);

    void destroy(fdb::object& selector);

private:

    void rebuild_indexes();

    std::string m_collection;

    fdb::index m_index;

    mutable std::fstream m_storage;
};

}
