#pragma once

#include "Arduino.h"

class WifiAPInfo {
  private:
    std::string BSSID= "";
    std::string SSID = "";
    int32_t RSSI=  -1;
    int32_t Channel = -1;
    int  Count = 0;
    int Change = 0;
    time_t FirstTime = 0;
  public:
    time_t LastTime = 0;
    WifiAPInfo(std::string bssi,std::string ssid,int32_t rssi,int32_t channel,time_t ts);
    void update(std::string ssid,int32_t rssi,int32_t channel,time_t ts);
    std::string get();
    std::string getTimeStr(time_t t);
};
