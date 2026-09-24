#include <DHT.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ==============================================================================
// ১. পিন ও হার্ডওয়্যার কনফিগারেশন
// ==============================================================================
const int moistureSensorPin = A0;  // সয়েল ময়েশ্চার সেন্সর (Analog A0)
const int mq135Pin          = A1;  // MQ135 গ্যাস সেন্সর (Analog A1)
const int ldrSensorPin      = A2;  // লাইট সেন্সর LDR (Analog A2 - ঐচ্ছিক)

const int pumpRelayPin      = 22;  // ওয়াটার পাম্প রিলে (Digital 22)
const int fanRelayPin       = 23;  // কুলিং/ভেন্টিলেশন ফ্যান রিলে (Digital 23)
const int redLedPin         = 24;  // অ্যালার্ট লাল এলইডি (Digital 24)
const int lightRelayPin     = 25;  // হিটার / গ্রোথ লাইট রিলে (Digital 25)

// DHT কনফিগারেশন (DHT11 বা DHT22)
#define DHTPIN 2                   // DHT ডাটা পিন (Digital 2)
#define DHTTYPE DHT22              // DHT11 ব্যবহার করলে DHT11 লিখুন

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C অ্যাড্রেস 0x27

// রিলের অন/অফ লজিক (Active-LOW রিলের ক্ষেত্রে LOW = ON, HIGH = OFF)
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ==============================================================================
// ২. ডিফল্ট অটোমেশন থ্রেশহোল্ড
// ==============================================================================
int   dryThreshold         = 600;   // মাটির শুষ্কতার সীমা (>600 হলে পাম্প চলবে)
float lowTempThreshold     = 31.0;  // নিম্ন তাপমাত্রা সীমা (<31 হলে লাইট অন)
float highTempThreshold    = 40.0;  // উচ্চ তাপমাত্রা সীমা (>40 হলে ফ্যান অন)
int   airQualityThreshold  = 100;   // ক্ষতিকর গ্যাসের সীমা (>100 হলে ফ্যান/অ্যালার্ট অন)

// ==============================================================================
// ৩. ডিভাইস স্টেট ও মোড (AUTO / MANUAL ড্যাশবোর্ড কন্ট্রোল)
// ==============================================================================
bool pumpManualMode  = false; // false = AUTO, true = MANUAL
bool fanManualMode   = false;
bool lightManualMode = false;

bool pumpState  = false; // true = ON, false = OFF
bool fanState   = false;
bool lightState = false;
bool ledState   = false;

// টাইমার ট্র্যাকিং (নন-ব্লকিং millis)
unsigned long lastSensorSendTime = 0;
const unsigned long SENSOR_SEND_INTERVAL = 3000; // প্রতি ৩ সেকেন্ডে ESP8266-এ ডাটা যাবে

unsigned long lastLcdSwitchTime = 0;
int lcdScreenIndex = 0;

float lastTemperature = 0.0;
float lastHumidity    = 0.0;
int   lastMoisture    = 0;
int   lastAirQuality  = 0;
int   lastLightVal    = 500;
String lastAirStatus  = "Good";

// ==============================================================================
// ৪. ড্যাশবোর্ড থেকে আসা কমান্ড হ্যান্ডলার (Serial1)
// ==============================================================================
void handleIncomingCommand(const String& jsonStr) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, jsonStr);
  if (err) return;

  const char* device = doc["device"];
  const char* action = doc["action"];
  const char* mode   = doc["mode"] | "MANUAL"; // "MANUAL" or "AUTO"

  Serial.print(F("[Dashboard Command] Device: "));
  Serial.print(device);
  Serial.print(F(" | Action: "));
  Serial.print(action);
  Serial.print(F(" | Mode: "));
  Serial.println(mode);

  bool isAuto = (strcmp(mode, "AUTO") == 0);

  // --- ওয়াটার পাম্প কন্ট্রোল ---
  if (strcmp(device, "water_pump") == 0 || strcmp(device, "pump") == 0) {
    pumpManualMode = !isAuto;
    if (!isAuto) {
      pumpState = (strcmp(action, "ON") == 0);
      digitalWrite(pumpRelayPin, pumpState ? RELAY_ON : RELAY_OFF);
    }
  }
  // --- কুলিং / ভেন্টিলেশন ফ্যান কন্ট্রোল ---
  else if (strcmp(device, "cooling_fan") == 0 || strcmp(device, "ventilation_fan") == 0 || strcmp(device, "fan") == 0) {
    fanManualMode = !isAuto;
    if (!isAuto) {
      fanState = (strcmp(action, "ON") == 0);
      digitalWrite(fanRelayPin, fanState ? RELAY_ON : RELAY_OFF);
    }
  }
  // --- লাইট / হিটার কন্ট্রোল ---
  else if (strcmp(device, "shade_motor") == 0 || strcmp(device, "light") == 0 || strcmp(device, "heater") == 0) {
    lightManualMode = !isAuto;
    if (!isAuto) {
      lightState = (strcmp(action, "ON") == 0);
      digitalWrite(lightRelayPin, lightState ? RELAY_ON : RELAY_OFF);
    }
  }
  // --- ড্যাশবোর্ড থেকে সেটিংস আপডেট হলে ---
  else if (strcmp(device, "settings") == 0 || doc.containsKey("type") && strcmp(doc["type"], "settings") == 0) {
    if (doc.containsKey("soil_min"))               dryThreshold        = doc["soil_min"];
    if (doc.containsKey("dry_threshold"))          dryThreshold        = doc["dry_threshold"];
    if (doc.containsKey("temperature_low"))        lowTempThreshold    = doc["temperature_low"];
    if (doc.containsKey("low_temp_threshold"))     lowTempThreshold    = doc["low_temp_threshold"];
    if (doc.containsKey("temperature_high"))       highTempThreshold   = doc["temperature_high"];
    if (doc.containsKey("high_temp_threshold"))     highTempThreshold   = doc["high_temp_threshold"];
    if (doc.containsKey("air_quality"))            airQualityThreshold = doc["air_quality"];
    if (doc.containsKey("air_quality_threshold"))  airQualityThreshold = doc["air_quality_threshold"];
  }
}

void checkIncomingSerial() {
  while (Serial1.available() > 0) {
    String line = Serial1.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      handleIncomingCommand(line);
    }
  }
}

// ==============================================================================
// ৫. সেন্সর রিডিং ও অটোমেশন রুলস (৩১° ও ৪০° এর মাঝে ফ্যান ও লাইট অফ)
// ==============================================================================
void readSensorsAndApplyAutomation() {
  // সয়েল ময়েশ্চার
  lastMoisture = analogRead(moistureSensorPin);

  // গ্যাস সেন্সর (MQ135)
  lastAirQuality = analogRead(mq135Pin);
  bool airBad = (lastAirQuality > airQualityThreshold);
  lastAirStatus = airBad ? "BAD!" : "Good";

  // এলডিআর (লাইট)
  int rawLdr = analogRead(ldrSensorPin);
  lastLightVal = map(rawLdr, 0, 1023, 0, 1000);

  // DHT সেন্সর
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) lastTemperature = t;
  if (!isnan(h)) lastHumidity = h;

  // --- ১. অটো সেচ (পাম্প) ---
  if (!pumpManualMode) {
    if (lastMoisture > dryThreshold) {
      pumpState = true;
      digitalWrite(pumpRelayPin, RELAY_ON);
    } else {
      pumpState = false;
      digitalWrite(pumpRelayPin, RELAY_OFF);
    }
  }

  // --- ২ ও ৩. তাপমাত্রা ও গ্যাস অনুযায়ী ফ্যান এবং লাইট কন্ট্রোল ---
  if (!isnan(lastTemperature)) {
    // ক) তাপমাত্রা ৪০° এর বেশি হলে -> ফ্যান চলবে, লাইট অফ
    if (lastTemperature > highTempThreshold) {
      if (!fanManualMode) {
        fanState = true;
        digitalWrite(fanRelayPin, RELAY_ON); // ফ্যান অন
      }
      if (!lightManualMode) {
        lightState = false;
        digitalWrite(lightRelayPin, RELAY_OFF); // লাইট অফ
      }
    }
    // খ) তাপমাত্রা ৩১° এর নিচে নামলে -> লাইট চলবে, ফ্যান অফ (যদি গ্যাস না থাকে)
    else if (lastTemperature < lowTempThreshold) {
      if (!lightManualMode) {
        lightState = true;
        digitalWrite(lightRelayPin, RELAY_ON); // লাইট অন
      }
      if (!fanManualMode) {
        // গ্যাস খারাপ থাকলে সেফটির জন্য ফ্যান অন হবে, নয়তো অফ
        fanState = airBad;
        digitalWrite(fanRelayPin, airBad ? RELAY_ON : RELAY_OFF);
      }
    }
    // গ) তাপমাত্রা ৩১° এবং ৪০° এর মাঝে থাকলে -> ফ্যান ও লাইট উভয়ই অফ
    else {
      if (!lightManualMode) {
        lightState = false;
        digitalWrite(lightRelayPin, RELAY_OFF); // লাইট অফ
      }
      if (!fanManualMode) {
        // তাপমাত্রা স্বাভাবিক থাকলে ফ্যান অফ (তবে ক্ষতিকর গ্যাস বের করতে ফ্যান চলবে)
        fanState = airBad;
        digitalWrite(fanRelayPin, airBad ? RELAY_ON : RELAY_OFF);
      }
    }
  }

  // --- ৪. অ্যালার্ট এলইডি (গ্যাস থাকলে অথবা তাপমাত্রা ৩১° এর নিচে নামলে) ---
  ledState = airBad || (!isnan(lastTemperature) && lastTemperature < lowTempThreshold);
  digitalWrite(redLedPin, ledState ? HIGH : LOW);
}

// ==============================================================================
// ৬. ESP8266-এ JSON ডাটা ট্রান্সমিশন
// ==============================================================================
void sendTelemetryToESP8266() {
  float soilPercent = map(constrain(lastMoisture, 300, 1023), 1023, 300, 0, 100);

  String jsonPayload = "{";
  jsonPayload += "\"temperature\":" + String(lastTemperature, 1) + ",";
  jsonPayload += "\"humidity\":" + String(lastHumidity, 1) + ",";
  jsonPayload += "\"soil_moisture\":" + String(soilPercent, 1) + ",";
  jsonPayload += "\"soil_raw\":" + String(lastMoisture) + ",";
  jsonPayload += "\"air_quality\":" + String(lastAirQuality) + ",";
  jsonPayload += "\"light\":" + String(lastLightVal) + ",";
  jsonPayload += "\"pump_status\":\"" + String(pumpState ? "ON" : "OFF") + "\",";
  jsonPayload += "\"fan_status\":\"" + String(fanState ? "ON" : "OFF") + "\",";
  jsonPayload += "\"light_status\":\"" + String(lightState ? "ON" : "OFF") + "\"";
  jsonPayload += "}";

  Serial1.println(jsonPayload);

  Serial.print(F("[JSON to ESP8266]: "));
  Serial.println(jsonPayload);
}

// ==============================================================================
// ৭. এলসিডি ডিসপ্লে আপডেট
// ==============================================================================
void updateLcdDisplay() {
  if (millis() - lastLcdSwitchTime > 2500) {
    lastLcdSwitchTime = millis();
    lcdScreenIndex = (lcdScreenIndex + 1) % 2;
    lcd.clear();

    if (lcdScreenIndex == 0) {
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      lcd.print(lastTemperature, 1);
      lcd.print("C");
      if (lastTemperature < lowTempThreshold) lcd.print(" COLD");
      else if (lastTemperature > highTempThreshold) lcd.print(" HOT");
      else lcd.print(" OK");

      lcd.setCursor(0, 1);
      lcd.print("Humid: ");
      lcd.print(lastHumidity, 0);
      lcd.print("% ");
      lcd.print(fanState ? "[FAN:ON]" : "[FAN:OFF]");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Soil: ");
      lcd.print(lastMoisture);
      lcd.print(pumpState ? " [P:ON]" : " [P:OFF]");

      lcd.setCursor(0, 1);
      lcd.print("Air: ");
      lcd.print(lastAirQuality);
      lcd.print(" (");
      lcd.print(lastAirStatus);
      lcd.print(")");
    }
  }
}

// ==============================================================================
// ৮. SETUP & MAIN LOOP
// ==============================================================================
void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Greenhouse Smart");
  lcd.setCursor(0, 1);
  lcd.print("System Loading..");
  delay(1500);

  pinMode(pumpRelayPin, OUTPUT);
  pinMode(fanRelayPin, OUTPUT);
  pinMode(lightRelayPin, OUTPUT);
  pinMode(redLedPin, OUTPUT);

  digitalWrite(pumpRelayPin, RELAY_OFF);
  digitalWrite(fanRelayPin, RELAY_OFF);
  digitalWrite(lightRelayPin, RELAY_OFF);
  digitalWrite(redLedPin, LOW);

  lcd.clear();
  lcd.print("System Ready!");
  delay(1000);
}

void loop() {
  checkIncomingSerial();
  readSensorsAndApplyAutomation();

  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorSendTime >= SENSOR_SEND_INTERVAL) {
    lastSensorSendTime = currentMillis;
    sendTelemetryToESP8266();
  }

  updateLcdDisplay();
}