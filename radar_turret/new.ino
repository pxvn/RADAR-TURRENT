/*
 * ============================================================================
 *                           RADAR TURRET v3.0
 * ============================================================================
 * 
 * An ESP32-based ultrasonic radar turret with web interface control.
 * 
 * HARDWARE REQUIREMENTS:
 *   - ESP32 DevKit
 *   - HC-SR04 Ultrasonic Sensor (TRIG: GPIO5, ECHO: GPIO35)
 *   - 2x Servo Motors (Scan: GPIO19, Arrow: GPIO18)
 *   - WS2812B NeoPixel Strip (GPIO26, 3 LEDs)
 *   - Passive Buzzer (GPIO23)
 *   - Push Button (GPIO27, active LOW)
 * 
 * FEATURES:
 *   - 4 Operating Modes: SENTRY, STEALTH, AGGRESSIVE, PARTY
 *   - Harry Potter (Hedwig's Theme) startup melody
 *   - Real-time web dashboard with radar visualization
 *   - Synchronized LED and buzzer alerts
 *   - Persistent configuration and intrusion logging
 * 
 * BUTTON CONTROLS:
 *   - Short Press: Cycle through modes
 *   - Long Press (2s): Toggle scanning ON/OFF
 * 
 * WEB INTERFACE:
 *   Connect to "RADAR_TURRET" WiFi (password: 12345678)
 *   Open browser to 192.168.4.1
 * 
 * AUTHOR: Raghav
 * VERSION: 3.0
 * DATE: February 2026
 * 
 * ============================================================================
 */

// ============================================================================
// INCLUDES
// ============================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <time.h>

#include "web.h"  // Web interface HTML

// ============================================================================
// PIN DEFINITIONS
// ============================================================================
#define PIN_TRIG        5     // Ultrasonic trigger
#define PIN_ECHO        35    // Ultrasonic echo
#define PIN_SERVO_SCAN  19    // Scanning servo
#define PIN_SERVO_ARROW 18    // Pointing servo
#define PIN_NEOPIXEL    26    // LED strip data
#define PIN_BUZZER      23    // Buzzer output
#define PIN_BUTTON      27    // Control button

// ============================================================================
// CONSTANTS
// ============================================================================
#define NUM_LEDS        3           // Number of NeoPixels
#define LOG_MAX_SIZE    50000       // Max log file size (50KB)
#define DEBOUNCE_MS     50          // Button debounce time
#define LONG_PRESS_MS   2000        // Long press threshold

// ============================================================================
// OPERATING MODES
// ============================================================================

/*
 * Mode definitions with their parameters:
 *   - Scan Speed: Delay between servo steps (lower = faster)
 *   - Brightness: LED intensity (0-255)
 *   - Buzzer: Whether alerts make sound
 */
enum Mode {
  MODE_SENTRY = 0,    // Balanced default mode
  MODE_STEALTH,       // Quiet, dim surveillance
  MODE_AGGRESSIVE,    // Fast scan, loud alerts
  MODE_PARTY,         // Rainbow fun mode
  MODE_COUNT
};

const char* MODE_NAMES[] = {"SENTRY", "STEALTH", "AGGRESSIVE", "PARTY"};

//                          {speed, brightness, buzzer}
const int MODE_PARAMS[][3] = {
  {20,  50,  1},    // SENTRY:     Normal speed, medium brightness, buzzer ON
  {40,  10,  0},    // STEALTH:    Slow, very dim, buzzer OFF
  {10,  255, 1},    // AGGRESSIVE: Fast, full brightness, buzzer ON
  {25,  100, 1}     // PARTY:      Medium speed, bright, buzzer ON
};

// ============================================================================
// HEDWIG'S THEME (Harry Potter Startup)
// ============================================================================

// Musical note frequencies
#define NOTE_B3  247
#define NOTE_E4  330
#define NOTE_G4  392
#define NOTE_FS4 370
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_D5  587
#define NOTE_CS5 554
#define NOTE_C5  523
#define NOTE_GS4 415

// Melody notes and timings
const int HEDWIG_NOTES[] = {
  NOTE_B3, NOTE_E4, NOTE_G4, NOTE_FS4, NOTE_E4, NOTE_B4, NOTE_A4,
  NOTE_FS4, NOTE_E4, NOTE_G4, NOTE_FS4, NOTE_D5, NOTE_CS5,
  NOTE_C5, NOTE_GS4, NOTE_B3, NOTE_E4
};

const int HEDWIG_TIMES[] = {
  250, 375, 125, 250, 500, 250, 750,
  500, 375, 125, 250, 500, 250,
  750, 500, 250, 750
};

const int HEDWIG_LEN = sizeof(HEDWIG_NOTES) / sizeof(HEDWIG_NOTES[0]);

// Mode switch sound effects
const int SFX_SENTRY[] = {440, 0, 440};
const int SFX_SENTRY_T[] = {100, 50, 100};

const int SFX_STEALTH[] = {220};
const int SFX_STEALTH_T[] = {150};

const int SFX_AGGRO[] = {880, 0, 880, 0, 880};
const int SFX_AGGRO_T[] = {50, 30, 50, 30, 50};

const int SFX_PARTY[] = {262, 294, 330, 349, 392, 440, 494, 523};
const int SFX_PARTY_T[] = {80, 80, 80, 80, 80, 80, 80, 120};

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
Servo servoScan;      // Radar sweep servo
Servo servoArrow;     // Target pointer servo
Adafruit_NeoPixel leds(NUM_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
WebServer server(80);
Preferences prefs;

// ============================================================================
// CONFIGURATION
// ============================================================================
struct Config {
  int maxDist   = 50;     // Detection range (cm)
  int lockTime  = 2000;   // Lock-on duration (ms)
  int minAngle  = 15;     // Sweep start angle
  int maxAngle  = 165;    // Sweep end angle
} config;

// ============================================================================
// STATE VARIABLES
// ============================================================================

// System state machine
enum State {
  STATE_STARTUP,      // Playing startup tune
  STATE_IDLE,         // Not scanning
  STATE_SCANNING,     // Active radar sweep
  STATE_LOCKED,       // Locked on target
  STATE_MODE_SWITCH   // Mode change animation
};

State state = STATE_STARTUP;
Mode mode = MODE_SENTRY;

// Radar position
int scanPos = 90;           // Current servo angle
int scanDir = 1;            // Sweep direction (+1 or -1)
int lastDist = 0;           // Last distance reading

// Intrusion tracking
int lastIntrudeAngle = 0;
int lastIntrudeDist = 0;

// Timing (all non-blocking)
unsigned long tScan = 0;        // Last scan step time
unsigned long tLock = 0;        // Lock-on start time
unsigned long tButton = 0;      // Button press start time
unsigned long tAnim = 0;        // Animation frame time
unsigned long tNote = 0;        // Melody note time

// Melody player state
const int* melodyNotes = nullptr;
const int* melodyTimes = nullptr;
int melodyLen = 0;
int melodyIdx = 0;
bool melodyPlaying = false;

// Animation state
int animFrame = 0;
int partyHue = 0;

// Button state
bool btnPressed = false;
bool longPressUsed = false;
bool scanning = false;
bool timeSynced = false;

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================
const char* WIFI_SSID = "RADAR_TURRET";
const char* WIFI_PASS = "12345678";

// ============================================================================
// SETUP
// ============================================================================

/*
 * setup()
 * -------
 * Initializes all hardware and starts the web server.
 * Plays Harry Potter theme on boot.
 */
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== RADAR TURRET v3.0 ===");

  // Initialize file system
  if (!SPIFFS.begin(true)) {
    Serial.println("[!] SPIFFS format required");
  }
  
  // Load saved settings
  prefs.begin("radar", false);
  loadConfig();
  
  // Configure GPIO
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Initialize servos
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servoScan.setPeriodHertz(50);
  servoScan.attach(PIN_SERVO_SCAN, 500, 2400);
  servoArrow.setPeriodHertz(50);
  servoArrow.attach(PIN_SERVO_ARROW, 500, 2400);

  // Initialize LEDs
  leds.begin();
  leds.setBrightness(50);
  leds.show();

  // Start WiFi Access Point
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("[+] WiFi AP: ");
  Serial.println(WiFi.softAPIP());

  // Setup HTTP routes
  setupRoutes();
  server.begin();
  Serial.println("[+] Web server started");
  
  // Center servos
  centerServos();
  
  // Play Harry Potter!
  state = STATE_STARTUP;
  playMelody(HEDWIG_NOTES, HEDWIG_TIMES, HEDWIG_LEN);
  Serial.println("[*] Playing Hedwig's Theme...");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

/*
 * loop()
 * ------
 * Main program loop. Handles:
 *   1. Web server requests
 *   2. Button input
 *   3. Melody playback
 *   4. State machine (startup/idle/scanning/locked/mode switch)
 */
void loop() {
  server.handleClient();
  handleButton();
  updateMelody();
  
  switch (state) {
    case STATE_STARTUP:    doStartup();    break;
    case STATE_IDLE:       doIdle();       break;
    case STATE_SCANNING:   doScanning();   break;
    case STATE_LOCKED:     doLocked();     break;
    case STATE_MODE_SWITCH: doModeSwitch(); break;
  }
}

// ============================================================================
// STATE HANDLERS
// ============================================================================

/*
 * doStartup()
 * -----------
 * Blue pulsing animation while Harry Potter plays.
 * Transitions to IDLE when melody finishes.
 */
void doStartup() {
  unsigned long now = millis();
  
  if (now - tAnim >= 100) {
    tAnim = now;
    animFrame++;
    int brightness = (sin(animFrame * 0.2) + 1) * 127;
    setLeds(0, 0, brightness);
  }
  
  if (!melodyPlaying) {
    state = STATE_IDLE;
    ledsOff();
    Serial.println("[+] Ready!");
  }
}

/*
 * doIdle()
 * --------
 * Runs idle animation based on current mode.
 * PARTY mode: rainbow cycle
 * Others: subtle color pulse
 */
void doIdle() {
  unsigned long now = millis();
  
  if (now - tAnim >= 100) {
    tAnim = now;
    
    if (mode == MODE_PARTY) {
      rainbowCycle();
    } else {
      animFrame++;
      int pulse = 5 + abs(sin(animFrame * 0.1) * 10);
      setModeColor(pulse);
    }
  }
}

/*
 * doScanning()
 * ------------
 * Performs radar sweep:
 *   1. Steps servo position
 *   2. Reads distance
 *   3. If object detected, goes to LOCKED state
 *   4. Updates LEDs based on mode
 */
void doScanning() {
  unsigned long now = millis();
  int speed = MODE_PARAMS[mode][0];
  
  if (now - tScan >= speed) {
    tScan = now;
    
    // Move servo
    scanPos += scanDir;
    if (scanPos >= config.maxAngle) { scanPos = config.maxAngle; scanDir = -1; }
    if (scanPos <= config.minAngle) { scanPos = config.minAngle; scanDir = 1; }
    servoScan.write(scanPos);
    
    // Read sensor
    lastDist = readDistance();
    
    // Check for detection
    if (lastDist > 0 && lastDist < config.maxDist) {
      state = STATE_LOCKED;
      tLock = now;
      
      servoArrow.write(scanPos);
      lastIntrudeAngle = scanPos;
      lastIntrudeDist = lastDist;
      logIntrusion(scanPos, lastDist);
      
      doAlert(lastDist);
    } else {
      // Idle animation while scanning
      if (mode == MODE_PARTY) {
        rainbowCycle();
      } else if (mode == MODE_STEALTH) {
        setLeds(0, 2, 0);
      } else {
        setModeColor(8);
      }
      noTone(PIN_BUZZER);
    }
  }
}

/*
 * doLocked()
 * ----------
 * Target lock-on state. Runs alert animation.
 * Returns to SCANNING when lock time expires.
 */
void doLocked() {
  unsigned long now = millis();
  
  doAlert(lastDist);
  
  if (now - tLock > config.lockTime) {
    state = STATE_SCANNING;
    ledsOff();
    noTone(PIN_BUZZER);
    servoArrow.write(90);
  }
}

/*
 * doModeSwitch()
 * --------------
 * Flashes mode color 3 times during transition.
 */
void doModeSwitch() {
  unsigned long now = millis();
  
  if (now - tAnim >= 80) {
    tAnim = now;
    animFrame++;
    
    if (animFrame % 2 == 0) {
      setModeColor(255);
    } else {
      ledsOff();
    }
    
    if (animFrame >= 6) {
      animFrame = 0;
      state = scanning ? STATE_SCANNING : STATE_IDLE;
      if (!scanning) ledsOff();
    }
  }
}

// ============================================================================
// ALERT SYSTEM
// ============================================================================

/*
 * doAlert(dist)
 * -------------
 * Runs distance-based alert animation.
 * Behavior varies by mode:
 *   - STEALTH: Dim LEDs, no sound
 *   - PARTY: Rainbow flash with varying tone
 *   - Others: Color based on distance (yellow→orange→red)
 */
void doAlert(int dist) {
  bool buzz = MODE_PARAMS[mode][2];
  int bright = MODE_PARAMS[mode][1];
  
  // Stealth: minimal alert
  if (mode == MODE_STEALTH) {
    setLeds(bright / 3, bright / 5, 0);
    return;
  }
  
  // Party: rainbow flash
  if (mode == MODE_PARTY) {
    rainbowCycle();
    if (buzz) tone(PIN_BUZZER, 500 + (millis() % 1000));
    return;
  }
  
  // Standard distance-based alerts
  if (dist < 5) {
    // CRITICAL: Red blink
    int rate = (mode == MODE_AGGRESSIVE) ? 50 : 100;
    if ((millis() / rate) % 2) setLeds(bright, 0, 0);
    else ledsOff();
    if (buzz) tone(PIN_BUZZER, (mode == MODE_AGGRESSIVE) ? 2500 : 2000);
  } 
  else if (dist < 15) {
    // WARNING: Orange
    setLeds(bright, bright * 55 / 100, 0);
    if (buzz) tone(PIN_BUZZER, (mode == MODE_AGGRESSIVE) ? 1800 : 1500);
  } 
  else {
    // CAUTION: Yellow
    setLeds(bright, bright, 0);
    if (buzz) tone(PIN_BUZZER, (mode == MODE_AGGRESSIVE) ? 1200 : 1000);
  }
}

// ============================================================================
// MELODY SYSTEM (Non-blocking)
// ============================================================================

/*
 * playMelody(notes, times, length)
 * --------------------------------
 * Starts playing a melody in the background.
 * Does not block - call updateMelody() in loop.
 */
void playMelody(const int* notes, const int* times, int len) {
  melodyNotes = notes;
  melodyTimes = times;
  melodyLen = len;
  melodyIdx = 0;
  melodyPlaying = true;
  tNote = millis();
  
  if (notes[0] > 0) tone(PIN_BUZZER, notes[0]);
  else noTone(PIN_BUZZER);
}

/*
 * updateMelody()
 * --------------
 * Called each loop iteration. Advances melody notes.
 */
void updateMelody() {
  if (!melodyPlaying) return;
  
  unsigned long now = millis();
  
  if (now - tNote >= melodyTimes[melodyIdx]) {
    melodyIdx++;
    
    if (melodyIdx >= melodyLen) {
      melodyPlaying = false;
      noTone(PIN_BUZZER);
      return;
    }
    
    tNote = now;
    if (melodyNotes[melodyIdx] > 0) {
      tone(PIN_BUZZER, melodyNotes[melodyIdx]);
    } else {
      noTone(PIN_BUZZER);
    }
  }
}

/*
 * playModeTune(m)
 * ---------------
 * Plays the sound effect for switching to mode m.
 */
void playModeTune(Mode m) {
  switch (m) {
    case MODE_SENTRY:     playMelody(SFX_SENTRY, SFX_SENTRY_T, 3); break;
    case MODE_STEALTH:    playMelody(SFX_STEALTH, SFX_STEALTH_T, 1); break;
    case MODE_AGGRESSIVE: playMelody(SFX_AGGRO, SFX_AGGRO_T, 5); break;
    case MODE_PARTY:      playMelody(SFX_PARTY, SFX_PARTY_T, 8); break;
  }
}

// ============================================================================
// LED HELPERS
// ============================================================================

void setLeds(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds.setPixelColor(i, leds.Color(r, g, b));
  }
  leds.show();
}

void ledsOff() {
  setLeds(0, 0, 0);
}

void setModeColor(int intensity) {
  switch (mode) {
    case MODE_SENTRY:     setLeds(0, intensity, 0); break;
    case MODE_STEALTH:    setLeds(0, intensity / 3, 0); break;
    case MODE_AGGRESSIVE: setLeds(intensity, 0, 0); break;
    case MODE_PARTY:      setLeds(intensity, 0, intensity); break;
  }
}

/*
 * rainbowCycle()
 * --------------
 * Advances and displays rainbow animation on LEDs.
 */
void rainbowCycle() {
  partyHue = (partyHue + 5) % 256;
  for (int i = 0; i < NUM_LEDS; i++) {
    leds.setPixelColor(i, colorWheel((partyHue + i * 40) % 256));
  }
  leds.show();
}

/*
 * colorWheel(pos)
 * ---------------
 * Returns a color from the rainbow wheel (0-255).
 */
uint32_t colorWheel(byte pos) {
  pos = 255 - pos;
  if (pos < 85) return leds.Color(255 - pos * 3, 0, pos * 3);
  if (pos < 170) { pos -= 85; return leds.Color(0, pos * 3, 255 - pos * 3); }
  pos -= 170;
  return leds.Color(pos * 3, 255 - pos * 3, 0);
}

// ============================================================================
// BUTTON HANDLER
// ============================================================================

/*
 * handleButton()
 * --------------
 * Reads button with debounce.
 *   - Short press: cycle mode
 *   - Long press (2s): toggle scanning
 */
void handleButton() {
  static int lastRead = HIGH;
  static unsigned long debounceT = 0;
  
  int reading = digitalRead(PIN_BUTTON);
  unsigned long now = millis();
  
  if (reading != lastRead) debounceT = now;
  
  if ((now - debounceT) > DEBOUNCE_MS) {
    // Button just pressed
    if (reading == LOW && !btnPressed) {
      btnPressed = true;
      tButton = now;
      longPressUsed = false;
    }
    
    // Check for long press while held
    if (reading == LOW && btnPressed && !longPressUsed) {
      if (now - tButton >= LONG_PRESS_MS) {
        longPressUsed = true;
        toggleScanning();
      }
    }
    
    // Button released
    if (reading == HIGH && btnPressed) {
      if (!longPressUsed) {
        cycleMode();
      }
      btnPressed = false;
    }
  }
  
  lastRead = reading;
}

/*
 * cycleMode()
 * -----------
 * Advances to next mode and plays its tune.
 */
void cycleMode() {
  mode = (Mode)((mode + 1) % MODE_COUNT);
  applyMode();
  
  state = STATE_MODE_SWITCH;
  animFrame = 0;
  tAnim = millis();
  
  playModeTune(mode);
  Serial.print("[MODE] "); Serial.println(MODE_NAMES[mode]);
}

/*
 * setMode(m)
 * ----------
 * Sets a specific mode (from web interface).
 */
void setMode(Mode m) {
  if (m >= MODE_COUNT) return;
  mode = m;
  applyMode();
  
  state = STATE_MODE_SWITCH;
  animFrame = 0;
  tAnim = millis();
  
  playModeTune(mode);
  Serial.print("[MODE] "); Serial.println(MODE_NAMES[mode]);
}

/*
 * applyMode()
 * -----------
 * Updates LED brightness for current mode.
 */
void applyMode() {
  leds.setBrightness(MODE_PARAMS[mode][1]);
}

/*
 * toggleScanning()
 * ----------------
 * Starts or stops radar scanning.
 */
void toggleScanning() {
  if (scanning) {
    scanning = false;
    state = STATE_IDLE;
    centerServos();
    ledsOff();
    noTone(PIN_BUZZER);
    Serial.println("[*] Stopped");
  } else {
    scanning = true;
    state = STATE_SCANNING;
    Serial.println("[*] Scanning...");
  }
  
  // Confirmation beep
  if (MODE_PARAMS[mode][2]) {
    tone(PIN_BUZZER, scanning ? 880 : 440, 100);
  }
}

// ============================================================================
// SENSOR & SERVO
// ============================================================================

/*
 * readDistance()
 * --------------
 * Triggers ultrasonic sensor and returns distance in cm.
 * Returns -1 if no echo received (out of range).
 */
int readDistance() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  
  long duration = pulseIn(PIN_ECHO, HIGH, 25000);
  if (duration == 0) return -1;
  return (int)(duration * 0.034 / 2);
}

void centerServos() {
  servoScan.write(90);
  servoArrow.write(90);
  scanPos = 90;
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void loadConfig() {
  config.maxDist  = prefs.getInt("dist", 50);
  config.lockTime = prefs.getInt("lock", 2000);
  config.minAngle = prefs.getInt("min", 15);
  config.maxAngle = prefs.getInt("max", 165);
  
  // Validate
  if (config.minAngle >= config.maxAngle) {
    config.minAngle = 15;
    config.maxAngle = 165;
  }
}

void saveConfig() {
  prefs.putInt("dist", config.maxDist);
  prefs.putInt("lock", config.lockTime);
  prefs.putInt("min", config.minAngle);
  prefs.putInt("max", config.maxAngle);
}

// ============================================================================
// LOGGING
// ============================================================================

/*
 * logIntrusion(angle, dist)
 * -------------------------
 * Appends intrusion event to log file with timestamp.
 * Auto-rotates log when it exceeds LOG_MAX_SIZE.
 */
void logIntrusion(int angle, int dist) {
  // Check size and rotate if needed
  if (SPIFFS.exists("/log.txt")) {
    File f = SPIFFS.open("/log.txt", "r");
    if (f && f.size() > LOG_MAX_SIZE) {
      f.close();
      SPIFFS.remove("/log.txt");
    } else if (f) {
      f.close();
    }
  }
  
  // Write entry
  File f = SPIFFS.open("/log.txt", FILE_APPEND);
  if (f) {
    char buf[80];
    if (timeSynced) {
      time_t now;
      time(&now);
      struct tm* t = localtime(&now);
      sprintf(buf, "[%02d:%02d:%02d] %s: %dcm @ %d°\n",
              t->tm_hour, t->tm_min, t->tm_sec, MODE_NAMES[mode], dist, angle);
    } else {
      sprintf(buf, "[%lu] %s: %dcm @ %d°\n",
              millis() / 1000, MODE_NAMES[mode], dist, angle);
    }
    f.print(buf);
    f.close();
  }
}

// ============================================================================
// HTTP ROUTES
// ============================================================================

void setupRoutes() {
  // Main page
  server.on("/", []() {
    server.send(200, "text/html", INDEX_HTML);
  });
  
  // Status (polled by frontend)
  server.on("/status", []() {
    char buf[150];
    int running = (state == STATE_SCANNING || state == STATE_LOCKED) ? 1 : 0;
    sprintf(buf, "{\"a\":%d,\"d\":%d,\"r\":%d,\"running\":%d,\"mode\":%d,\"li_a\":%d,\"li_d\":%d}",
            scanPos, lastDist, config.maxDist, running, mode, lastIntrudeAngle, lastIntrudeDist);
    server.send(200, "application/json", buf);
  });
  
  // Time sync
  server.on("/time_sync", []() {
    if (server.hasArg("ts")) {
      struct timeval tv;
      tv.tv_sec = server.arg("ts").toInt();
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      timeSynced = true;
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "ERR");
    }
  });
  
  // Logs
  server.on("/get_logs", []() {
    if (SPIFFS.exists("/log.txt")) {
      File f = SPIFFS.open("/log.txt", "r");
      if (f.size() == 0) {
        server.send(200, "text/plain", "-- EMPTY --");
      } else {
        server.streamFile(f, "text/plain");
      }
      f.close();
    } else {
      server.send(200, "text/plain", "-- NO DATA --");
    }
  });
  
  server.on("/clear_logs", []() {
    SPIFFS.remove("/log.txt");
    lastIntrudeAngle = 0;
    lastIntrudeDist = 0;
    server.send(200, "text/plain", "OK");
  });
  
  // Config
  server.on("/get_config", []() {
    String json = "{";
    json += "\"dst\":" + String(config.maxDist) + ",";
    json += "\"lck\":" + String(config.lockTime) + ",";
    json += "\"min\":" + String(config.minAngle) + ",";
    json += "\"max\":" + String(config.maxAngle) + "}";
    server.send(200, "application/json", json);
  });
  
  server.on("/save_config", []() {
    if (server.hasArg("d"))  config.maxDist = server.arg("d").toInt();
    if (server.hasArg("l"))  config.lockTime = server.arg("l").toInt();
    if (server.hasArg("mn")) config.minAngle = server.arg("mn").toInt();
    if (server.hasArg("mx")) config.maxAngle = server.arg("mx").toInt();
    
    if (config.minAngle >= config.maxAngle) {
      config.minAngle = 15;
      config.maxAngle = 165;
    }
    saveConfig();
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/reset_config", []() {
    prefs.clear();
    loadConfig();
    server.send(200, "text/plain", "OK");
  });
  
  // Controls
  server.on("/toggle", []() {
    toggleScanning();
    server.send(200, "text/plain", scanning ? "ON" : "OFF");
  });
  
  server.on("/mode", []() {
    if (server.hasArg("m")) {
      int m = server.arg("m").toInt();
      if (m >= 0 && m < MODE_COUNT) {
        setMode((Mode)m);
      }
    }
    server.send(200, "text/plain", MODE_NAMES[mode]);
  });
  
  // 404
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });
}
