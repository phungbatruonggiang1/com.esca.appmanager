// The list of installed applications, read from appinfo.json files.
//
// SAM keeps AppDescription/AppDescriptionList for this and treats installation
// as somebody else's job (AppInstallService). Same split here: the catalogue
// reads what is on disk, and the package manager puts it there.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace esca::appmanager {

struct AppDescription {
    std::string id;
    std::string title;
    std::string type = "native";
    std::string main;              ///< executable
    std::string icon;
    std::string user;              ///< run as this user; empty means the service's own
    bool removable = false;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    std::string dir;               ///< directory the appinfo.json came from

    bool valid() const { return !id.empty() && !main.empty(); }
};

class AppCatalog {
public:
    explicit AppCatalog(std::vector<std::string> directories);

    /// Rescan the directories. Returns how many applications were found, and
    /// reports unreadable entries through @p errors rather than failing.
    size_t reload(std::vector<std::string> &errors);

    std::vector<std::string> ids() const;
    const AppDescription *find(const std::string &id) const;

private:
    bool load_one(const std::string &path, std::string &error);

    std::vector<std::string> directories_;
    std::map<std::string, AppDescription> apps_;
};

} // namespace esca::appmanager
