tinyim_generate_protos(chat_server_protos
    ${CMAKE_SOURCE_DIR}/protos/chat.proto
)

add_executable(chat_server
    main.cpp
    chat_service_impl.cpp
    ${chat_server_protos_SRCS}
)

target_link_libraries(chat_server
    common
    grpc++
    protobuf::libprotobuf
    spdlog::spdlog
)
