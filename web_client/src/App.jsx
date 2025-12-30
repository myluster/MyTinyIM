import { useState, useEffect, useRef } from 'react';
import { ConnectionManager } from './services/connection_manager';
import { ScenarioRunner } from './services/scenario_runner';
import './index.css';

function App() {
  const [users, setUsers] = useState([]);
  const [manager, setManager] = useState(null);
  const [runner, setRunner] = useState(null);

  // Inputs
  const [startId, setStartId] = useState(1014);
  const [count, setCount] = useState(2);
  const [password, setPassword] = useState('pass123');
  const [targetId, setTargetId] = useState(1015);
  const [msgContent, setMsgContent] = useState('Hello World');

  // Init Manager once
  useEffect(() => {
    const mgr = new ConnectionManager((updatedUsers) => {
      setUsers([...updatedUsers]);
    });
    setManager(mgr);
    setRunner(new ScenarioRunner(mgr));
    return () => mgr.disconnectAll();
  }, []);

  const handleLogin = () => {
    if (manager) manager.loginBatch(Number(startId), Number(count), password);
  };

  const handleDisconnect = () => {
    if (manager) manager.disconnectAll();
    if (runner) runner.stop();
  };

  const handleSendAll = () => {
    if (!manager) return;
    users.forEach(u => {
      if (u.status === 'online') {
        const target = Number(targetId);
        if (u.id !== target) {
          manager.sendMessage(u.id, target, msgContent + ` (from ${u.id})`);
        }
      }
    });
  };

  // Stats
  const onlineCount = users.filter(u => u.status === 'online').length;

  return (
    <div className="dashboard">
      <div className="header">
        <h1>🚀 IM Load Test Client</h1>
        <div className="stats-bar">
          <div className="stat-item">
            <span className="stat-value">{users.length}</span>
            <span className="stat-label">Total Users</span>
          </div>
          <div className="stat-item">
            <span className="stat-value">{onlineCount}</span>
            <span className="stat-label">Online</span>
          </div>
        </div>
      </div>

      <div className="controls">
        {/* Simulation Controls */}
        <div className="control-group" style={{ width: '100%', justifyContent: 'center', marginBottom: '20px' }}>
          <h3 style={{ margin: '0 10px 0 0', color: '#fff' }}>🕹️ Simulation Scenarios</h3>
          <button onClick={() => runner && runner.stop()} style={{ background: '#333', border: '1px solid #f00', color: '#f00', padding: '10px 20px' }}>⏹ STOP ALL</button>
        </div>

        <div className="control-group" style={{ justifyContent: 'center', gap: '15px' }}>
          <button onClick={() => runner && runner.runDeepConversation(2000)} className="scenario-btn" style={{ background: 'linear-gradient(45deg, #FF9800, #F57C00)' }}>
            💬 Deep Conversation
            <div style={{ fontSize: '10px', opacity: 0.8 }}>2 Users • Ping-Pong Chat</div>
          </button>

          <button onClick={() => runner && runner.runGroupStorm(3000, 5)} className="scenario-btn" style={{ background: 'linear-gradient(45deg, #E91E63, #C2185B)' }}>
            🌪️ Group Storm
            <div style={{ fontSize: '10px', opacity: 0.8 }}>5 Users • Chaos Mode</div>
          </button>

          <button onClick={() => runner && runner.runOfflineBurst(4000)} className="scenario-btn" style={{ background: 'linear-gradient(45deg, #2196F3, #1976D2)' }}>
            📦 Offline Burst
            <div style={{ fontSize: '10px', opacity: 0.8 }}>Offline Store & Fwd</div>
          </button>

          <button onClick={() => runner && runner.runFriendLifecycle(5000)} className="scenario-btn" style={{ background: 'linear-gradient(45deg, #9C27B0, #7B1FA2)' }}>
            🔄 Friend Cycle
            <div style={{ fontSize: '10px', opacity: 0.8 }}>Add • Chat • Delete</div>
          </button>

          <button onClick={() => runner && runner.runMultiDeviceKick(Number(startId))} className="scenario-btn" style={{ background: 'linear-gradient(45deg, #F44336, #D32F2F)' }}>
            🦵 Kick War
            <div style={{ fontSize: '10px', opacity: 0.8 }}>Conflict Login Test</div>
          </button>
        </div>
      </div>

      <div className="user-grid">
        {users.map(user => (
          <div key={user.id} className="user-card">
            <div className="user-header">
              <div>
                <strong>User {user.id}</strong>
                {user.gateway && <div style={{ fontSize: '9px', color: '#888' }}>{user.gateway.replace('ws://', '')}</div>}
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: '5px' }}>
                <span style={{ fontSize: '10px', color: user.status === 'online' ? '#4caf50' : '#f44336' }}>
                  {user.status === 'online' ? '(Online)' : '(Offline)'}
                </span>
                <div className={`status-dot ${user.status}`}></div>
              </div>
            </div>
            <div className="user-logs">
              {user.logs.map((log, idx) => (
                <div key={idx} className={`log-entry ${log.type}`}>
                  [{log.time}] {log.text}
                </div>
              ))}
            </div>
            <div className="user-actions">
              {user.status === 'online' ? (
                <>
                  <button onClick={() => manager.disconnectSingle(user.id)} style={{ background: '#EF5350', fontSize: '10px', padding: '2px 5px' }}>Go Offline</button>
                  <button onClick={() => manager.syncMessages(user.id)} style={{ background: '#2196F3', fontSize: '10px', padding: '2px 5px' }}>Sync</button>
                </>
              ) : (
                <button onClick={() => manager.loginSingle(user.id, user.password || 'pass123')} style={{ background: '#4CAF50', fontSize: '10px', padding: '2px 5px' }}>Login</button>
              )}
              <button onClick={() => {
                const target = prompt("Send to UserID:");
                if (target) manager.sendMessage(user.id, Number(target), "Manual Msg");
              }} style={{ fontSize: '10px', padding: '2px 5px' }}>Send</button>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

export default App;
