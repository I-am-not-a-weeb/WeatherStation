#pragma once

#ifndef _STDIO_H_
#include <stdio.h>
#endif 

#ifndef _ESP8266WIFI_H_
#include <ESP8266WiFi.h>
#endif 

#define RSSI_MAX -50        // define maximum strength of signal in dBm
#define RSSI_MIN -100      // define minimum strength of signal in dBm

struct WiFi_scan_result{
  int8_t RSSI;
  uint8_t encryptionType;
  char SSID[32];
};

void updateScannedWiFis(std::vector<WiFi_scan_result> &scanned_Wifis,ESP8266WiFiClass &WiFi)
{
  WiFi.scanNetworksAsync([&](int arg){
    scanned_Wifis.clear();
    scanned_Wifis.reserve(arg);
    for(int i = 0; i<arg;i++)
    {
      char tmp_str[32]; 
      strcpy(tmp_str,WiFi.SSID(i).c_str());

      WiFi_scan_result tmp{(char)WiFi.RSSI(i),WiFi.encryptionType(i)};

      strcpy(tmp.SSID,tmp_str);
      scanned_Wifis.push_back(tmp);
    }
    WiFi.scanDelete();    // exception
  },true);

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
}