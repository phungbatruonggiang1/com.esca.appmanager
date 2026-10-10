#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

#include "app_manager.hpp"
#include "esca/appmanager/client.hpp" // kServiceName
#include "service.hpp"

namespace {

sdbus::IConnection *g_connection = nullptr;

void handle_signal(int)
{
    if (g_connection)
        g_connection->leaveEventLoop();
}

/// System applications ship under /usr/share; anything installed later lands
/// in the writable one. ESCA_APP_DIRS overrides both, for testing.
std::vector<std::string> app_directories()
{
    if (const char *override_dirs = std::getenv("ESCA_APP_DIRS")) {
        std::vector<std::string> dirs;
        std::string current;
        for (const char *p = override_dirs; *p; ++p) {
            if (*p == ':') {
                if (!current.empty())
                    dirs.push_back(current);
                current.clear();
            } else {
                current += *p;
            }
        }
        if (!current.empty())
            dirs.push_back(current);
        return dirs;
    }

    return { "/usr/share/esca/applications", "/home/weston/data/applications" };
}

} // namespace

int main()
{
    using namespace esca::appmanager;

    try {
        auto connection = sdbus::createBusConnection(sdbus::ServiceName{ kServiceName });
        g_connection = connection.get();

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        AppManager manager(app_directories());

        std::vector<std::string> errors;
        const size_t count = manager.reload(errors);
        for (const std::string &error : errors)
            std::fprintf(stderr, "esca-appmanagerd: %s\n", error.c_str());
        std::fprintf(stderr, "esca-appmanagerd: %zu application(s)\n", count);

        AppManagerService service(*connection, manager);

        std::fprintf(stderr, "esca-appmanagerd: ready on %s\n", kServiceName);
        connection->enterEventLoop();
        std::fprintf(stderr, "esca-appmanagerd: shutting down\n");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "esca-appmanagerd: fatal: %s\n", e.what());
        return 1;
    }
}
