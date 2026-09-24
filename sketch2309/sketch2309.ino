#include <DHT.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// আর্ডুইনো পিন সেটআপ
const int moistureSensorPin = A0;  // সয়েল ময়েশ্চার সেন্সর (Analog A0)
const int mq135Pin = A1;           // MQ135 গ্যাস সেন্সর (Analog A1)
const int pumpRelayPin = 22;       // পাম্প রিলে (Digital 22)
const int fanRelayPin = 23;        // ফ্যান রিলে (Digital 23)
const int redLedPin = 24;          // লাল অ্যালার্ট এলইডি (Digital 24)
const int lightRelayPin = 25;      // গ্রীনহাউস হিটার লাইট রিলে (Digital 25)

// DHT22 তাপমাত্রা পিন সেটআপ
#define DHTPIN 2                   // DHT22 ডাটা পিন (Digital 2)
#define DHTTYPE DHT22              // সেন্সর টাইপ DHT22

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// ==========================================
// থ্রেশহোল্ড সেটআপ
// ==========================================
const int dryThreshold = 600;         // মাটির শুষ্কতার সীমা
const float lowTempThreshold = 31.0;  // নিম্ন তাপমাত্রা সীমা (এর নিচে নামলে লাইট অন)
const float highTempThreshold = 40.0; // উচ্চ তাপমাত্রা সীমা (এর উপরে উঠলে ফ্যান অন)
const int airQualityThreshold = 100;  // ক্ষতিকর গ্যাসের সীমা

void setup() {
  Serial.begin(9600);
  dht.begin();
  
  // LCD ডিসপ্লে চালু করা
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Greenhouse Smart");
  lcd.setCursor(0, 1);
  lcd.print("System Loading..");
  delay(2000);

  // পিন মোড সেট করা
  pinMode(pumpRelayPin, OUTPUT);
  pinMode(fanRelayPin, OUTPUT);
  pinMode(lightRelayPin, OUTPUT);
  pinMode(redLedPin, OUTPUT);

  // শুরুতে সব বন্ধ রাখা (Active-LOW রিলের জন্য HIGH মানে বন্ধ)
  digitalWrite(pumpRelayPin, HIGH);
  digitalWrite(fanRelayPin, HIGH);
  digitalWrite(lightRelayPin, HIGH);
  digitalWrite(redLedPin, LOW);
}

void loop() {
  bool shouldFanBeOn = false;
  bool shouldLightBeOn = false;
  bool shouldLedBeOn = false;
  String airStatus = "Good";

  // ==========================================
  // ১. ১ম ফিচার: অটোমেটিক সেচ (Soil Moisture)
  // ==========================================
  int moistureLevel = analogRead(moistureSensorPin);

  Serial.print("মাটির আর্দ্রতা: ");
  Serial.println(moistureLevel);

  if (moistureLevel > dryThreshold) {
    digitalWrite(pumpRelayPin, LOW);   // মাটি শুষ্ক -> পাম্প চালু
    Serial.println("-> [পাম্প চালু]");
  } else {
    digitalWrite(pumpRelayPin, HIGH);  // পর্যাপ্ত পানি আছে -> পাম্প বন্ধ
    Serial.println("-> [পাম্প বন্ধ]");
  }

  // ==========================================
  // ২. ২য় ফিচার: তাপমাত্রা নিয়ন্ত্রণ লজিক
  // ==========================================
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("ত্রুটি: DHT22 সেন্সর থেকে ডাটা পাওয়া যাচ্ছে না!");
  } else {
    Serial.print("বর্তমান তাপমাত্রা: ");
    Serial.print(temperature);
    Serial.print(" °C | আর্দ্রতা: ");
    Serial.print(humidity);
    Serial.println("%");

    // ক) তাপমাত্রা ৩০° এর বেশি হলে -> ফ্যান চলবে, লাইট বন্ধ
    if (temperature > highTempThreshold) {
      shouldFanBeOn = true; 
      shouldLightBeOn = false;
      Serial.println("-> [অবস্থা: তাপমাত্রা ৩০° এর বেশি! ফ্যান চালু, লাইট বন্ধ]");
    }
    // খ) তাপমাত্রা lowTempThreshold এর কম হলে -> লাইট চলবে, ফ্যান বন্ধ
    else if (temperature < lowTempThreshold) {
      shouldLightBeOn = true;  
      shouldFanBeOn = false;
      shouldLedBeOn = true;    
      Serial.println("-> [অবস্থা: তাপমাত্রা কম! হিটার লাইট চালু]");
    }
    // গ) lowTempThreshold এবং ৩০.০° এর মাঝে থাকলে -> লাইট এবং ফ্যান উভয়ই বন্ধ
    else {
      shouldLightBeOn = false;
      shouldFanBeOn = false;
      Serial.println("-> [অবস্থা: তাপমাত্রা স্বাভাবিক! লাইট ও ফ্যান দুটিই বন্ধ]");
    }
  }

  // ==========================================
  // ৩. ৩য় ফিচার: ক্ষতিকর গ্যাস পরীক্ষা (MQ135)
  // ==========================================
  int airQualityRaw = analogRead(mq135Pin);

  Serial.print("বাতাসের মান: ");
  Serial.println(airQualityRaw);

  if (airQualityRaw > airQualityThreshold) {
    shouldFanBeOn = true;  // গ্যাস বের করতে ফ্যান চলবে
    shouldLedBeOn = true;  // সতর্কবার্তা লাল LED জ্বলবে
    airStatus = "BAD!";
    Serial.println("-> [সতর্কতা: গ্যাস শনাক্ত হয়েছে! ফ্যান চালু]");
  }

  // ==========================================
  // ৪. রিলে ও এলইডি এক্সিকিউশন
  // ==========================================
  // ফ্যান কন্ট্রোল
  if (shouldFanBeOn) {
    digitalWrite(fanRelayPin, LOW);   // ফ্যান অন
  } else {
    digitalWrite(fanRelayPin, HIGH);  // ফ্যান অফ
  }

  // হিটার লাইট কন্ট্রোল (সীমার মাঝে থাকলে অফ হবে)
  if (shouldLightBeOn) {
    digitalWrite(lightRelayPin, LOW);  // লাইট অন
  } else {
    digitalWrite(lightRelayPin, HIGH); // লাইট অফ
  }

  // অ্যালার্ট লাল এলইডি কন্ট্রোল
  if (shouldLedBeOn) {
    digitalWrite(redLedPin, HIGH);    // লাল এলইডি অন
  } else {
    digitalWrite(redLedPin, LOW);     // লাল এলইডি অফ
  }

  // ==========================================
  // ৫. LCD ডিসপ্লে
  // ==========================================
  // ১ম স্ক্রিন: তাপমাত্রা ও আর্দ্রতা
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  if (isnan(temperature)) {
    lcd.print("Error");
  } else {
    lcd.print(temperature, 1);
    lcd.print("C");
    if (temperature < lowTempThreshold) lcd.print(" (COLD)");
    else if (temperature > highTempThreshold) lcd.print(" (HOT)");
    else lcd.print(" (NORMAL)");
  }
  
  lcd.setCursor(0, 1);
  lcd.print("Humid: ");
  if (isnan(humidity)) lcd.print("Error");
  else { lcd.print(humidity, 0); lcd.print("%"); }
  delay(2000);

  // ২য় স্ক্রিন: মাটির আর্দ্রতা ও বাতাসের অবস্থা
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Soil: ");
  lcd.print(moistureLevel);
  if (moistureLevel > dryThreshold) lcd.print(" (Dry)");
  else lcd.print(" (Wet)");

  lcd.setCursor(0, 1);
  lcd.print("Air: ");
  lcd.print(airQualityRaw);
  lcd.print(" (" + airStatus + ")");
  
  Serial.println("----------------------------------------");
  delay(2000);
}