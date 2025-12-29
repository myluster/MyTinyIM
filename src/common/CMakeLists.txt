add_library(common
    db_pool.cpp
    redis_client.cpp
    service_registry.cpp
)

# Use PkgConfig to find non-cmake packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)
pkg_check_modules(HIREDIS REQUIRED hiredis)

target_include_directories(common PUBLIC 
    ${MYSQL_INCLUDE_DIRS}
    ${HIREDIS_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_directories(common PUBLIC
    ${MYSQL_LIBRARY_DIRS}
    ${HIREDIS_LIBRARY_DIRS}
)

target_link_libraries(common
    ${MYSQL_LIBRARIES}
    ${HIREDIS_LIBRARIES}
    spdlog::spdlog
)
