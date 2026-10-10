// What launchers and applications link against. Hides sdbus-c++ so a caller
// never writes a proxy by hand.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace esca::appmanager {

constexpr const char *kServiceName = "vn.sparc.esca.AppManager1";
constexpr const char *kObjectPath = "/vn/sparc/esca/AppManager1";
constexpr const char *kInterface = "vn.sparc.esca.AppManager1";

/// An installed application, as described by its appinfo.json.
struct AppInfo {
    std::string id;
    std::string title;
    std::string type;    ///< "native" today; "web" when there is something to run it
    std::string main;    ///< executable to run
    std::string icon;
    bool removable = false;
};

/// A launched instance. One application can have several.
struct RunningAppInfo {
    std::string app_id;
    std::string instance_id;
    int32_t pid = 0;
    std::string status;  ///< launching, running, paused, closing, stopped
    uint64_t since_ms = 0;
};

class AppManagerClient {
public:
    AppManagerClient();
    ~AppManagerClient();

    AppManagerClient(const AppManagerClient &) = delete;
    AppManagerClient &operator=(const AppManagerClient &) = delete;

    std::vector<std::string> list_apps();
    bool app_info(const std::string &app_id, AppInfo &out, std::string &error);

    /// Returns the instance id, empty on failure.
    std::string launch(const std::string &app_id,
                       const std::map<std::string, std::string> &params,
                       std::string &error);

    bool close_instance(const std::string &instance_id, bool force, std::string &error);
    bool close_app(const std::string &app_id, bool force, std::string &error);
    bool pause_instance(const std::string &instance_id, std::string &error);
    bool resume_instance(const std::string &instance_id, std::string &error);

    std::vector<RunningAppInfo> running();

    /// Rescan the application directories.
    bool reload(std::string &error);

    /// Called for every life-status change until the client is destroyed.
    /// Delivered on the proxy's own thread, so keep it short.
    void on_life_status(std::function<void(const RunningAppInfo &)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace esca::appmanager
