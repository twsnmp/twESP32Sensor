#include <map>
#include "BlueDevice.h"
#include "vendorcode.h"

std::map<int16_t, const char *> codeToVendor;

const char* getVendorFromCode(int16_t code) {
  if(codeToVendor.count(code)) {
    return codeToVendor[code]; 
  }
  for(codeToVendorEnt e:codetVendorList) {
    if( e.code == code) {
      codeToVendor[code] = e.name;
      return e.name;
    }
  }
  codeToVendor[code] = "";
  return "";
}


BlueDevice::BlueDevice(std::string address, std::string name, int32_t rssi, const char* addressType, int16_t code, time_t ts) {
  this->Address = address;
  this->Name = name;
  this->RSSI = rssi;
  this->MinRSSI = rssi;
  this->MaxRSSI = rssi;
  this->Code = code;
  this->AddressType = addressType;
  this->FistTime = ts;
  this->LastTime = ts;
  this->Vendor = getVendorFromCode(code);
  this->Count = 1;
}

void BlueDevice::update(int32_t rssi, time_t ts) {
  if (this->MaxRSSI < rssi) {
    this->MaxRSSI = rssi;
  }
  this->RSSI = rssi;
  this->LastTime = ts;
  this->Count++;
}

std::string BlueDevice::getTimeStr(time_t t) {
  char ts[128];
  struct tm *tm;
  tm = localtime(&t);
  strftime(ts, sizeof(ts), "%FT%X+09:00", tm);
  return std::string(ts);
}


std::string BlueDevice::get() {
  char codeStr[12];
  sprintf(codeStr,"0x%04x",this->Code);
  return "type=Device,address=" + this->Address + ",name=" + this->Name + ",rssi=" + std::to_string(this->RSSI) + ",min=" + std::to_string(this->MinRSSI) + ",max=" + std::to_string(this->MaxRSSI) + ",addrType=" + this->AddressType + 
  ",count=" + std::to_string(this->Count) +
  ",code=" + std::string(codeStr) +
  ",vendor=" + this->Vendor + 
  ",info=,ft=" + this->getTimeStr(this->FistTime) +
  ",lt=" + this->getTimeStr(this->LastTime);
}