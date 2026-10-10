#include "app_catalog.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace esca::appmanager {

namespace fs = std::filesystem;
using nlohmann::json;

AppCatalog::AppCatalog(std::vector<std::string> directories)
    : directories_(std::move(directories))
{
}

size_t AppCatalog::reload(std::vector<std::string> &errors)
{
    apps_.clear();

    for (const std::string &dir : directories_) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
            continue; // a missing app directory is normal, not an error

        for (const fs::directory_entry &entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_directory())
                continue;
            const fs::path info = entry.path() / "appinfo.json";
            if (!fs::exists(info))
                continue;

            std::string error;
            if (!load_one(info.string(), error))
                errors.push_back(error);
        }
    }
    return apps_.size();
}

bool AppCatalog::load_one(const std::string &path, std::string &error)
{
    std::ifstream in(path);
    if (!in) {
        error = path + ": cannot open";
        return false;
    }

    json doc;
    try {
        in >> doc;
    } catch (const json::exception &e) {
        error = path + ": " + e.what();
        return false;
    }

    AppDescription app;
    app.dir = fs::path(path).parent_path().string();
    app.id = doc.value("id", "");
    app.title = doc.value("title", app.id);
    app.type = doc.value("type", "native");
    app.main = doc.value("main", "");
    app.icon = doc.value("icon", "");
    app.user = doc.value("user", "");
    app.removable = doc.value("removable", false);

    // A relative main is resolved against the app directory, so an app can be
    // self-contained in one folder.
    if (!app.main.empty() && app.main.front() != '/')
        app.main = (fs::path(app.dir) / app.main).string();

    if (doc.contains("args") && doc["args"].is_array()) {
        for (const auto &arg : doc["args"])
            if (arg.is_string())
                app.args.push_back(arg.get<std::string>());
    }
    if (doc.contains("env") && doc["env"].is_object()) {
        for (const auto &[key, value] : doc["env"].items())
            if (value.is_string())
                app.env[key] = value.get<std::string>();
    }

    if (!app.valid()) {
        error = path + ": needs both \"id\" and \"main\"";
        return false;
    }
    if (app.type != "native") {
        // Web and QML apps need a runtime to hand them to; there is none here
        // yet, and silently launching nothing would be worse than refusing.
        error = path + ": unsupported type \"" + app.type + "\" (only native)";
        return false;
    }

    apps_[app.id] = std::move(app);
    return true;
}

std::vector<std::string> AppCatalog::ids() const
{
    std::vector<std::string> out;
    out.reserve(apps_.size());
    for (const auto &[id, app] : apps_)
        out.push_back(id);
    return out;
}

const AppDescription *AppCatalog::find(const std::string &id) const
{
    auto it = apps_.find(id);
    return it == apps_.end() ? nullptr : &it->second;
}

} // namespace esca::appmanager
