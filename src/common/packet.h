#pragma once

#include <cstdint>

#pragma pack(push, 1)
struct PacketHeader {
    char magic[2] = {'I', 'M'};
    uint8_t version = 1;
    uint16_t cmd_id = 0;
    uint32_t body_len = 0;
};
#pragma pack(pop)

enum CommandID : uint16_t {
    CMD_LOGIN_REQ = 0x1001, 
    CMD_LOGIN_RESP = 0x1002,
    CMD_HEARTBEAT_REQ = 0x1003, 
    CMD_HEARTBEAT_RESP = 0x1004,
    CMD_LOGOUT_REQ = 0x1005,
    CMD_LOGOUT_RESP = 0x1006,
    
    // Message
    CMD_MSG_SEND_REQ = 0x2001, 
    CMD_MSG_SEND_RESP = 0x2002,
    CMD_MSG_PUSH_NOTIFY = 0x2003, // Server -> Client (Signal)
    CMD_MSG_SYNC_REQ = 0x2004, // Client -> Server (Pull)
    CMD_MSG_SYNC_RESP = 0x2005,
    
    // Relation (Friend)
    CMD_FRIEND_APPLY_REQ = 0x3001,
    CMD_FRIEND_APPLY_RESP = 0x3002,
    CMD_FRIEND_ACCEPT_REQ = 0x3003,
    CMD_FRIEND_ACCEPT_RESP = 0x3004,
    CMD_FRIEND_LIST_REQ = 0x3005,
    CMD_FRIEND_LIST_RESP = 0x3006,

    // Group
    CMD_GROUP_CREATE_REQ = 0x4001,
    CMD_GROUP_CREATE_RESP = 0x4002,
    CMD_GROUP_JOIN_REQ = 0x4003,
    CMD_GROUP_JOIN_RESP = 0x4004,
    CMD_GROUP_LIST_REQ = 0x4005,
    CMD_GROUP_LIST_RESP = 0x4006,
    
    CMD_GROUP_APPLY_REQ = 0x4007,
    CMD_GROUP_APPLY_RESP = 0x4008,
    CMD_GROUP_ACCEPT_REQ = 0x4009,
    CMD_GROUP_ACCEPT_RESP = 0x4010
};
