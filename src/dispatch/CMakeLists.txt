# Generate Auth Protos for Dispatch Client
tinyim_generate_protos(dispatch_protos
    ${CMAKE_SOURCE_DIR}/protos/auth.proto
)

add_executable(dispatch_server 
    main.cpp 
    service_discovery.h
    ${dispatch_protos_SRCS}
)

target_link_libraries(dispatch_server 
    PRIVATE 
    common
    ${GRPC_LIBRARIES} 
    ${Protobuf_LIBRARIES} 
    Boost::system 
    Boost::thread
    spdlog::spdlog
)
