#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Flame sensor
#define flameDigitalPin D5
#define flameAnalogPin  A0

// DHT
#define DHTPIN D4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Timing variables
unsigned long lastDHTMillis = 0;
unsigned long lastSerialMillis = 0;

const unsigned long DHT_INTERVAL = 2000;
const unsigned long SERIAL_INTERVAL = 500;

void setup() {
  Serial.begin(9600);

  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  pinMode(flameDigitalPin, INPUT);

  Serial.println("System Initialized. Monitoring for fire...");
}

void loop() {
  unsigned long now = millis();

  // ===============================
  // FLAME SENSOR (REAL-TIME)
  // ===============================
  int digitalVal = digitalRead(flameDigitalPin);
  int analogVal  = analogRead(flameAnalogPin);

  // Serial flame logging (non-blocking)
  if (now - lastSerialMillis >= SERIAL_INTERVAL) {
    lastSerialMillis = now;

    if (digitalVal == LOW) {
      Serial.print("FIRE DETECTED! Intensity: ");
      Serial.println(1024 - analogVal);
    } else {
      Serial.print("S");
      Serial.println(analogVal);
    }
  }

  // ===============================
  // DHT + OLED (EVERY 2 SECONDS)
  // ===============================
  if (now - lastDHTMillis >= DHT_INTERVAL) {
    lastDHTMillis = now;

    float h = dht.readHumidity()+13;
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    // Serial output
    Serial.print("Hum: "); Serial.print(h); Serial.print("% ");
    Serial.print("Temp: "); Serial.print(t); Serial.println(" C");

    // OLED
    display.clearDisplay();

    // FIRE indicator
    if (digitalVal == LOW) {      
      display.setCursor(0, 20);
      display.setTextSize(1);
      display.println("FIRE!FIRE!FIRE!");
    } else {

      // Temperature
      display.setCursor(0, 20);
      display.print("Temp: ");
      display.setTextSize(2);
      display.print(t, 1);
      display.setTextSize(1);
      display.println(" C");

      // Humidity
      display.setCursor(0, 42);
      display.print("Humi: ");
      display.setTextSize(2);
      display.print(h, 0);
      display.setTextSize(1);
      display.println(" %");
      
    }
    display.display();
  }
}
