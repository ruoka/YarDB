import yar;
import net;
import std;

using namespace std;
using namespace net;
using namespace ext;

const auto usage = R"(yardb [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] [--file=<name>] [service_or_port])";

int main(int argc, char** argv)
try
{
    std::signal(std::sigterm, std::exit); // Handle kill
    std::signal(std::sigint,  std::exit); // Handle ctrl-c

    const auto arguments = span(argv,argc).subspan(1);
    auto file = "yar.db"s;
    auto service_or_port = "2112"s;
    slog.tag("YarDB");
    slog.level(net::syslog::severity::debug);

    for(string_view option : arguments)
    {
        if(option.starts_with("--clog"))
        {
            slog.redirect(clog);
        }
        else if(option.starts_with("--slog_tag="))
        {
            option.remove_prefix(option.find_first_not_of("--slog_tag="));
            slog.tag(option);
        }
        else if(option.starts_with("--slog_level="))
        {
            option.remove_prefix(option.find_first_not_of("--slog_level="));
            auto mask = 0;
            auto [ptr,ec] = std::from_chars(option.begin(),option.end(),mask);
            if(ec != std::errc() or ptr != option.end())
            {
                slog << error << " invalid syslog mask --slog_level=" << option << flush;
                return 1;
            }
            slog.level(mask);
        }
        else if(option.starts_with("--file="))
        {
            option.remove_prefix(option.find_first_not_of("--file="));
            file = option;
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
    slog << error << "Shit hit the fan!" << flush;
    return 1;
}
