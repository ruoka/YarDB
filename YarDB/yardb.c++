#include <csignal>
import yar;
import net;
import std;

using namespace std;
using namespace net;
using namespace utils;

const auto usage = R"(yardb [--help] [--clog] [--slog_level=<level>] [--file=<name>] [service_or_port])";

// Global atomic flag for shutdown request (async-signal-safe)
static std::atomic<bool> g_shutdown_requested{false};

// Signal handler - async-signal-safe: only sets flag
// Cannot call stop() here as it uses mutexes which are not async-signal-safe
extern "C" void signal_handler(int)
{
    // Signal handler must be async-signal-safe - only set atomic flag
    g_shutdown_requested.store(true, std::memory_order_release);
}

int main(int argc, char** argv)
try
{
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
    
    // Register signal handlers for graceful shutdown (before starting server)
    std::signal(SIGTERM, signal_handler); // Handle kill
    std::signal(SIGINT,  signal_handler); // Handle ctrl-c
    
    // Start server in background thread
    server.start();
    
    slog << notice << "Server started, waiting for shutdown signal" << flush;
    
    // Wait for shutdown signal
    // Poll periodically (check every 100ms) until shutdown is requested
    while(!g_shutdown_requested.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Shutdown requested - gracefully stop the server
    slog << notice << "Shutdown signal received, stopping server" << flush;
    server.stop();
    
    slog << notice << "Server stopped, exiting" << flush;
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
