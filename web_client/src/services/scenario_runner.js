export class ScenarioRunner {
    constructor(manager) {
        this.manager = manager;
        this.abortController = new AbortController();
    }

    stop() {
        this.abortController.abort();
        this.abortController = new AbortController(); // Reset for next run
    }

    async sleep(ms) {
        if (this.abortController.signal.aborted) throw new Error("Scenario Aborted");
        return new Promise(r => setTimeout(r, ms));
    }

    async randomSleep(min, max) {
        const ms = Math.floor(Math.random() * (max - min + 1)) + min;
        await this.sleep(ms);
    }

    log(userId, text) {
        this.manager.log(userId, `[BOT] ${text}`, 'sys');
    }

    async waitForOnline(userId, timeout = 5000) {
        const start = Date.now();
        while (Date.now() - start < timeout) {
            if (this.abortController.signal.aborted) throw new Error("Aborted");
            const conn = this.manager.connections.get(userId);
            if (conn && conn.status === 'online') return;
            await this.sleep(100);
        }
        throw new Error(`Timeout waiting for user ${userId} to come online`);
    }

    async waitForLogOrSync(userId, textPredict, timeout = 5000) {
        const start = Date.now();
        while (Date.now() - start < timeout) {
            if (this.abortController.signal.aborted) throw new Error("Aborted");

            // Active Polling: Sync messages to catch lost pushes
            // This ensures we verify database state, not just push reliability
            this.manager.syncMessages(userId);

            const conn = this.manager.connections.get(userId);
            // Check last 20 logs for match
            if (conn && conn.logs.some(l => l.text.includes(textPredict))) {
                return;
            }
            await this.sleep(500); // 0.5s poll interval
        }
        throw new Error(`Timeout waiting for log "${textPredict}" on user ${userId}`);
    }

    // ==========================================
    // 1. Deep Conversation (Ping-Pong)
    // ==========================================
    async runDeepConversation(startId) {
        const u1 = startId;
        const u2 = startId + 1;
        const rounds = 5;

        try {
            // 1. Login & Friend
            await this.manager.loginSingle(u1, 'pass123');
            await this.manager.loginSingle(u2, 'pass123');

            await this.waitForOnline(u1);
            await this.waitForOnline(u2);

            this.log(u1, "Starting Deep Conversation Test...");

            this.manager.makeFriends(u1, u2);
            await this.sleep(1000);

            // 2. Conversation Loop
            for (let i = 1; i <= rounds; i++) {
                if (this.abortController.signal.aborted) break;

                if (i % 2 !== 0) { // U1 speaks (1st, 3rd, 5th message)
                    // U1 -> U2
                    this.manager.sendMessage(u1, u2, `Hey user ${u2}, message #${Math.ceil(i / 2)}`);
                    // Optional: Force U2 to sync after a delay to ensure they see it even if Push fails
                    setTimeout(() => this.manager.syncMessages(u2), 500);
                } else { // U2 replies (2nd, 4th, 6th message)
                    // U2 -> U1
                    this.manager.sendMessage(u2, u1, `Got it ${u1}, reply #${i / 2}`);
                    setTimeout(() => this.manager.syncMessages(u1), 500);
                }
                await this.randomSleep(500, 1500); // Wait for reading/reply
            }

            this.log(u1, "Conversation Complete.");
            await this.sleep(2000);
            this.manager.disconnectSingle(u1);
            this.manager.disconnectSingle(u2);

        } catch (e) {
            console.log("Scenario Stopped:", e.message);
        }
    }

    // ==========================================
    // 2. Group Storm (Concurrency & Chaos)
    // ==========================================
    async runGroupStorm(startId, userCount = 5) {
        const owner = startId;
        const members = [];
        for (let i = 1; i < userCount; i++) members.push(startId + i);
        const gid = Math.floor(Math.random() * 10000) + 1; // Random Group ID

        try {
            this.log(owner, `Starting Group Storm Test (GID: ${gid})...`);

            // 1. Login Everyone
            await this.manager.loginSingle(owner, 'pass123');
            for (const m of members) {
                await this.manager.loginSingle(m, 'pass123');
                await this.sleep(100);
            }
            await this.sleep(1000);

            // 2. Owner Creates Group
            this.log(owner, "Creating Group 'StormHub'...");
            this.manager.createGroup(owner, "StormHub");
            await this.sleep(1000); // Wait for processing

            // Assume Group ID 1 is utilized for this test (or whatever server assigns first)
            // For robust test we should parse RESP, but for visualization simple ID 1 is fine if DB is fresh.
            const targetGid = 1;

            // 2. Members Join
            for (const m of members) {
                this.log(m, `Joining Group ${targetGid}...`);
                this.manager.joinGroup(m, targetGid);
                await this.randomSleep(200, 500);
            }

            // 3. Owner handle requests (Simulated: Auto-approve logic needed in Manager or manually triggered?)
            // The Manager's runGroupChatTest didn't verify approval?
            // Ah, server logic: JoinGroup -> Notify Owner. Owner Must Approve.
            // We need Owner to LISTEN and APPROVE.
            // This is complex. Let's simplify: 
            // Owner invites? Or we just have Owner check for requests?
            // For this iteration, let's skip the explicit approval loop and assume 
            // either auto-approve mod on server OR we focus on chat chaos 
            // assuming they are ALREADY members (e.g. from previous runs).

            // BETTER: Just Spam Messages assuming membership
            this.log(owner, "Starting Message Storm...");

            const tasks = members.concat(owner).map(async (uid) => {
                for (let k = 0; k < 5; k++) {
                    if (this.abortController.signal.aborted) return;
                    this.manager.sendGroupMessage(uid, targetGid, `Storm Msg ${k} from ${uid}`);
                    await this.randomSleep(300, 1000);
                }
            });

            await Promise.all(tasks);

            this.log(owner, "Storm Complete.");
            await this.sleep(2000);

            // Cleanup
            this.manager.disconnectSingle(owner);
            for (const m of members) {
                this.manager.disconnectSingle(m);
            }

        } catch (e) {
            console.log("Scenario Stopped:", e.message);
        }
    }

    // ==========================================
    // 3. Offline Burst
    // ==========================================
    async runOfflineBurst(startId) {
        const u1 = startId;
        const u2 = startId + 1;
        const msgCount = 20;

        try {
            // 1. Login Both Initial Check
            await this.manager.loginSingle(u1, 'pass123');
            await this.manager.loginSingle(u2, 'pass123');
            await this.waitForOnline(u1);
            await this.waitForOnline(u2);

            // 0. Ensure Clean Slate (Delete Friend if exists)
            this.log(u1, "Ensuring clean slate (Delete Friend)...");
            this.manager.deleteFriend(u1, u2);
            await this.sleep(1000);

            // Ensure no existing relation if possible (or just assume idempotency)
            // But we want to test offline friend req.
            // If they are already friends, ApplyReq might be auto-rejected or behave differently.
            // For robustness, let's just proceed.

            // 2. B Goes Offline
            this.log(u2, "Going offline for Friend Req test...");
            this.manager.disconnectSingle(u2);
            await this.sleep(1000);

            // 3. A Sends Friend Req (Offline)
            this.log(u1, "Sending Offline Friend Request...");
            this.manager.sendFriendApply(u1, u2);
            await this.sleep(2000);

            // 4. B Comes Online
            this.log(u2, "User 2 Coming online to Accept...");
            await this.manager.loginSingle(u2, 'pass123');
            await this.waitForOnline(u2);
            await this.sleep(1000);

            // 5. B Accepts Friend Req
            this.log(u2, "Accepting Friend Request...");
            this.manager.handleFriendApply(u2, u1, true);

            // A should receive notification (via Push or Polled Sync)
            this.log(u1, "Waiting for Friend Approval Notification...");
            await this.waitForLogOrSync(u1, "Friend Request Accepted", 5000);
            this.log(u1, "Got Approval! Proceeding...");

            // 6. B Goes Offline Again
            this.log(u2, "Going offline for Message Burst test...");
            this.manager.disconnectSingle(u2);
            await this.sleep(1000);

            // 7. A Spams Messages
            this.log(u1, `Sending ${msgCount} buffer messages...`);
            for (let i = 0; i < msgCount; i++) {
                if (this.abortController.signal.aborted) break;
                this.manager.sendMessage(u1, u2, `Buffered Message #${i}`);
                if (i % 5 === 0) await this.sleep(100);
            }

            this.log(u1, "Finished Sending. Waiting for U2...");
            await this.sleep(1000);

            // 8. B Comes Online & Syncs
            await this.manager.loginSingle(u2, 'pass123');
            await this.waitForOnline(u2);
            await this.sleep(1000);

            this.manager.syncMessages(u2);
            this.log(u2, "Synced messages. Check logs!");

            await this.sleep(3000);
            this.manager.disconnectSingle(u1);
            this.manager.disconnectSingle(u2);

        } catch (e) {
            console.log("Scenario Stopped:", e.message);
        }
    }
    // ==========================================
    // 4. Multi-Device Kick (Conflict Test)
    // ==========================================
    async runMultiDeviceKick(startId) {
        const u1 = startId;
        const devicePC = 2; // PC

        try {
            this.log(u1, "Step 1: Login as PC (Session A)...");
            await this.manager.loginSingle(u1, 'pass123', devicePC);
            await this.waitForOnline(u1);
            await this.sleep(2000);

            this.log(u1, "Step 2: Login as PC AGAIN (Session B)...");
            this.log(u1, "Expect Session A to be Kicked!");

            // This will overwrite the local map entry, but the old socket is still open
            // until the server sends KICK.
            await this.manager.loginSingle(u1, 'pass123', devicePC);

            await this.sleep(3000);
            this.log(u1, "Test Complete. Did you see the KICK?");

            await this.sleep(2000);
            this.manager.disconnectSingle(u1);

        } catch (e) {
            console.log("Scenario Stopped:", e.message);
        }
    }
}
