#include "app_manager.hpp"

#include <chrono>
#include <cstdio>
#include <sys/wait.h>

namespace esca::appmanager {

namespace {

uint64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

AppManager::AppManager(std::vector<std::string> app_directories)
    : catalog_(std::move(app_directories)), handler_(std::make_unique<NativeLifeHandler>())
{
    reaper_ = std::make_unique<ChildReaper>(
        [this](pid_t pid, int status) { handle_exit(pid, status); });
}

AppManager::~AppManager() = default;

size_t AppManager::reload(std::vector<std::string> &errors)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return catalog_.reload(errors);
}

std::vector<std::string> AppManager::app_ids() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return catalog_.ids();
}

const AppDescription *AppManager::app(const std::string &id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return catalog_.find(id);
}

std::string AppManager::launch(const std::string &app_id,
                               const std::map<std::string, std::string> &params,
                               std::string &error)
{
    RunningApp started;
    std::function<void(const RunningApp &)> notify;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const AppDescription *description = catalog_.find(app_id);
        if (!description) {
            error = "no such application: " + app_id;
            return {};
        }

        const pid_t pid = handler_->launch(*description, params, error);
        if (pid < 0)
            return {};

        RunningApp instance;
        instance.app_id = app_id;
        instance.instance_id = app_id + "-" + std::to_string(next_instance_++);
        instance.pid = pid;
        // "running" the moment it is forked: without a compositor telling us
        // when a window appears, any later transition would be invented.
        instance.status = "running";
        instance.since_ms = now_ms();

        instances_[instance.instance_id] = instance;
        started = instance;
        notify = on_status_;
    }

    if (notify)
        notify(started);
    return started.instance_id;
}

bool AppManager::close_instance(const std::string &instance_id, bool force, std::string &error)
{
    RunningApp closing;
    std::function<void(const RunningApp &)> notify;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        RunningApp *instance = find_locked(instance_id);
        if (!instance) {
            error = "no such instance: " + instance_id;
            return false;
        }
        if (!handler_->terminate(instance->pid, force)) {
            error = "cannot signal pid " + std::to_string(instance->pid);
            return false;
        }

        // Not "stopped": the process is still alive until the reaper says
        // otherwise, and claiming otherwise would let a client launch a second
        // copy too early.
        instance->status = "closing";
        instance->since_ms = now_ms();
        closing = *instance;
        notify = on_status_;
    }

    if (notify)
        notify(closing);
    return true;
}

bool AppManager::close_app(const std::string &app_id, bool force, std::string &error)
{
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &[instance_id, instance] : instances_)
            if (instance.app_id == app_id && instance.status != "stopped")
                ids.push_back(instance_id);
    }

    if (ids.empty()) {
        error = app_id + " is not running";
        return false;
    }

    bool all = true;
    for (const std::string &id : ids) {
        std::string one_error;
        if (!close_instance(id, force, one_error)) {
            error = one_error;
            all = false;
        }
    }
    return all;
}

bool AppManager::pause(const std::string &instance_id, std::string &error)
{
    RunningApp paused;
    std::function<void(const RunningApp &)> notify;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        RunningApp *instance = find_locked(instance_id);
        if (!instance) {
            error = "no such instance: " + instance_id;
            return false;
        }
        if (!handler_->pause(instance->pid)) {
            error = "cannot stop pid " + std::to_string(instance->pid);
            return false;
        }
        instance->status = "paused";
        instance->since_ms = now_ms();
        paused = *instance;
        notify = on_status_;
    }

    if (notify)
        notify(paused);
    return true;
}

bool AppManager::resume(const std::string &instance_id, std::string &error)
{
    RunningApp resumed;
    std::function<void(const RunningApp &)> notify;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        RunningApp *instance = find_locked(instance_id);
        if (!instance) {
            error = "no such instance: " + instance_id;
            return false;
        }
        if (!handler_->resume(instance->pid)) {
            error = "cannot continue pid " + std::to_string(instance->pid);
            return false;
        }
        instance->status = "running";
        instance->since_ms = now_ms();
        resumed = *instance;
        notify = on_status_;
    }

    if (notify)
        notify(resumed);
    return true;
}

std::vector<RunningApp> AppManager::running() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RunningApp> out;
    out.reserve(instances_.size());
    for (const auto &[id, instance] : instances_)
        out.push_back(instance);
    return out;
}

void AppManager::on_status_change(std::function<void(const RunningApp &)> callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    on_status_ = std::move(callback);
}

RunningApp *AppManager::find_locked(const std::string &instance_id)
{
    auto it = instances_.find(instance_id);
    return it == instances_.end() ? nullptr : &it->second;
}

void AppManager::handle_exit(pid_t pid, int status)
{
    RunningApp stopped;
    std::function<void(const RunningApp &)> notify;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &[instance_id, instance] : instances_) {
            if (instance.pid != pid)
                continue;

            instance.status = "stopped";
            instance.since_ms = now_ms();
            stopped = instance;
            notify = on_status_;
            found = true;
            break;
        }
        if (found)
            instances_.erase(stopped.instance_id);
    }

    if (!found)
        return;

    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    const int signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    std::fprintf(stderr, "esca-appmanagerd: %s (pid %d) exited, code %d signal %d\n",
                 stopped.instance_id.c_str(), pid, code, signal);

    if (notify)
        notify(stopped);
}

} // namespace esca::appmanager
