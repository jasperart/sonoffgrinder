#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <TTBOUNCE.h>

#include <Wire.h>
#include "SSD1306Wire.h"
// #include <brzo_i2c.h>
// #include "SSD1306Brzo.h"

#include "KY040rotary.h"

// Defines
#define VERSION "v1.0"
#define NAME "WemosGrinder"

#define ON LOW    // LOW is LED ON
#define OFF HIGH  // HIGH is LED OFF

// Configuration
const char* cSSID     = "Ponyhof";
const char* cPASSWORD = "daslebenisteinponyhof";

const bool bFlipDisplay = true;

// Buttons
const int GRINDER_PIN = D4; // Relay Pin of Wemos
const int LED_PIN = LED_BUILTIN; // LED Pin of Wemos
const int BUTTON_PIN = D3; // Signal In of grinder button
const int ROTARY_CLK_PIN = D5;
const int ROTARY_DT_PIN = D6;
const int ROTARY_SW_PIN = D7;

// Rotary setting
int rotaryMode = 0; // 0: inactive, 1: change single, 2: change double
const int tRotaryChange = 100;

// Display
const int I2C_DISPLAY_ADDRESS = 0x3c;
const int SDC_PIN = D1; // SCL Pin on Wemos
const int SDA_PIN = D2; // SDA Pin on Wemos
const OLEDDISPLAY_GEOMETRY gGeometry = GEOMETRY_64_48;  // would default to GEOMETRY_128_64

// Init Grind Settings
const unsigned long tMAX = 60000; // Maxmimum Time for grinding
const unsigned long tOVERLAY = 60000; // show additional information for this period
const unsigned long tDEBOUNCE = 50; // Debounce Time for Button
const unsigned long tPRESS = 300; // Time for Button Press Detection

char htmlResponse[3000];

bool bClick = false;
bool bDoubleClick = false;
bool bPress = false;

bool bWifiConnected = true; // initialize true to start Connection
bool bShowOverlay = true; // initialize true

int tSingleShot = 5000;
int tDualShot = 10000;

int tGrindDuration = 0;
int tGrindPeriod = 0;
unsigned long tGrindStart = 0;

os_timer_t timerGRINDER;

ESP8266WebServer server (80);
TTBOUNCE button = TTBOUNCE(BUTTON_PIN);
KY040 Rotary(ROTARY_CLK_PIN, ROTARY_DT_PIN, ROTARY_SW_PIN);  // Clk, DT, SW

// SSD1306Brzo display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN, gGeometry); // ADDRESS, SDA, SCL
SSD1306Wire display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN, gGeometry); // ADDRESS, SDA, SCL


void click() {
  if (digitalRead(GRINDER_PIN) == LOW) {
    digitalWrite(GRINDER_PIN, HIGH);  // turn Relais ON
    os_timer_arm(&timerGRINDER, tSingleShot, false);
    bClick = true;
    tGrindPeriod = tSingleShot;
    tGrindStart = millis();
    //Serial.println("Clicked");
    //Serial.println("Relais " + String(tSingleShot, DEC) + " ms ON");
    digitalWrite(LED_PIN, ON);
  } else {
    digitalWrite(GRINDER_PIN, LOW);
    os_timer_disarm(&timerGRINDER);
    bClick = false;
    bDoubleClick = false;
    //Serial.println("Abort!");
    //Serial.println("Relais OFF");
    digitalWrite(LED_PIN, OFF);
  }
}

void doubleClick() {
  if (digitalRead(GRINDER_PIN) == LOW) {
    // start grinding
    digitalWrite(GRINDER_PIN, HIGH);  // turn Relais ON
    os_timer_arm(&timerGRINDER, tDualShot, false);
    bDoubleClick = true;
    tGrindPeriod = tDualShot;
    tGrindStart = millis();
    //Serial.println("DoubleClicked");
    //Serial.println("Relais " + String(tDualShot, DEC) + " ms ON");
    digitalWrite(LED_PIN, ON);
  } else {
    // stop grinding
    digitalWrite(GRINDER_PIN, LOW);
    os_timer_disarm(&timerGRINDER);
    bClick = false;
    bDoubleClick = false;
    //Serial.println("Abort!");
    //Serial.println("Relais OFF");
    digitalWrite(LED_PIN, OFF);
  }
}

void press() {
  if(digitalRead(GRINDER_PIN) == LOW || (bClick == true || bDoubleClick == true)) {
    digitalWrite(GRINDER_PIN, HIGH);  // turn Relais ON
    bPress = true;
    tGrindPeriod = tMAX;
    if (bClick == false && bDoubleClick == false) {
      os_timer_arm(&timerGRINDER, tMAX, false);
      tGrindStart = millis(); // only initialize if manual Mode
    } else {
      os_timer_arm(&timerGRINDER, tMAX - (millis() - tGrindStart), false);
    }
    //Serial.println("Pressed");
    //Serial.println("Relais ON");
    digitalWrite(LED_PIN, ON);
  } else {
    digitalWrite(GRINDER_PIN, LOW);
    os_timer_disarm(&timerGRINDER);
    bPress = false;
    //Serial.println("Abort!");
    //Serial.println("Relais OFF");
    digitalWrite(LED_PIN, OFF);
  }
}

void timerCallback(void *pArg) {
  // start of timerCallback
  digitalWrite(GRINDER_PIN, LOW);
  //Serial.println("Timer expired");
  //Serial.println("Relais OFF");
  digitalWrite(LED_PIN, OFF);
  os_timer_disarm(&timerGRINDER);
  bClick = false;
  bDoubleClick = false;
  bPress = false;
}

void eeWriteInt(int pos, int val) {
  byte* p = (byte*) &val;
  EEPROM.write(pos, *p);
  EEPROM.write(pos + 1, *(p + 1));
  EEPROM.write(pos + 2, *(p + 2));
  EEPROM.write(pos + 3, *(p + 3));
  EEPROM.commit();
}

int eeGetInt(int pos) {
  int val;
  byte* p = (byte*) &val;
  *p        = EEPROM.read(pos);
  *(p + 1)  = EEPROM.read(pos + 1);
  *(p + 2)  = EEPROM.read(pos + 2);
  *(p + 3)  = EEPROM.read(pos + 3);
  return val;
}

void setSingleDuration(int duration){
  tSingleShot = duration;
  eeWriteInt(0, tSingleShot);
}
void setDoubleDuration(int duration){
  tDualShot = duration;
  eeWriteInt(4, tDualShot);
}

void handleRoot() {
   snprintf(htmlResponse, 3000,
              "<!DOCTYPE html>\
              <html lang=\"en\">\
                <head>\
                  <meta charset=\"utf-8\">\
                  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
                </head>\
                <body>\
                        <h1>Set Time for single Shot and double Shot</h1>\
                        <h3>Single Shot (Click): %d ms </h3>\
                        <h3><input type='text' name='date_ss' id='date_ss' size=2 autofocus>  ms</h3> \
                        <h3>Double Shot (Doubleclick): %d ms</h3>\
                        <h3><input type='text' name='date_ds' id='date_ds' size=2 autofocus>  ms</h3> \
                        <h3>You can press and hold the Button for manual Grinding.<br> If you press and hold after a Click or Doubleclick it will save the Time.</h3>\
                        <div>\
                        <br><button id=\"save_button\">Save</button>\
                        </div>\
                  <script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.3/jquery.min.js\"></script>\
                  <script>\
                    var ds;\
                    var ss;\
                    $('#save_button').click(function(e){\
                      e.preventDefault();\
                      ss = $('#date_ss').val();\
                      ds = $('#date_ds').val();\
                      $.get('/save?ss=' + ss + '&ds=' + ds, function(data){\
                        console.log(data);\
                      });\
                    });\
                  </script>\
                </body>\
              </html>", tSingleShot, tDualShot);
    server.send(200, "text/html", htmlResponse);
}

void handleSave() {
  // saving times via web
  if (server.arg("ss")!= "") {
    setSingleDuration(server.arg("ss").toInt());
    //Serial.println("Singleshot: " + tSingleShot);
  }
  if (server.arg("ds")!= "") {
    setDoubleDuration(server.arg("ds").toInt());
    //Serial.println("Doubleshot: " + tDualShot);
  }
  // send http success
  server.send ( 200, "text/plain", "Time updated. Reload page to see changes.");
}

void handleWifi() {
  // handle WiFi and connect if not connected

  if (WiFi.status() == WL_CONNECTED) {
    // Connected to WiFi
    if (bWifiConnected == false) {
      // Newly connected to WiFi
      //Serial.println("WiFi connected");
      //Serial.println("IP address: ");
      //Serial.println(WiFi.localIP());
      bWifiConnected = true;
      if (!MDNS.begin(NAME)) { // Start the mDNS responder for esp8266.local
        //Serial.println("Error setting up MDNS responder!");
      }
      //Serial.println("mDNS responder started");
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (bWifiConnected == true) {
      // (Re)start WiFi 
      //Serial.println("WiFi not connected");
      //Serial.print("Trying to connect to: ");
      //Serial.println(cSSID);
      WiFi.mode(WIFI_STA); // explicitly set the ESP8266 to be a WiFi-Client
      WiFi.persistent(false); // do not store Settings in EEPROM
      WiFi.hostname(NAME);
      WiFi.begin(cSSID, cPASSWORD);
      bWifiConnected = false;
    }
  }

  yield(); // allow WiFi and TCP/IP Tasks to run
  delay(10); // yield not sufficient
}

void handleDisplay() {
  display.clear();

  if((bClick == true || bDoubleClick == true) && bPress == false){
    // display timer grinding Progress
    bShowOverlay = false;
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    if(bClick == true) {
      display.drawString(0, 0, "SINGLE");
    } else if (bDoubleClick == true) {
      display.drawString(0, 0, "DOUBLE");
    }
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 16, String((millis() - tGrindStart)/1000.0,2) + "/" + String(tGrindPeriod/1000.0,2) + " s");
    display.drawProgressBar(0, 32, 62, 10, (millis() - tGrindStart) / (tGrindPeriod / 100));
  }

  if(bPress == true){
    // display manual grinding and setup Progress
    bShowOverlay = false;
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    if(bClick == true) {
      display.drawString(0, 0, "Save Single");
    } else if (bDoubleClick == true) {
      display.drawString(0, 0, "Save Double");
    } else {
      display.drawString(0, 0, "MANUAL");
    }
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 16, String(millis() - tGrindStart) + " ms");
    display.drawProgressBar(0, 32, 62, 10, (millis() - tGrindStart) / (tGrindPeriod / 100));
  }

  if(bClick == false && bDoubleClick == false && bPress == false){
    // default Screen  
    if(millis() > tOVERLAY) { 
      bShowOverlay = false; // disable Overlay
    } else {
      bShowOverlay = true; // reenable Overlay
    }
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    if( rotaryMode == 1 ){display.setFont(ArialMT_Plain_16);}
    else { display.setFont(ArialMT_Plain_10); }
    display.drawString(64, 16, "Single " + String(tSingleShot/1000.0,1) + " s"); // TODO: HIGHLIGHT IF IN CHANGE MODE BY ROTARY KNOB ( rotaryMode = 1 )
    if( rotaryMode == 2 ){display.setFont(ArialMT_Plain_16);}
    else{ display.setFont(ArialMT_Plain_10); }
    display.drawString(64, 34, "Double " + String(tDualShot/1000.0,1) + " s");   // TODO: HIGHLIGHT IF IN CHANGE MODE BY ROTARY KNOB ( rotaryMode = 2 )
  }
  
  // if(bShowOverlay == true) {
  //   // display additional Information in Overlay
  //   display.setTextAlignment(TEXT_ALIGN_LEFT);
  //   display.setFont(ArialMT_Plain_10);
  //   display.drawString(0, 0, NAME);
  //   display.setTextAlignment(TEXT_ALIGN_RIGHT);
  //   display.setFont(ArialMT_Plain_10);
  //   display.drawString(128, 0, VERSION);

  //   if (bWifiConnected == true) {
  //     // show IP when connected
  //     display.setTextAlignment(TEXT_ALIGN_RIGHT);
  //     display.setFont(ArialMT_Plain_10);
  //     display.drawString(128, 54, WiFi.localIP().toString());
  //   }
  // }

  // if (bWifiConnected == false) {
  //     // show SSID when not connected
  //     display.setTextAlignment(TEXT_ALIGN_RIGHT);
  //     display.setFont(ArialMT_Plain_10);
  //     display.drawString(128, 54, cSSID);
  //   }

  display.display();
}


void OnButtonClicked(void) {
  // Serial.println("Rotary button clicked.");
  rotaryMode +=1;
  if( rotaryMode > 2){
    // Serial.println("Storing times to EEPROM.");
    rotaryMode = 0;
    setSingleDuration(tSingleShot);
    setDoubleDuration(tDualShot);
  }
}
void OnButtonLeft(void) {
  // Serial.println("Rotary knob turned left. Decrease time.");
  // decrease time
  if( rotaryMode == 1 ) { 
    tSingleShot -= tRotaryChange;
  } else if( rotaryMode == 2 ) {
    tDualShot -= tRotaryChange;
  }  
}
void OnButtonRight(void) {
  // Serial.println("Rotary knob turned right. Increase time.");
  // increase time
  if( rotaryMode == 1 ) { 
    tSingleShot += tRotaryChange;
  } else if( rotaryMode == 2 ) {
    tDualShot += tRotaryChange;
  }  
}

void initRotary() {
  // init button
  if ( !Rotary.Begin() ) {
    Serial.println("unable to init rotate button");
    while (1);
  }
  // init button callbacks
  Rotary.OnButtonClicked(OnButtonClicked);
  Rotary.OnButtonLeft(OnButtonLeft);
  Rotary.OnButtonRight(OnButtonRight);
  Serial.println("KY-040 rotary encoder OK");
}

// *******SETUP*******
void setup() {
  //Serial.begin(115200); // Start serial

  pinMode(GRINDER_PIN, OUTPUT);      // define Grinder output Pin
  digitalWrite(GRINDER_PIN, LOW);    // turn Relais OFF
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ON);         // turn LED ON at start

  pinMode(BUTTON_PIN, INPUT_PULLUP);        // Bugfix ttbounce button.enablePullup(); not working

  handleWifi();

  display.init();
  if(bFlipDisplay == true) {display.flipScreenVertically();}
  handleDisplay();

  server.on("/", handleRoot );
  server.on("/save", handleSave);
  server.begin();
  //Serial.println ( "HTTP Server started" );

  os_timer_setfn(&timerGRINDER, timerCallback, NULL);

  button.setActiveLow(); button.enablePullup();   // Button from GPIO directly to GND, ebable internal pullup
  button.setDebounceInterval(tDEBOUNCE);
  button.setPressInterval(tPRESS);
  button.attachClick(click);                      // attach the Click Method to the Click Event
  button.attachDoubleClick(doubleClick);          // attach the double Click Method to the double Click Event
  button.attachPress(press);                      // attach the Press Method to the Press Event

  initRotary();

  EEPROM.begin(8);  // Initialize EEPROM
  tSingleShot = eeGetInt(0);
  tDualShot = eeGetInt(4);

  digitalWrite(LED_PIN, OFF);        // turn LED OFF after Setup
}

// *******LOOP*******
void loop() {

  server.handleClient();

  button.update();

  if(bPress == true) {
    // Press was detected
    if(button.read() == LOW) { // wait for Button release
      tGrindDuration = millis() - tGrindStart;
      os_timer_arm(&timerGRINDER, 0, false); // instantly fire Timer
      bPress = false;
      if (bClick == true) {
        setSingleDuration(tGrindDuration); // safe single Shot Time
        bClick = false;        
        //Serial.println("Single Shot set to " + String(tSingleShot, DEC) + " ms");
      }
      else if (bDoubleClick == true) {
        setDoubleDuration(tGrindDuration); // safe double Shot Time
        bDoubleClick = false;        
        //Serial.println("Double Shot set to " + String(tDualShot, DEC) + " ms");
      }
    }
  }

  Rotary.Process( millis() );
  handleWifi();
  handleDisplay();
}
