
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <TM1637TinyDisplay.h>
#include <WebConfig.h>
#include "WaterMeter.h"


bool buttonPressed = false;
unsigned long buttonPressedTime = 0;

TM1637TinyDisplay display(CLK, DIO);
int bright = 1;

ESP8266WebServer server;
WebConfig conf;



float meterValue = 0.0f;
float meterValueLastSaved = 0.0f;
unsigned short pulseCount = 0;
float divider = 1;

bool otaUpdate = false;

void ICACHE_RAM_ATTR buttonPressedHandler(){
  if(buttonPressedTime + 200 > millis()){
    return;
  }
  
  buttonPressedTime = millis();
  buttonPressed = true;;
}

void ICACHE_RAM_ATTR counterPulseHandler(){
  pulseCount++;
}

void setup() {
  display.begin();
  display.showString("HELLO");
  
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(COUNTER, INPUT_PULLUP);
  
  conf.setDescription(params);
  conf.readConfig();


  if(conf.getString("wifiPw") != ""){
      setWifiPw(conf.getString("wifiPw"));
      conf.setValue("wifiPw", "");
      conf.writeConfig();
  }

  if(digitalRead(BUTTON) == LOW){
    buttonPressedTime = millis();
  }
  while(digitalRead(BUTTON) == LOW){
    if(buttonPressedTime + 1000 < millis()){
      display.showString("OTA");
      otaUpdate = true;
      break;
    }
    delay(10);
  }
  while(digitalRead(BUTTON) == LOW){
    if(buttonPressedTime + 5000 < millis()){
      factoryReset();
      break;
    }
    delay(10);
  }
  
  
  display.setBrightness(conf.getInt("brightness"));
  
  pulseCount = conf.getFloat("pulseCount");
  divider = conf.getFloat("divider");

  setMeterValue();

  if(!otaUpdate){
    displayMeterValue();
  }

  meterValueLastSaved = meterValue;

  
  initWiFi();

  if(otaUpdate){
    display.showString("OTA");
    
    ArduinoOTA.onEnd([]() {
      display.showString("OTA done");
    });
    ArduinoOTA.onError([](ota_error_t error) {
      if (error == OTA_AUTH_ERROR) display.showString("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) display.showString("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) display.showString("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) display.showString("Receive Failed");
      else if (error == OTA_END_ERROR) display.showString("End Failed");
    });
    ArduinoOTA.begin();
    return;
  }

  char dns[30];
  sprintf(dns,"%s.local",conf.getApName());
  MDNS.begin(dns);

  server.on("/",handleIndex);
  server.on("/config",handleConfig);
  server.begin(80);
  
  displayMeterValue();

  
  attachInterrupt(digitalPinToInterrupt(BUTTON), buttonPressedHandler, FALLING);
  attachInterrupt(digitalPinToInterrupt(COUNTER), counterPulseHandler, FALLING);
}

boolean initWiFi() {
  
    boolean connected = false;
    WiFi.mode(WIFI_STA);
    if (conf.values[0] != "") {
      WiFi.begin(conf.values[0].c_str(),getWifiPw().c_str());
      uint8_t cnt = 0;
      while ((WiFi.status() != WL_CONNECTED) && (cnt<20)){
        delay(500);
        cnt++;
      }
      if (WiFi.status() == WL_CONNECTED) {
        String ip = "Connected IP " + WiFi.localIP().toString();
        display.showString(ip.c_str());
        connected = true;
      }
    }
    if (!connected) {
          WiFi.mode(WIFI_AP);
          WiFi.softAP(conf.getApName(),"",1);  
          display.showString("AP mode IP 192.168. 4.1");
    }
    return connected;
}

void handleConfig() {
  conf.handleFormRequest(&server);
  if (server.hasArg("SAVE")) {
    if(conf.getString("wifiPw") != ""){
      setWifiPw(conf.getString("wifiPw"));
      conf.setValue("wifiPw", "");
      conf.writeConfig();
    }

    pulseCount = conf.getFloat("pulseCount");
    divider = conf.getFloat("divider");
    
    setMeterValue();
    displayMeterValue();

    display.setBrightness(conf.getInt("brightness"));
  }
}

void handleIndex(){
  String apName = conf.getApName();
  float tankVolume = conf.getFloat("tankVolume");
  float tankPercent = (tankVolume - meterValue) / tankVolume * 100;
  if(meterValue >= tankVolume){
    tankPercent = 0;
  }
  
  char _buf[1200];
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  sprintf(_buf,HTML_INDEX,/*_apName.c_str()*/apName.c_str(), apName.c_str(), meterValue, tankPercent, tankPercent);
  server.send(200, "text/html", _buf);
}

void setWifiPw(String wifiPw){
  File file = SPIFFS.open(WIFIPWFILE, "w");
  file.print(wifiPw);
  file.close();
}

String getWifiPw(){
  String wifiPw = "";
  
  File file = SPIFFS.open(WIFIPWFILE, "r");

  if(!file){
    return "";
  }
  
  while (file.available()) {
    wifiPw += (char) file.read();
  }
      
  file.close();

  return wifiPw;
}

void setMeterValue(){
  meterValue = pulseCount / divider;
}

void displayMeterValue(){
  if(meterValue < 1){
    display.showNumberDec(meterValue*10, 0b10000000, true, 2, 2); 
  }else{
    display.showNumberDec(meterValue*10, 0b00100000, false); 
  }
}

void resetCounter(){
  display.showString("reset");
  display.clear();
  conf.setValue("pulseCount", "0");
  conf.writeConfig();
  
  pulseCount = 0; 
  setMeterValue();
  displayMeterValue();
  
  meterValueLastSaved = meterValue;
}

void factoryReset(){
  display.showString("factory reset");
  SPIFFS.format();
  ESP.restart();
}

void savePulseCount(){
  conf.setValue("pulseCount", (String) pulseCount);
  conf.writeConfig();
  meterValueLastSaved = meterValue;
}

void loop() {
  if(otaUpdate){
    ArduinoOTA.handle();
    return;
  }
  
  MDNS.update();
  
  server.handleClient();

    setMeterValue();
    displayMeterValue();

    if(buttonPressed){
      while(digitalRead(BUTTON) == LOW){
        if(buttonPressedTime + 3000 < millis()){
          buttonPressed = false;
          resetCounter();
          break;
        }
        delay(10);
      }
      buttonPressed = false;
    }

    if(meterValue > meterValueLastSaved + 1){
      savePulseCount();
    }
}
