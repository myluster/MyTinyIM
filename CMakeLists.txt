cmake_minimum_required(VERSION 3.10)
project(TinyIM)

set(CMAKE_CXX_STANDARD 20)

# Dependencies
# Dependencies
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
find_package(Threads REQUIRED)

# Find executables explicitly since we are not using CONFIG mode for gRPC
find_program(_PROTOBUF_PROTOC protoc)
find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

if(NOT _PROTOBUF_PROTOC OR NOT _GRPC_CPP_PLUGIN_EXECUTABLE)
    message(FATAL_ERROR "Could not find protoc or grpc_cpp_plugin!")
endif()

# New dependencies for gateway/tests
find_package(spdlog REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(GTest REQUIRED)

# Alias for compatibility with existing targets
if(NOT TARGET gRPC::grpc++)
    add_library(gRPC::grpc++ INTERFACE IMPORTED)
    target_include_directories(gRPC::grpc++ INTERFACE ${GRPC_INCLUDE_DIRS})
    target_link_directories(gRPC::grpc++ INTERFACE ${GRPC_LIBRARY_DIRS})
    target_link_libraries(gRPC::grpc++ INTERFACE ${GRPC_LIBRARIES})
endif()

# Protobuf generation helper
include(CMake/ProtoBuf.cmake)

# Includes
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_BINARY_DIR}) # For generated protos

# Subdirectories
add_subdirectory(src/common)
add_subdirectory(src/auth_server)
add_subdirectory(src/chat_server)
add_subdirectory(src/gateway)
add_subdirectory(src/user_server)
add_subdirectory(tests)
