#include <Arduino.h>
#include <esp_task_wdt.h>
#include <ACAN_ESP32.h>
#include <TaskScheduler.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "ArduinoJson.h"

#define WDT_TIMEOUT 3
#define CAN_SPEED 500000

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
CANMessage readMesg;
Scheduler ts;

int32_t milliamps = 0;
unsigned int batteryVoltageMin = 0;
unsigned int batteryVoltage = 0;
unsigned int batTemp = 0;
unsigned int inverterTemp = 0;
unsigned int motorTemp = 0;
unsigned int soc = 0;
int32_t watts = 0;
int32_t wattHours = 0;
unsigned int speed = 0;
float tripAh = 0;
int tripKm = 0;
float avgAhPerKM = 0;
unsigned long odoKM = 0;
unsigned long lastOdo = 0;
long lastAmpSeconds = 0;



void ms500Callback();
void staCheckCallback();
Task ms500Task(500, TASK_FOREVER, &ms500Callback);
Task staCheckTask(15000, TASK_FOREVER, &staCheckCallback);

void staCheckCallback() {
  staCheckTask.disable();
  if(!(uint32_t)WiFi.localIP()){
    WiFi.mode(WIFI_AP); //disable station mode
  }
}
void ms500Callback() {
    DynamicJsonDocument json(10240);

    String output;
    JsonObject socNode = json.createNestedObject();
    socNode["key"] = "soc";
    socNode["value"] = soc;
    
    JsonObject batTempNode = json.createNestedObject();
    batTempNode["key"] = "battery-temp";
    batTempNode["value"] = batTemp;

    JsonObject inverterTempNode = json.createNestedObject();
    inverterTempNode["key"] = "inverter-temp";
    inverterTempNode["value"] = inverterTemp;

    JsonObject motorTempNode = json.createNestedObject();
    motorTempNode["key"] = "motor-temp";
    motorTempNode["value"] = motorTemp;

    JsonObject batVoltNode = json.createNestedObject();
    batVoltNode["key"] = "battery-voltage";
    batVoltNode["value"] = batteryVoltage;

    JsonObject wattsNode = json.createNestedObject();
    wattsNode["key"] = "watts";
    wattsNode["value"] = watts;

    JsonObject wattHoursNode = json.createNestedObject();
    wattHoursNode["key"] = "watt-hours";
    wattHoursNode["value"] = wattHours;

    JsonObject milliampsNode = json.createNestedObject();
    milliampsNode["key"] = "milliamps";
    milliampsNode["value"] = milliamps;

    JsonObject tripAhNode = json.createNestedObject();
    tripAhNode["key"] = "trip-ah";
    tripAhNode["value"] = tripAh/3600;

    JsonObject tripKmNode = json.createNestedObject();
    tripKmNode["key"] = "trip-km";
    tripKmNode["value"] = tripKm;
    
    JsonObject speedNode = json.createNestedObject();
    speedNode["key"] = "speed";
    speedNode["value"] = speed;

    JsonObject avgAhPerKMNode = json.createNestedObject();
    avgAhPerKMNode["key"] = "average-ah-km";
    avgAhPerKMNode["value"] = avgAhPerKM;

    serializeJson(json, output);
    ws.textAll(output);
}



void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
 
  if(type == WS_EVT_CONNECT){
    Serial.println("Websocket client connection received"); 
  } else if(type == WS_EVT_DISCONNECT){
    Serial.println("Client disconnected");
 
  }
}

void setup() {
    Serial.begin(115200);

    esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
    esp_task_wdt_add(NULL); //add current thread to WDT watch

      // Initialize SPIFFS
    if(!SPIFFS.begin(true)){
      Serial.println("An Error has occurred while mounting SPIFFS");
      return;
    }

    //AP and Station Mode
    WiFi.mode(WIFI_AP_STA);
    WiFi.hostname("ESP32-DASH");
    // Connect to Wi-Fi
    WiFi.begin();

    ws.onEvent(onWsEvent);
    
    server.addHandler(&ws);

    server.on("/wifi", [&] (AsyncWebServerRequest *request) {
        bool updated = true;
        if(request->hasParam("apSSID", true) && request->hasParam("apPW", true)) 
        {
            WiFi.softAP(request->arg("apSSID").c_str(), request->arg("apPW").c_str());
        }
        else if(request->hasParam("staSSID", true) && request->hasParam("staPW", true)) 
        {
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(request->arg("staSSID").c_str(), request->arg("staPW").c_str());
        }
        else
        {
            File file = SPIFFS.open("/wifi.html", "r");
            String html = file.readString();
            file.close();
            html.replace("%staSSID%", WiFi.SSID());
            html.replace("%apSSID%", WiFi.softAPSSID());
            html.replace("%staIP%", WiFi.localIP().toString());
            request->send(200, "text/html", html);
            updated = false;
        }

        if (updated)
        {
            request->send(SPIFFS, "/wifi-updated.html");
        } 
    });

    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    server.begin();

    ACAN_ESP32_Settings settings(CAN_SPEED);
    settings.mRxPin = GPIO_NUM_16;
    settings.mTxPin = GPIO_NUM_17;

    ACAN_ESP32::can.begin(settings);

    ts.addTask(ms500Task);
    ms500Task.enable();

    ts.addTask(staCheckTask);
    staCheckTask.enable();

    Serial.println("Ready");

}

void loop() {
  // put your main code here, to run repeatedly:
  esp_task_wdt_reset();
  ts.execute();

  if (ACAN_ESP32::can.receive(readMesg)) {

    //move to configuration file
    switch(readMesg.id) {
      case 0x3FF: { //OI motor and inverter temps
        inverterTemp = readMesg.data[0];
        motorTemp = readMesg.data[1];
      }
      case 0x320://VW Speed from Combination Display
          speed = (readMesg.data[4] << 7) + (readMesg.data[3] >>1);
          break;
      case 0x520://VW Mileage (in KM * 10)
        odoKM = ((readMesg.data[7] & 0x0F) << 16) + (readMesg.data[6] << 8) + readMesg.data[5];
        //if odo increased add to trip
        if (odoKM > lastOdo && lastOdo != 0) {
          int difference = odoKM - lastOdo;
          tripKm += difference;
          avgAhPerKM = tripKm/tripAh;
        }
        lastOdo = odoKM;
        break;
      case 0x355: {//SimpBMS
          soc = (readMesg.data[1] << 8) + readMesg.data[0];
        break;
      }
      case 0x356: {//SimpBMS - battery temperature and voltage
          batteryVoltage = ((readMesg.data[1] << 8) + readMesg.data[0] / 100);
          batTemp = ((readMesg.data[5] << 8) + readMesg.data[4]);
        break;
      }
      case 0x373: {//SimpBMS - battery voltage minimum
          batteryVoltageMin = ((readMesg.data[1] << 8) + readMesg.data[0] / 100);
          break;
      }
      case 0x521: { //ISA current Shunt Current miliamps
        milliamps = readMesg.data[2] + (readMesg.data[3] << 8) + (readMesg.data[4] << 16) + (readMesg.data[5] << 24);
        break;
      }
      case 0x526: { //ISA current Shunt Current watts
        watts = readMesg.data[2] + (readMesg.data[3] << 8) + (readMesg.data[4] << 16) + (readMesg.data[5] << 24);
        break;
      }
      case 0x527: {
        long ampseconds = readMesg.data[2] + (readMesg.data[3] << 8) + (readMesg.data[4] << 16) + (readMesg.data[5] << 24);
        if (ampseconds > lastAmpSeconds && lastAmpSeconds != 0) {
          long difference = ampseconds - lastAmpSeconds;
          tripAh += difference;
        }
        lastAmpSeconds = ampseconds;
      }
      break;
      case 0x528: { //ISA current Shunt Current watts
        wattHours = readMesg.data[2] + (readMesg.data[3] << 8) + (readMesg.data[4] << 16) + (readMesg.data[5] << 24);
        break;
      }
    }
 }

}