/*
 * ============================================================================
 * RADAR TURRET WEB INTERFACE
 * ============================================================================
 * 
 * This file contains the HTML/CSS/JavaScript for the web-based control panel.
 * It is included by the main .ino file and served to connected clients.
 * 
 * FEATURES:
 *   - Real-time radar visualization with canvas
 *   - Mode selection buttons (SENTRY, STEALTH, AGGRESSIVE, PARTY)
 *   - Start/Stop scanning control
 *   - Configuration modal for settings
 *   - Intrusion log viewer with export
 * 
 * UI LAYOUT (Mobile-First):
 *   ┌─────────────────────────┐
 *   │  HEADER: Title + Badge  │
 *   │  [START] [LOGS] [CONFIG]│
 *   ├─────────────────────────┤
 *   │                         │
 *   │     RADAR CANVAS        │
 *   │                         │
 *   ├─────────────────────────┤
 *   │  MODE BUTTONS (4)       │
 *   ├─────────────────────────┤
 *   │  FOOTER: Last detection │
 *   └─────────────────────────┘
 * 
 * ============================================================================
 */

#ifndef WEB_H
#define WEB_H

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>RADAR TURRET</title>
  <style>
    /* ===== BASE STYLES ===== */
    :root {
      --bg: #000;
      --fg: #fff;
      --dim: #333;
      --green: #0f0;
      --red: #f00;
      --orange: #f80;
      --pink: #f0f;
    }
    
    * { 
      box-sizing: border-box; 
      margin: 0; 
      padding: 0;
      -webkit-tap-highlight-color: transparent;
    }
    
    body { 
      background: var(--bg); 
      color: var(--fg); 
      font-family: 'Courier New', monospace; 
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      overflow-x: hidden;
    }

    /* ===== HEADER ===== */
    #header {
      padding: 12px 15px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 10px;
      border-bottom: 1px solid var(--dim);
    }
    
    #title {
      font-size: 14px;
      font-weight: bold;
      letter-spacing: 1px;
      display: flex;
      align-items: center;
      gap: 8px;
    }
    
    #mode-badge {
      font-size: 9px;
      padding: 2px 6px;
      border: 1px solid;
      border-radius: 2px;
      text-transform: uppercase;
    }
    
    #mode-badge.sentry { border-color: var(--green); color: var(--green); }
    #mode-badge.stealth { border-color: #666; color: #666; }
    #mode-badge.aggressive { border-color: var(--red); color: var(--red); }
    #mode-badge.party { border-color: var(--pink); color: var(--pink); }
    #mode-badge.offline { border-color: var(--red); color: var(--red); }
    
    #ctrl-btns {
      display: flex;
      gap: 8px;
    }

    /* ===== BUTTONS ===== */
    .btn {
      background: var(--bg);
      color: var(--fg);
      border: 1px solid var(--fg);
      padding: 8px 12px;
      font-size: 11px;
      font-weight: bold;
      font-family: inherit;
      text-transform: uppercase;
      cursor: pointer;
      transition: all 0.15s;
    }
    
    .btn:active { background: var(--fg); color: var(--bg); }
    .btn.active { background: var(--green); color: #000; border-color: var(--green); }
    .btn.danger { border-color: var(--red); color: var(--red); }

    /* ===== RADAR SECTION ===== */
    #radar-container {
      flex: 1;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 10px;
      min-height: 280px;
    }
    
    #radar-canvas {
      width: 100%;
      max-width: 500px;
      height: 260px;
      display: block;
    }

    /* ===== MODE BUTTONS ===== */
    #mode-bar {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 8px;
      padding: 12px 15px;
      border-top: 1px solid var(--dim);
    }
    
    .mode-btn {
      padding: 10px 5px;
      font-size: 10px;
      text-align: center;
    }
    
    .mode-btn.sentry { border-color: var(--green); color: var(--green); }
    .mode-btn.stealth { border-color: #666; color: #666; }
    .mode-btn.aggressive { border-color: var(--red); color: var(--red); }
    .mode-btn.party { border-color: var(--pink); color: var(--pink); }
    .mode-btn.selected { background: #222; }

    /* ===== FOOTER ===== */
    #footer {
      padding: 10px 15px;
      font-size: 10px;
      color: #555;
      text-align: center;
      border-top: 1px solid var(--dim);
    }

    /* ===== MODAL ===== */
    .modal {
      display: none;
      position: fixed;
      inset: 0;
      background: rgba(0,0,0,0.95);
      z-index: 100;
      justify-content: center;
      align-items: center;
      padding: 15px;
    }
    
    .modal.active { display: flex; }
    
    .modal-box {
      width: 100%;
      max-width: 400px;
      max-height: 80vh;
      overflow-y: auto;
      border: 2px solid var(--fg);
      background: var(--bg);
      padding: 20px;
    }
    
    .modal h2 {
      font-size: 14px;
      margin-bottom: 15px;
      padding-bottom: 10px;
      border-bottom: 1px solid var(--dim);
    }
    
    /* ===== SETTINGS ===== */
    .setting {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 10px 0;
      border-bottom: 1px solid #111;
    }
    
    .setting label { font-size: 11px; }
    
    .setting .controls {
      display: flex;
      align-items: center;
      gap: 5px;
    }
    
    .adj-btn {
      width: 28px;
      height: 28px;
      background: var(--bg);
      color: var(--fg);
      border: 1px solid #444;
      font-size: 16px;
      cursor: pointer;
    }
    
    input[type="range"] {
      width: 80px;
      accent-color: var(--fg);
    }
    
    .val {
      width: 35px;
      text-align: right;
      font-size: 11px;
    }
    
    /* ===== LOGS ===== */
    #log-text {
      width: 100%;
      height: 200px;
      background: #111;
      color: #ddd;
      border: 1px solid #333;
      font-family: monospace;
      font-size: 10px;
      padding: 8px;
      resize: none;
    }
    
    .modal-actions {
      display: flex;
      gap: 8px;
      margin-top: 15px;
      justify-content: flex-end;
    }
  </style>
</head>
<body>

<!-- ===== HEADER ===== -->
<div id="header">
  <div id="title">
    RADAR
    <span id="mode-badge" class="offline">OFFLINE</span>
  </div>
  <div id="ctrl-btns">
    <button class="btn" id="btn-toggle" onclick="toggleScan()">START</button>
    <button class="btn" onclick="openModal('logs')">LOGS</button>
    <button class="btn" onclick="openModal('config')">CONFIG</button>
  </div>
</div>

<!-- ===== RADAR ===== -->
<div id="radar-container">
  <canvas id="radar-canvas"></canvas>
</div>

<!-- ===== MODE BUTTONS ===== -->
<div id="mode-bar">
  <button class="btn mode-btn sentry" onclick="setMode(0)">SENTRY</button>
  <button class="btn mode-btn stealth" onclick="setMode(1)">STEALTH</button>
  <button class="btn mode-btn aggressive" onclick="setMode(2)">AGGRO</button>
  <button class="btn mode-btn party" onclick="setMode(3)">PARTY</button>
</div>

<!-- ===== FOOTER ===== -->
<div id="footer">
  <span id="last-detection">READY</span>
</div>

<!-- ===== LOGS MODAL ===== -->
<div id="modal-logs" class="modal">
  <div class="modal-box">
    <h2>INTRUSION LOGS</h2>
    <textarea id="log-text" readonly>Loading...</textarea>
    <div class="modal-actions">
      <button class="btn" onclick="exportLogs()">EXPORT</button>
      <button class="btn danger" onclick="wipeLogs()">WIPE</button>
      <button class="btn" onclick="closeModal('logs')">CLOSE</button>
    </div>
  </div>
</div>

<!-- ===== CONFIG MODAL ===== -->
<div id="modal-config" class="modal">
  <div class="modal-box">
    <h2>SETTINGS</h2>
    
    <div class="setting">
      <label>MAX RANGE (cm)</label>
      <div class="controls">
        <button class="adj-btn" onclick="adjust('dist',-5)">-</button>
        <input type="range" id="cfg-dist" min="10" max="200" oninput="updateVal('dist')">
        <button class="adj-btn" onclick="adjust('dist',5)">+</button>
        <span class="val" id="val-dist">50</span>
      </div>
    </div>
    
    <div class="setting">
      <label>LOCK TIME (ms)</label>
      <div class="controls">
        <button class="adj-btn" onclick="adjust('lock',-100)">-</button>
        <input type="range" id="cfg-lock" min="500" max="5000" step="100" oninput="updateVal('lock')">
        <button class="adj-btn" onclick="adjust('lock',100)">+</button>
        <span class="val" id="val-lock">2000</span>
      </div>
    </div>
    
    <div class="setting">
      <label>MIN ANGLE</label>
      <div class="controls">
        <button class="adj-btn" onclick="adjust('min',-5)">-</button>
        <input type="range" id="cfg-min" min="0" max="80" oninput="updateVal('min')">
        <button class="adj-btn" onclick="adjust('min',5)">+</button>
        <span class="val" id="val-min">15</span>
      </div>
    </div>
    
    <div class="setting">
      <label>MAX ANGLE</label>
      <div class="controls">
        <button class="adj-btn" onclick="adjust('max',-5)">-</button>
        <input type="range" id="cfg-max" min="100" max="180" oninput="updateVal('max')">
        <button class="adj-btn" onclick="adjust('max',5)">+</button>
        <span class="val" id="val-max">165</span>
      </div>
    </div>
    
    <div class="modal-actions">
      <button class="btn" onclick="resetSettings()">RESET</button>
      <button class="btn" onclick="saveSettings()">SAVE</button>
      <button class="btn" onclick="closeModal('config')">CLOSE</button>
    </div>
  </div>
</div>

<!-- ===== JAVASCRIPT ===== -->
<script>
// ========== STATE ==========
const canvas = document.getElementById('radar-canvas');
const ctx = canvas.getContext('2d');
let width, height;
let scanAngle = 90;
let maxRange = 50;
let currentMode = 0;
let isRunning = false;
let isOffline = true;
let objects = [];

const modeNames = ['sentry', 'stealth', 'aggressive', 'party'];
const modeColors = ['#0f0', '#666', '#f00', '#f0f'];

// ========== INIT ==========
function init() {
  resizeCanvas();
  window.addEventListener('resize', resizeCanvas);
  draw();
  
  // Sync time with ESP32
  fetch('/time_sync?ts=' + Math.floor(Date.now() / 1000)).catch(() => {});
  
  // Start polling
  setInterval(pollStatus, 100);
}

function resizeCanvas() {
  const container = document.getElementById('radar-container');
  width = Math.min(container.clientWidth - 20, 500);
  height = 260;
  canvas.width = width;
  canvas.height = height;
}

// ========== POLLING ==========
function pollStatus() {
  fetch('/status')
    .then(r => r.ok ? r.json() : Promise.reject())
    .then(data => {
      isOffline = false;
      scanAngle = data.a;
      maxRange = data.r || 50;
      currentMode = data.mode || 0;
      isRunning = data.running === 1;
      
      // Add detected object
      if (data.d > 0) {
        objects.push({ a: data.a, r: data.d, life: 1.0 });
      }
      
      // Update last detection
      if (data.li_a && data.li_d) {
        document.getElementById('last-detection').textContent = 
          `LAST: ${data.li_d}cm @ ${data.li_a}°`;
      }
      
      updateUI();
      draw();
    })
    .catch(() => {
      isOffline = true;
      updateUI();
      draw();
    });
}

function updateUI() {
  const badge = document.getElementById('mode-badge');
  const toggleBtn = document.getElementById('btn-toggle');
  
  if (isOffline) {
    badge.className = 'offline';
    badge.textContent = 'OFFLINE';
  } else {
    badge.className = modeNames[currentMode];
    badge.textContent = modeNames[currentMode].toUpperCase();
  }
  
  toggleBtn.textContent = isRunning ? 'STOP' : 'START';
  toggleBtn.classList.toggle('active', isRunning);
  
  // Update mode buttons
  document.querySelectorAll('.mode-btn').forEach((btn, i) => {
    btn.classList.toggle('selected', i === currentMode);
  });
}

// ========== RADAR DRAWING ==========
function draw() {
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, width, height);
  
  const cx = width / 2;
  const cy = height - 5;
  const radius = height - 15;
  const color = modeColors[currentMode] || '#0f0';
  
  // Draw grid arcs
  ctx.strokeStyle = '#222';
  ctx.lineWidth = 1;
  for (let i = 1; i <= 4; i++) {
    ctx.beginPath();
    ctx.arc(cx, cy, radius * (i / 4), Math.PI, 0);
    ctx.stroke();
  }
  
  // Draw angle lines
  for (let deg = 30; deg <= 150; deg += 30) {
    const rad = -deg * Math.PI / 180;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + radius * Math.cos(rad), cy + radius * Math.sin(rad));
    ctx.stroke();
  }
  
  // Draw scan line
  const scanRad = -scanAngle * Math.PI / 180;
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + radius * Math.cos(scanRad), cy + radius * Math.sin(scanRad));
  ctx.stroke();
  
  // Draw sweep glow
  ctx.fillStyle = color + '20';
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.arc(cx, cy, radius, scanRad - 0.1, scanRad + 0.02);
  ctx.fill();
  
  // Draw detected objects
  objects.forEach(obj => obj.life -= 0.02);
  objects = objects.filter(obj => obj.life > 0);
  
  objects.forEach(obj => {
    const objRad = -obj.a * Math.PI / 180;
    const dist = (obj.r / maxRange) * radius;
    
    if (dist > 0 && dist <= radius) {
      const x = cx + dist * Math.cos(objRad);
      const y = cy + dist * Math.sin(objRad);
      
      ctx.fillStyle = `rgba(255, 50, 50, ${obj.life})`;
      ctx.beginPath();
      ctx.arc(x, y, 4 + (1 - obj.life) * 4, 0, Math.PI * 2);
      ctx.fill();
    }
  });
  
  // Draw offline message
  if (isOffline) {
    ctx.fillStyle = '#f00';
    ctx.font = 'bold 16px monospace';
    ctx.textAlign = 'center';
    ctx.fillText('DISCONNECTED', cx, cy - 40);
  }
}

// ========== CONTROLS ==========
function toggleScan() {
  fetch('/toggle');
}

function setMode(mode) {
  fetch('/mode?m=' + mode);
}

// ========== MODALS ==========
function openModal(name) {
  document.getElementById('modal-' + name).classList.add('active');
  
  if (name === 'logs') {
    document.getElementById('log-text').value = 'Loading...';
    fetch('/get_logs').then(r => r.text()).then(t => {
      document.getElementById('log-text').value = t;
    });
  }
  
  if (name === 'config') {
    fetch('/get_config').then(r => r.json()).then(c => {
      setInput('dist', c.dst);
      setInput('lock', c.lck);
      setInput('min', c.min);
      setInput('max', c.max);
    });
  }
}

function closeModal(name) {
  document.getElementById('modal-' + name).classList.remove('active');
}

// ========== LOGS ==========
function exportLogs() {
  const text = document.getElementById('log-text').value;
  const blob = new Blob([text], { type: 'text/csv' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'radar_logs.csv';
  a.click();
}

function wipeLogs() {
  if (confirm('Delete all logs?')) {
    fetch('/clear_logs');
    document.getElementById('log-text').value = '[CLEARED]';
  }
}

// ========== CONFIG ==========
function setInput(name, value) {
  document.getElementById('cfg-' + name).value = value;
  document.getElementById('val-' + name).textContent = value;
}

function updateVal(name) {
  const val = document.getElementById('cfg-' + name).value;
  document.getElementById('val-' + name).textContent = val;
}

function adjust(name, delta) {
  const el = document.getElementById('cfg-' + name);
  let val = parseInt(el.value) + delta;
  val = Math.max(parseInt(el.min), Math.min(parseInt(el.max), val));
  el.value = val;
  updateVal(name);
}

function saveSettings() {
  const params = new URLSearchParams({
    d: document.getElementById('cfg-dist').value,
    l: document.getElementById('cfg-lock').value,
    mn: document.getElementById('cfg-min').value,
    mx: document.getElementById('cfg-max').value
  });
  fetch('/save_config?' + params);
  closeModal('config');
}

function resetSettings() {
  if (confirm('Reset to defaults?')) {
    fetch('/reset_config');
    closeModal('config');
  }
}

// ========== START ==========
init();
</script>

</body>
</html>
)rawliteral";

#endif // WEB_H
