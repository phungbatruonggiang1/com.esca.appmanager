#include "life_handler.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <grp.h>
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

namespace esca::appmanager {

namespace fs = std::filesystem;

namespace {

/// Weston creates wayland-0 on some images and wayland-1 on others, so look
/// for whatever socket is actually there instead of guessing. Learned the hard
/// way getting the dashboard to start.
std::string find_wayland_display(const std::string &runtime_dir)
{
    std::error_code ec;
    if (!fs::is_directory(runtime_dir, ec))
        return {};

    for (const auto &entry : fs::directory_iterator(runtime_dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("wayland-", 0) == 0 && name.find(".lock") == std::string::npos)
            return name;
    }
    return {};
}

struct Credentials {
    uid_t uid;
    gid_t gid;
    std::string home;
    bool valid = false;
};

Credentials lookup_user(const std::string &name)
{
    Credentials out;
    if (name.empty())
        return out;

    const struct passwd *pw = getpwnam(name.c_str());
    if (!pw)
        return out;

    out.uid = pw->pw_uid;
    out.gid = pw->pw_gid;
    out.home = pw->pw_dir ? pw->pw_dir : "";
    out.valid = true;
    return out;
}

[[noreturn]] void child_fail(const char *what)
{
    // In the child after fork: no allocation, no exceptions, just say why and
    // leave with a status the parent can tell apart from the app's own.
    const char *reason = std::strerror(errno);
    ssize_t ignored = write(STDERR_FILENO, what, std::strlen(what));
    ignored = write(STDERR_FILENO, ": ", 2);
    ignored = write(STDERR_FILENO, reason, std::strlen(reason));
    ignored = write(STDERR_FILENO, "\n", 1);
    (void)ignored;
    _exit(127);
}

} // namespace

pid_t NativeLifeHandler::launch(const AppDescription &app,
                                const std::map<std::string, std::string> &params,
                                std::string &error)
{
    if (access(app.main.c_str(), X_OK) != 0) {
        error = app.main + ": " + std::strerror(errno);
        return -1;
    }

    const Credentials creds = lookup_user(app.user);
    if (!app.user.empty() && !creds.valid) {
        error = "no such user: " + app.user;
        return -1;
    }

    // Build argv and env before forking: after fork only async-signal-safe
    // calls are allowed, and these allocate.
    std::vector<std::string> argv_storage{ app.main };
    for (const std::string &arg : app.args)
        argv_storage.push_back(arg);
    for (const auto &[key, value] : params)
        argv_storage.push_back("--" + key + "=" + value);

    std::vector<std::string> env_storage;
    const uid_t target_uid = creds.valid ? creds.uid : getuid();
    const std::string runtime_dir = "/run/user/" + std::to_string(target_uid);
    env_storage.push_back("XDG_RUNTIME_DIR=" + runtime_dir);
    const std::string wayland = find_wayland_display(runtime_dir);
    if (!wayland.empty())
        env_storage.push_back("WAYLAND_DISPLAY=" + wayland);
    if (!creds.home.empty())
        env_storage.push_back("HOME=" + creds.home);
    env_storage.push_back("PATH=/usr/sbin:/usr/bin:/sbin:/bin");
    env_storage.push_back("ESCA_APP_ID=" + app.id);
    for (const auto &[key, value] : app.env)
        env_storage.push_back(key + "=" + value);

    std::vector<char *> argv;
    for (std::string &s : argv_storage)
        argv.push_back(s.data());
    argv.push_back(nullptr);

    std::vector<char *> envp;
    for (std::string &s : env_storage)
        envp.push_back(s.data());
    envp.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        return -1;
    }

    if (pid == 0) {
        // Own session, so closing the app takes its children with it and a
        // crash in the app cannot signal the manager.
        if (setsid() < 0)
            child_fail("setsid");

        if (creds.valid) {
            if (initgroups(app.user.c_str(), creds.gid) < 0)
                child_fail("initgroups");
            if (setgid(creds.gid) < 0)
                child_fail("setgid");
            if (setuid(creds.uid) < 0)
                child_fail("setuid");
        }

        if (!app.dir.empty())
            (void)chdir(app.dir.c_str());

        execve(app.main.c_str(), argv.data(), envp.data());
        child_fail("execve");
    }

    return pid;
}

bool NativeLifeHandler::terminate(pid_t pid, bool force)
{
    // Signal the whole process group: an application that spawned helpers
    // should take them with it.
    return kill(-pid, force ? SIGKILL : SIGTERM) == 0 || kill(pid, force ? SIGKILL : SIGTERM) == 0;
}

bool NativeLifeHandler::pause(pid_t pid)
{
    return kill(-pid, SIGSTOP) == 0 || kill(pid, SIGSTOP) == 0;
}

bool NativeLifeHandler::resume(pid_t pid)
{
    return kill(-pid, SIGCONT) == 0 || kill(pid, SIGCONT) == 0;
}

ChildReaper::ChildReaper(ExitCallback on_exit) : on_exit_(std::move(on_exit))
{
    thread_ = std::thread(&ChildReaper::run, this);
}

ChildReaper::~ChildReaper()
{
    stop_ = true;
    if (thread_.joinable())
        thread_.join();
}

void ChildReaper::run()
{
    while (!stop_) {
        int status = 0;
        const pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            if (on_exit_)
                on_exit_(pid, status);
            continue; // there may be more waiting
        }
        // No child has exited (or there are none); waiting blocks shutdown, so
        // poll instead. Application exits are rare enough for 200ms to be
        // invisible.
        usleep(200 * 1000);
    }
}

} // namespace esca::appmanager
