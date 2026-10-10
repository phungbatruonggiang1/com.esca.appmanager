// The catalogue, the running applications and the policy between them.
//
// This is SAM's RunningAppList plus PolicyManager, minus everything that only
// means something inside webOS: no launch points (our launcher reads the
// catalogue directly), no foreground tracking (Weston does not tell us who is
// on top), and no install/remove (that is the package manager's job, as it is
// SAM's AppInstallService).

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app_catalog.hpp"
#include "life_handler.hpp"

namespace esca::appmanager {

struct RunningApp {
    std::string app_id;
    std::string instance_id;
    pid_t pid = 0;
    /// launching -> running -> (paused) -> closing -> stopped
    std::string status;
    uint64_t since_ms = 0;
};

class AppManager {
public:
    explicit AppManager(std::vector<std::string> app_directories);
    ~AppManager();

    size_t reload(std::vector<std::string> &errors);

    std::vector<std::string> app_ids() const;
    const AppDescription *app(const std::string &id) const;

    /// Returns the new instance id, or an empty string with @p error set.
    std::string launch(const std::string &app_id,
                       const std::map<std::string, std::string> &params,
                       std::string &error);

    bool close_instance(const std::string &instance_id, bool force, std::string &error);
    bool close_app(const std::string &app_id, bool force, std::string &error);
    bool pause(const std::string &instance_id, std::string &error);
    bool resume(const std::string &instance_id, std::string &error);

    std::vector<RunningApp> running() const;

    /// Fired for every status change, including applications that exit on
    /// their own. Called from the reaper thread as well as from D-Bus calls.
    void on_status_change(std::function<void(const RunningApp &)> callback);

private:
    void handle_exit(pid_t pid, int status);
    void set_status(RunningApp &instance, const std::string &status);
    RunningApp *find_locked(const std::string &instance_id);

    mutable std::mutex mutex_;
    AppCatalog catalog_;
    std::unique_ptr<ILifeHandler> handler_;
    std::map<std::string, RunningApp> instances_; ///< by instance id
    uint64_t next_instance_ = 1;
    std::function<void(const RunningApp &)> on_status_;

    // Last, so it is destroyed first: the reaper calls back into this object.
    std::unique_ptr<ChildReaper> reaper_;
};

} // namespace esca::appmanager
