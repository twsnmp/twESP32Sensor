#include "WifiAPInfo.h"

WifiAPInfo::WifiAPInfo(std::string bssid,std::string ssid,int32_t rssi,int32_t channel,time_t ts) {
  this->BSSID = bssid;
  this->SSID = ssid;
  this->Channel = channel;
  this->RSSI = rssi;
  this->Count = 1;
  this->Change = 0;
  this->FirstTime = ts;
  this->LastTime = ts;
}

void WifiAPInfo::update(std::string ssid,int32_t rssi,int32_t channel,time_t ts) {
  if(ssid != this->SSID  || channel != this->Channel) {
    this->Change++;
  }
  this->SSID = ssid;
  this->Channel = channel;
  this->RSSI = rssi;
  this->Count++;
  this->LastTime = ts;
}

std::string WifiAPInfo::getTimeStr(time_t t) {
  char ts[128];
  struct tm *tm;
  tm = localtime(&t);
  strftime(ts, sizeof(ts), "%FT%X+09:00", tm);
  return std::string(ts);
} 

std::string WifiAPInfo::get(void) {
  return "type=APInfo,ssid=" + this->SSID +",bssid="+this->BSSID + ",rssi="+ std::to_string(this->RSSI) + 
  ",Channel="+ std::to_string(this->Channel) +",info=,count="+std::to_string(this->Count) + ",change="+ std::to_string(this->Change)+",vendor=,ft="+ this->getTimeStr(this->FirstTime) +",lt=" + this->getTimeStr(this->LastTime);
}