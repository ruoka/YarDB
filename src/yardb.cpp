#include "db/rest/server.hpp"
#include "net/syslogstream.hpp"

using namespace std;
using namespace net;

const auto usage = R"(yardb [service_or_port])";

int main(int argc, char *argv[])
try
{
    clog << usage << endl;

    auto service_or_port = "2112"s;

    if(argc > 1)
        service_or_port = argv[1];

    slog.redirect(clog);
    slog.tag("YarDB");
    slog.level(syslog::severity::debug);
    slog << notice << "Initializing server" << flush;
    auto engine = db::engine{};
    auto server = db::rest::server{engine};
    slog << notice << "Initialized server" << flush;
    server.start(service_or_port);
    slog << notice << "Closed server" << flush;
    return 0;
}
catch(const exception& e)
{
    slog << error << e.what() << flush;
    return 1;
}
catch(...)
{
    slog << error << "Shit hit the fan!" << flush;
    return 1;
}
