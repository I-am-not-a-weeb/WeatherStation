#include <cstdlib>
#include <vector>


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

char ssid[]     = "TP-Kink";
char password[] = "21371488";

AsyncWebServer server(80);       
//ESP8266WebServer server(80);

WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);

std::vector<WiFi_scan_result> scanned_Wifis;

unsigned long serialInterval = 0;
unsigned long DHT22Interval = 0;

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
//=======================================================================
//                              Setup         
//=======================================================================

void setup() {

  Serial.begin(115200);

  WiFi.begin(ssid, password);  

  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 1000 );
    Serial.print ( "." );
  }

  Serial.println ("Connected");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.macAddress());
  Serial.println(WiFi.SSID());  
  Serial.println(WiFi.RSSI());
  Serial.println(WiFi.BSSIDstr());
  Serial.println(WiFi.channel());

  WiFi.softAP("test_test", "21371488");
  Serial.println("AP started");
  Serial.println(WiFi.softAPIP());
  Serial.println(WiFi.softAPmacAddress());

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

  Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties


  /*digitalWrite(D1,LOW);
  digitalWrite(D2,LOW);
  digitalWrite(D3,LOW);
  digitalWrite(D4,LOW);*/


  Wire.begin(D6,D5);            // (SDA,SCL)

  if(!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire)) //BH1750::ONE_TIME_HIGH_RES_MODE
  {
    Serial.println(F("Error initialising BH1750"));
  }

  /*
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", handleIndex());
  });
  //*/

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", handleIndex());
  });

  server.on("/updateWifi",[](AsyncWebServerRequest *request){
    updateScannedWiFis(scanned_Wifis,WiFi);

    String index_html;
    index_html.reserve(1024);

    index_html+="<table>";
    index_html+="<tr class=\"item\"><td>SSID</td><td>Signal</td><td>Encryption</td></tr>";

    for(std::vector<WiFi_scan_result>::iterator i = scanned_Wifis.begin(); i != scanned_Wifis.end(); i++)
    {
      index_html+="<tr class=\"item\">";

      index_html+=!(i->SSID[0] == '\0' || i->SSID[0] == '0') ? "<td>" : "<td class=\"hidden\">"; 
      index_html+=!(i->SSID[0] == '\0' || i->SSID[0] == '0') ? i->SSID : "SSID Hidden";
      index_html+="</td>";

      index_html+="<td>";
      index_html+=dBmtoPercentage(i->RSSI);
      index_html+="\%";
      index_html+="\n";
      index_html+="</td>";

      index_html+="<td>";
      index_html+=i->encryptionType;
      index_html+="</td>";

      index_html+="</tr>";
    }
    index_html+="</table>";

    request->send(200, "text/plane",index_html);
  });


  server.begin();
}

//=======================================================================
//                              Loop         
//=======================================================================


void loop() {

  //server.handleClient();


  if(millis()-DHT22Interval<2000)
  {
    
  }
  else
  {
    serialUpdate();
    updateTempHumi();
    air_quality = gasSensor.getCorrectedPPM(temp,humi);
    lux = lightMeter.readLightLevel();
    DHT22Interval = millis();
  }

  if(millis()-serialInterval<5000)
  {
    
  }
  else
  {
    printSerial();
    serialInterval = millis();
  }
}
void serialEvent() 
{
  if(Serial.available()){
    String incomingString = Serial.readString();

    analogWrite(D3,incomingString.toInt());
    Serial.println(incomingString.toInt());
    }
}


//=======================================================================
//                          Definitions  
//=======================================================================

/*struct WiFi_scan_result{
  int8_t RSSI;
  uint8_t encryptionType;
  char SSID[32];
};

void updateScannedWiFis()
{
  WiFi.scanNetworksAsync([](int arg){
    scanned_Wifis.clear();
    scanned_Wifis.reserve(arg);
    for(int i = 0; i<arg;i++)
    {
      scanned_Wifis.push_back(WiFi_scan_result{(char)WiFi.RSSI(i),WiFi.encryptionType(i),*WiFi.SSID(i).c_str()});
    }
  },true);

  WiFi.scanDelete();
}

int dBmtoPercentage(int dBm)
{
  int quality;
    if(dBm <= RSSI_MIN)
    {
        quality = 0;
    }
    else if(dBm >= RSSI_MAX)
    {  
        quality = 100;
    }
    else
    {
        quality = 2 * (dBm + 100);
   }

  return quality;
}*/

//=======================================================================
//                          HTML Pages 
//=======================================================================

String handleIndex()
{
  String index_html;
  index_html.reserve(1024);
  index_html = R"rawliteral(<!DOCTYPE html><html></html>
  <head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">
    <link rel=\"icon\" href=\"data:,\">
    <style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}
      .button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;
      text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}
      table{ padding: 20px; width:100%; height:100%;}
      .hidden{ background-color: #CD5C5C }
      .button2 {background-color: #77878A;}
      body{align-items:center;}
      .grid-addons{display: grid;grid-template-columns: repeat(auto-fill, 20em);grid-gap: 1rem;justify-content: space-between; /* 4 */}
      .WIFI{background-color: #FFA07A; width:100%; height:100%;  border-style: solid; border-color: black; font-size:10px;}
    </style>
    <script>
    function updateWiFi()
    {
    const xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200)
        {
            document.getElementById("WIFI").innerHTML = this.responseText;
        }
    }
    document.getElementById("WIFI").innerHTML = "Updating...";
    xhttp.open("GET", "updateWifi", true);
    xhttp.send()
    }

    </script>
    </head>
    <body><h1>ESP8266 Web Server</h1>)rawliteral";  

  index_html+="<div class=\"grid-addons\">";
  index_html+="<div class=\"WIFI\">";              // klocek z wifi
  index_html+="<table>";
  index_html+="<tr class=\"item\"><td>SSID</td><td>Signal</td><td>Encryption</td></tr>";

  for(std::vector<WiFi_scan_result>::iterator i = scanned_Wifis.begin(); i != scanned_Wifis.end(); i++)
  {
    index_html+="<tr class=\"item\">";
            
    index_html+=!(i->SSID[0] == '\0' || i->SSID[0] == '0') ? "<td>" : "<td class=\"hidden\">"; 
    index_html+=!(i->SSID[0] == '\0' || i->SSID[0] == '0') ? i->SSID : "SSID Hidden";
    index_html+="</td>";

    index_html+="<td>";
    index_html+=dBmtoPercentage(i->RSSI);
    index_html+="\%";
    index_html+="\n";
    index_html+="</td>";

    index_html+="<td>";
    index_html+=i->encryptionType;
    index_html+="</td>";

    index_html+="</tr>";
  }
  index_html+="</table>";
  index_html+="</div>";

  return index_html;
}

