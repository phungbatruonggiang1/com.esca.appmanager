// Starting and stopping an application process.
//
// SAM hands each application type to its own life handler -- web apps go to
// WAM, native ones to a container. The same seam is here: NativeLifeHandler is
// the only implementation, and a Flutter or web runtime would be another.

#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <sys/types.h>
#include <thread>

#include "app_catalog.hpp"

namespace esca::appmanager {

class ILifeHandler {
public:
    virtual ~ILifeHandler() = default;

    /// Start the application. Returns its pid, or -1 with @p error set.
    virtual pid_t launch(const AppDescription &app,
                         const std::map<std::string, std::string> &params,
                         std::string &error) = 0;

    /// SIGTERM, or SIGKILL when @p force.
    virtual bool terminate(pid_t pid, bool force) = 0;
    virtual bool pause(pid_t pid) = 0;
    virtual bool resume(pid_t pid) = 0;
};

class NativeLifeHandler final : public ILifeHandler {
public:
    pid_t launch(const AppDescription &app,
                 const std::map<std::string, std::string> &params,
                 std::string &error) override;
    bool terminate(pid_t pid, bool force) override;
    bool pause(pid_t pid) override;
    bool resume(pid_t pid) override;
};

/// Reaps exited children and reports them.
///
/// Without this every closed application stays a zombie, and the manager never
/// learns that an app died on its own rather than being closed.
class ChildReaper {
public:
    using ExitCallback = std::function<void(pid_t pid, int status)>;

    explicit ChildReaper(ExitCallback on_exit);
    ~ChildReaper();

    ChildReaper(const ChildReaper &) = delete;
    ChildReaper &operator=(const ChildReaper &) = delete;

private:
    void run();

    ExitCallback on_exit_;
    std::atomic<bool> stop_{ false };
    std::thread thread_;
};

} // namespace esca::appmanager
