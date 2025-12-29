# Helper function to generate protos messages and grpc services
function(tinyim_generate_protos TARGET_NAME PROTO_FILES)
    set(_PROTO_SRCS)
    set(_PROTO_HDRS)
    set(_GRPC_SRCS)
    set(_GRPC_HDRS)

    foreach(FIL ${PROTO_FILES})
        get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
        get_filename_component(FIL_WE ${FIL} NAME_WE)

        # Generate Protobuf
        list(APPEND _PROTO_SRCS "${CMAKE_BINARY_DIR}/${FIL_WE}.pb.cc")
        list(APPEND _PROTO_HDRS "${CMAKE_BINARY_DIR}/${FIL_WE}.pb.h")
        
        # Generate gRPC
        list(APPEND _GRPC_SRCS "${CMAKE_BINARY_DIR}/${FIL_WE}.grpc.pb.cc")
        list(APPEND _GRPC_HDRS "${CMAKE_BINARY_DIR}/${FIL_WE}.grpc.pb.h")

        add_custom_command(
            OUTPUT "${CMAKE_BINARY_DIR}/${FIL_WE}.pb.cc"
                   "${CMAKE_BINARY_DIR}/${FIL_WE}.pb.h"
                   "${CMAKE_BINARY_DIR}/${FIL_WE}.grpc.pb.cc"
                   "${CMAKE_BINARY_DIR}/${FIL_WE}.grpc.pb.h"
            COMMAND ${_PROTOBUF_PROTOC}
            ARGS --grpc_out=${CMAKE_BINARY_DIR}
                 --cpp_out=${CMAKE_BINARY_DIR}
                 -I "${CMAKE_SOURCE_DIR}/protos"
                 --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin
                 "${ABS_FIL}"
            DEPENDS "${ABS_FIL}"
            COMMENT "Running gRPC C++ protocol buffer compiler on ${FIL}"
            VERBATIM
        )
    endforeach()

    set_source_files_properties(${_PROTO_SRCS} ${_PROTO_HDRS} ${_GRPC_SRCS} ${_GRPC_HDRS} PROPERTIES GENERATED TRUE)
    
    # Export variables to parent scope
    set(${TARGET_NAME}_SRCS ${_PROTO_SRCS} ${_GRPC_SRCS} PARENT_SCOPE)
    set(${TARGET_NAME}_HDRS ${_PROTO_HDRS} ${_GRPC_HDRS} PARENT_SCOPE)
endfunction()
