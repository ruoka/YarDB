#include <csignal>
#include <string_view>
#include "gsl/span.hpp"
#include "net/syslogstream.hpp"
#include "db/restful_web_server.hpp"

using namespace std;
using namespace string_literals;
using namespace gsl;
using namespace net;
using namespace ext;

const auto usage = R"(yardb [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] [--file=<name>] [service_or_port])";

int main(int argc, char** argv)
try
{
    std::signal(SIGTERM, std::exit); // Handle kill
    std::signal(SIGINT,  std::exit); // Handle ctrl-c

    const auto arguments = make_span(argv,argc).subspan(1);
    auto file = "yar.db"s;
    auto service_or_port = "2112"s;
    slog.tag("YarDB");
    slog.level(net::syslog::severity::debug);

    for(const string_view option : arguments)
    {
        if(option.find("--clog") == 0)
        {
            slog.redirect(clog);
        }
        else if(option.find("--slog_tag=") == 0)
        {
            const auto name = string{option.substr(option.find('=')+1)};
            slog.tag(name);
        }
        else if(option.find("--slog_level=") == 0)
        {
            const auto mask = stol(option.substr(option.find('=')+1));
            slog.level(mask);
        }
        else if(option.find("--file") == 0)
        {
            file = option.substr(option.find('=')+1);
        }
        else if(option.find("--help") == 0)
        {
            clog << usage << endl;
            return 0;
        }
        else if(option.find("-") == 0)
        {
            clog << usage << endl;
            return 1;
        }
        else
        {
            service_or_port = option;
        }
    }

    slog << notice << "Initializing server" << flush;
    db::restful_web_server(file, service_or_port);
    slog << notice << "Closed server" << flush;
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
