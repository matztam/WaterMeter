#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <TM1637TinyDisplay.h>
#include <WebConfig.h>
#include "WaterMeter.h"

const unsigned long WIFI_RETRY_INTERVAL = 180000;

bool buttonPressed = false;
unsigned long buttonPressedTime = 0;
// ISR-shared: pulse counter and last pulse timestamp (us)
volatile unsigned long pulseCount = 0;
volatile uint32_t lastPulseUs = 0;

// Minimum allowed interval between pulses in microseconds
const uint32_t MIN_PULSE_US = 6000;

unsigned long wifiConnectFailedTime = 0;


TM1637TinyDisplay display(CLK, DIO);
int bright = 1;

ESP8266WebServer server;
WebConfig conf;


float meterValue = 0.0f;
float meterValueLastSaved = 0.0f;
float divider = 1;

bool otaUpdate = false;
uint16_t otaPin = 0;

void ICACHE_RAM_ATTR buttonPressedHandler() {
  if (buttonPressedTime + 200 > millis()) {
    return;
  }

  buttonPressedTime = millis();
  buttonPressed = true;
  ;
}

void ICACHE_RAM_ATTR counterPulseHandler() {
  uint32_t now = micros();
  if ((uint32_t)(now - lastPulseUs) >= MIN_PULSE_US) {
    pulseCount++;
    lastPulseUs = now;
  }
}

void setup() {
  display.begin();
  display.showString("HELLO");

  pinMode(BUTTON, INPUT_PULLUP);

  conf.setDescription(params);
  conf.readConfig();


  if (conf.getString("wifiPw") != "") {
    setWifiPw(conf.getString("wifiPw"));
    conf.setValue("wifiPw", "");
    conf.writeConfig();
  }

  if (digitalRead(BUTTON) == LOW) {
    buttonPressedTime = millis();
  }
  while (digitalRead(BUTTON) == LOW) {
    if (buttonPressedTime + 1000 < millis()) {
      display.showString("OTA");
      otaUpdate = true;
      break;
    }
    delay(10);
  }
  while (digitalRead(BUTTON) == LOW) {
    if (buttonPressedTime + 5000 < millis()) {
      factoryReset();
      break;
    }
    delay(10);
  }


  display.setBrightness(conf.getInt("brightness"));

  pulseCount = conf.getFloat("pulseCount");
  divider = conf.getFloat("divider");

  setMeterValue();

  if (!otaUpdate) {
    displayMeterValue();
  }

  meterValueLastSaved = meterValue;


  initWiFi();

  if (otaUpdate) {
    ota();
    return;
  }

  char dns[30];
  sprintf(dns, "%s.local", conf.getApName());
  MDNS.begin(dns);

  server.on("/", handleIndex);
  server.on("/config", handleConfig);
  server.on("/ota", handleOta);
  server.begin(80);

  displayMeterValue();

  pinMode(COUNTER, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON), buttonPressedHandler, FALLING);
  attachInterrupt(digitalPinToInterrupt(COUNTER), counterPulseHandler, FALLING);
}

boolean initWiFi() {

  boolean connected = false;
  WiFi.mode(WIFI_STA);
  if (conf.values[0] != "") {
    WiFi.begin(conf.values[0].c_str(), getWifiPw().c_str());
    uint8_t cnt = 0;
    while ((WiFi.status() != WL_CONNECTED) && (cnt < 20)) {
      delay(500);
      cnt++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      String ip = "Connected IP " + WiFi.localIP().toString();
      display.showString(ip.c_str());

      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);

      connected = true;
    }
  }
  if (!connected) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(conf.getApName(), "", 1);
    display.showString("AP mode IP 192.168. 4.1");
    wifiConnectFailedTime = millis();
  }

  return connected;
}

void showOtaPin() {
  display.showNumberDec(otaPin, 0, true, 4, 0);
}

void ota() {
  // Zufaellige vierstellige PIN als OTA-Passwort, nur auf dem Display sichtbar
  otaPin = ESP.random() % 10000;
  char pin[5];
  snprintf(pin, sizeof(pin), "%04u", (unsigned int)otaPin);
  ArduinoOTA.setPassword(pin);

  display.showString("OTA PIN");
  showOtaPin();

  ArduinoOTA.onEnd([]() {
    display.showString("OTA done");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) display.showString("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) display.showString("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) display.showString("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) display.showString("Receive Failed");
    else if (error == OTA_END_ERROR) display.showString("End Failed");
    showOtaPin();
  });
  ArduinoOTA.begin();
  return;
}

void handleConfig() {
  conf.handleFormRequest(&server);
  if (server.hasArg("SAVE")) {
    if (conf.getString("wifiPw") != "") {
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

void handleOta() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OTA-Modus aktiv. Die PIN wird auf dem Display angezeigt.");
  ota();
  otaUpdate = true;
}

void handleIndex() {
  String apName = conf.getApName();
  float tankVolume = conf.getFloat("tankVolume");
  float tankPercent = (tankVolume - meterValue) / tankVolume * 100;
  if (meterValue >= tankVolume) tankPercent = 0;

  char buf[1600];
  int len = snprintf(
    buf, sizeof(buf),
    HTML_INDEX,
    apName.c_str(),
    apName.c_str(),
    meterValue,
    tankPercent,
    tankPercent,
    pulseCount);

  server.sendHeader("Connection", "close");
  server.send(200, "text/html", buf);
}


void setWifiPw(String wifiPw) {
  File file = SPIFFS.open(WIFIPWFILE, "w");
  file.print(wifiPw);
  file.close();
}

String getWifiPw() {
  String wifiPw = "";

  File file = SPIFFS.open(WIFIPWFILE, "r");

  if (!file) {
    return "";
  }

  while (file.available()) {
    wifiPw += (char)file.read();
  }

  file.close();

  return wifiPw;
}

void setMeterValue() {
  meterValue = pulseCount / divider;
}

void displayMeterValue() {
  if (meterValue < 1) {
    display.showNumberDec(meterValue * 10, 0b10000000, true, 2, 2);
  } else {
    display.showNumberDec(meterValue * 10, 0b00100000, false);
  }
}

void resetCounter() {
  display.showString("reset");
  display.clear();
  conf.setValue("pulseCount", String(0.0f));
  conf.writeConfig();

  pulseCount = 0;
  setMeterValue();
  displayMeterValue();

  savePulseCount();

  meterValueLastSaved = meterValue;
}

void factoryReset() {
  display.showString("factory reset");
  SPIFFS.format();
  ESP.restart();
}

void savePulseCount() {
  conf.setValue("pulseCount", String(pulseCount));
  conf.writeConfig();
  meterValueLastSaved = meterValue;
}

void loop() {
  if (otaUpdate) {
    ArduinoOTA.handle();

    if (digitalRead(BUTTON) == LOW) {
      delay(1000);
      if (digitalRead(BUTTON) == LOW) {
        ESP.restart();
      }
    }

    return;
  }

  MDNS.update();

  server.handleClient();

  // OTA wurde ueber das Webinterface gestartet: PIN nicht ueberschreiben
  if (otaUpdate) {
    return;
  }

  setMeterValue();
  displayMeterValue();

  if (buttonPressed) {
    bool counterReset = false;
    while (digitalRead(BUTTON) == LOW) {
      delay(100);
      if (buttonPressedTime + 3000 < millis()) {
        buttonPressed = false;
        resetCounter();
        counterReset = true;
        break;
      }
    }

    if (!counterReset) {
      // Kurzer Druck: Hinweis als Laufschrift anzeigen
      display.showString("Press and hold to reset");
      displayMeterValue();
    }

    // Prellen beim Loslassen waehrend der Laufschrift ignorieren
    buttonPressed = false;
  }

  if (meterValue > meterValueLastSaved + 1) {
    savePulseCount();
  }

  // 1) Erkennen, wenn die Verbindung unterwegs verloren geht
  if (WiFi.status() != WL_CONNECTED && wifiConnectFailedTime == 0) {
    // WLAN ist weg – starte Retry-Timer
    wifiConnectFailedTime = millis();
  }

  // Ohne konfigurierte SSID dauerhaft im AP-Modus bleiben, keine Retries
  if (conf.getString("ssid") == "") {
    wifiConnectFailedTime = 0;
  }

  // 2) Alle WIFI_RETRY_INTERVAL ms erneut verbinden, wenn wir nicht verbunden sind
  if (wifiConnectFailedTime != 0 && millis() - wifiConnectFailedTime >= WIFI_RETRY_INTERVAL) {
    display.showString("RetryWiFi");
    bool connected = initWiFi();  // versucht erneut
    if (connected) {
      // Verbindung steht wieder: Timer aus
      wifiConnectFailedTime = 0;

      if ((WiFi.getMode() & WIFI_AP) != 0) {
        // Soft-AP ist aktiv, also schalte ihn ab
        WiFi.softAPdisconnect(true);
        // und wechsle in reinen STA-Modus
        WiFi.mode(WIFI_STA);
      }
    } else {
      // immer noch weg: Timer neu starten für den nächsten Versuch
      wifiConnectFailedTime = millis();
    }
  }

  yield();
}
