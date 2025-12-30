
// Protocol Constants
export const MAGIC = [0x49, 0x4D]; // "IM"
export const PROTOCOL_VERSION = 1;

export const CMD = {
    LOGIN_REQ: 0x1001,
    LOGIN_RESP: 0x1002,
    MSG_SEND_REQ: 0x2001,
    MSG_SEND_RESP: 0x2002,
    MSG_PUSH_NOTIFY: 0x2003,
    MSG_SYNC_REQ: 0x2004,
    MSG_SYNC_RESP: 0x2005,
    HEARTBEAT_REQ: 0x1003,
    HEARTBEAT_RESP: 0x1004,
    LOGOUT_REQ: 0x1005,
    LOGOUT_RESP: 0x1006,
    KICK_NOTIFY: 0x1006, // Same as Logout Resp (Server sends LogoutResp for Kick)

    // Relation (Friend) - 0x30xx
    FRIEND_APPLY_REQ: 0x3001,
    FRIEND_APPLY_RESP: 0x3002,
    FRIEND_HANDLE_REQ: 0x3003, // CMD_FRIEND_ACCEPT_REQ
    FRIEND_HANDLE_RESP: 0x3004,
    FRIEND_LIST_REQ: 0x3005,
    FRIEND_LIST_RESP: 0x3006,
    // FRIEND_DEL not in server yet

    // Group - 0x40xx
    GROUP_CREATE_REQ: 0x4001,
    GROUP_CREATE_RESP: 0x4002,
    GROUP_JOIN_REQ: 0x4003,
    GROUP_JOIN_RESP: 0x4004,
    GROUP_LIST_REQ: 0x4005,
    GROUP_LIST_RESP: 0x4006,

    GROUP_APPLY_REQ: 0x4007,
    GROUP_APPLY_RESP: 0x4008,
    GROUP_HANDLE_REQ: 0x4009, // CMD_GROUP_ACCEPT_REQ
    GROUP_HANDLE_RESP: 0x4010,

    GROUP_QUIT_REQ: 0x4011, // Guessing if exists, or remove if not in header
    GROUP_QUIT_RESP: 0x4012,
};

export class IMProtocol {
    static buildMessage(cmd, bodyBytes) {
        // Body bytes must be ArrayBuffer, Uint8Array, or similar
        const bodyArray = bodyBytes instanceof Uint8Array ? bodyBytes : new Uint8Array(bodyBytes);

        const headerLen = 9; // Magic(2) + Ver(1) + Cmd(2) + Len(4) = 9
        const buffer = new ArrayBuffer(headerLen + bodyArray.length);
        const view = new DataView(buffer);

        // Header
        view.setUint8(0, MAGIC[0]);
        view.setUint8(1, MAGIC[1]);
        view.setUint8(2, PROTOCOL_VERSION);
        view.setUint16(3, cmd, false); // Big endian (Network)
        // No Seq in header
        view.setUint32(5, bodyArray.length, false);

        // Body
        new Uint8Array(buffer, headerLen).set(bodyArray);

        return buffer;
    }

    static parseMessage(data) {
        if (data.byteLength < 9) {
            throw new Error("Message too short");
        }
        const view = new DataView(data);

        const header = {
            magic: [view.getUint8(0), view.getUint8(1)],
            version: view.getUint8(2),
            cmd: view.getUint16(3, false),
            length: view.getUint32(5, false)
        };

        const body = data.slice(9);
        return { header, body: new Uint8Array(body) };
    }

    // --- Simple Protobuf-like Encoders (Matching C++ server logic) ---

    static encodeVarint(value) {
        const bytes = [];
        while (value > 127) {
            bytes.push((value & 0x7F) | 0x80);
            value >>>= 7;
        }
        bytes.push(value & 0x7F);
        return bytes;
    }

    static encodeLoginReq(username, password, deviceType = 3) {
        const usernameBytes = new TextEncoder().encode(username.toString());
        const passwordBytes = new TextEncoder().encode(password);
        // device_id is optional string
        const deviceId = "web-" + Math.floor(Math.random() * 10000);
        const deviceIdBytes = new TextEncoder().encode(deviceId);

        const buffer = [];

        // Field 1: username (string, wire=2) -> tag = (1<<3)|2 = 10
        buffer.push(10);
        buffer.push(...this.encodeVarint(usernameBytes.length));
        buffer.push(...usernameBytes);

        // Field 2: password (string, wire=2) -> tag = 18
        buffer.push(18);
        buffer.push(...this.encodeVarint(passwordBytes.length));
        buffer.push(...passwordBytes);

        // Field 3: device_id (string, wire=2) -> tag = (3<<3)|2 = 26
        buffer.push(26);
        buffer.push(...this.encodeVarint(deviceIdBytes.length));
        buffer.push(...deviceIdBytes);

        // Field 4: device_type (enum/varint, wire=0) -> tag = (4<<3)|0 = 32
        buffer.push(32);
        buffer.push(...this.encodeVarint(deviceType));

        return new Uint8Array(buffer);
    }

    static encodeMsgSendReq(senderId, receiverId, content) {
        const contentBytes = new TextEncoder().encode(content);
        const buffer = [];

        // Field 1: sender_id
        buffer.push((1 << 3) | 0);
        buffer.push(...this.encodeVarint(senderId));

        // Field 2: receiver_id
        buffer.push((2 << 3) | 0);
        buffer.push(...this.encodeVarint(receiverId));

        // Field 4: type (TEXT=1)
        buffer.push((4 << 3) | 0);
        buffer.push(1);

        // Field 5: content
        buffer.push((5 << 3) | 2);
        buffer.push(...this.encodeVarint(contentBytes.length));
        buffer.push(...contentBytes);

        return new Uint8Array(buffer);
    }

    static encodeFriendApplyReq(userId, targetUserId, reason) {
        const reasonBytes = new TextEncoder().encode(reason);
        const buffer = [];

        // Field 1: user_id
        buffer.push((1 << 3) | 0);
        buffer.push(...this.encodeVarint(userId));

        // Field 2: target_user_id
        buffer.push((2 << 3) | 0);
        buffer.push(...this.encodeVarint(targetUserId));

        // Field 3: reason
        buffer.push((3 << 3) | 2);
        buffer.push(...this.encodeVarint(reasonBytes.length));
        buffer.push(...reasonBytes);

        return new Uint8Array(buffer);
    }

    static encodeFriendHandleReq(userId, requestUserId, agree) {
        const buffer = [];

        // Field 1: user_id
        buffer.push((1 << 3) | 0);
        buffer.push(...this.encodeVarint(userId));

        // Field 2: apply_id (placeholder 0)
        buffer.push((2 << 3) | 0);
        buffer.push(0);

        // Field 3: requester_id
        buffer.push((3 << 3) | 0);
        buffer.push(...this.encodeVarint(requestUserId));

        // Field 4: accept
        buffer.push((4 << 3) | 0);
        buffer.push(agree ? 1 : 0);

        return new Uint8Array(buffer);
    }

    static encodeFriendDelReq(userId, friendId) {
        const buffer = [];
        buffer.push((1 << 3) | 0);
        buffer.push(...this.encodeVarint(userId));
        buffer.push((2 << 3) | 0);
        buffer.push(...this.encodeVarint(friendId));
        return new Uint8Array(buffer);
    }

    static encodeMsgSyncReq(userId, localMaxSeq, limit) {
        const buffer = [];
        buffer.push((1 << 3) | 0);
        buffer.push(...this.encodeVarint(userId));
        buffer.push((2 << 3) | 0);
        buffer.push(...this.encodeVarint(localMaxSeq));
        buffer.push((3 << 3) | 0);
        buffer.push(...this.encodeVarint(limit));
        return new Uint8Array(buffer);
    }

    static encodeGroupJoinReq(groupId, userId) {
        const buffer = [];
        buffer.push((1 << 3) | 0);
        buffer.push(...this.encodeVarint(groupId));
        buffer.push((2 << 3) | 0);
        buffer.push(...this.encodeVarint(userId));
        return new Uint8Array(buffer);
    }

    static encodeGroupQuitReq(userId, groupId) {
        const buffer = [];
        buffer.push((1 << 3) | 0);
        buffer.push(...this.encodeVarint(userId));
        buffer.push((2 << 3) | 0);
        buffer.push(...this.encodeVarint(groupId));
        return new Uint8Array(buffer);
    }

    static encodeGroupCreateReq(ownerId, groupName) {
        const nameBytes = new TextEncoder().encode(groupName);
        const buffer = [];
        // Field 1: group_name (string)
        buffer.push((1 << 3) | 2);
        buffer.push(...this.encodeVarint(nameBytes.length));
        buffer.push(...nameBytes);

        // Field 2: owner_id (varint)
        buffer.push((2 << 3) | 0);
        buffer.push(...this.encodeVarint(ownerId));

        return new Uint8Array(buffer);
    }

    static encodeGroupMsgReq(senderId, groupId, content) {
        const contentBytes = new TextEncoder().encode(content);
        const buffer = [];

        // Field 1: sender_id
        buffer.push((1 << 3) | 0);
        buffer.push(...this.encodeVarint(senderId));

        // Field 3: group_id
        buffer.push((3 << 3) | 0);
        buffer.push(...this.encodeVarint(groupId));

        // Field 4: type (TEXT=1)
        buffer.push((4 << 3) | 0);
        buffer.push(1);

        // Field 5: content
        buffer.push((5 << 3) | 2);
        buffer.push(...this.encodeVarint(contentBytes.length));
        buffer.push(...contentBytes);

        return new Uint8Array(buffer);
    }
    // --- Decoders ---

    static decodeVarint(view, offset) {
        let value = 0;
        let shift = 0;
        let length = 0;
        while (true) {
            if (offset + length >= view.byteLength) throw new Error("Varint out of bounds");
            const byte = view.getUint8(offset + length);
            value |= (byte & 0x7F) << shift;
            shift += 7;
            length++;
            if ((byte & 0x80) === 0) break;
        }
        return { value, length };
    }

    static decodeString(view, offset, len) {
        const bytes = new Uint8Array(view.buffer, view.byteOffset + offset, len);
        return new TextDecoder().decode(bytes);
    }

    static decodeLoginResp(buffer) {
        const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
        let offset = 0;
        const result = { user_id: 0, token: "", success: false, err_msg: "" };

        while (offset < buffer.byteLength) {
            const { value: tag, length: tagLen } = this.decodeVarint(view, offset);
            offset += tagLen;
            const fieldNum = tag >> 3;
            const wireType = tag & 7;

            if (wireType === 0) { // Varint
                const { value, length } = this.decodeVarint(view, offset);
                offset += length;
                if (fieldNum === 1) result.user_id = value;
                else if (fieldNum === 4) result.success = !!value; // Field 4 is success
            } else if (wireType === 2) { // Length Delimited
                const { value: len, length: lenLen } = this.decodeVarint(view, offset);
                offset += lenLen;
                if (fieldNum === 2) result.token = this.decodeString(view, offset, len);
                else if (fieldNum === 3) result.nickname = this.decodeString(view, offset, len);
                else if (fieldNum === 5) result.err_msg = this.decodeString(view, offset, len); // Field 5 is error_message
                offset += len;
            } else {
                // Skip unknown
                if (wireType === 0) {
                    const { length } = this.decodeVarint(view, offset);
                    offset += length;
                } else if (wireType === 2) {
                    const { value: len, length: lenLen } = this.decodeVarint(view, offset);
                    offset += lenLen + len;
                } else {
                    break;
                }
            }
        }
        return result;
    }

    static decodeMsgSendResp(buffer) {
        const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
        let offset = 0;
        const result = { msg_id: 0, seq_id: 0, success: false, err_msg: "" };

        while (offset < buffer.byteLength) {
            const { value: tag, length: tagLen } = this.decodeVarint(view, offset);
            offset += tagLen;
            const fieldNum = tag >> 3;
            const wireType = tag & 7;

            if (wireType === 0) { // Varint
                const { value, length } = this.decodeVarint(view, offset);
                offset += length;
                if (fieldNum === 1) result.msg_id = value;
                else if (fieldNum === 2) result.seq_id = value;
                else if (fieldNum === 3) result.success = !!value;
            } else if (wireType === 2) { // Length Delimited
                const { value: len, length: lenLen } = this.decodeVarint(view, offset);
                offset += lenLen;
                if (fieldNum === 4) result.err_msg = this.decodeString(view, offset, len);
                offset += len;
            } else {
                // Skip unknown
                if (wireType === 0) {
                    const { length } = this.decodeVarint(view, offset);
                    offset += length;
                } else if (wireType === 2) {
                    const { value: len, length: lenLen } = this.decodeVarint(view, offset);
                    offset += lenLen + len;
                } else {
                    break;
                }
            }
        }
        return result;
    }

    static decodeMsgSyncResp(buffer) {
        // MsgSyncResp: 1:max_seq(varint), 2:msgs(repeated MessageData), 3:has_more(bool)
        const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
        let offset = 0;
        const result = { max_seq: 0, msgs: [], has_more: false };

        while (offset < buffer.byteLength) {
            const { value: tag, length: tagLen } = this.decodeVarint(view, offset);
            offset += tagLen;
            const fieldNum = tag >> 3;
            const wireType = tag & 7;

            if (fieldNum === 1) { // max_seq
                const { value, length } = this.decodeVarint(view, offset);
                result.max_seq = value;
                offset += length;
            } else if (fieldNum === 2) { // msgs (repeated)
                const { value: len, length: lenLen } = this.decodeVarint(view, offset);
                offset += lenLen;
                // Decode MessageData
                const msgData = this.decodeMessageData(new Uint8Array(buffer.buffer, buffer.byteOffset + offset, len));
                result.msgs.push(msgData);
                offset += len;
            } else if (fieldNum === 3) { // has_more
                const { value, length } = this.decodeVarint(view, offset);
                result.has_more = !!value;
                offset += length;
            } else {
                // Skip unknown
                // naive skip for now, assuming valid proto from our server
                if (wireType === 0) {
                    const { length } = this.decodeVarint(view, offset);
                    offset += length;
                } else if (wireType === 2) {
                    const { value: len, length: lenLen } = this.decodeVarint(view, offset);
                    offset += lenLen + len;
                } else {
                    console.warn("Unknown wire type", wireType);
                    break;
                }
            }
        }
        return result;
    }

    static decodeMessageData(buffer) {
        const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
        let offset = 0;
        const msg = {
            msg_id: 0, seq_id: 0, sender_id: 0, receiver_id: 0,
            group_id: 0, type: 0, create_time: 0, content: ""
        };

        while (offset < buffer.byteLength) {
            const { value: tag, length: tagLen } = this.decodeVarint(view, offset);
            offset += tagLen;
            const fieldNum = tag >> 3;
            const wireType = tag & 7;

            if (wireType === 0) { // Varint
                const { value, length } = this.decodeVarint(view, offset);
                offset += length;
                if (fieldNum === 1) msg.msg_id = value;
                else if (fieldNum === 2) msg.seq_id = value;
                else if (fieldNum === 3) msg.sender_id = value;
                else if (fieldNum === 4) msg.group_id = value; // Proto Field 4
                else if (fieldNum === 5) msg.type = value;     // Proto Field 5
                // Field 7 is created_at in Proto, but is string? No, Proto says string created_at = 7
                // Wait, created_at is Length Delimited if string.
                // If ChatServiceImpl populates it as string, it's NOT wireType 0.
            } else if (wireType === 2) { // Length Delimited
                const { value: len, length: lenLen } = this.decodeVarint(view, offset);
                offset += lenLen;
                if (fieldNum === 6) { // content (Proto Field 6)
                    msg.content = this.decodeString(view, offset, len);
                } else if (fieldNum === 7) { // created_at (Proto Field 7)
                    msg.created_at = this.decodeString(view, offset, len);
                }
                offset += len;
            }
        }
        return msg;
    }
    static decodeHeartbeatResp(buffer) {
        const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
        let offset = 0;
        const result = { server_time: 0, max_seq_id: 0 };

        while (offset < buffer.byteLength) {
            const { value: tag, length: tagLen } = this.decodeVarint(view, offset);
            offset += tagLen;
            const fieldNum = tag >> 3;

            if (fieldNum === 1) { // server_time
                const { value, length } = this.decodeVarint(view, offset);
                result.server_time = value;
                offset += length;
            } else if (fieldNum === 2) { // max_seq_id
                const { value, length } = this.decodeVarint(view, offset);
                result.max_seq_id = value;
                offset += length;
            } else {
                // Skip unknown
                const wireType = tag & 7;
                if (wireType === 0) {
                    const { length } = this.decodeVarint(view, offset);
                    offset += length;
                } else if (wireType === 2) {
                    const { value: len, length: lenLen } = this.decodeVarint(view, offset);
                    offset += lenLen + len;
                } else {
                    break;
                }
            }
        }
        return result;
    }
}
