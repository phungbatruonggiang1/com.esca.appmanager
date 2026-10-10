cmake_minimum_required(VERSION 3.16)
project(esca-appmanager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)

pkg_check_modules(SDBUSCPP REQUIRED IMPORTED_TARGET sdbus-c++)

# --- libesca-appclient: what launchers and applications link ----------------
add_library(esca-appclient SHARED src/client.cpp)
target_include_directories(esca-appclient PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(esca-appclient PUBLIC PkgConfig::SDBUSCPP)
set_target_properties(esca-appclient PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0)

# --- esca-appmanagerd: the service ------------------------------------------
add_executable(esca-appmanagerd
    src/main.cpp
    src/app_catalog.cpp
    src/life_handler.cpp
    src/app_manager.cpp
    src/service.cpp)
target_include_directories(esca-appmanagerd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(esca-appmanagerd PRIVATE
    PkgConfig::SDBUSCPP nlohmann_json::nlohmann_json Threads::Threads)

# --- esca-appctl: CLI, and the way to test without a launcher ---------------
add_executable(esca-appctl src/appctl.cpp)
target_include_directories(esca-appctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(esca-appctl PRIVATE esca-appclient)

install(TARGETS esca-appclient LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS esca-appmanagerd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS esca-appctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/esca DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES data/vn.sparc.esca.AppManager1.conf
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d)
install(FILES data/esca-appmanagerd.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
install(DIRECTORY apps/ DESTINATION ${CMAKE_INSTALL_DATADIR}/esca/applications)
