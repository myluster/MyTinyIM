project(TinyIMTests)

# Generate Protos
tinyim_generate_protos(relation_protos ${CMAKE_SOURCE_DIR}/protos/relation.proto)
tinyim_generate_protos(chat_client_protos ${CMAKE_SOURCE_DIR}/protos/chat.proto)

# Create object libraries for proto sources
add_library(relation_protos_obj OBJECT ${relation_protos_SRCS})
add_library(chat_client_protos_obj OBJECT ${chat_client_protos_SRCS})

# Integration Test (The Single Test Binary)
add_executable(integration_test 
    integration_test.cpp 
    test_client.cpp
    $<TARGET_OBJECTS:chat_client_protos_obj>
    $<TARGET_OBJECTS:relation_protos_obj>
)

target_link_libraries(integration_test
    common
    protobuf::libprotobuf
    gRPC::grpc++
    GTest::gtest_main
    spdlog::spdlog
    Boost::system
)
