/*
 * CYD_App_Launcher.ino
 * One-screen PulseSensor dashboard for ESP32 CYD (Cheap Yellow Display).
 *
 * Hardware:
 *   Board:        ESP32-2432S028R (CYD)
 *   Display:      ILI9341 320x240 TFT
 *   PulseSensor:  signal wire on GPIO 35 (found with AnalogPinScanner)
 *   RGB LED:      Red=4, Green=16, Blue=17 (active-low onboard CYD LED)
 *
 * This sketch intentionally stays in one file for Arduino IDE beginners.
 *
 * PulseSensorPlayground functions used, following the library resources:
 *   analogReadResolution(10)  -> match PulseSensorPlayground's 0..1023 math
 *   getLatestSample()         -> live waveform
 *   sawStartOfBeat()          -> one-shot beat event
 *   getBeatsPerMinute()       -> BPM readout
 *   getInterBeatIntervalMs()  -> IBI readout
 *   getPulseAmplitude()       -> signal quality helper
 */

#include <TFT_eSPI.h>
#define USE_ARDUINO_INTERRUPTS true
#include <PulseSensorPlayground.h>

// ===== CYD PINS =====

#define PULSE_PIN 35
#define BACKLIGHT 21
#define LED_RED_PIN 4
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN 17

// ===== PULSESENSOR SETTINGS =====

#define PULSE_THRESHOLD 550
#define NO_BEAT_TIMEOUT 3000
#define MIN_QUALIFIED_BPM 40
#define MAX_QUALIFIED_BPM 180
#define MIN_QUALIFIED_IBI 333
#define MAX_QUALIFIED_IBI 1500
#define MIN_QUALIFIED_AMPLITUDE 20
#define SIGNAL_QUALITY_STEPS 12
#define LOCK_QUALITY_STEPS 10
#define REARM_SIGNAL_RANGE 120
#define REARM_NO_BEAT_MS 2200
#define REARM_COOLDOWN_MS 3500

// ===== SCREEN LAYOUT =====

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define GRAPH_X 8
#define GRAPH_Y 48
#define GRAPH_W 304
#define GRAPH_H 112

#define PANEL_Y 170
#define PANEL_H 62

// ===== COLORS (RGB565) =====

#define COLOR_BG 0x0000
#define COLOR_PANEL 0x0841
#define COLOR_PANEL_DARK 0x0400
#define COLOR_GRID 0x18E3
#define COLOR_GRID_SOFT 0x10A2
#define COLOR_TEXT 0xFFFF
#define COLOR_MUTED 0x8C71
#define COLOR_CYAN 0x07FF
#define COLOR_CYAN_DARK 0x0452
#define COLOR_TEAL 0x05F3
#define COLOR_RED 0xF800
#define COLOR_RED_DARK 0x6000
#define COLOR_AMBER 0xFBE0

// ===== GLOBAL OBJECTS =====

TFT_eSPI tft = TFT_eSPI();
PulseSensorPlayground pulseSensor;

// ===== LIVE SENSOR STATE =====

int currentSignal = 512;
int displayBPM = 0;
int displayIBI = 0;
int pulseAmplitude = 0;
int minSignal = 512;
int maxSignal = 512;

unsigned long lastBeatTime = 0;
unsigned long lastQualifiedBeatTime = 0;
unsigned long lastPanelDraw = 0;
unsigned long lastGraphDraw = 0;
unsigned long lastSerialPrint = 0;
unsigned long lastDetectorRearmTime = 0;

bool lockedSignal = false;
bool previousLockedSignal = false;
bool pulseSensorReady = false;
int signalQuality = 0;
int rearmCount = 0;

// ===== GRAPH STATE =====

int graphX = 0;
int lastGraphY = GRAPH_Y + GRAPH_H / 2;

// ===== RED LED FADE STATE =====

int ledBrightness = 0;
#define LED_FADE_SPEED 12

// ===== FORWARD DECLARATIONS =====

void setup();
void loop();
void setupLED();
void setRedLED(int brightness);
void updateLED();
void setupPulseSensor();
void readPulseSensor();
bool isQualifiedBeat(int bpm, int ibi, int amplitude);
void maybeRearmDetector();
void rearmPulseDetector(const char* reason);
void updateSignalRange();
void drawStaticScreen();
void drawHeader();
void drawGraphFrame();
void drawGraphColumnBackground(int localX);
void drawWaveform();
void drawPanels();
void drawMetricPanel(int x, const char* label, int value, const char* unit, bool valid);
void drawSignalPanel();
void drawQualitySegments(int x, int y);
void drawLedIndicator(int x, int y);
void drawCenteredText(const char* text, int x, int y, int w, int textSize, uint16_t color, uint16_t bg);
uint16_t blendRed(int brightness);

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("CYD one-screen PulseSensor dashboard");

  pinMode(BACKLIGHT, OUTPUT);
  digitalWrite(BACKLIGHT, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);

  setupLED();
  setupPulseSensor();
  drawStaticScreen();
}

void loop() {
  readPulseSensor();
  updateLED();
  drawWaveform();

  if (millis() - lastPanelDraw >= 180 || lockedSignal != previousLockedSignal) {
    lastPanelDraw = millis();
    drawHeader();
    drawPanels();
    previousLockedSignal = lockedSignal;
  }

  if (millis() - lastSerialPrint >= 500) {
    lastSerialPrint = millis();
    Serial.printf("signal=%d amp=%d bpm=%d ibi=%d locked=%d quality=%d\n",
                  currentSignal, pulseAmplitude, displayBPM, displayIBI,
                  lockedSignal ? 1 : 0, signalQuality);
  }
}

// ===== HARDWARE SETUP =====

void setupLED() {
  // ESP32 Arduino Core 3.x API. The CYD RGB LED is active-low.
  ledcAttach(LED_RED_PIN, 5000, 8);
  ledcAttach(LED_GREEN_PIN, 5000, 8);
  ledcAttach(LED_BLUE_PIN, 5000, 8);

  setRedLED(0);
  ledcWrite(LED_GREEN_PIN, 255);
  ledcWrite(LED_BLUE_PIN, 255);
}

void setRedLED(int brightness) {
  brightness = constrain(brightness, 0, 255);
  ledcWrite(LED_RED_PIN, 255 - brightness);
}

void updateLED() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 10) return;

  lastUpdate = millis();
  if (ledBrightness > 0) {
    ledBrightness -= LED_FADE_SPEED;
    if (ledBrightness < 0) ledBrightness = 0;
  }
  setRedLED(ledBrightness);
}

void setupPulseSensor() {
  // PulseSensorPlayground's detector and ESP32 example expect 10-bit samples.
  // ESP32 defaults to 12-bit, which can make the raw waveform look great while
  // the library's threshold math waits in the wrong scale.
  analogReadResolution(10);

  pulseSensor.analogInput(PULSE_PIN);
  pulseSensor.setThreshold(PULSE_THRESHOLD);
  pulseSensorReady = pulseSensor.begin();

  if (!pulseSensorReady) {
    Serial.println("PulseSensor initialization failed");
  }
}

// ===== SENSOR AND BEAT LOGIC =====

void readPulseSensor() {
  currentSignal = pulseSensor.getLatestSample();
  pulseAmplitude = pulseSensor.getPulseAmplitude();
  updateSignalRange();
  maybeRearmDetector();

  if (pulseSensor.sawStartOfBeat()) {
    int bpm = pulseSensor.getBeatsPerMinute();
    int ibi = pulseSensor.getInterBeatIntervalMs();
    bool qualified = isQualifiedBeat(bpm, ibi, pulseAmplitude);

    lastBeatTime = millis();

    if (qualified) {
      displayBPM = bpm;
      displayIBI = ibi;
      lastQualifiedBeatTime = millis();
      signalQuality += 3;
      if (signalQuality > SIGNAL_QUALITY_STEPS) signalQuality = SIGNAL_QUALITY_STEPS;
    } else {
      signalQuality -= 1;
      if (signalQuality < 0) signalQuality = 0;
    }

    lockedSignal = signalQuality >= LOCK_QUALITY_STEPS;

    // Blink/fade the rear red LED only after the beat is qualified.
    if (lockedSignal && qualified) {
      ledBrightness = 255;
    }
  }

  if (millis() - lastQualifiedBeatTime > NO_BEAT_TIMEOUT) {
    lockedSignal = false;
    signalQuality = 0;
    displayBPM = 0;
    displayIBI = 0;
  }
}

bool isQualifiedBeat(int bpm, int ibi, int amplitude) {
  if (bpm < MIN_QUALIFIED_BPM || bpm > MAX_QUALIFIED_BPM) return false;
  if (ibi < MIN_QUALIFIED_IBI || ibi > MAX_QUALIFIED_IBI) return false;
  if (amplitude < MIN_QUALIFIED_AMPLITUDE) return false;
  return true;
}

void maybeRearmDetector() {
  unsigned long now = millis();
  int liveRange = maxSignal - minSignal;
  bool signalLooksAlive = liveRange >= REARM_SIGNAL_RANGE;
  bool detectorIsQuiet = (now - lastBeatTime) >= REARM_NO_BEAT_MS;
  bool rearmCooledDown = (now - lastDetectorRearmTime) >= REARM_COOLDOWN_MS;

  if (!lockedSignal && signalLooksAlive && detectorIsQuiet && rearmCooledDown) {
    rearmPulseDetector("alive signal without beat event");
  }
}

void rearmPulseDetector(const char* reason) {
  Serial.print("Re-arming PulseSensor detector: ");
  Serial.println(reason);

  pulseSensor.pause();
  delay(8);
  pulseSensor.resume();

  lastDetectorRearmTime = millis();
  lastBeatTime = millis();
  signalQuality = 0;
  displayBPM = 0;
  displayIBI = 0;
  lockedSignal = false;
  rearmCount++;
}

void updateSignalRange() {
  static unsigned long lastDecay = 0;

  if (millis() - lastDecay >= 100) {
    lastDecay = millis();
    minSignal = min(minSignal + 4, currentSignal);
    maxSignal = max(maxSignal - 4, currentSignal);
  }

  minSignal = min(minSignal, currentSignal);
  maxSignal = max(maxSignal, currentSignal);

  if (maxSignal - minSignal < 80) {
    int center = currentSignal;
    minSignal = center - 40;
    maxSignal = center + 40;
  }
}

// ===== STATIC UI =====

void drawStaticScreen() {
  tft.fillScreen(COLOR_BG);
  drawHeader();
  drawGraphFrame();
  drawPanels();
}

void drawHeader() {
  tft.fillRect(0, 0, SCREEN_WIDTH, 42, COLOR_BG);
  tft.drawFastHLine(0, 41, SCREEN_WIDTH, COLOR_GRID);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setCursor(10, 8);
  tft.print("LIVE BEAT DETECTION");

  uint16_t statusColor = lockedSignal ? COLOR_TEAL : COLOR_AMBER;
  const char* statusText = lockedSignal ? "QUALIFIED BEAT" : "SIGNAL SEARCH";

  tft.fillRoundRect(184, 6, 126, 24, 6, lockedSignal ? 0x0248 : 0x4200);
  tft.drawRoundRect(184, 6, 126, 24, 6, statusColor);
  drawCenteredText(statusText, 184, 13, 126, 1, statusColor, lockedSignal ? 0x0248 : 0x4200);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 22);
  tft.print(lockedSignal ? "LOCKED" : "SEARCHING");
}

void drawGraphFrame() {
  tft.fillRoundRect(GRAPH_X - 2, GRAPH_Y - 2, GRAPH_W + 4, GRAPH_H + 4, 6, COLOR_PANEL_DARK);
  tft.drawRoundRect(GRAPH_X - 2, GRAPH_Y - 2, GRAPH_W + 4, GRAPH_H + 4, 6, COLOR_GRID);
  tft.fillRect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, COLOR_BG);

  for (int x = 0; x <= GRAPH_W; x += 38) {
    tft.drawFastVLine(GRAPH_X + x, GRAPH_Y, GRAPH_H, COLOR_GRID_SOFT);
  }
  for (int y = 0; y <= GRAPH_H; y += 28) {
    tft.drawFastHLine(GRAPH_X, GRAPH_Y + y, GRAPH_W, COLOR_GRID_SOFT);
  }

  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setCursor(GRAPH_X + 6, GRAPH_Y + 5);
  tft.print("LIVE LINE");
}

void drawGraphColumnBackground(int localX) {
  int screenX = GRAPH_X + localX;
  tft.drawFastVLine(screenX, GRAPH_Y, GRAPH_H, COLOR_BG);

  if (localX % 38 == 0) {
    tft.drawFastVLine(screenX, GRAPH_Y, GRAPH_H, COLOR_GRID_SOFT);
  }

  for (int y = 0; y <= GRAPH_H; y += 28) {
    tft.drawPixel(screenX, GRAPH_Y + y, COLOR_GRID_SOFT);
  }
}

// ===== LIVE GRAPH =====

void drawWaveform() {
  if (millis() - lastGraphDraw < 20) return;
  lastGraphDraw = millis();

  int y = map(currentSignal, minSignal, maxSignal, GRAPH_Y + GRAPH_H - 8, GRAPH_Y + 8);
  y = constrain(y, GRAPH_Y + 8, GRAPH_Y + GRAPH_H - 8);

  drawGraphColumnBackground(graphX);
  drawGraphColumnBackground((graphX + 1) % GRAPH_W);
  drawGraphColumnBackground((graphX + 2) % GRAPH_W);

  uint16_t waveColor = lockedSignal ? COLOR_CYAN : COLOR_CYAN_DARK;

  if (graphX > 0) {
    tft.drawLine(GRAPH_X + graphX - 1, lastGraphY, GRAPH_X + graphX, y, waveColor);
    tft.drawPixel(GRAPH_X + graphX, y - 1, waveColor);
    tft.drawPixel(GRAPH_X + graphX, y + 1, waveColor);
  }

  if (ledBrightness > 180) {
    tft.fillCircle(GRAPH_X + graphX, y, 3, COLOR_RED);
  }

  lastGraphY = y;
  graphX++;

  if (graphX >= GRAPH_W) {
    graphX = 0;
    lastGraphY = y;
    drawGraphFrame();
  }
}

// ===== DASHBOARD PANELS =====

void drawPanels() {
  drawMetricPanel(8, "BPM", displayBPM, "", lockedSignal);
  drawMetricPanel(118, "IBI", displayIBI, "ms", lockedSignal);
  drawSignalPanel();
}

void drawMetricPanel(int x, const char* label, int value, const char* unit, bool valid) {
  const int w = 102;
  tft.fillRoundRect(x, PANEL_Y, w, PANEL_H, 6, COLOR_PANEL);
  tft.drawRoundRect(x, PANEL_Y, w, PANEL_H, 6, valid ? COLOR_TEAL : COLOR_GRID);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.setCursor(x + 10, PANEL_Y + 9);
  tft.print(label);

  tft.setTextSize(4);
  tft.setTextColor(valid ? COLOR_TEXT : COLOR_MUTED, COLOR_PANEL);
  tft.setCursor(x + 10, PANEL_Y + 25);

  if (valid) {
    if (strcmp(label, "BPM") == 0) {
      tft.printf("%3d", value);
    } else {
      tft.setTextSize(3);
      tft.printf("%3d", value);
    }
  } else {
    tft.print("--");
  }

  if (valid && unit[0] != '\0') {
    tft.setTextSize(1);
    tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft.setCursor(x + 72, PANEL_Y + 45);
    tft.print(unit);
  }
}

void drawSignalPanel() {
  const int x = 228;
  const int w = 84;

  tft.fillRoundRect(x, PANEL_Y, w, PANEL_H, 6, COLOR_PANEL);
  tft.drawRoundRect(x, PANEL_Y, w, PANEL_H, 6, lockedSignal ? COLOR_RED : COLOR_GRID);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.setCursor(x + 9, PANEL_Y + 8);
  tft.print("QUALITY");

  drawQualitySegments(x + 9, PANEL_Y + 24);
  drawLedIndicator(x + 58, PANEL_Y + 32);

  tft.setTextSize(1);
  tft.setTextColor(lockedSignal ? COLOR_TEAL : COLOR_AMBER, COLOR_PANEL);
  tft.setCursor(x + 9, PANEL_Y + 48);
  tft.printf("%02d/12", signalQuality);

  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.setCursor(x + 50, PANEL_Y + 48);
  tft.printf("R%d", rearmCount);
}

void drawQualitySegments(int x, int y) {
  const int segmentW = 4;
  const int segmentGap = 2;
  for (int i = 0; i < SIGNAL_QUALITY_STEPS; i++) {
    uint16_t color = COLOR_GRID;
    if (i < signalQuality) {
      color = i < LOCK_QUALITY_STEPS ? COLOR_AMBER : COLOR_TEAL;
    }
    tft.fillRect(x + i * (segmentW + segmentGap), y, segmentW, 18, color);
  }
}

void drawLedIndicator(int x, int y) {
  uint16_t ledColor = blendRed(ledBrightness);
  tft.drawCircle(x, y, 12, COLOR_RED_DARK);
  tft.fillCircle(x, y, 8, ledColor);
  if (ledBrightness > 120) {
    tft.drawCircle(x, y, 14, COLOR_RED);
  }
}

void drawCenteredText(const char* text, int x, int y, int w, int textSize, uint16_t color, uint16_t bg) {
  int charW = 6 * textSize;
  int textW = strlen(text) * charW;
  int cursorX = x + max(0, (w - textW) / 2);
  tft.setTextSize(textSize);
  tft.setTextColor(color, bg);
  tft.setCursor(cursorX, y);
  tft.print(text);
}

uint16_t blendRed(int brightness) {
  brightness = constrain(brightness, 0, 255);
  if (brightness < 20) return COLOR_RED_DARK;
  if (brightness < 120) return 0xA800;
  return COLOR_RED;
}
