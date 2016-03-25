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
        m_version{'1'},
        m_hostname{"Kaiuss-MBP.Home"},
        m_appname{"fi.kaius.syslog"},
        m_procid{2112},
        m_msgid{0},
        m_remote{distribute("localhost","syslog")},
        m_local{std::clog},
        m_empty{true}
    {}

    auto& set_severity(severity s)
    {
        m_severity = s;
        return *this;
    }

    auto& header()
    {
/* syslog
        m_os << '<' << 8 * (int)m_facility + (int)m_severity << '>'  // PRI
             << m_version                                            // VERSION
             << ' '                                                  // SP
             << std::to_string(std::chrono::system_clock::now())     // TIMESTAMP
             << ' '                                                  // SP
             << m_hostname                                           // HOSTNAME
             << ' '                                                  // SP
             << m_appname                                            // APP-NAME
             << ' '                                                  // SP
             << m_procid                                             // PROCID
             << ' '                                                  // SP
             << '-'                                                  // STRUCTURED-DATA
             << ' '                                                  // SP
             << ++m_msgid                                            // MSGID
             << ' ';                                                 // SP
*/
// asl

        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
        duration -= seconds;
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);

        m_local <<  "[Time "        << seconds.count() << ']'
                << " [TimeNanoSec " << nanoseconds.count() << ']'
                << " [Level "       << (int)m_severity << ']'
                << " [Host "        << m_hostname << ']'
                << " [Sender "      << m_appname << ']'
                << " [PID "         << m_procid << ']'
                << " [Facility syslog] [UID 501] [GID 20] [ReadGID 80]"
                << " [Message ";

        m_remote <<  "[Time "        << seconds.count() << ']'
                 << " [TimeNanoSec " << nanoseconds.count() << ']'
                 << " [Level "       << (int)m_severity << ']'
                 << " [Host "        << m_hostname << ']'
                 << " [Sender "      << m_appname << ']'
                 << " [PID "         << m_procid << ']'
                 << " [Facility syslog] [UID 501] [GID 20] [ReadGID 80]"
                 << " [Message ";

        m_empty = false;

        return *this;
    }

    auto& flush()
    {
        m_local  << ']' << std::flush;
        m_remote << ']' << std::flush;
        m_empty = true;
        return *this;
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
        if(m_empty) header();
        m_local  << t;
        m_remote << t;
        return *this;
    }

private:

    facility m_facility;

    severity m_severity;

    const char m_version;

    const std::string m_hostname;

    std::string m_appname;

    const int m_procid;

    int m_msgid;

    bool m_empty;

    std::ostream& m_local;

    oendpointstream m_remote;
};

auto& error(syslog& sl)
{
    return sl.set_severity(syslog::severity::error);
}

auto& warning(syslog& sl)
{
    return sl.set_severity(syslog::severity::warning);
}

auto& notice(syslog& sl)
{
    return sl.set_severity(syslog::severity::notice);
}

auto& info(syslog& sl)
{
    return sl.set_severity(syslog::severity::info);
}

auto& debug(syslog& sl)
{
    return sl.set_severity(syslog::severity::debug);
}

auto& flush2(syslog& sl)
{
    return sl.flush();
}

static auto slog = syslog{};

} // namespace net
