#include <span>
#include <csignal>
#include <string_view>
#include "net/syslogstream.hpp"
#include "db/restful_web_server.hpp"

using namespace std;
using namespace string_literals;
using namespace net;
using namespace ext;

const auto usage = R"(yardb [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] [--file=<name>] [service_or_port])";

int main(int argc, char** argv)
try
{
    std::signal(SIGTERM, std::exit); // Handle kill
    std::signal(SIGINT,  std::exit); // Handle ctrl-c

    const auto arguments = span(argv,argc).subspan(1);
    auto file = "yar.db"s;
    auto service_or_port = "2112"s;
    slog.tag("YarDB");
    slog.level(net::syslog::severity::debug);

    for(const string_view option : arguments)
    {
        if(option.starts_with("--clog"))
        {
            slog.redirect(clog);
        }
        else if(option.starts_with("--slog_tag="))
        {
            const auto name = option.substr(option.find('=')+1);
            slog.tag(name);
        }
        else if(option.starts_with("--slog_level="))
        {
            const auto mask = stol(option.substr(option.find('=')+1));
            slog.level(mask);
        }
        else if(option.starts_with("--file"))
        {
            file = option.substr(option.find('=')+1);
        }
        else if(option.starts_with("--help"))
        {
            clog << usage << endl;
            return 0;
        }
        else if(option.starts_with("-"))
        {
            clog << usage << endl;
            return 1;
        }
        else
        {
            service_or_port = option;
        }
    }

    slog << notice << "Starting up server" << flush;
    db::restful_web_server(file, service_or_port);
    slog << notice << "Shutting down server" << flush;
    return 0;
}
catch(const system_error& e)
{
    slog << error << "System error with code " << e.code() << " " << e.what() << flush;
    return 1;
}
catch(const exception& e)
{
    slog << error << "Exception " << e.what() << flush;
    return 1;
}
catch(...)
{
    slog << error << "Shit hit the fan!" << flush;
    return 1;
}
