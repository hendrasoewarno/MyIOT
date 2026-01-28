#include "DHT.h"
#include <IRremoteESP8266.h>
#include <ir_Panasonic.h> 

#include <RTClib.h>

// DHT
#define DHT_PIN D6
#define DHTTYPE DHT22
#define BUTTON_PIN D5
#define LED_BUILT D4

DHT dht(DHT_PIN, DHTTYPE);

// Infrared - Panasonic AC
#define kIrLed D3
IRPanasonicAc ac(kIrLed); 

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

void sendACCommandON(int temp, uint8_t mode, uint8_t fan, uint8_t vSwing, uint8_t hSwing) {
  ac.begin();   // ⭐ reset Gree internal state
  ac.setModel(kPanasonicRkr); 
  
  // 2. Basic Settings
  ac.setTemp(temp); 
  ac.setMode(mode);
  ac.setFan(fan); // e.g., kPanasonicAcFanAuto, kPanasonicAcFanMin, kPanasonicAcFanMax

  // 3. Swing Control (Vertical and Horizontal)
  ac.setSwingVertical(vSwing);   // e.g., kPanasonicAcSwingVAuto, kPanasonicAcSwingVHighest
  ac.setSwingHorizontal(hSwing); // e.g., kPanasonicAcSwingHAuto, kPanasonicAcSwingHMiddle

  // 4. Humidity / Powerful / Quiet Modes
  // Note: Not all Panasonic models support discrete humidity percentages. 
  // Most use "Quiet" or "Powerful" modes to manage comfort.
  ac.setQuiet(false);
  ac.setPowerful(false);

  Serial.println("Sending IR command...");
 // 1. Power ON
  ac.on();
  ac.send();        
  delay(80);
}

void sendACCommandOFF() {
  ac.begin();   // ⭐ reset Gree internal state
  ac.setModel(kPanasonicRkr); 
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

  //LED BUILTIN Control
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

//PUSH BUTTON Control
  if (now - lastButtonMillis >= BUTTON_INTERVAL) {

    // read the state of the switch/button:
    int button_state;    
    button_state = digitalRead(BUTTON_PIN);  

    if(button_state==LOW) {
      lastButtonMillis = now; //chect button next 500ms
      
      powerState=!powerState;
      if (powerState) {
        Serial.println("Switch AC ON");

        sendACCommandON(temperature, kPanasonicAcCool, kPanasonicAcFanAuto, kPanasonicAcSwingVAuto, kPanasonicAcSwingHMiddle);

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
      Fan Speed  kPanasonicAcFanAuto, kPanasonicAcFanMin, kPanasonicAcFanMed, kPanasonicAcFanMax
      Vertical Swing  kPanasonicAcSwingVAuto, kPanasonicAcSwingVHighest, kPanasonicAcSwingVLowest
      Horizontal Swing  kPanasonicAcSwingHAuto, kPanasonicAcSwingHMiddle, kPanasonicAcSwingHFullLeft
      Modes kPanasonicAcCool, kPanasonicAcHeat, kPanasonicAcDry, kPanasonicAcAuto    
      */
  
      //check and set temperatur each 5 minutes
      if (DHT_count > (60*5)/2) {
        Serial.println("Check Temperature");
        DHT_count = 0;
    
        //Ini untuk mencegah suku turun naik terlalu cepat
        //awalnya 23, kemudian turun ke 22, 21
        //jika dibawah 18, maka naik ke 19, sebelum akhirnya ke 20
        
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
            sendACCommandON(temperature, kPanasonicAcCool, kPanasonicAcFanAuto, kPanasonicAcSwingVAuto, kPanasonicAcSwingHMiddle);
          }
          led_builtin(5,200);
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
