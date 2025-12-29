tinyim_generate_protos(auth_server_protos
    ${CMAKE_SOURCE_DIR}/protos/auth.proto
)

add_executable(auth_server
    main.cpp
    auth_service_impl.cpp
    ${auth_server_protos_SRCS}
)

target_link_libraries(auth_server
    common
    grpc++
    protobuf::libprotobuf
    spdlog::spdlog
)
