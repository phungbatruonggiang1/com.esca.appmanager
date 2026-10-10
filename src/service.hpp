#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

#include "app_manager.hpp"

namespace esca::appmanager {

/// One running instance as it crosses the bus: app id, instance id, pid,
/// status, and when that status was set.
using RunningTuple = sdbus::Struct<std::string, std::string, int32_t, std::string, uint64_t>;

class AppManagerService {
public:
    AppManagerService(sdbus::IConnection &connection, AppManager &manager);
    ~AppManagerService();

private:
    std::vector<std::string> list_apps();
    std::map<std::string, sdbus::Variant> app_info(const std::string &app_id);
    std::string launch(const std::string &app_id,
                       const std::map<std::string, std::string> &params);
    int32_t close_instance(const std::string &instance_id, bool force);
    int32_t close_app(const std::string &app_id, bool force);
    int32_t pause(const std::string &instance_id);
    int32_t resume(const std::string &instance_id);
    std::vector<RunningTuple> running();
    int32_t reload();

    void emit_status(const RunningApp &instance);

    sdbus::IConnection &connection_;
    AppManager &manager_;
    std::unique_ptr<sdbus::IObject> object_;
    std::string last_error_;
};

} // namespace esca::appmanager
