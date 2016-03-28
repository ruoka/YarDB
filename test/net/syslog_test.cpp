#include <gtest/gtest.h>
#include "net/syslog.hpp"

using namespace std::string_literals;
using namespace net;

TEST(NetSyslogstreamTest,Debug)
{
    slog << debug << "Kaius testing "s << 123 << flush2;
}

TEST(NetSyslogstreamTest,Info)
{
    slog << info << "Kaius testing "s << 456 << flush2;
}

TEST(NetSyslogstreamTest,Notice)
{
    slog << notice << "Kaius testing "s << "-123-" << flush2;
}

TEST(NetSyslogstreamTest,Warning)
{
    slog << warning << "Kaius testing "s << 789.0 << flush2;
}

TEST(NetSyslogstreamTest,Error)
{
    slog << error << "Kaius testing "s << true << 321 << false << flush2;
}
