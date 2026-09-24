/*
 * ==============================================================================
 * Smart Greenhouse Automation & Monitoring System — ESP8266 Gateway / Bridge
 * Bridges Arduino Mega 2560 (Sensors & Actuators) <-> Node.js / React Dashboard
 * ==============================================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// ==============================================================================
// 1. নেটওয়ার্ক ও ব্যাকএন্ড কনফিগারেশন
// ==============================================================================
const char* WIFI_SSID     = "Roy";     // আপনার ওয়াইফাই নাম
const char* WIFI_PASSWORD = "Blackdevil0007"; // আপনার ওয়াইফাই পাসওয়ার্ড

// ব্যাকএন্ড পিসির আইপি ও পোর্ট (CMD-তে `ipconfig` দিয়ে IPv4 দেখুন)
const char* BACKEND_HOST  = "192.168.68.105";
const int   BACKEND_PORT  = 5000;

// API এন্ডপয়েন্টস
const char* SENSOR_ENDPOINT = "/api/sensors";
const char* SYSTEM_ENDPOINT = "/api/system/status";

// ==============================================================================
// 2. অবজেক্ট ও গ্লোবাল স্টেট
// ==============================================================================
ESP8266WebServer server(80);
WiFiClient wifiClient;

// Heartbeat ইন্টারভাল
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL = 15000; // ১৫ সেকেন্ড

// আর্ডুইনো মেগা থেকে প্রাপ্ত সর্বশেষ ডাটা ক্যাশ
float currentTemp       = 0.0;
float currentHumidity   = 0.0;
int   currentSoil       = 0;
int   currentAirQuality = 0;
bool  pumpStatus        = false;
bool  fanStatus         = false;
bool  lightStatus       = false;

// থ্রেশহোল্ড সেটিংস (ড্যাশবোর্ডের সাথে সিঙ্ক)
struct AutomationSettings {
  int   dry_threshold         = 600;
  float low_temp_threshold   = 31.0;
  float high_temp_threshold  = 40.0;
  int   air_quality_threshold = 100;
} settings;

// ==============================================================================
// 3. ব্যাকএন্ডে ডাটা পাঠানোর ফাংশনসমূহ (HTTP POST)
// ==============================================================================

// মেগা থেকে প্রাপ্ত JSON সরাসরি ব্যাকএন্ডে পোস্ট করা
void postSensorDataToBackend(const String& jsonPayload) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "http://" + String(BACKEND_HOST) + ":" + String(BACKEND_PORT) + SENSOR_ENDPOINT;
  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(jsonPayload);
  if (httpCode > 0) {
    Serial.printf("[HTTP POST Sensors] Code: %d\n", httpCode);
  } else {
    Serial.printf("[HTTP POST Sensors] Failed: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// ব্যাকএন্ডে সিস্টেম হার্টবিট পাঠানো
void postHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "http://" + String(BACKEND_HOST) + ":" + String(BACKEND_PORT) + SYSTEM_ENDPOINT;
  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["arduino_status"] = "connected";
  doc["esp8266_status"] = "online";
  doc["wifi_status"]    = "connected";
  doc["esp8266_ip"]     = WiFi.localIP().toString();
  doc["wifi_signal"]    = WiFi.RSSI();

  String requestBody;
  serializeJson(doc, requestBody);

  int httpCode = http.POST(requestBody);
  if (httpCode > 0) {
    Serial.printf("[Heartbeat] Sent (%d)\n", httpCode);
  } else {
    Serial.printf("[Heartbeat] Failed: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// ==============================================================================
// 4. লোকাল ওয়েব সার্ভার রিকোয়েস্ট হ্যান্ডলার (PORT 80)
// ==============================================================================

void handlePing() {
  StaticJsonDocument<128> doc;
  doc["status"] = "ok";
  doc["device"] = "esp8266_mega_bridge";
  doc["uptime"] = millis() / 1000;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleStatus() {
  StaticJsonDocument<512> doc;
  doc["device"] = "greenhouse_controller";
  doc["ip"]     = WiFi.localIP().toString();
  doc["rssi"]   = WiFi.RSSI();

  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["temperature"]   = currentTemp;
  sensors["humidity"]      = currentHumidity;
  sensors["soil_moisture"] = currentSoil;
  sensors["air_quality"]   = currentAirQuality;

  JsonObject actuators = doc.createNestedObject("actuators");
  actuators["water_pump"]   = pumpStatus ? "ON" : "OFF";
  actuators["fan"]          = fanStatus ? "ON" : "OFF";
  actuators["heater_light"] = lightStatus ? "ON" : "OFF";

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCommand() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char* device = doc["device"];
  const char* action = doc["action"];

  // কমান্ড মেগাতে পাঠানোর জন্য সিরিয়ালে ফরোয়ার্ড করা
  Serial.printf("CMD:%s:%s\n", device ? device : "", action ? action : "");

  StaticJsonDocument<128> resDoc;
  resDoc["success"] = true;
  resDoc["device"]  = device;
  resDoc["action"]  = action;

  String res;
  serializeJson(resDoc, res);
  server.send(200, "application/json", res);
}

void handleSettings() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  if (doc.containsKey("dry_threshold"))         settings.dry_threshold         = doc["dry_threshold"];
  if (doc.containsKey("low_temp_threshold"))    settings.low_temp_threshold   = doc["low_temp_threshold"];
  if (doc.containsKey("high_temp_threshold"))   settings.high_temp_threshold  = doc["high_temp_threshold"];
  if (doc.containsKey("air_quality_threshold")) settings.air_quality_threshold = doc["air_quality_threshold"];

  // সেটিংস মেগাতে পাঠানো
  Serial.printf("SET:%d:%.1f:%.1f:%d\n", 
                settings.dry_threshold, 
                settings.low_temp_threshold, 
                settings.high_temp_threshold, 
                settings.air_quality_threshold);

  server.send(200, "application/json", "{\"success\":true,\"message\":\"Settings synced with Mega\"}");
}

// ==============================================================================
// 5. SETUP & INITIALIZATION
// ==============================================================================

void setup() {
  // মেগার Serial1-এর সাথে যোগাযোগের জন্য ৯৬০০ বাউড রেট
  Serial.begin(9600);
  delay(500);

  Serial.println("\n[ESP8266] Smart Greenhouse Gateway Starting...");

  // ওয়াইফাই কানেকশন
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(500);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Failed to connect! Running offline.");
  }

  // ওয়েব সার্ভার রুট সেটআপ
  server.on("/ping",     HTTP_GET,  handlePing);
  server.on("/status",   HTTP_GET,  handleStatus);
  server.on("/command",  HTTP_POST, handleCommand);
  server.on("/settings", HTTP_POST, handleSettings);
  server.begin();
  Serial.println("[HTTP] WebServer active on port 80");
}

// ==============================================================================
// 6. MAIN LOOP
// ==============================================================================

void loop() {
  server.handleClient();

  // ১. Arduino Mega থেকে সিরিয়ালে JSON ডাটা রিড করা
  if (Serial.available()) {
    String incoming = Serial.readStringUntil('\n');
    incoming.trim();

    // ভ্যালিড JSON চেক
    if (incoming.startsWith("{") && incoming.endsWith("}")) {
      StaticJsonDocument<384> doc;
      DeserializationError error = deserializeJson(doc, incoming);

      if (!error) {
        // মেমরি স্টেট আপডেট
        currentTemp       = doc["temperature"]  | currentTemp;
        currentHumidity   = doc["humidity"]     | currentHumidity;
        currentSoil       = doc["soilMoisture"] | currentSoil;
        currentAirQuality = doc["airQuality"]   | currentAirQuality;
        pumpStatus        = doc["pumpStatus"]   | pumpStatus;
        fanStatus         = doc["fanStatus"]    | fanStatus;
        lightStatus       = doc["lightStatus"]  | lightStatus;

        // সাথে সাথে ব্যাকএন্ডের MySQL-এ পোস্ট করা
        postSensorDataToBackend(incoming);
      }
    }
  }

  // ২. নিয়মিত হার্টবিট পাঠানো (প্রতি ১৫ সেকেন্ডে)
  unsigned long currentMillis = millis();
  if (currentMillis - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
    lastHeartbeatTime = currentMillis;
    postHeartbeat();
  }
}