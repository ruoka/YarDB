#pragma once

#include <map>
#include "xson/fson.hpp"

using namespace std::string_literals;

namespace fdb {

using object = xson::fson::object;

class range_iterator
{
public:

    range_iterator(fdb::object* selector, std::map<std::int64_t,std::streamoff>::iterator itr) :
        m_selector{selector},
        m_current{itr}
    {}

    range_iterator(const range_iterator& itr) :
        m_selector{itr.m_selector},
        m_current{itr.m_current}
    {}

    std::streamoff operator * ()
    {
        return m_current->second;
    }

    range_iterator& operator ++ () // FIXME
    {
        ++m_current;
        return *this;
    }

    bool operator != (const range_iterator& itr) const
    {
        return m_current != itr.m_current;
    }

private:

    fdb::object* m_selector;

    std::map<std::int64_t,std::streamoff>::iterator m_current;

    friend class range;
};

class range
{
public:

    range(fdb::object* selector, std::map<std::int64_t,std::streamoff>* sequence) :
        m_selector{selector},
        m_sequence{sequence},
        m_begin{selector, sequence->begin()},
        m_end{selector, sequence->end()}
    {
        if(selector->has("_id"s)) // We have a primary key
        {
            m_begin.m_current = m_end.m_current = m_sequence->find((*selector)["_id"s]);
            if(m_end.m_current != m_sequence->end())
                ++m_end.m_current;
        }
    }

    range(const range& r) :
        m_selector{r.m_selector},
        m_sequence{r.m_sequence},
        m_begin{r.m_begin},
        m_end{r.m_end}
    {}

    range_iterator begin()
    {
        return m_begin;
    }

    range_iterator end()
    {
        return m_end;
    }

    range_iterator destroy(range_iterator& itr)
    {
        return {m_selector, m_sequence->erase(itr.m_current)};
    }

private:

    fdb::object* m_selector;

    std::map<std::int64_t,std::streamoff>* m_sequence;

    range_iterator m_begin, m_end;
};

class index
{
public:

    std::streamoff& operator [] (const std::int64_t& id)
    {
        m_xxx = std::max(id,m_xxx);
        return m_sequence[id];
    }

    fdb::range range(fdb::object& selector)
    {
        return fdb::range{&selector, &m_sequence};
    }

    std::int64_t sequence()
    {
        return ++m_xxx;
    }

private:

    std::int64_t m_xxx{0};

    std::map<std::int64_t,std::streamoff> m_sequence;
};

} // namespace fdb
