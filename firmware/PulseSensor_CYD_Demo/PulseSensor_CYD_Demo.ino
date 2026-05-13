/*
 * PulseSensor_CYD_Demo.ino
 * One-file PulseSensor Playground demo for the ESP32 CYD / Cheap Yellow Display.
 *
 * This sketch is intentionally kept in one Arduino file so beginners can open,
 * read, install, and modify it without chasing helper tabs.
 *
 * What it shows:
 *   1. Live PPG waveform from getLatestSample()
 *   2. Beat markers from sawStartOfBeat()
 *   3. BPM from getBeatsPerMinute()
 *   4. IBI timing from getInterBeatIntervalMs()
 *   5. Signal quality from BPM + IBI + getPulseAmplitude()
 *   6. A simple tachogram from qualified IBI values
 *
 * Hardware defaults:
 *   Board:       ESP32-2432S028R CYD
 *   Display:     ILI9341 320x240 TFT
 *   PulseSensor: signal wire on GPIO 35
 *   RGB LED:     Red=4, Green=16, Blue=17, active-low
 *
 * Educational demo only. Not a medical device.
 */

// ===== TFT_eSPI CYD SETUP =====
// These defines make the sketch more self-contained. They are the same CYD
// display pins used by the previous launcher compile flags.

#define USER_SETUP_LOADED 1
#define ILI9341_DRIVER 1
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21
#define TFT_BACKLIGHT_ON HIGH
#define LOAD_GLCD 1
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000

#include <TFT_eSPI.h>
#define USE_ARDUINO_INTERRUPTS true
#include <PulseSensorPlayground.h>

// ===== HARDWARE PINS =====

#define PULSE_PIN 35
#define BACKLIGHT 21
#define LED_RED_PIN 4
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN 17

// ===== PULSESENSOR SETTINGS =====

#define PULSE_THRESHOLD 550
#define NO_BEAT_TIMEOUT_MS 3000
#define MIN_QUALIFIED_BPM 40
#define MAX_QUALIFIED_BPM 180
#define MIN_QUALIFIED_IBI 333
#define MAX_QUALIFIED_IBI 1500
#define MIN_QUALIFIED_AMPLITUDE 20
#define SIGNAL_QUALITY_STEPS 12
#define LOCK_QUALITY_STEPS 10
#define BPM_WINDOW_SIZE 10
#define TACHO_SIZE 60

// ===== DISPLAY TIMING =====

#define AUTO_CYCLE_SCREENS true
#define SCREEN_TIME_MS 9000
#define SAMPLE_DRAW_MS 20
#define PANEL_DRAW_MS 180
#define SERIAL_PRINT_MS 700

// ===== SCREEN LAYOUT =====

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define HEADER_H 34
#define FOOTER_H 28
#define GRAPH_X 8
#define GRAPH_Y 44
#define GRAPH_W 304
#define GRAPH_H 128
#define PANEL_Y 178
#define PANEL_H 54

// ===== COLORS (RGB565) =====

#define COLOR_BG 0x0000
#define COLOR_SCREEN 0x0841
#define COLOR_PANEL 0x1082
#define COLOR_PANEL_DARK 0x0400
#define COLOR_GRID 0x18E3
#define COLOR_GRID_SOFT 0x10A2
#define COLOR_TEXT 0xFFFF
#define COLOR_MUTED 0x8C71
#define COLOR_CYAN 0x07FF
#define COLOR_CYAN_DARK 0x0452
#define COLOR_TEAL 0x05F3
#define COLOR_GREEN 0x7E8F
#define COLOR_RED 0xF800
#define COLOR_RED_DARK 0x6000
#define COLOR_AMBER 0xFBE0
#define COLOR_BLUE 0x6C9F

// ===== GLOBAL OBJECTS =====

TFT_eSPI tft = TFT_eSPI();
PulseSensorPlayground pulseSensor;

enum DemoScreen {
  SCREEN_SIGNAL,
  SCREEN_QUALITY,
  SCREEN_TACHOGRAM
};

// Arduino's sketch preprocessor can place generated setup()/loop() prototypes
// in awkward spots in larger one-file sketches. Declaring them early keeps the
// generated C++ straightforward.
void setup();
void loop();

DemoScreen currentScreen = SCREEN_SIGNAL;

// ===== LIVE SENSOR STATE =====

int currentSignal = 512;
int displayBPM = 0;
int displayIBI = 0;
int pulseAmplitude = 0;
int minSignal = 512;
int maxSignal = 512;
int signalQuality = 0;
int ledBrightness = 0;

bool pulseSensorReady = false;
bool lockedSignal = false;
bool screenDirty = true;

unsigned long lastBeatTime = 0;
unsigned long lastQualifiedBeatTime = 0;
unsigned long lastScreenChange = 0;
unsigned long lastDraw = 0;
unsigned long lastPanelDraw = 0;
unsigned long lastSerialPrint = 0;

// ===== LIVE PPG DRAWING STATE =====

int graphX = 0;
int lastGraphY = GRAPH_Y + GRAPH_H / 2;

// ===== BEAT HISTORY =====

int qualifiedIbis[BPM_WINDOW_SIZE];
int qualifiedIbiCount = 0;
int qualifiedIbiIndex = 0;

int tachogram[TACHO_SIZE];
int tachogramCount = 0;
int tachogramIndex = 0;

struct BeatMarker {
  int x;
  int y;
  int ibi;
  bool qualified;
  unsigned long bornAt;
};

#define MARKER_COUNT 12
BeatMarker markers[MARKER_COUNT];
int markerIndex = 0;

// ===== FORWARD DECLARATIONS =====

void setupLED();
void setRedLED(int brightness);
void updateLED();
void setupPulseSensor();
void readPulseSensor();
void handleBeat(int bpm, int ibi, int amplitude);
bool isQualifiedBeat(int bpm, int ibi, int amplitude);
void addQualifiedIbi(int ibi);
void addTachogramIbi(int ibi);
int qualifiedIbiAverage();
int warmedBPM();
float calculateRmssd();
float calculateSdnn();
void updateSignalRange();
void changeScreen(DemoScreen nextScreen);
void maybeAutoCycleScreen();
void drawCurrentScreen();
void drawHeader(const char* title, const char* status, uint16_t statusColor);
void drawFooter(const char* left, const char* right);
void drawGrid(int x, int y, int w, int h, int xStep, int yStep);
void drawSignalScreen(bool fullRedraw);
void drawSignalFrame();
void drawSignalSample();
void drawSignalPanels();
void drawBeatMarker(int x, int y, bool qualified);
void drawQualityScreen();
void drawQualityEvents();
void drawQualityMeter(int x, int y);
void drawBpmWindow(int x, int y);
void drawTachogramScreen();
void drawTachogramBars(int x, int y, int w, int h);
void drawCenteredText(const char* text, int x, int y, int w, int textSize, uint16_t color, uint16_t bg);
uint16_t blendRed(int brightness);

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("PulseSensor CYD real-time demo");

  pinMode(BACKLIGHT, OUTPUT);
  digitalWrite(BACKLIGHT, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);

  setupLED();
  setupPulseSensor();

  for (int i = 0; i < TACHO_SIZE; i++) tachogram[i] = 0;
  drawCurrentScreen();
}

void loop() {
  readPulseSensor();
  updateLED();
  maybeAutoCycleScreen();

  if (screenDirty) {
    drawCurrentScreen();
    screenDirty = false;
  }

  if (currentScreen == SCREEN_SIGNAL) {
    drawSignalSample();
    if (millis() - lastPanelDraw >= PANEL_DRAW_MS) {
      lastPanelDraw = millis();
      drawSignalPanels();
    }
  } else if (millis() - lastPanelDraw >= PANEL_DRAW_MS) {
    lastPanelDraw = millis();
    drawCurrentScreen();
  }

  if (millis() - lastSerialPrint >= SERIAL_PRINT_MS) {
    lastSerialPrint = millis();
    Serial.printf("screen=%d signal=%d amp=%d bpm=%d ibi=%d quality=%d locked=%d window=%d\n",
                  currentScreen, currentSignal, pulseAmplitude, displayBPM, displayIBI,
                  signalQuality, lockedSignal ? 1 : 0, qualifiedIbiCount);
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
    ledBrightness -= 12;
    if (ledBrightness < 0) ledBrightness = 0;
  }
  setRedLED(ledBrightness);
}

void setupPulseSensor() {
  // PulseSensor Playground expects 10-bit samples: 0..1023, idle near 512.
  // ESP32 defaults to 12-bit, so this line matters.
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

  if (pulseSensor.sawStartOfBeat()) {
    int bpm = pulseSensor.getBeatsPerMinute();
    int ibi = pulseSensor.getInterBeatIntervalMs();
    handleBeat(bpm, ibi, pulseAmplitude);
  }

  if (millis() - lastQualifiedBeatTime > NO_BEAT_TIMEOUT_MS) {
    lockedSignal = false;
    signalQuality = 0;
    displayBPM = 0;
    displayIBI = 0;
    qualifiedIbiCount = 0;
  }
}

void handleBeat(int bpm, int ibi, int amplitude) {
  bool qualified = isQualifiedBeat(bpm, ibi, amplitude);
  lastBeatTime = millis();

  if (qualified) {
    displayIBI = ibi;
    addQualifiedIbi(ibi);
    addTachogramIbi(ibi);
    lastQualifiedBeatTime = millis();
    signalQuality += 3;
    if (signalQuality > SIGNAL_QUALITY_STEPS) signalQuality = SIGNAL_QUALITY_STEPS;
  } else {
    signalQuality -= 1;
    if (signalQuality < 0) signalQuality = 0;
  }

  lockedSignal = signalQuality >= LOCK_QUALITY_STEPS && qualifiedIbiCount >= BPM_WINDOW_SIZE;
  displayBPM = lockedSignal ? warmedBPM() : bpm;

  if (lockedSignal && qualified) ledBrightness = 255;

  int y = map(currentSignal, minSignal, maxSignal, GRAPH_Y + GRAPH_H - 8, GRAPH_Y + 8);
  y = constrain(y, GRAPH_Y + 8, GRAPH_Y + GRAPH_H - 8);
  markers[markerIndex] = {graphX, y, ibi, qualified, millis()};
  markerIndex = (markerIndex + 1) % MARKER_COUNT;

  if (currentScreen != SCREEN_SIGNAL) screenDirty = true;
}

bool isQualifiedBeat(int bpm, int ibi, int amplitude) {
  if (bpm < MIN_QUALIFIED_BPM || bpm > MAX_QUALIFIED_BPM) return false;
  if (ibi < MIN_QUALIFIED_IBI || ibi > MAX_QUALIFIED_IBI) return false;
  if (amplitude < MIN_QUALIFIED_AMPLITUDE) return false;
  return true;
}

void addQualifiedIbi(int ibi) {
  qualifiedIbis[qualifiedIbiIndex] = ibi;
  qualifiedIbiIndex = (qualifiedIbiIndex + 1) % BPM_WINDOW_SIZE;
  if (qualifiedIbiCount < BPM_WINDOW_SIZE) qualifiedIbiCount++;
}

void addTachogramIbi(int ibi) {
  tachogram[tachogramIndex] = ibi;
  tachogramIndex = (tachogramIndex + 1) % TACHO_SIZE;
  if (tachogramCount < TACHO_SIZE) tachogramCount++;
}

int qualifiedIbiAverage() {
  if (qualifiedIbiCount == 0) return 0;
  long sum = 0;
  for (int i = 0; i < qualifiedIbiCount; i++) sum += qualifiedIbis[i];
  return sum / qualifiedIbiCount;
}

int warmedBPM() {
  int avgIbi = qualifiedIbiAverage();
  if (avgIbi == 0) return 0;
  return 60000 / avgIbi;
}

float calculateRmssd() {
  if (tachogramCount < 3) return 0;
  float sumSquares = 0;
  int pairs = 0;

  for (int i = 1; i < tachogramCount; i++) {
    int nowIndex = (tachogramIndex - i + TACHO_SIZE) % TACHO_SIZE;
    int prevIndex = (tachogramIndex - i - 1 + TACHO_SIZE) % TACHO_SIZE;
    int now = tachogram[nowIndex];
    int prev = tachogram[prevIndex];
    if (now == 0 || prev == 0) continue;
    float diff = now - prev;
    sumSquares += diff * diff;
    pairs++;
  }

  if (pairs == 0) return 0;
  return sqrt(sumSquares / pairs);
}

float calculateSdnn() {
  if (tachogramCount < 2) return 0;
  float sum = 0;
  int count = 0;

  for (int i = 0; i < tachogramCount; i++) {
    int value = tachogram[i];
    if (value == 0) continue;
    sum += value;
    count++;
  }
  if (count < 2) return 0;

  float mean = sum / count;
  float variance = 0;
  for (int i = 0; i < tachogramCount; i++) {
    int value = tachogram[i];
    if (value == 0) continue;
    variance += (value - mean) * (value - mean);
  }
  return sqrt(variance / count);
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

// ===== SCREEN SELECTION =====

void changeScreen(DemoScreen nextScreen) {
  currentScreen = nextScreen;
  lastScreenChange = millis();
  graphX = 0;
  lastGraphY = GRAPH_Y + GRAPH_H / 2;
  screenDirty = true;
}

void maybeAutoCycleScreen() {
  if (!AUTO_CYCLE_SCREENS) return;
  if (millis() - lastScreenChange < SCREEN_TIME_MS) return;

  if (currentScreen == SCREEN_SIGNAL) changeScreen(SCREEN_QUALITY);
  else if (currentScreen == SCREEN_QUALITY) changeScreen(SCREEN_TACHOGRAM);
  else changeScreen(SCREEN_SIGNAL);
}

void drawCurrentScreen() {
  if (currentScreen == SCREEN_SIGNAL) {
    drawSignalScreen(true);
  } else if (currentScreen == SCREEN_QUALITY) {
    drawQualityScreen();
  } else {
    drawTachogramScreen();
  }
}

// ===== SHARED DRAWING HELPERS =====

void drawHeader(const char* title, const char* status, uint16_t statusColor) {
  tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_H, COLOR_BG);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_WIDTH, COLOR_GRID);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setCursor(10, 6);
  tft.print("PULSESENSOR PLAYGROUND");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(10, 20);
  tft.print(title);

  tft.fillRoundRect(190, 7, 120, 20, 5, COLOR_PANEL_DARK);
  tft.drawRoundRect(190, 7, 120, 20, 5, statusColor);
  drawCenteredText(status, 190, 13, 120, 1, statusColor, COLOR_PANEL_DARK);
}

void drawFooter(const char* left, const char* right) {
  tft.fillRect(0, SCREEN_HEIGHT - FOOTER_H, SCREEN_WIDTH, FOOTER_H, COLOR_BG);
  tft.drawFastHLine(0, SCREEN_HEIGHT - FOOTER_H, SCREEN_WIDTH, COLOR_GRID);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setCursor(10, SCREEN_HEIGHT - 18);
  tft.print(left);
  tft.setCursor(198, SCREEN_HEIGHT - 18);
  tft.print(right);
}

void drawGrid(int x, int y, int w, int h, int xStep, int yStep) {
  tft.fillRect(x, y, w, h, COLOR_BG);
  for (int gx = 0; gx <= w; gx += xStep) {
    tft.drawFastVLine(x + gx, y, h, COLOR_GRID_SOFT);
  }
  for (int gy = 0; gy <= h; gy += yStep) {
    tft.drawFastHLine(x, y + gy, w, COLOR_GRID_SOFT);
  }
}

// ===== SCREEN 1: LIVE PPG =====

void drawSignalScreen(bool fullRedraw) {
  if (!fullRedraw) return;
  drawHeader("LIVE PPG SIGNAL", lockedSignal ? "LOCKED" : "SEARCHING", lockedSignal ? COLOR_TEAL : COLOR_AMBER);
  drawSignalFrame();
  drawSignalPanels();
}

void drawSignalFrame() {
  tft.fillRoundRect(GRAPH_X - 2, GRAPH_Y - 2, GRAPH_W + 4, GRAPH_H + 4, 6, COLOR_PANEL_DARK);
  tft.drawRoundRect(GRAPH_X - 2, GRAPH_Y - 2, GRAPH_W + 4, GRAPH_H + 4, 6, COLOR_GRID);
  drawGrid(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, 38, 32);

  int thresholdY = map(PULSE_THRESHOLD, minSignal, maxSignal, GRAPH_Y + GRAPH_H - 8, GRAPH_Y + 8);
  thresholdY = constrain(thresholdY, GRAPH_Y + 8, GRAPH_Y + GRAPH_H - 8);
  for (int x = GRAPH_X; x < GRAPH_X + GRAPH_W; x += 9) {
    tft.drawFastHLine(x, thresholdY, 5, COLOR_CYAN_DARK);
  }

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(GRAPH_X + 6, GRAPH_Y + 7);
  tft.print("getLatestSample()");
  tft.setTextColor(COLOR_CYAN, COLOR_BG);
  tft.setCursor(GRAPH_X + 6, thresholdY - 10);
  tft.print("threshold");
}

void drawSignalSample() {
  if (millis() - lastDraw < SAMPLE_DRAW_MS) return;
  lastDraw = millis();

  int y = map(currentSignal, minSignal, maxSignal, GRAPH_Y + GRAPH_H - 8, GRAPH_Y + 8);
  y = constrain(y, GRAPH_Y + 8, GRAPH_Y + GRAPH_H - 8);

  int eraseX = GRAPH_X + graphX;
  tft.drawFastVLine(eraseX, GRAPH_Y, GRAPH_H, COLOR_BG);
  if (graphX % 38 == 0) tft.drawFastVLine(eraseX, GRAPH_Y, GRAPH_H, COLOR_GRID_SOFT);
  for (int gy = 0; gy <= GRAPH_H; gy += 32) tft.drawPixel(eraseX, GRAPH_Y + gy, COLOR_GRID_SOFT);

  if (graphX > 0) {
    uint16_t waveColor = lockedSignal ? COLOR_CYAN : COLOR_RED;
    tft.drawLine(GRAPH_X + graphX - 1, lastGraphY, GRAPH_X + graphX, y, waveColor);
    tft.drawPixel(GRAPH_X + graphX, y - 1, waveColor);
    tft.drawPixel(GRAPH_X + graphX, y + 1, waveColor);
  }

  for (int i = 0; i < MARKER_COUNT; i++) {
    if (markers[i].bornAt == 0) continue;
    if (millis() - markers[i].bornAt > 2200) continue;
    if (abs(markers[i].x - graphX) < 2) continue;
    drawBeatMarker(markers[i].x, markers[i].y, markers[i].qualified);
  }

  lastGraphY = y;
  graphX++;

  if (graphX >= GRAPH_W) {
    graphX = 0;
    lastGraphY = y;
    drawSignalFrame();
  }
}

void drawSignalPanels() {
  char left[28];
  char right[28];
  snprintf(left, sizeof(left), "IBI %d ms", displayIBI);
  snprintf(right, sizeof(right), "amp %d", pulseAmplitude);
  drawFooter(left, right);

  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.fillRect(242, 17, 66, 16, COLOR_BG);
  tft.setCursor(242, 18);
  if (displayBPM > 0) tft.printf("%3d", displayBPM);
  else tft.print("-- ");
  tft.setTextSize(1);
  tft.print("BPM");
}

void drawBeatMarker(int x, int y, bool qualified) {
  int screenX = GRAPH_X + x;
  uint16_t color = qualified ? COLOR_AMBER : COLOR_RED;
  tft.drawFastVLine(screenX, GRAPH_Y, GRAPH_H, qualified ? COLOR_GRID : COLOR_RED_DARK);
  if (qualified) {
    tft.fillCircle(screenX, y, 4, color);
  } else {
    tft.drawCircle(screenX, y, 4, color);
  }
}

// ===== SCREEN 2: SIGNAL QUALITY + BPM WARM-UP =====

void drawQualityScreen() {
  drawHeader("QUALITY + BPM WARM-UP", lockedSignal ? "LOCKED" : "SEARCHING", lockedSignal ? COLOR_TEAL : COLOR_AMBER);
  tft.fillRoundRect(GRAPH_X - 2, GRAPH_Y - 2, GRAPH_W + 4, 184, 6, COLOR_PANEL_DARK);
  tft.drawRoundRect(GRAPH_X - 2, GRAPH_Y - 2, GRAPH_W + 4, 184, 6, COLOR_GRID);
  drawGrid(GRAPH_X, GRAPH_Y, GRAPH_W, 184, 38, 32);

  drawQualityEvents();
  drawQualityMeter(218, 60);
  drawBpmWindow(16, 134);

  int bpm = warmedBPM();
  tft.setTextSize(1);
  tft.setTextColor(lockedSignal ? COLOR_GREEN : COLOR_AMBER, COLOR_BG);
  tft.setCursor(16, 202);
  if (lockedSignal) tft.printf("LOCKED %d BPM", bpm);
  else tft.printf("BUILDING %d/10", qualifiedIbiCount);

  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setCursor(154, 202);
  tft.print("BPM=60000/avg(10 IBI)");

  char left[32];
  char right[24];
  snprintf(left, sizeof(left), "qualified %d/10", qualifiedIbiCount);
  snprintf(right, sizeof(right), "quality %02d/12", signalQuality);
  drawFooter(left, right);
}

void drawQualityEvents() {
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(16, 56);
  tft.print("candidate beats");

  for (int i = 0; i < MARKER_COUNT; i++) {
    int idx = (markerIndex + i) % MARKER_COUNT;
    if (markers[idx].bornAt == 0) continue;
    int x = 18 + i * 15;
    int y = 86;
    uint16_t color = markers[idx].qualified ? COLOR_GREEN : COLOR_RED;
    tft.drawFastVLine(x, y - 12, 24, color);
    if (markers[idx].qualified) tft.fillCircle(x, y, 4, color);
    else tft.drawCircle(x, y, 4, color);
  }

  tft.setTextColor(COLOR_GREEN, COLOR_BG);
  tft.setCursor(18, 111);
  tft.print("counted");
  tft.setTextColor(COLOR_RED, COLOR_BG);
  tft.setCursor(82, 111);
  tft.print("rejected");
}

void drawQualityMeter(int x, int y) {
  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setCursor(x, y - 14);
  tft.print("quality 0-12");

  for (int i = 0; i < SIGNAL_QUALITY_STEPS; i++) {
    uint16_t color = COLOR_GRID;
    if (i < signalQuality) color = i < LOCK_QUALITY_STEPS ? COLOR_AMBER : COLOR_TEAL;
    tft.fillRect(x + i * 7, y, 5, 42, color);
  }

  int lockX = x + LOCK_QUALITY_STEPS * 7 - 2;
  tft.drawFastVLine(lockX, y - 5, 55, COLOR_TEAL);
  tft.setTextColor(COLOR_TEAL, COLOR_BG);
  tft.setCursor(lockX - 8, y + 53);
  tft.print("lock");
}

void drawBpmWindow(int x, int y) {
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(x, y - 13);
  tft.print("10 qualified IBI readings for BPM");

  for (int i = 0; i < BPM_WINDOW_SIZE; i++) {
    int slotX = x + i * 29;
    bool filled = i < qualifiedIbiCount;
    tft.fillRect(slotX, y, 24, 18, filled ? COLOR_PANEL : COLOR_BG);
    tft.drawRect(slotX, y, 24, 18, filled ? COLOR_GREEN : COLOR_GRID);
    tft.setTextSize(1);
    tft.setTextColor(filled ? COLOR_TEXT : COLOR_MUTED, filled ? COLOR_PANEL : COLOR_BG);
    tft.setCursor(slotX + 3, y + 6);
    if (filled) {
      int idx = (qualifiedIbiIndex - qualifiedIbiCount + i + BPM_WINDOW_SIZE) % BPM_WINDOW_SIZE;
      tft.printf("%d", qualifiedIbis[idx] / 10);
    } else {
      tft.print("--");
    }
  }
}

// ===== SCREEN 3: SIMPLE TACHOGRAM =====

void drawTachogramScreen() {
  drawHeader("SIMPLE TACHOGRAM", "LAST 60 BEATS", COLOR_TEXT);
  tft.fillRoundRect(GRAPH_X - 2, GRAPH_Y - 2, GRAPH_W + 4, 184, 6, COLOR_PANEL_DARK);
  tft.drawRoundRect(GRAPH_X - 2, GRAPH_Y - 2, GRAPH_W + 4, 184, 6, COLOR_GRID);
  drawTachogramBars(GRAPH_X + 10, GRAPH_Y + 14, GRAPH_W - 20, 146);

  char left[32];
  char right[32];
  snprintf(left, sizeof(left), "RMSSD %dms", (int)calculateRmssd());
  snprintf(right, sizeof(right), "SDNN %dms", (int)calculateSdnn());
  drawFooter(left, right);
}

void drawTachogramBars(int x, int y, int w, int h) {
  drawGrid(x, y, w, h, 38, 32);

  int count = tachogramCount;
  if (count == 0) {
    tft.setTextSize(2);
    tft.setTextColor(COLOR_MUTED, COLOR_BG);
    drawCenteredText("waiting for IBI", x, y + 58, w, 1, COLOR_MUTED, COLOR_BG);
    return;
  }

  int minIbi = 700;
  int maxIbi = 1000;
  int sum = 0;
  int realCount = 0;
  for (int i = 0; i < count; i++) {
    int idx = (tachogramIndex - count + i + TACHO_SIZE) % TACHO_SIZE;
    int value = tachogram[idx];
    if (value == 0) continue;
    minIbi = min(minIbi, value - 30);
    maxIbi = max(maxIbi, value + 30);
    sum += value;
    realCount++;
  }

  if (maxIbi - minIbi < 160) {
    int center = realCount ? sum / realCount : 850;
    minIbi = center - 80;
    maxIbi = center + 80;
  }

  int mean = realCount ? sum / realCount : 850;
  int meanY = map(mean, minIbi, maxIbi, y + h - 8, y + 8);
  meanY = constrain(meanY, y + 8, y + h - 8);
  tft.drawFastHLine(x, meanY, w, COLOR_GRID);

  int barGap = max(3, w / max(1, count));
  int barW = max(2, barGap / 2);
  for (int i = 0; i < count; i++) {
    int idx = (tachogramIndex - count + i + TACHO_SIZE) % TACHO_SIZE;
    int value = tachogram[idx];
    if (value == 0) continue;
    int barX = x + i * barGap + barGap / 2;
    int barY = map(value, minIbi, maxIbi, y + h - 8, y + 8);
    barY = constrain(barY, y + 8, y + h - 8);
    uint16_t color = i > count - 8 ? COLOR_AMBER : COLOR_RED;
    tft.fillRect(barX - barW / 2, min(meanY, barY), barW, abs(meanY - barY) + 1, color);
  }

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(x + 6, y + 6);
  tft.printf("%dms", maxIbi);
  tft.setCursor(x + 6, y + h - 14);
  tft.printf("%d", minIbi);
}

// ===== SMALL DRAWING HELPERS =====

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
