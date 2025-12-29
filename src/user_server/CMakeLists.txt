tinyim_generate_protos(user_server_protos
    ${CMAKE_SOURCE_DIR}/protos/relation.proto
)
tinyim_generate_protos(chat_client_protos
    ${CMAKE_SOURCE_DIR}/protos/chat.proto
)

add_executable(user_server
    main.cpp
    relation_service_impl.cpp
    ${user_server_protos_SRCS}
    ${chat_client_protos_SRCS}
)

target_link_libraries(user_server
    common
    grpc++
    protobuf::libprotobuf
    spdlog::spdlog
)
