/*
 * RADAR TURRET v2.0
 * ==================
 * ESP32-based radar turret with web interface
 * 
 * Features:
 *  - Ultrasonic distance scanning
 *  - Web-based control & monitoring
 *  - Configurable parameters
 *  - NeoPixel alerts
 *  - Intrusion logging with timestamps
 * 
 * Bug Fixes from v1.0:
 *  - Non-blocking operations (no delay() in main loop)
 *  - Proper button debouncing
 *  - Config validation
 *  - Log file size management
 *  - Dynamic radar scaling
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <time.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================
#define TRIG_PIN        5
#define ECHO_PIN        35
#define SERVO_SCAN_PIN  19
#define SERVO_ARROW_PIN 18
#define NEOPIXEL_PIN    26
#define BUZZER_PIN      23
#define BUTTON_PIN      27

// ============================================================================
// CONSTANTS
// ============================================================================
#define NUM_PIXELS      3
#define LOG_MAX_SIZE    50000   // 50KB max log size
#define DEBOUNCE_MS     50
#define ANIM_FRAME_MS   80

// ============================================================================
// OBJECTS
// ============================================================================
Servo scanServo;
Servo arrowServo;
Adafruit_NeoPixel strip(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
Preferences preferences;

// ============================================================================
// CONFIGURATION (with defaults)
// ============================================================================
struct Config {
  int scanSpeed   = 20;     // Delay in ms per degree
  int maxDist     = 50;     // Max detection range cm
  int lockTime    = 2000;   // Lock duration ms
  int ledBright   = 50;     // 0-255
  int minAngle    = 15;     // Sweep start
  int maxAngle    = 165;    // Sweep end
  bool buzzerOn   = true;
} cfg;

// ============================================================================
// STATE MACHINE
// ============================================================================
enum SystemState {
  STATE_IDLE,       // Not scanning
  STATE_SCANNING,   // Normal radar sweep
  STATE_LOCKED,     // Locked onto target
  STATE_STARTUP,    // Starting animation
  STATE_TEST_ALERT  // Testing alert
};

SystemState currentState = STATE_IDLE;

// ============================================================================
// STATE VARIABLES
// ============================================================================
int scanPos = 90;
int scanDirection = 1;
int lastDistance = 0;
int lastIntrusionAngle = 0;
int lastIntrusionDist = 0;

// Timing (non-blocking)
unsigned long lastScanTime = 0;
unsigned long lockOnStartTime = 0;
unsigned long animStartTime = 0;
unsigned long lastButtonTime = 0;
unsigned long lastTimeSyncAttempt = 0;

// Flags
bool timeSynced = false;
int animFrame = 0;
int lastArrowPos = 90;

// ============================================================================
// WIFI CONFIG
// ============================================================================
const char *ssid = "RADAR_TURRET";
const char *password = "12345678";

// ============================================================================
// WEB INTERFACE HTML
// ============================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>RADAR</title>
<style>
  :root { --bg: #000; --fg: #fff; --dim: #222; --accent: #0f0; }
  * { box-sizing: border-box; }
  body { background: var(--bg); color: var(--fg); font-family: 'Courier New', monospace; margin: 0; padding: 0; overflow: hidden; height: 100vh; display: flex; flex-direction: column; align-items: center; justify-content: center; }
  
  #radar-box { position: relative; width: 100%; max-width: 600px; height: 350px; display: flex; justify-content: center; align-items: flex-end; overflow: hidden; }
  canvas { width: 100%; height: 100%; display: block; background: #000; }

  .ui-btn { 
    background: #000; color: var(--fg); border: 1px solid var(--fg); padding: 8px 16px; 
    cursor: pointer; font-size: 12px; font-weight: bold; text-transform: uppercase; 
    transition: all 0.2s;
  }
  .ui-btn:hover { background: var(--fg); color: var(--bg); }
  .ui-btn:active { background: #888; }
  .ui-btn.active { background: var(--accent); color: #000; border-color: var(--accent); }
  .ui-btn.danger { border-color: #f55; color: #f55; }
  .ui-btn.danger:hover { background: #f55; color: #000; }
  
  #header { position: absolute; top: 0; width: 100%; padding: 15px; display: flex; justify-content: space-between; align-items: center; box-sizing: border-box; z-index: 10; max-width: 800px; }
  #title { font-size: 18px; font-weight: bold; letter-spacing: 2px; border-bottom: 2px solid var(--fg); padding-bottom: 5px; display: flex; align-items: center; gap: 10px; }
  #status-badge { font-size: 10px; padding: 3px 8px; border: 1px solid; border-radius: 2px; }
  #status-badge.online { border-color: #0f0; color: #0f0; }
  #status-badge.scanning { border-color: #0f0; color: #0f0; background: rgba(0,255,0,0.1); }
  #status-badge.paused { border-color: #f80; color: #f80; }
  #status-badge.offline { border-color: #f00; color: #f00; }
  
  #btn-group { display: flex; gap: 10px; flex-wrap: wrap; justify-content: flex-end; }
  
  #footer { position: absolute; bottom: 10px; width: 100%; display: flex; justify-content: center; gap: 10px; z-index: 10; }
  #last-intrusion { font-size: 11px; color: #888; }
  
  .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.95); z-index: 20; flex-direction: column; align-items: center; justify-content: center; backdrop-filter: blur(2px); }
  .modal.active { display: flex; }
  .modal-content { 
    width: 90%; max-width: 550px; max-height: 90vh; overflow-y: auto;
    border: 2px solid var(--fg); padding: 25px; background: #000; 
    box-shadow: 0 0 30px rgba(255,255,255,0.1); display: flex; flex-direction: column; gap: 15px;
  }
  .modal h2 { margin: 0 0 10px 0; border-bottom: 1px solid #333; padding-bottom: 10px; font-size: 1.4rem; letter-spacing: 1px; }
  
  .setting { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #111; }
  .setting label { font-size: 0.9rem; font-weight: bold; flex: 1; }
  .setting .ctrl { display: flex; align-items: center; gap: 8px; flex: 2; justify-content: flex-end; }
  
  input[type="range"] { flex: 1; accent-color: var(--fg); cursor: pointer; height: 5px; }
  
  .adj-btn { 
    width: 25px; height: 25px; background: transparent; color: var(--fg); border: 1px solid #444; 
    cursor: pointer; display: flex; align-items: center; justify-content: center; font-weight: bold;
    font-size: 16px; transition: border 0.2s;
  }
  .adj-btn:hover { border-color: #fff; background: #111; }
  
  span.val { font-weight: bold; width: 45px; text-align: right; font-size: 14px; color: #fff; }
  
  textarea { width: 100%; height: 250px; background: #111; color: #eee; border: 1px solid #333; font-family: 'Courier New', monospace; font-size: 12px; resize: none; padding: 10px; box-sizing: border-box; outline: none; }
  
  .actions { display: flex; gap: 10px; margin-top: 20px; justify-content: flex-end; flex-wrap: wrap; }
  
  ::-webkit-scrollbar { width: 6px; }
  ::-webkit-scrollbar-thumb { background: #333; border-radius: 3px; }
</style>
</head>
<body>

  <div id="header">
    <div id="title">
      RADAR TURRET
      <span id="status-badge" class="offline">OFFLINE</span>
    </div>
    <div id="btn-group">
      <button class="ui-btn" id="btn-toggle" onclick="toggleScan()">[ START ]</button>
      <button class="ui-btn" onclick="openLogs()">[ LOGS ]</button>
      <button class="ui-btn" onclick="openConfig()">[ CONFIG ]</button>
    </div>
  </div>

  <div id="radar-box">
    <canvas id="radarCanvas"></canvas>
  </div>
  
  <div id="footer">
    <span id="last-intrusion">--</span>
    <button class="ui-btn" onclick="testAlert()" style="padding:4px 8px;font-size:10px;">TEST</button>
    <button class="ui-btn" onclick="centerServos()" style="padding:4px 8px;font-size:10px;">CENTER</button>
  </div>

  <div id="modal-logs" class="modal">
    <div class="modal-content">
      <h2>SECURITY LOGS</h2>
      <textarea id="log-display" readonly>Loading...</textarea>
      <div class="actions">
        <button class="ui-btn" onclick="downloadLogs()">EXPORT</button>
        <button class="ui-btn danger" onclick="clearLogs()">WIPE</button>
        <button class="ui-btn" onclick="closeModal('modal-logs')">CLOSE</button>
      </div>
    </div>
  </div>

  <div id="modal-config" class="modal">
    <div class="modal-content">
      <h2>SYSTEM CONFIG</h2>
      
      <div class="setting">
        <label>SCAN SPEED (ms)</label>
        <div class="ctrl">
          <button class="adj-btn" onclick="adj('cfg-speed', -1)">-</button>
          <input type="range" id="cfg-speed" min="5" max="100">
          <button class="adj-btn" onclick="adj('cfg-speed', 1)">+</button>
          <span id="val-speed" class="val"></span>
        </div>
      </div>
      
      <div class="setting">
        <label>MAX RANGE (cm)</label>
        <div class="ctrl">
          <button class="adj-btn" onclick="adj('cfg-dist', -5)">-</button>
          <input type="range" id="cfg-dist" min="10" max="200">
          <button class="adj-btn" onclick="adj('cfg-dist', 5)">+</button>
          <span id="val-dist" class="val"></span>
        </div>
      </div>
      
      <div class="setting">
        <label>LOCK TIME (ms)</label>
        <div class="ctrl">
          <button class="adj-btn" onclick="adj('cfg-lock', -100)">-</button>
          <input type="range" id="cfg-lock" min="500" max="5000" step="100">
          <button class="adj-btn" onclick="adj('cfg-lock', 100)">+</button>
          <span id="val-lock" class="val"></span>
        </div>
      </div>
      
      <div class="setting">
        <label>BRIGHTNESS</label>
        <div class="ctrl">
          <button class="adj-btn" onclick="adj('cfg-bright', -10)">-</button>
          <input type="range" id="cfg-bright" min="0" max="255">
          <button class="adj-btn" onclick="adj('cfg-bright', 10)">+</button>
          <span id="val-bright" class="val"></span>
        </div>
      </div>
      
      <div class="setting">
        <label>MIN ANGLE</label>
        <div class="ctrl">
          <button class="adj-btn" onclick="adj('cfg-min', -5)">-</button>
          <input type="range" id="cfg-min" min="0" max="80">
          <button class="adj-btn" onclick="adj('cfg-min', 5)">+</button>
          <span id="val-min" class="val"></span>
        </div>
      </div>
      
       <div class="setting">
        <label>MAX ANGLE</label>
        <div class="ctrl">
          <button class="adj-btn" onclick="adj('cfg-max', -5)">-</button>
          <input type="range" id="cfg-max" min="100" max="180">
          <button class="adj-btn" onclick="adj('cfg-max', 5)">+</button>
          <span id="val-max" class="val"></span>
        </div>
      </div>
      
      <div class="setting">
        <label>BUZZER</label>
        <div class="ctrl"><input type="checkbox" id="cfg-bz" style="width:20px; height:20px; flex:0;"></div>
      </div>
      
      <div class="actions">
        <button class="ui-btn" onclick="resetConfig()">RESET</button>
        <button class="ui-btn" onclick="saveConfig()">SAVE</button>
        <button class="ui-btn" onclick="closeModal('modal-config')">CANCEL</button>
      </div>
    </div>
  </div>

<script>
  const canvas = document.getElementById('radarCanvas');
  const ctx = canvas.getContext('2d');
  let width, height, objects = [], scanAngle = 90;
  let maxRange = 50; // Will be updated from server
  let isRunning = false;
  let isOffline = true;
  let reconnectAttempts = 0;
  
  function resize() {
    const box = document.getElementById('radar-box');
    width = box.clientWidth; height = box.clientHeight;
    canvas.width = width; canvas.height = height;
  }
  window.addEventListener('resize', resize);
  setTimeout(resize, 100);

  function syncTime() {
    fetch('/time_sync?ts=' + Math.floor(Date.now()/1000))
      .catch(() => {});
  }
  syncTime();

  resize(); draw({a:90, d:0});

  setInterval(() => {
    fetch('/status')
    .then(r => { if(!r.ok) throw 1; return r.json() })
    .then(d => {
      isOffline = false;
      reconnectAttempts = 0;
      scanAngle = d.a;
      maxRange = d.r || 50;
      isRunning = d.running === 1;
      
      // Update status badge
      const badge = document.getElementById('status-badge');
      if(isRunning) {
        badge.className = 'scanning';
        badge.textContent = 'SCANNING';
        document.getElementById('btn-toggle').textContent = '[ STOP ]';
        document.getElementById('btn-toggle').classList.add('active');
      } else {
        badge.className = 'online';
        badge.textContent = 'PAUSED';
        document.getElementById('btn-toggle').textContent = '[ START ]';
        document.getElementById('btn-toggle').classList.remove('active');
      }
      
      if(d.d > 0) objects.push({ a: d.a, r: d.d, t: 1.0 });
      
      // Update last intrusion
      if(d.li_a && d.li_d) {
        document.getElementById('last-intrusion').textContent = 
          `LAST: ${d.li_d}cm @ ${d.li_a}°`;
      }
      
      draw(d);
    })
    .catch(e => {
      isOffline = true;
      reconnectAttempts++;
      const badge = document.getElementById('status-badge');
      badge.className = 'offline';
      badge.textContent = 'OFFLINE';
      
      // Retry time sync on reconnect
      if(reconnectAttempts === 3) syncTime();
      
      draw({a:scanAngle, d:0}); 
    });
  }, 100);

  function draw(data) {
    ctx.fillStyle = '#000'; ctx.fillRect(0,0,width,height);
    const ox = width / 2; const oy = height - 10; const maxR = height - 20;
    
    // Grid
    ctx.strokeStyle = '#333'; ctx.lineWidth = 1; ctx.font = "10px monospace"; ctx.textAlign = "center"; ctx.textBaseline = "middle";
    for(let i=1; i<=4; i++) {
      ctx.beginPath(); ctx.arc(ox, oy, maxR*(i/4), Math.PI, 0); ctx.stroke();
      const distLabel = Math.round((i/4) * maxRange);
      ctx.fillStyle = '#666'; ctx.fillText(distLabel + "cm", ox, oy - maxR*(i/4) - 5);
    }
    for(let deg=30; deg<=150; deg+=30) {
      const rad = -deg * Math.PI / 180;
      ctx.beginPath(); ctx.moveTo(ox, oy); ctx.lineTo(ox + maxR * Math.cos(rad), oy + maxR * Math.sin(rad)); ctx.stroke();
      ctx.fillStyle = '#666'; 
      ctx.fillText(deg+"°", ox + (maxR+15) * Math.cos(rad), oy + (maxR+15) * Math.sin(rad));
    }

    // Scan Line
    const rad = -(scanAngle * Math.PI / 180);
    ctx.strokeStyle = '#0f0'; ctx.lineWidth = 2; ctx.beginPath();
    ctx.moveTo(ox, oy); ctx.lineTo(ox + maxR * Math.cos(rad), oy + maxR * Math.sin(rad));
    ctx.stroke();
    
    // Sector Fill (sweep trail)
    const grad = ctx.createRadialGradient(ox, oy, 0, ox, oy, maxR);
    grad.addColorStop(0, 'rgba(0, 80, 0, 0.3)');
    grad.addColorStop(1, 'rgba(0, 30, 0, 0.1)');
    ctx.fillStyle = grad;
    ctx.beginPath(); ctx.moveTo(ox,oy); ctx.arc(ox, oy, maxR, rad - 0.15, rad + 0.05); ctx.fill();

    // Objects (fade out)
    objects.forEach(o => o.t -= 0.015); 
    objects = objects.filter(o => o.t > 0);
    objects.forEach(o => {
      const br = -(o.a * Math.PI / 180);
      const r_px = (o.r / maxRange) * maxR;
      if(r_px <= maxR && r_px > 0) {
        const bx = ox + r_px * Math.cos(br); const by = oy + r_px * Math.sin(br);
        ctx.fillStyle = `rgba(255, 50, 50, ${o.t})`;
        ctx.beginPath(); ctx.arc(bx, by, 4 + (1-o.t)*6, 0, 2*Math.PI); ctx.fill();
        
        // Glow effect
        ctx.fillStyle = `rgba(255, 100, 100, ${o.t * 0.3})`;
        ctx.beginPath(); ctx.arc(bx, by, 10 + (1-o.t)*8, 0, 2*Math.PI); ctx.fill();
      }
    });
    
    if(isOffline) {
      ctx.fillStyle = '#f00'; ctx.font = 'bold 20px monospace'; ctx.textAlign = 'center';
      ctx.fillText(":: DISCONNECTED ::", ox, oy - 50);
    }
  }

  // === CONTROLS ===
  function toggleScan() {
    fetch('/toggle').then(r => r.text());
  }
  
  function testAlert() {
    fetch('/test_alert').then(r => r.text());
  }
  
  function centerServos() {
    fetch('/center').then(r => r.text());
  }

  // === LOGS ===
  function openLogs() {
    document.getElementById('modal-logs').classList.add('active');
    document.getElementById('log-display').value = "Fetching...";
    fetch('/get_logs?t='+Date.now()).then(r => r.ok?r.text():"Error").then(t => {
      document.getElementById('log-display').value = t;
    });
  }
  
  function clearLogs() {
    if(confirm("Confirm Wipe Data?")) {
      fetch('/clear_logs');
      document.getElementById('log-display').value = "[CLEARED]";
    }
  }
  
  function downloadLogs() {
    const txt = document.getElementById('log-display').value;
    const blob = new Blob([txt], {type: 'text/csv'});
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement('a'); a.href = url; a.download = "radar_logs.csv"; a.click();
  }
  
  // === CONFIG ===
  function openConfig() {
    document.getElementById('modal-config').classList.add('active');
    fetch('/get_config').then(r=>r.json()).then(c => {
      setVal('cfg-speed', c.spd);
      setVal('cfg-dist', c.dst);
      setVal('cfg-lock', c.lck);
      setVal('cfg-bright', c.brt);
      setVal('cfg-min', c.min);
      setVal('cfg-max', c.max);
      document.getElementById('cfg-bz').checked = (c.bz == 1);
    });
  }
  
  function adj(id, delta) {
    const el = document.getElementById(id);
    let v = parseInt(el.value) + delta;
    v = Math.max(parseInt(el.min), Math.min(parseInt(el.max), v));
    el.value = v;
    upd(id);
  }

  function upd(id) {
    const el = document.getElementById(id);
    document.getElementById('val-'+id.split('-')[1]).innerText = el.value;
  }
  
  function setVal(id, v) {
    const el = document.getElementById(id);
    if(el) { el.value = v; upd(id); }
  }
  
  function resetConfig() {
    if(confirm("Reset all settings?")) {
      fetch('/reset_config');
      closeModal('modal-config');
    }
  }
  
  function saveConfig() {
    const q = [
      's='+document.getElementById('cfg-speed').value,
      'd='+document.getElementById('cfg-dist').value,
      'l='+document.getElementById('cfg-lock').value,
      'br='+document.getElementById('cfg-bright').value,
      'mn='+document.getElementById('cfg-min').value,
      'mx='+document.getElementById('cfg-max').value,
      'b='+(document.getElementById('cfg-bz').checked ? 1 : 0)
    ].join('&');
    
    fetch('/save_config?'+q);
    closeModal('modal-config');
  }

  function closeModal(id) { document.getElementById(id).classList.remove('active'); }
  
  ['speed','dist','lock','bright','min','max'].forEach(k => {
    document.getElementById('cfg-'+k).oninput = function() { upd('cfg-'+k); }
  });
  
</script>
</body>
</html>
)rawliteral";

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== RADAR TURRET v2.0 ===");

  // Initialize SPIFFS
  if(!SPIFFS.begin(true)) {
    Serial.println("[!] SPIFFS Formatting...");
  }
  
  // Load configuration
  preferences.begin("radar-app", false);
  loadConfig();
  validateConfig();
  
  // GPIO Setup
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Servo Setup
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  scanServo.setPeriodHertz(50);
  scanServo.attach(SERVO_SCAN_PIN, 500, 2400);
  arrowServo.setPeriodHertz(50);
  arrowServo.attach(SERVO_ARROW_PIN, 500, 2400);

  // NeoPixel Setup
  strip.begin();
  strip.setBrightness(cfg.ledBright);
  strip.show();

  // WiFi AP Setup
  WiFi.softAP(ssid, password);
  Serial.print("[+] AP IP: ");
  Serial.println(WiFi.softAPIP());

  // --- HTTP Endpoints ---
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/time_sync", handleTimeSync);
  server.on("/get_logs", handleGetLogs);
  server.on("/clear_logs", handleClearLogs);
  server.on("/get_config", handleGetConfig);
  server.on("/save_config", handleSaveConfig);
  server.on("/reset_config", handleResetConfig);
  server.on("/toggle", handleToggle);
  server.on("/center", handleCenter);
  server.on("/test_alert", handleTestAlert);
  server.onNotFound([]() { server.send(404, "text/plain", "404"); });

  server.begin();
  Serial.println("[+] Web server started");
  
  // Initial state
  centerServos();
  ledOff();
  
  Serial.println("[+] Ready!");
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  server.handleClient();
  handleButton();
  
  switch(currentState) {
    case STATE_IDLE:
      // Do nothing, waiting for start
      break;
      
    case STATE_SCANNING:
      runRadarScan();
      break;
      
    case STATE_LOCKED:
      handleLockOn();
      break;
      
    case STATE_STARTUP:
      runStartupAnimation();
      break;
      
    case STATE_TEST_ALERT:
      runTestAlert();
      break;
  }
}

// ============================================================================
// STATE HANDLERS
// ============================================================================
void runRadarScan() {
  unsigned long now = millis();
  
  if (now - lastScanTime >= (unsigned long)cfg.scanSpeed) {
    lastScanTime = now;
    
    // Update scan position
    scanPos += scanDirection;
    if (scanPos >= cfg.maxAngle) {
      scanPos = cfg.maxAngle;
      scanDirection = -1;
    }
    if (scanPos <= cfg.minAngle) {
      scanPos = cfg.minAngle;
      scanDirection = 1;
    }
    
    scanServo.write(scanPos);
    
    // Get distance reading
    int dist = getDistance();
    lastDistance = dist;
    
    if (dist > 0 && dist < cfg.maxDist) {
      // Object detected!
      currentState = STATE_LOCKED;
      lockOnStartTime = now;
      
      // Move arrow to target
      arrowServo.write(scanPos);
      lastArrowPos = scanPos;
      
      // Log intrusion
      lastIntrusionAngle = scanPos;
      lastIntrusionDist = dist;
      logIntrusion(scanPos, dist);
      
      // Alert!
      runAlert(dist);
    } else {
      ledIdle();
      noTone(BUZZER_PIN);
    }
  }
}

void handleLockOn() {
  unsigned long now = millis();
  
  // Keep alerting based on last known distance
  runAlert(lastDistance);
  
  // Check if lock time expired
  if (now - lockOnStartTime > (unsigned long)cfg.lockTime) {
    currentState = STATE_SCANNING;
    ledOff();
    noTone(BUZZER_PIN);
    
    // Reset arrow to center
    arrowServo.write(90);
    lastArrowPos = 90;
  }
}

void runStartupAnimation() {
  unsigned long now = millis();
  
  if (now - animStartTime >= ANIM_FRAME_MS) {
    animStartTime = now;
    
    if (animFrame % 2 == 0) {
      setAllLeds(0, 0, 255);  // Blue
    } else {
      ledOff();
    }
    
    animFrame++;
    
    if (animFrame >= 6) {  // 3 blinks done
      animFrame = 0;
      currentState = STATE_SCANNING;
      ledIdle();
    }
  }
}

void runTestAlert() {
  unsigned long now = millis();
  
  if (now - animStartTime >= 100) {
    animStartTime = now;
    animFrame++;
    
    // Cycle through alerts
    if (animFrame < 3) {
      setAllLeds(255, 255, 0);  // Yellow
      if(cfg.buzzerOn) tone(BUZZER_PIN, 1000);
    } else if (animFrame < 6) {
      setAllLeds(255, 140, 0);  // Orange
      if(cfg.buzzerOn) tone(BUZZER_PIN, 1500);
    } else if (animFrame < 9) {
      if (animFrame % 2) setAllLeds(255, 0, 0);
      else ledOff();
      if(cfg.buzzerOn) tone(BUZZER_PIN, 2000);
    } else {
      animFrame = 0;
      currentState = STATE_IDLE;
      ledOff();
      noTone(BUZZER_PIN);
    }
  }
}

// ============================================================================
// ALERT SYSTEM
// ============================================================================
void runAlert(int dist) {
  if (dist < 5) {
    // CRITICAL: Fast blink red + high pitch
    if ((millis() / 100) % 2 == 0) {
      setAllLeds(255, 0, 0);
    } else {
      ledOff();
    }
    if(cfg.buzzerOn) tone(BUZZER_PIN, 2000);
  } else if (dist < 15) {
    // WARNING: Orange + mid pitch
    setAllLeds(255, 140, 0);
    if(cfg.buzzerOn) tone(BUZZER_PIN, 1500);
  } else {
    // CAUTION: Yellow + low pitch
    setAllLeds(255, 255, 0);
    if(cfg.buzzerOn) tone(BUZZER_PIN, 1000);
  }
}

// ============================================================================
// LED HELPERS
// ============================================================================
void setAllLeds(uint8_t r, uint8_t g, uint8_t b) {
  for(int i = 0; i < NUM_PIXELS; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

void ledOff() {
  setAllLeds(0, 0, 0);
}

void ledIdle() {
  setAllLeds(0, 8, 0);  // Dim green
}

// ============================================================================
// SENSOR
// ============================================================================
int getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 25000);  // 25ms timeout (~4m max)
  
  if (duration == 0) return -1;
  
  return (int)(duration * 0.034 / 2);
}

// ============================================================================
// CONFIG MANAGEMENT
// ============================================================================
void loadConfig() {
  cfg.scanSpeed  = preferences.getInt("speed", 20);
  cfg.maxDist    = preferences.getInt("dist", 50);
  cfg.lockTime   = preferences.getInt("lock", 2000);
  cfg.ledBright  = preferences.getInt("bright", 50);
  cfg.minAngle   = preferences.getInt("min", 15);
  cfg.maxAngle   = preferences.getInt("max", 165);
  cfg.buzzerOn   = preferences.getBool("bz", true);
  
  Serial.println("[+] Config loaded");
}

void saveConfig() {
  preferences.putInt("speed", cfg.scanSpeed);
  preferences.putInt("dist", cfg.maxDist);
  preferences.putInt("lock", cfg.lockTime);
  preferences.putInt("bright", cfg.ledBright);
  preferences.putInt("min", cfg.minAngle);
  preferences.putInt("max", cfg.maxAngle);
  preferences.putBool("bz", cfg.buzzerOn);
  
  Serial.println("[+] Config saved");
}

void validateConfig() {
  // Ensure angles are valid
  if (cfg.minAngle >= cfg.maxAngle) {
    cfg.minAngle = 15;
    cfg.maxAngle = 165;
    Serial.println("[!] Invalid angles, reset to defaults");
  }
  
  // Clamp values
  cfg.scanSpeed = constrain(cfg.scanSpeed, 5, 100);
  cfg.maxDist   = constrain(cfg.maxDist, 10, 200);
  cfg.lockTime  = constrain(cfg.lockTime, 500, 5000);
  cfg.ledBright = constrain(cfg.ledBright, 0, 255);
  cfg.minAngle  = constrain(cfg.minAngle, 0, 80);
  cfg.maxAngle  = constrain(cfg.maxAngle, 100, 180);
}

// ============================================================================
// LOGGING
// ============================================================================
void logIntrusion(int ang, int dist) {
  // Check log file size and rotate if needed
  if (SPIFFS.exists("/log.txt")) {
    File check = SPIFFS.open("/log.txt", "r");
    if (check && check.size() > LOG_MAX_SIZE) {
      check.close();
      SPIFFS.remove("/log.txt");
      Serial.println("[!] Log rotated (size limit)");
    } else if (check) {
      check.close();
    }
  }
  
  // Append log entry
  File f = SPIFFS.open("/log.txt", FILE_APPEND);
  if (f) {
    char buf[80];
    
    if (timeSynced) {
      time_t now;
      time(&now);
      struct tm *t = localtime(&now);
      sprintf(buf, "[%02d:%02d:%02d] INTRUSION: %dcm @ %d°\n", 
              t->tm_hour, t->tm_min, t->tm_sec, dist, ang);
    } else {
      sprintf(buf, "[%lu] INTRUSION: %dcm @ %d°\n", 
              millis() / 1000, dist, ang);
    }
    
    f.print(buf);
    f.close();
    Serial.print("[LOG] "); Serial.print(buf);
  }
}

// ============================================================================
// BUTTON HANDLER (Non-blocking debounce)
// ============================================================================
void handleButton() {
  static int lastBtnState = HIGH;
  static unsigned long lastDebounceTime = 0;
  
  int reading = digitalRead(BUTTON_PIN);
  
  if (reading != lastBtnState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    static int stableBtnState = HIGH;
    
    if (reading != stableBtnState) {
      stableBtnState = reading;
      
      // Button pressed (LOW)
      if (stableBtnState == LOW) {
        toggleRunning();
      }
    }
  }
  
  lastBtnState = reading;
}

void toggleRunning() {
  if (currentState == STATE_IDLE) {
    // Start scanning with animation
    currentState = STATE_STARTUP;
    animStartTime = millis();
    animFrame = 0;
    Serial.println("[*] Starting scan...");
  } else {
    // Stop
    currentState = STATE_IDLE;
    centerServos();
    ledOff();
    noTone(BUZZER_PIN);
    Serial.println("[*] Stopped");
  }
}

// ============================================================================
// SERVO HELPERS
// ============================================================================
void centerServos() {
  scanServo.write(90);
  arrowServo.write(90);
  scanPos = 90;
  lastArrowPos = 90;
}

// ============================================================================
// HTTP HANDLERS
// ============================================================================
void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleStatus() {
  char buf[128];
  int running = (currentState == STATE_SCANNING || currentState == STATE_LOCKED) ? 1 : 0;
  
  sprintf(buf, "{\"a\":%d,\"d\":%d,\"r\":%d,\"running\":%d,\"li_a\":%d,\"li_d\":%d}",
          scanPos, lastDistance, cfg.maxDist, running, 
          lastIntrusionAngle, lastIntrusionDist);
  
  server.send(200, "application/json", buf);
}

void handleTimeSync() {
  if (server.hasArg("ts")) {
    struct timeval tv;
    tv.tv_sec = server.arg("ts").toInt();
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    timeSynced = true;
    Serial.println("[+] Time synced");
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleGetLogs() {
  if (SPIFFS.exists("/log.txt")) {
    File f = SPIFFS.open("/log.txt", "r");
    if (f.size() == 0) {
      server.send(200, "text/plain", "-- EMPTY LOG --");
    } else {
      server.streamFile(f, "text/plain");
    }
    f.close();
  } else {
    server.send(200, "text/plain", "-- NO DATA --");
  }
}

void handleClearLogs() {
  SPIFFS.remove("/log.txt");
  lastIntrusionAngle = 0;
  lastIntrusionDist = 0;
  Serial.println("[!] Logs cleared");
  server.send(200, "text/plain", "CLEARED");
}

void handleGetConfig() {
  String json = "{";
  json += "\"spd\":" + String(cfg.scanSpeed) + ",";
  json += "\"dst\":" + String(cfg.maxDist) + ",";
  json += "\"lck\":" + String(cfg.lockTime) + ",";
  json += "\"brt\":" + String(cfg.ledBright) + ",";
  json += "\"min\":" + String(cfg.minAngle) + ",";
  json += "\"max\":" + String(cfg.maxAngle) + ",";
  json += "\"bz\":" + String(cfg.buzzerOn ? 1 : 0);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleSaveConfig() {
  if (server.hasArg("s"))  cfg.scanSpeed  = server.arg("s").toInt();
  if (server.hasArg("d"))  cfg.maxDist    = server.arg("d").toInt();
  if (server.hasArg("l"))  cfg.lockTime   = server.arg("l").toInt();
  if (server.hasArg("br")) cfg.ledBright  = server.arg("br").toInt();
  if (server.hasArg("mn")) cfg.minAngle   = server.arg("mn").toInt();
  if (server.hasArg("mx")) cfg.maxAngle   = server.arg("mx").toInt();
  if (server.hasArg("b"))  cfg.buzzerOn   = server.arg("b").toInt();
  
  validateConfig();
  saveConfig();
  
  strip.setBrightness(cfg.ledBright);
  strip.show();
  
  server.send(200, "text/plain", "SAVED");
}

void handleResetConfig() {
  preferences.clear();
  loadConfig();
  validateConfig();
  
  strip.setBrightness(cfg.ledBright);
  strip.show();
  
  Serial.println("[!] Config reset to defaults");
  server.send(200, "text/plain", "RESET");
}

void handleToggle() {
  toggleRunning();
  server.send(200, "text/plain", currentState != STATE_IDLE ? "RUNNING" : "STOPPED");
}

void handleCenter() {
  centerServos();
  server.send(200, "text/plain", "CENTERED");
}

void handleTestAlert() {
  if (currentState == STATE_IDLE) {
    currentState = STATE_TEST_ALERT;
    animStartTime = millis();
    animFrame = 0;
    server.send(200, "text/plain", "TESTING");
  } else {
    server.send(200, "text/plain", "BUSY");
  }
}
