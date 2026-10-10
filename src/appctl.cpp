// CLI for the application manager: the first client, and the way to test it
// without a launcher.
//
//   esca-appctl list
//   esca-appctl info    <app_id>
//   esca-appctl launch  <app_id> [key=value ...]
//   esca-appctl close   <instance_id> [--force]
//   esca-appctl kill    <app_id> [--force]
//   esca-appctl pause   <instance_id>
//   esca-appctl resume  <instance_id>
//   esca-appctl running
//   esca-appctl watch

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "esca/appmanager/client.hpp"

using namespace esca::appmanager;

namespace {

int usage()
{
    std::fprintf(stderr,
                 "usage: esca-appctl list\n"
                 "       esca-appctl info    <app_id>\n"
                 "       esca-appctl launch  <app_id> [key=value ...]\n"
                 "       esca-appctl close   <instance_id> [--force]\n"
                 "       esca-appctl kill    <app_id> [--force]\n"
                 "       esca-appctl pause   <instance_id>\n"
                 "       esca-appctl resume  <instance_id>\n"
                 "       esca-appctl running\n"
                 "       esca-appctl watch\n");
    return 2;
}

bool has_flag(int argc, char **argv, const char *flag)
{
    for (int i = 2; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0)
            return true;
    return false;
}

int cmd_running(AppManagerClient &client)
{
    const auto instances = client.running();
    if (instances.empty()) {
        std::printf("nothing running\n");
        return 0;
    }
    std::printf("%-28s %-8s %-10s %s\n", "INSTANCE", "PID", "STATUS", "APP");
    for (const RunningAppInfo &instance : instances)
        std::printf("%-28s %-8d %-10s %s\n", instance.instance_id.c_str(), instance.pid,
                    instance.status.c_str(), instance.app_id.c_str());
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage();

    const std::string command = argv[1];
    AppManagerClient client;
    std::string error;

    if (command == "list") {
        const auto ids = client.list_apps();
        if (ids.empty()) {
            std::fprintf(stderr, "no applications (is esca-appmanagerd running?)\n");
            return 1;
        }
        for (const auto &id : ids)
            std::printf("%s\n", id.c_str());
        return 0;
    }

    if (command == "running")
        return cmd_running(client);

    if (command == "watch") {
        client.on_life_status([](const RunningAppInfo &instance) {
            std::printf("%-28s %-10s %s\n", instance.instance_id.c_str(),
                        instance.status.c_str(), instance.app_id.c_str());
            std::fflush(stdout);
        });
        std::printf("watching life-status changes, Ctrl-C to stop\n");
        for (;;)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (argc < 3)
        return usage();
    const std::string target = argv[2];

    if (command == "info") {
        AppInfo info;
        if (!client.app_info(target, info, error)) {
            std::fprintf(stderr, "info: %s\n", error.c_str());
            return 1;
        }
        std::printf("id        %s\ntitle     %s\ntype      %s\nmain      %s\nicon      %s\n"
                    "removable %s\n",
                    info.id.c_str(), info.title.c_str(), info.type.c_str(), info.main.c_str(),
                    info.icon.c_str(), info.removable ? "yes" : "no");
        return 0;
    }

    if (command == "launch") {
        std::map<std::string, std::string> params;
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            const size_t eq = arg.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "launch parameters are key=value, got \"%s\"\n", arg.c_str());
                return 2;
            }
            params[arg.substr(0, eq)] = arg.substr(eq + 1);
        }

        const std::string instance = client.launch(target, params, error);
        if (instance.empty()) {
            std::fprintf(stderr, "launch: %s\n", error.c_str());
            return 1;
        }
        std::printf("%s\n", instance.c_str());
        return 0;
    }

    const bool force = has_flag(argc, argv, "--force");

    if (command == "close") {
        if (!client.close_instance(target, force, error)) {
            std::fprintf(stderr, "close: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
    if (command == "kill") {
        if (!client.close_app(target, force, error)) {
            std::fprintf(stderr, "kill: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
    if (command == "pause") {
        if (!client.pause_instance(target, error)) {
            std::fprintf(stderr, "pause: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
    if (command == "resume") {
        if (!client.resume_instance(target, error)) {
            std::fprintf(stderr, "resume: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    return usage();
}
