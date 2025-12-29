add_executable(gateway_server
    main.cpp
    http_session.cpp
    websocket_session.cpp
    connection_manager.cpp
)

target_link_libraries(gateway_server
    common
    grpc++
    protobuf::libprotobuf
    spdlog::spdlog
    Boost::system
    Boost::thread
)

tinyim_generate_protos(auth_client_protos
    ${CMAKE_SOURCE_DIR}/protos/auth.proto
)
tinyim_generate_protos(chat_client_protos
    ${CMAKE_SOURCE_DIR}/protos/chat.proto
)

target_link_libraries(gateway_server
    ${auth_client_protos_SRCS}
    ${chat_client_protos_SRCS}
)
