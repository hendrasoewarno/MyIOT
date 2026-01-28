#include "DHT.h"
#include <IRremoteESP8266.h>
#include <ir_Gree.h>  // Changed to Gree library

#include <RTClib.h>

// DHT
#define DHT_PIN D6
#define DHTTYPE DHT22
#define BUTTON_PIN D5
#define LED_BUILT D4

DHT dht(DHT_PIN, DHTTYPE);

// Infrared - Gree AC
#define kIrLed D3
IRGreeAC ac(kIrLed);  // Changed to Gree AC object

//RTC
RTC_DS3231 rtc;

// Timing variables
unsigned long lastDHTMillis = 0;
unsigned long lastButtonMillis = 0;
unsigned long lastBuiltInMillis = 0;

const unsigned long DHT_INTERVAL = 2000;
unsigned long DHT_count = (60*5)/2+1; //turn AC ON and set Temperature
const unsigned long BUTTON_INTERVAL = 500;
unsigned long BUILTIN_INTERVAL = 250;

int temperature = 23;
int lastTemperature = 0;

// PowerOn/Off
boolean powerState = false;
boolean builtinState = LOW;
unsigned int builtinCount = 0; //no blink

void led_builtin(int n, int ms){
  for (int i=0; i<n;i++) {
    digitalWrite(LED_BUILTIN, LOW);    // Turn the LED on
    delay(ms);                       // Wait for a second
    digitalWrite(LED_BUILTIN, HIGH); // Turn the LED off
    delay(ms);  
  }  
}

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  led_builtin(1, 200);

  // initialize the pushbutton pin as an pull-up input
  // the pull-up input pin will be HIGH when the switch is open and LOW when the switch is closed.
  pinMode(BUTTON_PIN, INPUT_PULLUP);  

  while (!rtc.begin()) {
    Serial.println("Couldn't find RTC!");
    led_builtin(1,400);
    yield(); // Allow ESP8266 background tasks
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time!");
    // Set to compile time (only works if system has correct time)
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    
    // OR set manually:
    // rtc.adjust(DateTime(2024, 1, 20, 15, 30, 0)); // Year, Month, Day, Hour, Minute, Second
  }
  
  dht.begin(); // Ensure this is called
}

void sendACCommandON(int temp, uint8_t mode, uint8_t fan, bool vSwingAuto, uint8_t vSwingPos) { 
  ac.begin();   // ⭐ reset Gree internal state

  // 2. Basic Settings
  ac.setTemp(temp); 
  ac.setMode(mode);
  ac.setFan(fan); // e.g., kGreeFanAuto, kGreeFanLow, kGreeFanMed, kGreeFanHigh
  
  // 3. Swing Control (Gree uses different swing options)
  // For swing position (0-4 for fixed positions, 5 for auto)
  // Auto swing mode
  // void setSwingVertical(const bool automatic, const uint8_t position);
  ac.setSwingVertical(vSwingAuto, vSwingPos);
  
  // 4. Additional Gree features
  ac.setLight(true);      // LED display on/off
  ac.setXFan(false);      // Fresh air/ionizer
  ac.setSleep(false);     // Sleep mode
  ac.setTurbo(false);     // Turbo mode
  ac.setIFeel(false);     // I-Feel (follow remote temp) mode
  Serial.println("Sending Gree IR command...");
  // 1. Power ON
  ac.on();  
  ac.send();
  delay(80);
}

void sendACCommandOFF() {
  ac.begin();   // ⭐ reset Gree internal state
  Serial.println("Sending Gree OFF command...");
  ac.off();  
  ac.send();
  delay(80);
}

DateTime addHours(DateTime dt, int hours) {
  return DateTime(dt.unixtime() + (hours * 3600));
}

void loop() {
  unsigned long now = millis();
  
  DateTime rtcnow =  addHours(rtc.now(), 0);

  if (now - lastBuiltInMillis >= BUILTIN_INTERVAL) {
    if (builtinCount >0) {
      builtinCount--;
      lastBuiltInMillis = now;      
      if (builtinState==LOW) {
        Serial.println("ON");
        digitalWrite(LED_BUILTIN, LOW);
        builtinState = HIGH;
      }
      else {
        Serial.println("OFF");
        digitalWrite(LED_BUILTIN, HIGH);
        builtinState = LOW;
      }
    }
  }

  if (now - lastButtonMillis >= BUTTON_INTERVAL) {

    // read the state of the switch/button:
    int button_state;    
    button_state = digitalRead(BUTTON_PIN);  

    if(button_state==LOW) {
      lastButtonMillis = now; //chect button next 500ms
      
      powerState=!powerState;
      if (powerState) {
        Serial.println("Switch AC ON");

        sendACCommandON(temperature, 
                   kGreeCool,             // Cooling mode
                   kGreeFanAuto,          // Auto fan speed
                   true,
                   kGreeSwingLastPos);        // Auto vertical swing


        builtinState=LOW;
        builtinCount=2; //ON->OFF
        lastBuiltInMillis = now;
      }
      else {
        Serial.println("Switch AC OFF");

        sendACCommandOFF();
        builtinState=LOW;
        builtinCount=4; //ON->OFF->ON->OFF
        lastBuiltInMillis = now;
      }                   
  
      DHT_count = 0; //reset count to adjust temperature
    }
  }
 
  // ===============================
  // DHT + OLED (EVERY 2 SECONDS)
  // ===============================
  if (now - lastDHTMillis >= DHT_INTERVAL) {
    lastDHTMillis = now;
    
    DHT_count++;

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
    }
    else {

      /*
      GREE AC CONSTANTS:
      
      Fan Speed:
      kGreeFanAuto, kGreeFanLow, kGreeFanMed, kGreeFanHigh
      
      Modes:
      kGreeAuto, kGreeCool, kGreeHeat, kGreeDry, kGreeFan
      
      Vertical Swing:
      kGreeSwingLastPos, kGreeSwingAuto, kGreeSwingUp, kGreeSwingMiddleUp,
      kGreeSwingMiddle, kGreeSwingMiddleDown, kGreeSwingDown
      
      Swing Auto: true/false for auto swing mode
      */
  
      //check and set temperatur each 5 minutes
      if (DHT_count > (60*5)/2) {
        Serial.println("Check Temperature");
        DHT_count = 0;
    
        //Ini untuk mencegah suku turun naik terlalu cepat
        //awalnya 23, kemudian turun ke 22, 21
        //jika dibawah 18, maka naik ke 19, sebelum akhirnya ke 20
        //Pada display Gree adalah +2
        //Misalkan kita set 23, maka Indikator pada AC adalah 25
        
        if (t > 24)
          temperature=23;
        else if (t>23)      
          temperature=22;
        else if (t>22)      
          temperature=21;
        else if (t<18)
          temperature=19;      
        else if (t<20)
          temperature=20;
        else  
          temperature=21;
          
        if (abs(temperature-lastTemperature)>=1) {
          Serial.println("Adjust Temperature");
          // Send Gree AC command
          if (powerState) {
            sendACCommandON(temperature, 
                         kGreeCool,             // Cooling mode
                         kGreeFanAuto,          // Auto fan speed
                         true,
                         kGreeSwingLastPos);        // Auto vertical swing
          }
          //led_builtin(5,200);
          lastTemperature = temperature;
        }
      }
   
      // Format as string
      char dateStr[20];
      sprintf(dateStr, "%04d-%02d-%02d %02d:%02d:%02d", 
            rtcnow.year(), rtcnow.month(), rtcnow.day(), 
            rtcnow.hour(), rtcnow.minute(), rtcnow.second());
      Serial.print(dateStr);        
       // Serial output
      Serial.print(" Hum: "); Serial.print(h); Serial.print("% ");
      Serial.print(" Temp: "); Serial.print(t); Serial.print(" C");
      Serial.print(" IOT: "); Serial.print(temperature); Serial.print(" C");
      Serial.print(" ACT: ");
      Serial.print(temperature); Serial.print(" C");
      if (powerState)
        Serial.print(" AC: ON");
      else    
        Serial.print(" AC: OFF");    
      Serial.print(" COUNT:"); Serial.println(DHT_count);
    }
  }
}
