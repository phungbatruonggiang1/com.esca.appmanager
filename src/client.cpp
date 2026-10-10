#include "esca/appmanager/client.hpp"

#include <sdbus-c++/sdbus-c++.h>

namespace esca::appmanager {

namespace {
/// Mirrors the service's RunningTuple.
using RunningTuple = sdbus::Struct<std::string, std::string, int32_t, std::string, uint64_t>;
} // namespace

struct AppManagerClient::Impl {
    std::unique_ptr<sdbus::IProxy> proxy;
    std::function<void(const RunningAppInfo &)> on_status;

    std::string service_error()
    {
        std::string message;
        try {
            proxy->callMethod("GetErrorMessage").onInterface(kInterface).storeResultsTo(message);
        } catch (const sdbus::Error &e) {
            message = e.getMessage();
        }
        return message;
    }
};

AppManagerClient::AppManagerClient() : impl_(std::make_unique<Impl>())
{
    // The proxy brings its own connection and event loop thread, so signals
    // arrive without the caller running a loop.
    impl_->proxy = sdbus::createProxy(sdbus::ServiceName{ kServiceName },
                                      sdbus::ObjectPath{ kObjectPath });
}

AppManagerClient::~AppManagerClient() = default;

std::vector<std::string> AppManagerClient::list_apps()
{
    std::vector<std::string> ids;
    try {
        impl_->proxy->callMethod("ListApps").onInterface(kInterface).storeResultsTo(ids);
    } catch (const sdbus::Error &) {
        // The service may not be up yet; an empty list says so well enough.
    }
    return ids;
}

bool AppManagerClient::app_info(const std::string &app_id, AppInfo &out, std::string &error)
{
    std::map<std::string, sdbus::Variant> info;
    try {
        impl_->proxy->callMethod("GetAppInfo")
            .onInterface(kInterface)
            .withArguments(app_id)
            .storeResultsTo(info);
    } catch (const sdbus::Error &e) {
        error = e.getMessage();
        return false;
    }

    if (info.empty()) {
        error = impl_->service_error();
        return false;
    }

    auto text = [&info](const char *key) -> std::string {
        auto it = info.find(key);
        return it == info.end() ? std::string() : it->second.get<std::string>();
    };

    out.id = text("id");
    out.title = text("title");
    out.type = text("type");
    out.main = text("main");
    out.icon = text("icon");
    auto removable = info.find("removable");
    out.removable = removable != info.end() && removable->second.get<bool>();
    return true;
}

std::string AppManagerClient::launch(const std::string &app_id,
                                     const std::map<std::string, std::string> &params,
                                     std::string &error)
{
    std::string instance;
    try {
        impl_->proxy->callMethod("Launch")
            .onInterface(kInterface)
            .withArguments(app_id, params)
            .storeResultsTo(instance);
    } catch (const sdbus::Error &e) {
        error = e.getMessage();
        return {};
    }

    if (instance.empty())
        error = impl_->service_error();
    return instance;
}

bool AppManagerClient::close_instance(const std::string &instance_id, bool force,
                                      std::string &error)
{
    int32_t rc = -1;
    try {
        impl_->proxy->callMethod("Close")
            .onInterface(kInterface)
            .withArguments(instance_id, force)
            .storeResultsTo(rc);
    } catch (const sdbus::Error &e) {
        error = e.getMessage();
        return false;
    }
    if (rc != 0) {
        error = impl_->service_error();
        return false;
    }
    return true;
}

bool AppManagerClient::close_app(const std::string &app_id, bool force, std::string &error)
{
    int32_t rc = -1;
    try {
        impl_->proxy->callMethod("CloseByAppId")
            .onInterface(kInterface)
            .withArguments(app_id, force)
            .storeResultsTo(rc);
    } catch (const sdbus::Error &e) {
        error = e.getMessage();
        return false;
    }
    if (rc != 0) {
        error = impl_->service_error();
        return false;
    }
    return true;
}

bool AppManagerClient::pause_instance(const std::string &instance_id, std::string &error)
{
    int32_t rc = -1;
    try {
        impl_->proxy->callMethod("Pause")
            .onInterface(kInterface)
            .withArguments(instance_id)
            .storeResultsTo(rc);
    } catch (const sdbus::Error &e) {
        error = e.getMessage();
        return false;
    }
    if (rc != 0) {
        error = impl_->service_error();
        return false;
    }
    return true;
}

bool AppManagerClient::resume_instance(const std::string &instance_id, std::string &error)
{
    int32_t rc = -1;
    try {
        impl_->proxy->callMethod("Resume")
            .onInterface(kInterface)
            .withArguments(instance_id)
            .storeResultsTo(rc);
    } catch (const sdbus::Error &e) {
        error = e.getMessage();
        return false;
    }
    if (rc != 0) {
        error = impl_->service_error();
        return false;
    }
    return true;
}

std::vector<RunningAppInfo> AppManagerClient::running()
{
    std::vector<RunningTuple> tuples;
    try {
        impl_->proxy->callMethod("Running").onInterface(kInterface).storeResultsTo(tuples);
    } catch (const sdbus::Error &) {
        return {};
    }

    std::vector<RunningAppInfo> out;
    out.reserve(tuples.size());
    for (const RunningTuple &tuple : tuples) {
        RunningAppInfo info;
        info.app_id = tuple.get<0>();
        info.instance_id = tuple.get<1>();
        info.pid = tuple.get<2>();
        info.status = tuple.get<3>();
        info.since_ms = tuple.get<4>();
        out.push_back(info);
    }
    return out;
}

bool AppManagerClient::reload(std::string &error)
{
    int32_t count = -1;
    try {
        impl_->proxy->callMethod("Reload").onInterface(kInterface).storeResultsTo(count);
    } catch (const sdbus::Error &e) {
        error = e.getMessage();
        return false;
    }
    return count >= 0;
}

void AppManagerClient::on_life_status(std::function<void(const RunningAppInfo &)> callback)
{
    impl_->on_status = std::move(callback);
    impl_->proxy->uponSignal("AppLifeStatusChanged")
        .onInterface(kInterface)
        .call([this](const std::string &app_id, const std::string &instance_id,
                     const std::string &status) {
            if (!impl_->on_status)
                return;
            RunningAppInfo info;
            info.app_id = app_id;
            info.instance_id = instance_id;
            info.status = status;
            impl_->on_status(info);
        });
}

} // namespace esca::appmanager
