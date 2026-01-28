#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <time.h>

// --- PIN DEFINITIONS ---
#define TRIG_PIN 5
#define ECHO_PIN 35
#define SERVO_SCAN_PIN 19
#define SERVO_ARROW_PIN 18
#define NEOPIXEL_PIN 26
#define BUZZER_PIN 23
#define BUTTON_PIN 27

// --- CONSTANTS ---
#define NUM_PIXELS 3

// --- OBJECTS ---
Servo scanServo;
Servo arrowServo;
Adafruit_NeoPixel strip(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
Preferences preferences;

// --- CONFIGURATION VARIABLES ---
int cfg_scan_speed = 20;     // Delay in ms per degree
int cfg_max_dist = 50;       // Max detection range cm
int cfg_lock_time = 2000;    // Lock duration ms
int cfg_led_bright = 50;     // 0-255
int cfg_min_angle = 15;      // Sweep Start
int cfg_max_angle = 165;     // Sweep End
bool cfg_buzzer_enabled = true;

// --- STATE VARIABLES ---
bool isRunning = false;
int scanPos = 90;
int scanDirection = 1;
int lastDistance = -1;
unsigned long lastScanTime = 0;
unsigned long lockOnStartTime = 0;
bool isLockedOn = false;
bool timeSynced = false;

// --- WIFI CREDS ---
const char *ssid = "RADAR_TURRET";
const char *password = "12345678";

// --- WEB INTERFACE ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>RADAR</title>
<style>
  :root { --bg: #000; --fg: #fff; --dim: #222; }
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
  
  #header { position: absolute; top: 0; width: 100%; padding: 15px; display: flex; justify-content: space-between; align-items: center; box-sizing: border-box; z-index: 10; max-width: 800px; }
  #title { font-size: 18px; font-weight: bold; letter-spacing: 2px; border-bottom: 2px solid var(--fg); padding-bottom: 5px; }
  #btn-group { display: flex; gap: 10px; }
  
  .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.95); z-index: 10; flex-direction: column; align-items: center; justify-content: center; backdrop-filter: blur(2px); }
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
  
  .actions { display: flex; gap: 10px; margin-top: 20px; justify-content: flex-end; }
  
  /* Scrollbar */
  ::-webkit-scrollbar { width: 6px; }
  ::-webkit-scrollbar-thumb { background: #333; border-radius: 3px; }
</style>
</head>
<body>

  <div id="header">
    <div id="title">RADAR TURRET</div>
    <div id="btn-group">
      <button class="ui-btn" onclick="openLogs()">[ LOGS ]</button>
      <button class="ui-btn" onclick="openConfig()">[ CONFIG ]</button>
    </div>
  </div>

  <div id="radar-box">
    <canvas id="radarCanvas"></canvas>
  </div>

  <div id="modal-logs" class="modal">
    <div class="modal-content">
      <h2>SECURITY LOGS</h2>
      <textarea id="log-display" readonly>Loading...</textarea>
      <div class="actions">
        <button class="ui-btn" onclick="downloadLogs()">EXPORT</button>
        <button class="ui-btn" onclick="clearLogs()" style="border-color: #f55; color: #f55;">WIPE</button>
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
            <input type="range" id="cfg-dist" min="10" max="100">
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
  
  function resize() {
    const box = document.getElementById('radar-box');
    width = box.clientWidth; height = box.clientHeight;
    canvas.width = width; canvas.height = height;
  }
  window.addEventListener('resize', resize);
  setTimeout(resize, 100);

  fetch('/time_sync?ts=' + Math.floor(Date.now()/1000));
  
  let isOffline = false;

  // Init Draw
  resize(); draw({a:90, d:0});

  setInterval(() => {
    fetch('/status')
    .then(r=>{ if(!r.ok) throw 1; return r.json() })
    .then(d => {
      isOffline = false;
      scanAngle = d.a;
      if(d.d > 0) objects.push({ a: d.a, r: d.d, t: 1.0 });
      draw(d);
    })
    .catch(e => {
        isOffline = true;
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
        ctx.fillStyle = '#666'; ctx.fillText((i*25)+"%", ox, oy - maxR*(i/4) - 5);
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
    
    // Sector Fill
    ctx.fillStyle = 'rgba(0, 50, 0, 0.2)';
    ctx.beginPath(); ctx.moveTo(ox,oy); ctx.arc(ox, oy, maxR, rad - 0.1, rad + 0.1); ctx.fill();

    // Objects
    objects.forEach(o => o.t -= 0.02); objects = objects.filter(o => o.t > 0);
    objects.forEach(o => {
        const br = -(o.a * Math.PI / 180);
        const r_px = (o.r / 50) * maxR; 
        if(r_px <= maxR) {
            const bx = ox + r_px * Math.cos(br); const by = oy + r_px * Math.sin(br);
            ctx.fillStyle = `rgba(255, 50, 50, ${o.t})`;
            ctx.beginPath(); ctx.arc(bx, by, 5 + (1-o.t)*5, 0, 2*Math.PI); ctx.fill();
        }
    });
    
    if(isOffline) {
        ctx.fillStyle = '#f00'; ctx.font = 'bold 20px monospace'; ctx.textAlign = 'center';
        ctx.fillText(":: DISCONNECTED ::", ox, oy - 50);
    }
  }

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
    const a = document.createElement('a'); a.href = url; a.download = "logs.csv"; a.click();
  }
  
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
  
  // UI Helpers
  function adj(id, delta) {
      const el = document.getElementById(id);
      let v = parseInt(el.value) + delta;
      if(v > parseInt(el.max)) v = parseInt(el.max);
      if(v < parseInt(el.min)) v = parseInt(el.min);
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

void setup() {
  Serial.begin(115200);

  if(!SPIFFS.begin(true)) Serial.println("SPIFFS Formatting...");
  preferences.begin("radar-app", false);
  loadConfig();
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1); ESP32PWM::allocateTimer(2); ESP32PWM::allocateTimer(3);
  scanServo.setPeriodHertz(50); scanServo.attach(SERVO_SCAN_PIN, 500, 2400); 
  arrowServo.setPeriodHertz(50); arrowServo.attach(SERVO_ARROW_PIN, 500, 2400);

  strip.begin();
  strip.setBrightness(cfg_led_bright);
  strip.show();

  WiFi.softAP(ssid, password);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/", []() { server.send(200, "text/html", index_html); });
  
  server.on("/status", []() {
    char buf[64]; sprintf(buf, "{\"a\":%d,\"d\":%d}", scanPos, lastDistance);
    server.send(200, "application/json", buf);
  });
  
  server.on("/time_sync", []() {
    if(server.hasArg("ts")) {
        struct timeval tv; tv.tv_sec = server.arg("ts").toInt(); tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        timeSynced = true;
        server.send(200, "text/plain", "OK");
    } else server.send(400, "text/plain", "Bad Request");
  });
  
  server.on("/get_logs", [](){
      if(SPIFFS.exists("/log.txt")){
          File f = SPIFFS.open("/log.txt", "r");
          if(f.size() == 0) server.send(200, "text/plain", "-- EMPTY LOG --");
          else server.streamFile(f, "text/plain");
          f.close();
      } else server.send(200, "text/plain", "-- NO DATA --");
  });
  
  server.on("/get_config", [](){
      String j = "{";
      j += "\"spd\":" + String(cfg_scan_speed) + ",";
      j += "\"dst\":" + String(cfg_max_dist) + ",";
      j += "\"lck\":" + String(cfg_lock_time) + ",";
      j += "\"brt\":" + String(cfg_led_bright) + ",";
      j += "\"min\":" + String(cfg_min_angle) + ",";
      j += "\"max\":" + String(cfg_max_angle) + ",";
      j += "\"bz\":" + String(cfg_buzzer_enabled);
      j += "}";
      server.send(200, "application/json", j);
  });
  
  server.on("/clear_logs", [](){ SPIFFS.remove("/log.txt"); server.send(200, "text/plain", "CLEARED"); });
  
  server.on("/save_config", [](){
      if(server.hasArg("s")) cfg_scan_speed = server.arg("s").toInt();
      if(server.hasArg("d")) cfg_max_dist = server.arg("d").toInt();
      if(server.hasArg("l")) cfg_lock_time = server.arg("l").toInt();
      if(server.hasArg("br")) cfg_led_bright = server.arg("br").toInt();
      if(server.hasArg("mn")) cfg_min_angle = server.arg("mn").toInt();
      if(server.hasArg("mx")) cfg_max_angle = server.arg("mx").toInt();
      if(server.hasArg("b")) cfg_buzzer_enabled = server.arg("b").toInt();
      saveConfig();
      strip.setBrightness(cfg_led_bright); strip.show();
      server.send(200, "text/plain", "SAVED");
  });
  
  server.on("/reset_config", [](){ preferences.clear(); loadConfig(); strip.setBrightness(cfg_led_bright); strip.show(); server.send(200, "text/plain", "RESET"); });
  server.onNotFound([]() { server.send(404, "text/plain", "404"); });

  server.begin();
  centerServos();
  setAllLeds(0,0,0);
}

void loop() {
  server.handleClient();
  handleButton();
  if (isRunning) {
    if (isLockedOn) handleLockOn();
    else runRadarScan();
  }
}

void runRadarScan() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastScanTime >= cfg_scan_speed) {
    lastScanTime = currentMillis;
    scanPos += scanDirection;
    if (scanPos >= cfg_max_angle) { scanPos = cfg_max_angle; scanDirection = -1; }
    if (scanPos <= cfg_min_angle) { scanPos = cfg_min_angle; scanDirection = 1; }
    scanServo.write(scanPos);
    
    int dist = getDistance();
    lastDistance = dist; 
    
    if (dist > 0 && dist < cfg_max_dist) {
        isLockedOn = true;
        lockOnStartTime = millis();
        arrowServo.write(scanPos);
        logIntrusion(scanPos, dist);
        alertLogic(dist);
    } else {
        setAllLeds(0, 5, 0); // Dim Green Idle
        arrowServo.write(90); 
        noTone(BUZZER_PIN);
    }
  }
}

void handleLockOn() {
    unsigned long currentMillis = millis();
    // Keep alerting based on the last known distance
    alertLogic(lastDistance);

    if (currentMillis - lockOnStartTime > cfg_lock_time) {
        isLockedOn = false;
        setAllLeds(0, 255, 0); // Green confirmation
        delay(100); 
        setAllLeds(0,0,0);
        noTone(BUZZER_PIN);
    }
}

void alertLogic(int dist) {
    if(dist < 5) {
        // CRITICAL (< 5cm): RED BLINK FAST + HIGH PITCH
        if ((millis() / 100) % 2 == 0) setAllLeds(255, 0, 0); else setAllLeds(0, 0, 0);
        if(cfg_buzzer_enabled) tone(BUZZER_PIN, 2000); 
    } else if (dist < 15) {
        // WARNING (< 15cm): ORANGE + MID PITCH
        setAllLeds(255, 140, 0);
        if(cfg_buzzer_enabled) tone(BUZZER_PIN, 1500);
    } else {
        // CAUTION (< Max): YELLOW + LOW PITCH
        setAllLeds(255, 255, 0);
        if(cfg_buzzer_enabled) tone(BUZZER_PIN, 1000);
    }
}

void logIntrusion(int ang, int d) {
    File f = SPIFFS.open("/log.txt", FILE_APPEND);
    if(f) {
        time_t now; time(&now); struct tm * t = localtime(&now); char buf[64];
        if(timeSynced) sprintf(buf, "[%02d:%02d:%02d] INT: %dcm @ %ddeg\n", t->tm_hour, t->tm_min, t->tm_sec, d, ang);
        else sprintf(buf, "[NO_TIME] INT: %dcm @ %ddeg\n", d, ang);
        f.print(buf); f.close();
    }
}

int getDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 20000); 
  if (duration == 0) return -1;
  return duration * 0.034 / 2;
}

void loadConfig() {
    cfg_scan_speed = preferences.getInt("speed", 20);
    cfg_max_dist = preferences.getInt("dist", 50);
    cfg_lock_time = preferences.getInt("lock", 2000);
    cfg_led_bright = preferences.getInt("bright", 50);
    cfg_min_angle = preferences.getInt("min", 15);
    cfg_max_angle = preferences.getInt("max", 165);
    cfg_buzzer_enabled = preferences.getBool("bz", true);
}

void saveConfig() {
    preferences.putInt("speed", cfg_scan_speed);
    preferences.putInt("dist", cfg_max_dist);
    preferences.putInt("lock", cfg_lock_time);
    preferences.putInt("bright", cfg_led_bright);
    preferences.putInt("min", cfg_min_angle);
    preferences.putInt("max", cfg_max_angle);
    preferences.putBool("bz", cfg_buzzer_enabled);
}

void handleButton() {
  static int lastBtnState = HIGH; int btnState = digitalRead(BUTTON_PIN);
  if (btnState == LOW && lastBtnState == HIGH) {
    isRunning = !isRunning;
    if (!isRunning) { centerServos(); setAllLeds(0,0,0); noTone(BUZZER_PIN); } 
    else { for(int i=0;i<3;i++) { setAllLeds(0,0,255); delay(100); setAllLeds(0,0,0); delay(100); } }
    delay(200); 
  }
  lastBtnState = btnState;
}

void centerServos() { scanServo.write(90); arrowServo.write(90); scanPos = 90; }
void setAllLeds(uint8_t r, uint8_t g, uint8_t b) { for(int i=0; i<NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(r, g, b)); strip.show(); }
