import { IMProtocol, CMD, MAGIC, PROTOCOL_VERSION } from '../utils/im_protocol';

export class ConnectionManager {
    constructor(onUpdate) {
        this.onUpdate = onUpdate; // Callback to update React state
        this.connections = new Map(); // userId -> { ws, status, logs: [] }
        this.gatewayUrl = null; // Will differ per user if real load balancing, but for now typical consistent endpoint
    }

    async discoverGateway() {
        try {
            // Use proxy path /api
            const res = await fetch('/api/discover/chat');
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            const json = await res.json();
            if (json.code !== 0) throw new Error(json.msg);

            // Construct WebSocket URL
            // Ensure we use the proxied address or direct?
            // If Dispatch returns 127.0.0.1:8080, we can connect directly if CORS allows WS (WS usually allows cross-origin).
            // Start Service Discovery (Proxy /api/discover/chat -> Backend)
            // Backend returns: {"code":0, "data": {"gateway_url": "ws://..."}}
            // Note: Since we use proxy, we need to ensure the response matches what Dispatch sends via HTTP.

            // Dispatch/ProcessRequest returns data directly or wrapper?
            // "data": { ... "gateway_url": ... }
            const gatewayUrl = json.data.gateway_url;
            if (!gatewayUrl) throw new Error("No gateway_url in response");

            // Replace localhost with window.location.hostname for remote access support?
            // Usually OK to keep as is if running local.
            return gatewayUrl;
        } catch (e) {
            console.error("Discovery failed", e);
            throw e;
        }
    }

    async loginBatch(startId, count, password) {
        for (let i = 0; i < count; i++) {
            const userId = startId + i;
            if (this.connections.has(userId)) continue;
            // Introduce explicit delay to visualize per-user dispatch
            this.createConnection(userId, password, 3);
            await new Promise(r => setTimeout(r, 100)); // 100ms staggering
        }
    }

    async loginSingle(userId, password, deviceType = 3) {
        // If already connected with same device type, ignore?
        // For Kick test, we might want to force new connection. 
        // Let's allow creating a new connection object always, which effectively replaces the old reference in the map.
        // The old WS will remain open until server kicks it.

        await this.createConnection(userId, password, deviceType);
    }

    async createConnection(userId, password, deviceType) {
        // 1. Discover Gateway (Per user for LB visualization)
        let gatewayUrl;
        try {
            gatewayUrl = await this.discoverGateway();
        } catch (e) {
            console.error(e);
            return;
        }

        // Preserve existing logs if reconnecting
        const existingLogs = this.connections.get(userId)?.logs || [];

        const state = {
            id: userId,
            status: 'connecting',
            logs: existingLogs,
            ws: null,
            password: password,
            localSeq: 0,
            deviceType: deviceType,
            gateway: gatewayUrl // Store which gateway we hit
        };

        // This overwrites the "active" connection for UI purposes
        this.connections.set(userId, state);
        this.notify();

        const ws = new WebSocket(gatewayUrl);
        ws.binaryType = 'arraybuffer';
        state.ws = ws;

        // capture state in closure for callbacks to ensure we log to the right object
        // (though if we replaced the map entry, we might want to log to the *orphaned* object? 
        //  The UI only shows what's in the map. So logs to orphaned socket usually disappear unless we merge them.
        //  Let's keep it simple: Log to the map entry.)

        ws.onopen = () => {
            this.log(userId, `Connected to ${gatewayUrl} (Dev:${deviceType})`, 'tx');
            this.updateUserStatus(userId, 'handshaking');
            // Send Login
            const loginBody = IMProtocol.encodeLoginReq(userId, password, deviceType);
            const msg = IMProtocol.buildMessage(CMD.LOGIN_REQ, loginBody);
            ws.send(msg);
            this.log(userId, 'Sent LoginReq', 'tx');
        };

        ws.onmessage = (event) => {
            try {
                const { header, body } = IMProtocol.parseMessage(event.data);
                this.handleMessage(userId, header, body);
            } catch (e) {
                this.log(userId, `Parse Error: ${e.message}`, 'err');
            }
        };

        ws.onerror = (e) => {
            this.log(userId, 'WS Error', 'err');
        };

        ws.onclose = () => {
            // Only update status to offline if this is still the active connection
            const current = this.connections.get(userId);
            if (current && current.ws === ws) {
                this.updateUserStatus(userId, 'offline');
                this.log(userId, 'Disconnected', 'err');
            } else {
                // This was an orphaned socket (e.g. kicked one)
                // We should probably log this event to the active UI if possible, or console
                console.log(`[User ${userId}] Orphaned socket closed`);
                // Optional: Push a log to the active view saying "Previous session closed"
                this.log(userId, `(Prev Session Closed)`, 'warn');
            }
        };
    }

    handleMessage(userId, header, body) {
        switch (header.cmd) {
            case CMD.LOGIN_RESP:
                try {
                    const lResp = IMProtocol.decodeLoginResp(body);
                    if (lResp.success) {
                        this.updateUserStatus(userId, 'online');
                        this.log(userId, 'Login Success', 'rx');
                        this.startHeartbeat(userId);
                        // Auto-sync offline messages
                        this.syncMessages(userId);
                    } else {
                        this.log(userId, `Login Failed: ${lResp.err_msg}`, 'err');
                        // Do NOT set status to online
                    }
                } catch (e) {
                    this.log(userId, `Login Decode Err: ${e.message}`, 'err');
                }
                break;
            case CMD.MSG_SEND_RESP:
                try {
                    const ack = IMProtocol.decodeMsgSendResp(body);
                    if (ack.success) {
                        this.log(userId, `Ack Msg (seq=${ack.seq_id}, msg=${ack.msg_id})`, 'rx');
                    } else {
                        this.log(userId, `Ack Failed: ${ack.err_msg || 'Unknown'}`, 'err');
                    }
                } catch (e) {
                    this.log(userId, `Ack Decode Err`, 'err');
                }
                break;
            case CMD.MSG_PUSH_NOTIFY:
                this.log(userId, 'New Message Received! Syncing...', 'rx');
                this.syncMessages(userId);
                break;
            case CMD.KICK_NOTIFY:
                this.log(userId, 'KICKED by Server', 'err');
                this.updateUserStatus(userId, 'kicked');
                break;
            case CMD.FRIEND_APPLY_RESP:
                this.log(userId, 'Friend Req Sent', 'tx');
                break;
            case CMD.FRIEND_HANDLE_RESP:
                this.log(userId, 'Friend Req Handled', 'tx');
                break;
            case CMD.FRIEND_DEL_RESP:
                this.log(userId, 'Friend Deleted', 'rx');
                break;
            case CMD.MSG_SYNC_RESP:
                try {
                    const syncData = IMProtocol.decodeMsgSyncResp(body);
                    const conn = this.connections.get(userId);

                    if (syncData.msgs.length > 0 && conn) {
                        // Update local max seq
                        if (syncData.max_seq > conn.localSeq) {
                            conn.localSeq = syncData.max_seq;
                        }

                        this.log(userId, `Synced ${syncData.msgs.length} new msgs`, 'rx');
                        syncData.msgs.forEach(msg => {
                            this.log(userId, `From ${msg.sender_id}: ${msg.content || '(type ' + msg.type + ')'}`, 'rx');
                        });
                    } else if (conn) {
                        if (syncData.max_seq > conn.localSeq) {
                            conn.localSeq = syncData.max_seq;
                        }
                    }
                } catch (e) {
                    this.log(userId, `Sync Decode Err: ${e.message}`, 'err');
                }
                break;
            case CMD.HEARTBEAT_RESP:
                try {
                    const hbData = IMProtocol.decodeHeartbeatResp(body);
                    this.log(userId, `Heartbeat Ack (seq=${hbData.max_seq_id || 0})`, 'rx');

                    // Reliability Check: If server has newer messages, Sync!
                    const conn = this.connections.get(userId);
                    if (conn && hbData.max_seq_id > conn.localSeq) {
                        this.log(userId, `Detected missing msgs (Local: ${conn.localSeq}, Remote: ${hbData.max_seq_id}). Syncing...`, 'warn');
                        this.syncMessages(userId);
                    }
                } catch (e) {
                    this.log(userId, 'Heartbeat Ack', 'rx');
                }
                break;
                break;
            case CMD.GROUP_CREATE_RESP:
                this.log(userId, 'Group Created!', 'rx');
                break;
            case CMD.GROUP_JOIN_RESP:
                this.log(userId, 'Joined Group', 'rx');
                break;
            case CMD.GROUP_QUIT_RESP:
                this.log(userId, 'Quit Group', 'rx');
                break;
            default:
                this.log(userId, `Cmd: ${header.cmd.toString(16)}`, 'rx');
        }
    }

    // New helper to establish friendship
    makeFriends(senderId, targetId) {
        this.sendFriendApply(senderId, targetId);
        setTimeout(() => {
            this.handleFriendApply(targetId, senderId, true);
        }, 200);
    }

    sendFriendApply(senderId, targetId) {
        const connSender = this.connections.get(senderId);
        if (!connSender || connSender.status !== 'online') return;

        const applyMsg = IMProtocol.encodeFriendApplyReq(senderId, targetId, "Let's test");
        connSender.ws.send(IMProtocol.buildMessage(CMD.FRIEND_APPLY_REQ, applyMsg));
        // this.log(senderId, `Sent Friend Req to ${targetId}`, 'tx'); // Optional log
    }

    handleFriendApply(userId, friendId, agree) {
        const conn = this.connections.get(userId);
        if (!conn || conn.status !== 'online') return;

        const handleMsg = IMProtocol.encodeFriendHandleReq(userId, friendId, agree);
        conn.ws.send(IMProtocol.buildMessage(CMD.FRIEND_HANDLE_REQ, handleMsg));
        this.log(userId, `Accepted friend ${friendId}`, 'tx');
    }

    deleteFriend(userId, friendId) {
        const conn = this.connections.get(userId);
        if (!conn || conn.status !== 'online') return;

        const body = IMProtocol.encodeFriendDelReq(userId, friendId);
        conn.ws.send(IMProtocol.buildMessage(CMD.FRIEND_DEL_REQ, body));
        this.log(userId, `Deleting friend ${friendId} `, 'tx');
    }

    syncMessages(userId) {
        const conn = this.connections.get(userId);
        if (!conn || conn.status !== 'online') return;

        // Use localSeq to only fetch new messages
        const body = IMProtocol.encodeMsgSyncReq(userId, conn.localSeq, 50);
        conn.ws.send(IMProtocol.buildMessage(CMD.MSG_SYNC_REQ, body));
        this.log(userId, `Syncing from seq ${conn.localSeq}...`, 'tx');
    }

    createGroup(userId, groupName) {
        const conn = this.connections.get(userId);
        if (!conn || conn.status !== 'online') return;

        const body = IMProtocol.encodeGroupCreateReq(userId, groupName);
        conn.ws.send(IMProtocol.buildMessage(CMD.GROUP_CREATE_REQ, body));
        this.log(userId, `Creating group '${groupName}'`, 'tx');
    }

    joinGroup(userId, groupId) {
        const conn = this.connections.get(userId);
        if (!conn || conn.status !== 'online') return;

        const body = IMProtocol.encodeGroupJoinReq(groupId, userId);
        conn.ws.send(IMProtocol.buildMessage(CMD.GROUP_JOIN_REQ, body));
        this.log(userId, `Joining group ${groupId} `, 'tx');
    }

    quitGroup(userId, groupId) {
        const conn = this.connections.get(userId);
        if (!conn || conn.status !== 'online') return;

        const body = IMProtocol.encodeGroupQuitReq(userId, groupId);
        conn.ws.send(IMProtocol.buildMessage(CMD.GROUP_QUIT_REQ, body));
        this.log(userId, `Quitting group ${groupId} `, 'tx');
    }

    sendGroupMessage(userId, groupId, content) {
        const conn = this.connections.get(userId);
        if (!conn || conn.status !== 'online') return;

        const body = IMProtocol.encodeGroupMsgReq(userId, groupId, content);
        conn.ws.send(IMProtocol.buildMessage(CMD.MSG_SEND_REQ, body));
        this.log(userId, `To Group ${groupId}: ${content} `, 'tx');
    }

    sendMessage(fromUserId, toUserId, content) {
        const conn = this.connections.get(fromUserId);
        if (!conn || conn.status !== 'online') return;

        const body = IMProtocol.encodeMsgSendReq(fromUserId, toUserId, content);
        const msg = IMProtocol.buildMessage(CMD.MSG_SEND_REQ, body);
        conn.ws.send(msg);
        this.log(fromUserId, `To ${toUserId}: ${content} `, 'tx');
    }



    startHeartbeat(userId) {
        const conn = this.connections.get(userId);
        if (!conn) return;

        if (conn.hbInterval) clearInterval(conn.hbInterval);

        conn.hbInterval = setInterval(() => {
            if (conn.ws.readyState === WebSocket.OPEN) {
                const msg = IMProtocol.buildMessage(CMD.HEARTBEAT_REQ, []);
                conn.ws.send(msg);
                // We don't log HB to avoid spam, or log only debug
            } else {
                clearInterval(conn.hbInterval);
            }
        }, 30000); // 30s
    }

    disconnectAll() {
        this.connections.forEach(c => {
            if (c.ws) c.ws.close();
        });
        this.connections.clear();
        this.notify();
    }

    disconnectSingle(userId) {
        const conn = this.connections.get(userId);
        if (conn) {
            if (conn.ws) conn.ws.close();
            conn.status = 'offline';
            this.updateUserStatus(userId, 'offline');
        }
    }

    // Helpers
    log(userId, text, type = 'tx') {
        const conn = this.connections.get(userId);
        if (conn) {
            conn.logs = [{
                time: new Date().toLocaleTimeString(),
                text,
                type
            }, ...conn.logs].slice(0, 100); // Keep last 100
            this.notify();
        }
    }

    updateUserStatus(userId, status) {
        const conn = this.connections.get(userId);
        if (conn) {
            conn.status = status;
            this.notify();
        }
    }

    notify() {
        // Convert Map to Array for React
        this.onUpdate(Array.from(this.connections.values()));
    }
}
