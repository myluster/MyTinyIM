# IM 可视化测试客户端使用指南

## 🎯 功能特性

这是一个基于 React + WebSocket 的可视化 IM 系统测试工具，用于直观展示和验证后端系统的各项功能。

### 核心能力

1. **多用户并发模拟**: 可同时模拟数百个用户在线
2. **负载均衡可视化**: 每个用户独立发现网关，可观察分配到的网关地址
3. **设备类型支持**: 支持 PC/Mobile/Web/Tablet 等多种设备类型
4. **实时日志流**: 每个模拟用户的日志实时显示（发送/接收/错误/警告）

### 内置自动化测试场景

| 场景 | 描述 | 测试目标 |
|------|------|----------|
| 💬 Deep Conversation | 双人 Ping-Pong 对话（5轮） | 消息推送、离线拉取、顺序性 |
| 🌪️ Group Storm | 5人群组消息轰炸 | 群组功能、并发吞吐 |
| 📦 Offline Burst | 离线消息突发测试 | 离线存储、好友申请流程 |
| 🔄 Friend Cycle | 好友完整生命周期 | 添加、聊天、删除 |
| 🦵 Kick War | 多端互踢测试 | 同设备类型冲突登录检测 |

## 🚀 快速开始

### 1. 启动后端服务

确保后端服务已经运行：
```bash
# 在项目根目录
cd infra/scripts
./server_start.bat  # Windows
# 或
./server_start.sh   # Linux
```

### 2. 启动前端开发服务器

```bash
cd web_client
npm install  # 首次运行
npm run dev
```

访问: `http://localhost:5173`

### 3. 开始测试

#### 方式一：手动控制
- 在顶部输入 `Start User ID`、用户数量、密码
- 点击 "Login Batch" 批量登录
- 在每个用户卡片上手动点击 "Send" 发消息

#### 方式二：自动化场景
- 直接点击场景按钮（如 "💬 Deep Conversation"）
- 观察日志流中的自动化测试执行
- 场景运行中可点击 "⏹ STOP ALL" 终止

## 📊 可视化元素说明

### 用户卡片
```
┌─────────────────────────────┐
│ User 1014                   │  ← 用户 ID
│ ws://127.0.0.1:8081         │  ← Gateway 地址（LB可视化）
│ (Online) ●                  │  ← 状态指示器
├─────────────────────────────┤
│ [14:32:01] Connected...     │  ← 实时日志流
│ [14:32:02] Login Success    │    - 灰色：发送
│ [14:32:10] From 1015: Hi!   │    - 绿色：接收
│ [14:32:15] KICKED by Server │    - 红色：错误
│                             │    - 橙色：警告
├─────────────────────────────┤    - 蓝色：系统
│ [Go Offline] [Send]         │  ← 操作按钮
└─────────────────────────────┘
```

### 日志颜色编码
- **灰色 (tx)**: 客户端发送的消息
- **绿色 (rx)**: 服务端接收或推送的消息
- **红色 (err)**: 连接错误、被踢等
- **橙色 (warn)**: 警告信息（如孤儿连接关闭）
- **蓝色 (sys)**: Scenario 自动化脚本日志

## 🧪 测试场景详解

### 🦵 Kick War (多端互踢)

**目的**: 验证同一用户在相同设备类型下二次登录时，旧会话被踢下线。

**步骤**:
1. User 1014 作为 PC (DeviceType=2) 登录 → Session A
2. 等待 2 秒
3. User 1014 再次作为 PC 登录 → Session B
4. 观察日志中 "KICKED by Server" 消息
5. Session A 的 WebSocket 关闭，显示 "(Prev Session Closed)"

**预期结果**:
```
[14:30:00] Connected to ws://127.0.0.1:8081 (Dev:2)
[14:30:01] Login Success
[14:30:03] Step 2: Login as PC AGAIN (Session B)...
[14:30:03] Connected to ws://127.0.0.1:8081 (Dev:2)
[14:30:04] Login Success
[14:30:04] KICKED by Server           ← 旧会话收到踢通知
[14:30:04] (Prev Session Closed)       ← 旧连接关闭
```

### 💬 Deep Conversation

双用户 Ping-Pong 对话测试，验证：
- 实时推送 (`MSG_PUSH_NOTIFY`)
- 消息同步 (`MSG_SYNC_REQ/RESP`)
- 顺序性保证

### 🌪️ Group Storm

5人群组并发消息测试，验证：
- 群组创建 (`GROUP_CREATE_REQ`)
- 群组加入 (`GROUP_JOIN_REQ`)
- 群消息广播

**注意**: 当前实现假设第一个创建的群组 ID 为 1。

## 🔧 开发与调试

### 修改场景参数

编辑 `src/services/scenario_runner.js`:

```javascript
async runDeepConversation(startId) {
    const rounds = 5;  // 修改对话轮数
    // ...
}
```

### 添加新场景

1. 在 `ScenarioRunner` 类中添加新方法：
```javascript
async runMyScenario(startId) {
    try {
        // 你的逻辑
        this.log(userId, "Step 1...", 'sys');
        await this.sleep(1000);
        // ...
    } catch (e) {
        console.log("Scenario Stopped:", e.message);
    }
}
```

2. 在 `App.jsx` 中添加按钮：
```jsx
<button onClick={() => runner && runner.runMyScenario(1000)} 
        className="scenario-btn" 
        style={{ background: 'linear-gradient(45deg, #00BCD4, #0097A7)' }}>
    🎨 My Scenario
    <div style={{ fontSize: '10px', opacity: 0.8 }}>Description</div>
</button>
```

### 协议编码

所有协议编码在 `src/utils/im_protocol.js`。如需支持新消息类型：

```javascript
static encodeMyReq(param1, param2) {
    const buffer = [];
    // Field 1: param1 (varint)
    buffer.push((1 << 3) | 0);
    buffer.push(...this.encodeVarint(param1));
    // ...
    return new Uint8Array(buffer);
}
```

## 🎨 界面定制

所有样式在 `src/index.css`，基于 CSS 变量：

```css
:root {
  --bg-color: #1a1a1a;        /* 背景色 */
  --card-bg: #2d2d2d;         /* 卡片背景 */
  --accent: #646cff;          /* 主题色 */
  --success: #4caf50;         /* 成功绿 */
  --error: #f44336;           /* 错误红 */
}
```

## 📈 性能考虑

- **建议并发用户数**: ≤ 50 （浏览器 WebSocket 限制）
- **日志保留**: 每用户最多 100 条（防止内存泄漏）
- **Heartbeat 间隔**: 30秒
- **Discovery 缓存**: 目前每个连接独立 Discovery（可优化）

## 🐛 常见问题

### Q: "Gateway Discovery Failed"
**A**: 检查 Dispatch Service 是否运行在 `localhost:8000`，或修改 `vite.config.js` 中的代理配置。

### Q: 日志显示 "Parse Error"
**A**: 协议版本不匹配。确认前后端使用相同的 `im_protocol.proto` 定义。

### Q: 群组测试失败 "Group not found"
**A**: 数据库中群组尚不存在。运行一次 `full_reset_test.bat` 清理数据，或手动创建 Group ID 1。

### Q: Kick Test 没看到被踢
**A**: 确认后端 `auth_service` 已实现跨网关踢人逻辑，检查 `gateway.log` 中是否有 `KickConnection` 调用。

## 📝 更新日志

### v2.0 (2025-12-28)
- ✨ 新增多设备类型支持 (PC/Mobile/Web)
- ✨ 新增负载均衡可视化（显示 Gateway 地址）
- ✨ 新增 Kick War 场景
- ✨ 改进日志系统（新增 warn/sys 类型）
- 🐛 修复孤儿连接日志混乱问题
- 🐛 修复群组创建协议支持

### v1.0 (Initial)
- 基础多用户模拟
- Deep Conversation / Group Storm / Offline Burst 场景
- 实时日志流

---

**Happy Testing! 🎉**
