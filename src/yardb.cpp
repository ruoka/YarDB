#include "db/rest/server.hpp"
#include "net/syslogstream.hpp"

using namespace std;
using namespace net;

const auto usage = R"(
Currently supported options commands are:
)";

int main(int, char**)
try
{
    slog.redirect(clog);
    slog.tag("YarDB");
    slog.level(syslog::severity::debug);
    slog << notice << "Initializing server" << flush;
    auto engine = db::engine{};
    auto server = db::rest::server{engine};
    slog << notice << "Initialized server" << flush;
    server.start("2112");
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
