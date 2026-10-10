#include "service.hpp"

#include <cstdio>

#include "esca/appmanager/client.hpp" // name, path and interface constants

namespace esca::appmanager {

namespace {
constexpr int32_t kOk = 0;
constexpr int32_t kErr = -1;
} // namespace

AppManagerService::AppManagerService(sdbus::IConnection &connection, AppManager &manager)
    : connection_(connection), manager_(manager)
{
    object_ = sdbus::createObject(connection_, sdbus::ObjectPath{ kObjectPath });

    object_->addVTable(
        sdbus::registerMethod("ListApps").implementedAs([this] { return list_apps(); }),
        sdbus::registerMethod("GetAppInfo")
            .implementedAs([this](const std::string &id) { return app_info(id); }),
        sdbus::registerMethod("Launch")
            .implementedAs([this](const std::string &id,
                                  const std::map<std::string, std::string> &params) {
                return launch(id, params);
            }),
        sdbus::registerMethod("Close")
            .implementedAs([this](const std::string &instance, const bool &force) {
                return close_instance(instance, force);
            }),
        sdbus::registerMethod("CloseByAppId")
            .implementedAs([this](const std::string &id, const bool &force) {
                return close_app(id, force);
            }),
        sdbus::registerMethod("Pause")
            .implementedAs([this](const std::string &instance) { return pause(instance); }),
        sdbus::registerMethod("Resume")
            .implementedAs([this](const std::string &instance) { return resume(instance); }),
        sdbus::registerMethod("Running").implementedAs([this] { return running(); }),
        sdbus::registerMethod("Reload").implementedAs([this] { return reload(); }),
        sdbus::registerMethod("GetErrorMessage").implementedAs([this] { return last_error_; }),
        sdbus::registerSignal("AppLifeStatusChanged")
            .withParameters<std::string, std::string, std::string>(),
        sdbus::registerSignal("CatalogChanged"))
        .forInterface(kInterface);

    // The reaper thread calls this too, when an application exits on its own.
    // sdbus-c++ connections are safe to send from another thread, which is
    // what keeps this from needing a queue back onto the event loop.
    manager_.on_status_change([this](const RunningApp &instance) { emit_status(instance); });
}

AppManagerService::~AppManagerService()
{
    // The manager outlives this object in main(), so stop it calling back into
    // a half-destroyed signal emitter.
    manager_.on_status_change(nullptr);
}

std::vector<std::string> AppManagerService::list_apps()
{
    return manager_.app_ids();
}

std::map<std::string, sdbus::Variant> AppManagerService::app_info(const std::string &app_id)
{
    std::map<std::string, sdbus::Variant> out;
    const AppDescription *app = manager_.app(app_id);
    if (!app) {
        last_error_ = "no such application: " + app_id;
        return out;
    }

    out["id"] = sdbus::Variant(app->id);
    out["title"] = sdbus::Variant(app->title);
    out["type"] = sdbus::Variant(app->type);
    out["main"] = sdbus::Variant(app->main);
    out["icon"] = sdbus::Variant(app->icon);
    out["removable"] = sdbus::Variant(app->removable);
    return out;
}

std::string AppManagerService::launch(const std::string &app_id,
                                      const std::map<std::string, std::string> &params)
{
    std::string error;
    const std::string instance = manager_.launch(app_id, params, error);
    if (instance.empty()) {
        last_error_ = error;
        std::fprintf(stderr, "esca-appmanagerd: launch %s failed: %s\n", app_id.c_str(),
                     error.c_str());
    }
    return instance;
}

int32_t AppManagerService::close_instance(const std::string &instance_id, bool force)
{
    std::string error;
    if (!manager_.close_instance(instance_id, force, error)) {
        last_error_ = error;
        return kErr;
    }
    return kOk;
}

int32_t AppManagerService::close_app(const std::string &app_id, bool force)
{
    std::string error;
    if (!manager_.close_app(app_id, force, error)) {
        last_error_ = error;
        return kErr;
    }
    return kOk;
}

int32_t AppManagerService::pause(const std::string &instance_id)
{
    std::string error;
    if (!manager_.pause(instance_id, error)) {
        last_error_ = error;
        return kErr;
    }
    return kOk;
}

int32_t AppManagerService::resume(const std::string &instance_id)
{
    std::string error;
    if (!manager_.resume(instance_id, error)) {
        last_error_ = error;
        return kErr;
    }
    return kOk;
}

std::vector<RunningTuple> AppManagerService::running()
{
    std::vector<RunningTuple> out;
    for (const RunningApp &instance : manager_.running())
        out.emplace_back(instance.app_id, instance.instance_id,
                         static_cast<int32_t>(instance.pid), instance.status,
                         instance.since_ms);
    return out;
}

int32_t AppManagerService::reload()
{
    std::vector<std::string> errors;
    const size_t count = manager_.reload(errors);
    for (const std::string &error : errors)
        std::fprintf(stderr, "esca-appmanagerd: %s\n", error.c_str());

    last_error_ = errors.empty() ? "" : errors.front();
    object_->emitSignal(sdbus::SignalName{ "CatalogChanged" })
        .onInterface(sdbus::InterfaceName{ kInterface });
    return static_cast<int32_t>(count);
}

void AppManagerService::emit_status(const RunningApp &instance)
{
    object_->emitSignal(sdbus::SignalName{ "AppLifeStatusChanged" })
        .onInterface(sdbus::InterfaceName{ kInterface })
        .withArguments(instance.app_id, instance.instance_id, instance.status);
}

} // namespace esca::appmanager
