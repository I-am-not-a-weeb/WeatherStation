#include <cstdlib>
#include <vector>
#include <fstream>

#include <NTPClient.h>
#include <LittleFS.h>
#include <FSTools.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
//#include <ESP8266WebServer.h>
#include <WiFiUdp.h>

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <ESP8266TimerInterrupt.h>
#include <ESP8266_ISR_Timer.h>
#include <Arduino.h>
#include <Wire.h>

#include <DHT.h>
#include <Adafruit_Sensor.h>

#include <MQ135.h>

#include <BH1750.h>

#include "WiFiScan.h"

#include <ArduinoJson.h>

#include <AsyncMqttClient.h>

#include <Ticker.h>
//=======================================================================
//                           Declarations  
//=======================================================================

String handleIndex();

//struct WiFi_scan_result;

int dBmtoPercentage(int dBm);

//=======================================================================
//                         Global Variables
//=======================================================================

#define dSecond 1000
#define dMinute 1000*60
#define dHour 1000*60*60
#define dDay 1000*60*60*24

File configFile;

StaticJsonDocument<1024> configJson;        

AsyncWebServer server(80);       
//ESP8266WebServer server(80);

WiFiUDP ntpUDP;

int GMT = 0;

NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);

std::vector<WiFi_scan_result> scanned_Wifis;

MQ135 gasSensor(A0);

DHT dht(D7, DHT22);

BH1750 lightMeter;

volatile float temp = 0.0;
volatile float humi = 0.0;

volatile float air_quality = 0.0;

volatile float lux = 0.0;

volatile int countRPM = 0;
volatile int mSec = 0;
volatile int fanRPM = 0;

volatile int fanReadings[5] = {0,0,0,0,0};

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

#define MQTT_HOST IPAddress(192, 168, 1, 4)
#define MQTT_PORT 1883

bool save_config = false;

unsigned long timer_serial  {5000};
unsigned long timer_dht22   {2000};
unsigned long timer_mqtt    {10000};

//=======================================================================
//                            Interrupts     
//=======================================================================

ESP8266Timer ITimer;

ESP8266_ISR_Timer ISR_Timer;

void IRAM_ATTR counterRPM()
{
  if(millis() - mSec < 1000) 
    countRPM++;
  else 
  {
    fanReadings[0] = fanReadings[1];
    fanReadings[1] = fanReadings[2];
    fanReadings[2] = fanReadings[3];
    fanReadings[3] = fanReadings[4];
    fanReadings[4] = countRPM*60/2;

    countRPM = 0;
    mSec = millis();
  }
}

void IRAM_ATTR TimerHandler()
{
  ISR_Timer.run();
}

void updateTempHumi()
{
  float newTemp = dht.readTemperature();
  float newHumi = dht.readHumidity();

  if(!isnan(newTemp) && !isnan(newHumi))
  {
    temp = newTemp;
    humi = newHumi;
  }
}

void serialUpdate()
{
  fanRPM = (fanReadings[4]+fanReadings[3]+fanReadings[2]+fanReadings[1]+fanReadings[0])/5;
}

void printRPM()
{
  Serial.print((String)(fanRPM)+" RPM\n");
}

void printTempHumi()
{
  Serial.print((String)(temp)+" C\n");
  Serial.print((String)(humi)+" %\n");
}

void printPPM()
{
  Serial.print((String)(air_quality)+" PPM\n");
}

void printLux()
{
  Serial.print((String)(lux)+" lx\n");
}

void printSerial()
{
  printRPM();
  printTempHumi();
  printPPM();
  printLux();
  Serial.println();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  mqttClient.connect();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, [](){mqttClient.connect();});
  }
}

//=======================================================================
//                              Setup         
//=======================================================================

void setup() {
  Serial.begin(74880);

  if(!LittleFS.begin())
  {
    Serial.println("Error: LittleFS mount failed!");
  }

  configFile = LittleFS.open("/config.json", "r");

  DeserializationError jsonErrs = deserializeJson(configJson, configFile);

  if(jsonErrs)
  {
    Serial.println("Error: JSON deserialization failed!" + String(jsonErrs.c_str()));
  }

  Serial.println("\nLoading timers.");

  timer_dht22   = configJson.as<JsonObject>()["timers"].as<JsonObject>()["dht22_timer"];
  timer_serial  = configJson.as<JsonObject>()["timers"].as<JsonObject>()["serial_timer"];
  timer_mqtt    = configJson.as<JsonObject>()["timers"].as<JsonObject>()["mqtt_timer"];

  Serial.println("DHT22: " + String(timer_dht22));
  Serial.println("Serial: " + String(timer_serial));
  Serial.println("MQTT: " + String(timer_mqtt));

  Serial.println("Loading system settings.");   

  GMT = configJson.as<JsonObject>()["system"].as<JsonObject>()["timezone"];

  Serial.println("Timezone: " + String(GMT));


  Serial.println("Initializing MQTT.");

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  mqttClient.setClientId("SmartGrowBox");

  mqttClient.setKeepAlive(5000);

  mqttClient.setCleanSession(true);

  mqttClient.onConnect([](bool sessionPresent) 
    {
      Serial.println("Connected to MQTT.");

      mqttClient.subscribe("/settings",2);
    });

  //MQTT message received callback

  mqttClient.onMessage([](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) 
    {
      Serial.println("Message received, topic: "+String(topic));

      if(strcmp(topic,"/settings")==0)
      {
        Serial.println("Settings received.");
        StaticJsonDocument<512> tmp_configJson;

        DeserializationError jsonErrs = deserializeJson(tmp_configJson, payload);

        if(jsonErrs)
        {
          Serial.println("Error: JSON deserialization failed!" + String(jsonErrs.c_str()));
          return;
        }

        /*
        Serial.println(tmp_configJson.as<JsonObject>()["rmv-wifi"].as<String>());
        Serial.println(tmp_configJson.as<JsonObject>().containsKey("rmv-wifi"));
        Serial.println(tmp_configJson.as<JsonObject>()["rmv-wifi"].as<JsonArray>());
        Serial.println("aaa");
        Serial.println(configJson.as<JsonObject>()["wifi"].as<JsonArray>());
        Serial.println("bbb");*/

        if(tmp_configJson.as<JsonObject>().containsKey("add-wifi"))
        {
          configJson.as<JsonObject>()["wifi"].add(tmp_configJson.as<JsonObject>()["add-wifi"]);
          Serial.println("Added: " + String(tmp_configJson.as<JsonObject>()["add-wifi"]["ssid"]));
        }

        if(tmp_configJson.as<JsonObject>().containsKey("rmv-wifi"))
        {
          Serial.println("Removing: ");
          bool trace = false;
          for(auto kv : tmp_configJson.as<JsonObject>()["rmv-wifi"].as<JsonArray>())
          {
            Serial.println(kv.as<String>());
            auto i = configJson.as<JsonObject>()["wifi"].as<JsonArray>().begin();
            for(auto to_del : configJson.as<JsonObject>()["wifi"].as<JsonArray>())
            {
              Serial.println("Iterator:" + String(i->as<String>()));
              Serial.println("If: "+ String(to_del["ssid"] == kv["ssid"]));
              if(to_del["ssid"] == kv["ssid"])
              {
                Serial.println("Removed: " + String(i->as<String>()));
                configJson.as<JsonObject>()["wifi"].as<JsonArray>().remove(i);
                trace = true;
                break;
              }
              Serial.println("Next");
              i+=1;
            }
            if(trace) 
            {
              Serial.println("Trace break");
              break;
            }
            
          }
        }

        if(tmp_configJson.as<JsonObject>().containsKey("timezone"))
        {
          configJson.as<JsonObject>()["timezone"] = tmp_configJson.as<JsonObject>()["timezone"].as<int>();
          Serial.println("Timezone set to: " + String(tmp_configJson.as<JsonObject>()["timezone"]));
        }

        if(tmp_configJson.as<JsonObject>().containsKey("timers"))
        {
          if(tmp_configJson.as<JsonObject>()["timers"].as<JsonObject>().containsKey("serial_timer"))
          {
            configJson.as<JsonObject>()["timers"].as<JsonObject>()["serial_timer"] = tmp_configJson.as<JsonObject>()["timers"].as<JsonObject>()["serial_timer"].as<unsigned long>();
            Serial.println("Serial timer set to: " + String(tmp_configJson.as<JsonObject>()["timers"]["serial_timer"]));
          }

          if(tmp_configJson.as<JsonObject>()["timers"].as<JsonObject>().containsKey("dht22_timer"))
          {
            configJson.as<JsonObject>()["timers"].as<JsonObject>()["dht22_timer"] = tmp_configJson.as<JsonObject>()["timers"].as<JsonObject>()["dht22_timer"].as<unsigned long>();
            Serial.println("DHT22 timer set to: " + String(tmp_configJson.as<JsonObject>()["timers"]["dht22_timer"]));
          }

          if(tmp_configJson.as<JsonObject>()["timers"].as<JsonObject>().containsKey("mqtt_timer"))
          {
            configJson.as<JsonObject>()["timers"].as<JsonObject>()["mqtt_timer"] = tmp_configJson.as<JsonObject>()["timers"].as<JsonObject>()["mqtt_timer"].as<unsigned long>();
            Serial.println("MQTT timer set to: " + String(tmp_configJson.as<JsonObject>()["timers"]["mqtt_timer"]));
          }
        }

        save_config = true;
      }
      else if(false)
      {
        
      }
      else
      {
        Serial.println("Unknown topic.");
      }

    });

  mqttClient.onPublish([](uint16_t packetId) 
    {
      Serial.println(packetId);
    });
  
  Serial.println("MQTT server set:");
  Serial.println("MQTT_HOST: "+ MQTT_HOST.toString());

  //mqttClient.connect();

  for(unsigned short int i = 0; i < configJson["wifi"].size(); i++)
  {
    Serial.println(String("\nTrying: ") + String(configJson["wifi"][i]["ssid"]));

    WiFi.begin(String(configJson["wifi"][i]["ssid"]), String(configJson["wifi"][i]["password"]));

    for(unsigned short int j = 0; j < 10 && (WiFi.status() != WL_CONNECTED); j++)
    {
      delay ( 1000 );
      Serial.print ( "." );
    }

    if(WiFi.status()==WL_CONNECTED) break;

    WiFi.disconnect();
  }

  if(WiFi.status()!=WL_CONNECTED)
  {
    Serial.println("WiFi not connected");
  }
  else
  {
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.macAddress());
    Serial.println(WiFi.SSID());  
    Serial.println(WiFi.RSSI());
    Serial.println(WiFi.BSSIDstr());
    Serial.println(WiFi.channel());
  }

  //WiFi.begin(ssid, password);  

  /*while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 1000 );
    Serial.print ( "." );
  }*/

  WiFi.softAP("test_test", "21371488");
  Serial.println("AP started");
  Serial.println(WiFi.softAPIP());
  Serial.println(WiFi.softAPmacAddress());

  timeClient.setTimeOffset(3600*GMT);

  timeClient.begin();

  dht.begin();

  pinMode(D3,OUTPUT);
  pinMode(D4,INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(D4), counterRPM, CHANGE);

  if (ITimer.attachInterruptInterval(50L * 1000, TimerHandler))
  {
    Serial.print(F("Starting ITimer OK, millis() = ")); Serial.println(millis());
    mSec = millis();
  }
  else
    Serial.println(F("Can't set ITimer. Select another freq. or timer"));

  //ISR_Timer.setInterval(5000, serialUpdate);
  Serial.println("1");
  Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties

  /*digitalWrite(D1,LOW);
  digitalWrite(D2,LOW);
  digitalWrite(D3,LOW);
  digitalWrite(D4,LOW);*/

  Wire.begin(D6,D5);            // (SDA,SCL)
Serial.println("2");
  if(!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire)) //BH1750::ONE_TIME_HIGH_RES_MODE
  {
    Serial.println(F("Error initialising BH1750"));
  }

  timeClient.update();
Serial.println("3");

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json","{\"temp\": \""
      + String(temp) + "\", \"humi\": \""+String(humi)
      +"\", \"ppm\": \""+String(air_quality)+"\", \"lux\": \""
      +String(lux)+"\", \"rpm\": \"" + String(fanRPM)+"\"}");
  });

  server.on("/settings",HTTP_GET,[](AsyncWebServerRequest *request){
    request->send(200, "application/json", configJson.as<String>());
  });

  Serial.println("4");
  while(!mqttClient.connected())
  {
    mqttClient.connect();
    delay(100);
  }
  Serial.println("5");
  server.begin();
  Serial.println("MQTT server connection status: " + String(mqttClient.connected()));
  Serial.println("6");
}

//=======================================================================
//                              Loop         
//=======================================================================

//
//  "serial_timer": 5000
//  "dht22_timer":  2000
//  "mqtt_timer":  10000

unsigned long serialInterval = 0;
unsigned long DHT22Interval = 0;
unsigned long mqqtInterval = 0;

void loop() {

  if(millis()-DHT22Interval>timer_dht22)
  {
    serialUpdate();
    updateTempHumi();
    air_quality = gasSensor.getCorrectedPPM(temp,humi);
    lux = lightMeter.readLightLevel();
    DHT22Interval = millis();
  }

  if(millis()-serialInterval>timer_serial)
  {
    printSerial();
    serialInterval = millis();
  }

  if(millis()-mqqtInterval>timer_mqtt)
  {
    if(mqttClient.connected())
    {
      //Serial.println("Trying to send data to server.");

      String tmp {"{\"temp\": \""
      + String(temp) + "\", \"humi\": \""+String(humi)
      +"\", \"ppm\": \""+String(air_quality)+"\", \"lux\": \""
      +String(lux)+"\", \"rpm\": \"" + String(fanRPM)+"\"}"};

    yield();

    mqttClient.publish("server/update",2,true,tmp.c_str(), tmp.length()+1, false);
    }
    else
    {
      mqttClient.disconnect();

      Serial.println("MQTT server connection status: " + String(mqttClient.connected()));
      mqttClient.connect();
    }
    
    mqqtInterval = millis();
  }

  if(save_config)
  {
    Serial.println(configJson.as<String>());

    configFile = LittleFS.open("/config.json", "w");

    serializeJson(configJson, configFile);

    configFile.close();

    Serial.println("Config saved.");

    save_config = false;
  }
}

//=======================================================================
//                          Definitions  
//=======================================================================

void serialEvent() 
{
  if(Serial.available()){
    String incomingString = Serial.readString();

    analogWrite(D3,incomingString.toInt());
    Serial.println(incomingString.toInt());
    }
}