#pragma once

#include "net/sender.hpp"
#include "xson/utility.hpp"

namespace net
{

class syslog
{
public:

    enum class facility : int
    {
        user   = 1,
        local0 = 16,
        local1 = 17,
        local2 = 18,
        local3 = 19,
        local4 = 20,
        local5 = 21,
        local6 = 22,
        local7 = 23
    };

    enum class severity : int
    {
         emergency = 0,
         alert     = 1,
         critical  = 2,
         error     = 3,
         warning   = 4,
         notice    = 5,
         info      = 6,
         debug     = 7
    };

    syslog() :
        m_facility{facility::user},
        m_severity{severity::debug},
        m_level{severity::debug},
        m_host{"Kaiuss-MBP.Home"},
        m_tag{"syslog"},
        m_pid{2112},
        m_remote{distribute("localhost","syslog")},
        m_local{std::clog},
        m_empty{true}
    {}

    void set_severity(severity s)
    {
        m_severity = s;
    }

    void set_level(severity s)
    {
        m_severity = s;
    }

    auto& operator<< (syslog& (*pf)(syslog&))
    {
        (*pf)(*this);
        return *this;
    }

    template <typename T,
              typename = std::enable_if_t<!std::is_pointer<T>::value>>
    auto& operator<< (const T& t)
    {
        if(m_level >= m_severity)
        {
            if(m_empty) header();
            m_local  << t;
            m_remote << t;
        }
        return *this;
    }

    auto& flush()
    {
        if(m_level >= m_severity)
        {
            m_local.flush();
            m_remote.flush();
            m_empty = true;
        }
        return *this;
    }

private:

    void header()
    {
        m_local << '<' << 8 * (int)m_facility + (int)m_severity << '>'  // PRI
                 << std::to_string2(std::chrono::system_clock::now())     // TIMESTAMP
                 << ' '                                                  // SP
                 << m_host                                           // HOSTNAME
                 << ' '                                                  // SP
                 << m_tag                                            // APP-NAME
                 << '['                                                  //
                 << m_pid                                             // PID
                 << ']'                                                  //
                 << ':'                                                  //
                 << ' ';                                                 // SP
        m_remote << '<' << 8 * (int)m_facility + (int)m_severity << '>'  // PRI
                 << std::to_string2(std::chrono::system_clock::now())     // TIMESTAMP
                 << ' '                                                  // SP
                 << m_host                                           // HOSTNAME
                 << ' '                                                  // SP
                 << m_tag                                            // APP-NAME
                 << '['                                                  //
                 << m_pid                                             // PID
                 << ']'                                                  //
                 << ':'                                                  //
                 << ' ';                                                 // SP
        m_empty = false;
    }

    facility m_facility;

    severity m_severity;

    severity m_level;

    const std::string m_host;

    std::string m_tag;

    const int m_pid;

    bool m_empty;

    std::ostream& m_local;

    oendpointstream m_remote;
};

auto& error(syslog& sl)
{
    sl.set_severity(syslog::severity::error);
    return sl;
}

auto& warning(syslog& sl)
{
    sl.set_severity(syslog::severity::warning);
    return sl;
}

auto& notice(syslog& sl)
{
    sl.set_severity(syslog::severity::notice);
    return sl;
}

auto& info(syslog& sl)
{
    sl.set_severity(syslog::severity::info);
    return sl;
}

auto& debug(syslog& sl)
{
    sl.set_severity(syslog::severity::debug);
    return sl;
}

auto& flush2(syslog& sl)
{
    sl.flush();
    return sl;
}

static auto slog = syslog{};

} // namespace net
