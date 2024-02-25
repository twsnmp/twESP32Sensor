#pragma once

#include "Arduino.h"

class BlueDevice {
  protected:
    std::string Address = "";
    std::string Name = "";
    int32_t RSSI = 0;
    int32_t MinRSSI = 0;
    int32_t MaxRSSI = 0;
    const char* AddressType = "";
    const char* Vendor = "";
    int16_t Code = 0x0000;
    int Count = 0;
    time_t FistTime = 0;
    std::string getTimeStr(time_t t);
  public:
    time_t LastTime = 0;
    BlueDevice(std::string address,std::string name,int32_t rssi,const char* addressType,int16_t code,time_t ts);
    void update(int32_t rssi,time_t ts);
    std::string get();
};