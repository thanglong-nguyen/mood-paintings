#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <WiFi.h>
#include <WiFiUdp.h>

// ================================================================
// WIFI SETTINGS 
// Run ipconfig on PC → copy the IPv4 address → paste into pcIP
// ================================================================
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* pcIP     = "YOUR_PC_IP_ADDRESS";
const int   udpPort  = 5005;

WiFiUDP udp;

// ================================================================
// MAX30102 SENSOR
// ================================================================
MAX30105 particleSensor;

// ================================================================
// BPM — rolling average over last 4 beats
// ================================================================
const byte  RATE_SIZE = 4;
byte        rates[RATE_SIZE];
byte        rateSpot  = 0;
long        lastBeat  = 0;
float       beatsPerMinute = 0;
int         beatAvg   = 0;

// ================================================================
// HRV — RMSSD from successive differences (Stanford HeartBeet method)
//
// interval = delta - lastDelta  (change between consecutive beat gaps)
// gate: abs(interval) < 200 ms
// RMSSD = sqrt( sum(interval²) / (N-1) )
// ================================================================
#define NUM_HRV_SAMPLES  10
#define MAX_HRV_INTERVAL 200
int16_t HRintervals[NUM_HRV_SAMPLES];
uint8_t HRV_index = 0;
int16_t lastDelta = 0;
float   rmssd     = 0.0;

// ================================================================
// PERSONAL BASELINE CALIBRATION
//
// Timer starts on first finger contact and runs uninterrupted for 30s.
// Only beats with beatsPerMinute > 45 are added to the sample pool,
// so early warmup noise is excluded without resetting the clock.
//
// Thresholds (% of personal resting baseline):
//   Calm       >= 85%
//   Neutral    >= 60%
//   Stressed   >= 35%
//   HighStress <  35%  (or BPM > 120)
// ================================================================
#define BASELINE_DURATION_MS 30000
#define BASELINE_MAX_SAMPLES 60
#define MIN_BASELINE_SAMPLES 10  // calibrate after 10 stable samples (~10-15s)

float   baselineSamples[BASELINE_MAX_SAMPLES];
uint8_t baselineCount = 0;
float   rmssdBaseline = 0.0;
float   threshCalm    = 0.0;
float   threshNeutral = 0.0;
float   threshStressed= 0.0;

unsigned long fingerOnTime = 0;
bool calibrated = false;

// ================================================================
// FINGER DETECTION
// ================================================================
#define IR_THRESHOLD  50000
#define SLEEP_TIMEOUT 30000
unsigned long lastFingerTime = 0;

// ================================================================
// UDP RATE LIMITING
// ================================================================
unsigned long lastSendTime = 0;
#define MIN_SEND_INTERVAL 500

// ================================================================
// SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(6, 7);

  // --- Sensor ---
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 not found — check wiring!");
    while (1);
  }
  particleSensor.setup(60, 4, 2, 100, 411, 4096);
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeIR(0x1F);
  particleSensor.setPulseAmplitudeGreen(0);
  Serial.println("Sensor ready.");

  // --- WiFi ---
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("XIAO IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Sending to PC at: ");
    Serial.print(pcIP);
    Serial.print(":");
    Serial.println(udpPort);
  } else {
    Serial.println("\nWiFi FAILED — will keep retrying in background.");
  }

  udp.begin(udpPort);
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  long irValue = particleSensor.getIR();

  // --- No finger: reset everything ---
  if (irValue < IR_THRESHOLD) {
    HRV_index     = 0;
    calibrated    = false;
    fingerOnTime  = 0;
    baselineCount = 0;
    rmssd         = 0.0;
    lastDelta     = 0;
    if (millis() - lastFingerTime > SLEEP_TIMEOUT) {
      sendMoodPacket(0, 0.0, "NoFinger");
      lastFingerTime = millis();
    }
    delay(200);
    return;
  }

  if (fingerOnTime == 0) {
    fingerOnTime = millis();
    Serial.println("Finger detected — collecting personal baseline...");
  }

  lastFingerTime = millis();

  // --- Beat detected ---
  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute > 20 && beatsPerMinute < 255) {

      // Rolling BPM average
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
      beatAvg /= RATE_SIZE;

      // Successive RR difference
      int16_t interval = (int16_t)(delta - lastDelta);
      lastDelta = delta;

      // Gate: reject motion artefacts
      if (abs(interval) < MAX_HRV_INTERVAL) {
        HRintervals[HRV_index++] = interval;
        HRV_index %= NUM_HRV_SAMPLES;

        // RMSSD = sqrt( sum(interval²) / (N-1) )
        float sumSq = 0.0;
        for (int i = 0; i < NUM_HRV_SAMPLES; i++) {
          sumSq += (float)HRintervals[i] * HRintervals[i];
        }
        rmssd = sqrt(sumSq / (NUM_HRV_SAMPLES - 1));

        // --- Baseline collection ---
        // Collect samples when BPM is stable (> 45).
        // Calibrate as soon as MIN_BASELINE_SAMPLES are collected —
        // no waiting for a fixed time window, so a brief finger lift
        // doesn't restart the whole process.
        if (!calibrated && rmssd > 0 && beatsPerMinute > 35) {
          if (baselineCount < BASELINE_MAX_SAMPLES) {
            baselineSamples[baselineCount++] = rmssd;
          }

          // Calibrate once we have enough samples
          if (baselineCount >= MIN_BASELINE_SAMPLES) {
            float sum = 0;
            for (int i = 0; i < baselineCount; i++) sum += baselineSamples[i];
            rmssdBaseline  = sum / baselineCount;
            threshCalm     = rmssdBaseline * 0.85f;
            threshNeutral  = rmssdBaseline * 0.60f;
            threshStressed = rmssdBaseline * 0.35f;
            calibrated     = true;

            Serial.print("Baseline RMSSD: "); Serial.println(rmssdBaseline, 1);
            Serial.print("Calm     >= ");     Serial.println(threshCalm, 1);
            Serial.print("Neutral  >= ");     Serial.println(threshNeutral, 1);
            Serial.print("Stressed >= ");     Serial.println(threshStressed, 1);
            Serial.println("Personal calibration done — mood active.");
          } else {
            // Still collecting — report progress
            if (millis() - lastSendTime >= MIN_SEND_INTERVAL) {
              Serial.print("Calibrating... ");
              Serial.print(baselineCount); Serial.print("/");
              Serial.println(MIN_BASELINE_SAMPLES);
              sendMoodPacket(beatAvg, rmssd, "Calibrating");
              lastSendTime = millis();
            }
            return;
          }
        }
      }

      // Send mood
      if (millis() - lastSendTime >= MIN_SEND_INTERVAL) {
        sendMoodPacket(beatAvg, rmssd, getMood(beatAvg, rmssd));
        lastSendTime = millis();
      }
    }
  }
}

// ================================================================
// MOOD CLASSIFIER — all thresholds personal, derived from baseline
// ================================================================
String getMood(int bpm, float rmssdVal) {
  if (!calibrated)                return "Calibrating";
  if (bpm > 120)                  return "HighStress";
  if (rmssdVal >= threshCalm)     return "Calm";
  if (rmssdVal >= threshNeutral)  return "Neutral";
  if (rmssdVal >= threshStressed) return "Stressed";
  return "HighStress";
}

// ================================================================
// UDP SENDER — "BPM:72,RMSSD:44.3,MOOD:Calm"
// ================================================================
void sendMoodPacket(int bpm, float rmssdVal, String mood) {
  String msg = "BPM:"    + String(bpm) +
               ",RMSSD:" + String(rmssdVal, 1) +
               ",MOOD:"  + mood;

  Serial.println(msg);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi down — reconnecting...");
    WiFi.begin(ssid, password);
    int w = 0;
    while (WiFi.status() != WL_CONNECTED && w < 20) { delay(500); w++; }
    if (WiFi.status() == WL_CONNECTED) Serial.println("Reconnected!");
  }
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(pcIP, udpPort);
    udp.print(msg);
    udp.endPacket();
  }
}
