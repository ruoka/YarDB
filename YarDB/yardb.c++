#include <csignal>
import yar;
import net;
import std;

using namespace std;
using namespace net;
using namespace utils;

const auto usage = R"(yardb [--help] [--clog] [--slog_level=<level>] [--file=<name>] [service_or_port])";

int main(int argc, char** argv)
try
{
    std::signal(SIGTERM, std::exit); // Handle kill
    std::signal(SIGINT,  std::exit); // Handle ctrl-c

    const auto arguments = span(argv,argc).subspan(1);
    auto file = "yar.db"s;
    auto service_or_port = "2112"s;
    slog.app_name("yardb")
        .log_level(net::syslog::severity::debug)
        .format(net::log_format::jsonl);  // Use JSONL format by default

    for(string_view option : arguments)
    {
        if(option == "--help")
        {
            clog << usage << endl;
            return 0;
        }

        if(option == "--clog")
        {
            slog.redirect(clog);
            continue;
        }

        if(option.starts_with("--slog_level="))
        {
            auto level_str = option.substr(string_view{"--slog_level="}.size());
            auto mask = 0u;
            auto [ptr,ec] = std::from_chars(level_str.begin(), level_str.end(), mask);
            if(ec != std::errc() or ptr != level_str.end())
            {
                clog << "Error: invalid syslog mask --slog_level=" << level_str << endl;
                clog << usage << endl;
                return 1;
            }
            slog.log_level(static_cast<net::syslog::severity>(mask));
            continue;
        }

        if(option.starts_with("--file="))
        {
            file = option.substr(string_view{"--file="}.size());
            continue;
        }

        if(option.starts_with("-"))
        {
            clog << "Error: unknown option " << option << endl;
            clog << usage << endl;
            return 1;
        }

        service_or_port = option;
    }

    slog << notice << "Starting up server" << flush;
    auto server = yar::http::rest_api_server{file, service_or_port};
    server.listen(); // Blocks forever
    slog << notice << "Shutting down server" << flush;
    return 0;
}
catch(const system_error& e)
{
    slog << error << "System error with code " << e.code() << " aka " << quoted(e.what()) << flush;
    return 1;
}
catch(const exception& e)
{
    slog << error << "Exception " << quoted(e.what()) << flush;
    return 1;
}
catch(...)
{
    slog << error << "Unexpected error occurred" << flush;
    return 1;
}
