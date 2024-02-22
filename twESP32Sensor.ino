#include <map>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include "AsyncUDP.h"
#include "NTPClient.h"
#include "BLEDevice.h"
#include "BlueDevice.h"
#include "WifiAPInfo.h"
#include <Preferences.h>
#include "esp_freertos_hooks.h"

// Define constant
#define VERSION "1.0.0"
#define PIN_LED 2

// Function prottypes
std::string getTimeStamp();
void cleanup();
void sendMonitor();
void doWifiAPScan();
void doBuleScan();
String getInput(const char* msg);
void checkSerial();

IPAddress dst;
int port =514;

WiFiUDP udp;
NTPClient ntp(udp, "ntp.nict.jp", 32400, 60000);
AsyncUDP syslog;
WiFiClass wifi;
BLEScan* pBLEScan;
Preferences pref;


std::map<std::string, std::unique_ptr<BlueDevice>> blueDeviceMap;
std::map<std::string, std::unique_ptr<WifiAPInfo>> wifiApInfoMap;

int count = 0;
int tick = 0;
int idle = 0;

bool IRAM_ATTR idleHook(void) {
  idle++;
  return true;
}
// Tick数カウントアップ
void IRAM_ATTR tickHook(void) {
  tick++;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  delay(2000);
  esp_register_freertos_idle_hook_for_cpu(&idleHook, 0);
  esp_register_freertos_tick_hook_for_cpu(&tickHook, 0);
  esp_register_freertos_idle_hook_for_cpu(&idleHook, 1);
  esp_register_freertos_tick_hook_for_cpu(&tickHook, 1);

  Serial.printf("Start twESP32Sensor %s\n",VERSION);
  pref.begin("twESP32Config", true);
  bool needConfig = pref.getBool("config", true);
  if (needConfig) {
    pref.end();
    pref.begin("twESP32Config", false);
    String in = "";
    while(in == "") {
      in = getInput("enter ssid:");
    }
    pref.putString("ssid", in);
    in = getInput("enter password:");
    pref.putString("password", in);
    in = "";
    while(in == "" || !dst.fromString(in.c_str())) {
      in = getInput("enter syslog ip:");
    }
    pref.putString("syslog", in);
    in = getInput("enter syslog port:");
    if (in != "") {
      int port = atoi(in.c_str());
      if (port >0 && port < 0xffff) {
        pref.putInt("port",port);
      }
    }
    pref.putBool("config", false);
  }
  String ssid = pref.getString("ssid", "");
  String password = pref.getString("password", "");
  String syslog = pref.getString("syslog", "");
  port = pref.getInt("port", 514);
  dst.fromString(syslog.c_str());
  
  Serial.printf("Config ssid=%s,password=%s\n", ssid, password);
  pref.end();

  wifi.mode(WIFI_STA);
  wifi.setAutoConnect(false);
  wifi.begin(ssid, password);
  Serial.printf("Connecting to %s", ssid);
  delay(3000);
  int n = 0;
  while (wifi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED, n % 2 == 0 ? HIGH : LOW);
    delay(500);
    Serial.print(".");
    checkSerial();
    n++;
  }
  Serial.print("\nConnected, IP address: ");
  Serial.println(WiFi.localIP());
  ntp.begin();
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  Serial.println("Setup End");
}

void loop() {
  ntp.update();
  checkSerial();
  if (count % 60 == 0) {
    Serial.println(ESP.getFreeHeap());
    doBlueScan();
    count += 4;
    cleanup();
  }
  if (count % 60 == 10) {
    doWifiAPScan();
  }
  if (count % 60 == 15) {
    sendMonitor();
  }
  digitalWrite(PIN_LED, count % 2 == 0 ? HIGH : LOW);
  delay(1000);
  count++;
}


// OMRONの環境センサー
// OMRONSセンサーのデータ
// https://omronfs.omron.com/ja_JP/ecb/products/pdf/CDSC-016A-web1.pdf
// P60
// https://armadillo.atmark-techno.com/howto/armadillo_2JCIE-BU01_GATT
// d5 02
// 01     Data Type
// c5     連番
// a9 09  温度 0.01℃
// cd 1a  湿度 0.01%
// 0d 00  照度 1lx
// 26 6c 0f 00 気圧 1hPa
// 3d 13  騒音 0.01dB
// 07 00  eTVOC 1ppb
// c3 01  二酸化炭素 1ppm
// ff
void checkOMROM(BLEAdvertisedDevice* d) {
  // Name: , Address: ef:6e:29:f9:8c:48, manufacturer data: d5020147f5086f13b500fba50f00f4132109c10aff, rssi: -77
  std::string data = d->getManufacturerData();
  if (data.length() < 20 || data[2] != 0x01) {
    return;
  }
  float temp = (data[5] * 256 + data[4]) * 0.01;
  float hum = (data[7] * 256 + data[6]) * 0.01;
  int lx = data[9] * 256 + data[8];
  int seq = data[3];
  float press = ((data[13] * 0x01000000) + (data[12] * 0x00010000) + (data[11] * 0x00000100) + data[10]) * 0.001;
  float sound = (data[15] * 256 + data[14]) * 0.01;
  int eTVOC = data[17] * 256 + data[16];
  int co2 = data[19] * 256 + data[18];
  std::string address = d->getAddress().toString();
  int rssi = d->getRSSI();
  Serial.printf("omron address=%s rssi=%d temp=%.02f hum=%.02f lx=%d press=%.02f sound=%.02f eTVOC=%d co2=%d ts=%s", address.c_str(), rssi, temp, hum, lx, press, sound, eTVOC, co2, getTimeStamp().c_str());
  Serial.println("");

  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: type=OMRONEnv,address=%s,name=,rssi=%d,seq=%d,temp=%.02f,hum=%.02f,lx=%d,press=%.02f,sound=%.02f,eTVOC=%d,co2=%d", 21 * 8 + 6, getTimeStamp().c_str(), wifi.localIP().toString().c_str(), address.c_str(), rssi, seq, temp, hum, lx, press, sound, eTVOC, co2);
  syslog.sendTo(msg, dst, port);
}

void checkSwitchBotMotion(BLEAdvertisedDevice* d) {
  //Name: , Address: d7:bb:ea:e7:cf:58, manufacturer data: 6909d7bbeae7cf58922c0045, rssi: -60, serviceData: s
  if (!d->haveServiceData()) {
    return;
  }
  std::string data = d->getServiceData();
  if (data.length() < 6 || data[0] != 's') {
    return;
  }
  // 73 40 e4 00 1c 02
  const char* m = data[1] & 0x40 ? "true" : "false";
  const char* l = data[5] & 0x02 ? "true" : "false";
  int bat = data[2];
  int t = (data[3] * 256 + data[4]) + ((data[5] & 0x80) ? 0x10000 : 0);
  std::string address = d->getAddress().toString();
  int rssi = d->getRSSI();
  Serial.printf("motion address=%s,rssi=%d,moving=%s,battery=%d,light=%s,diffTime=%d", address.c_str(), rssi, m, bat, l, t);
  Serial.println("");
  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: type=SwitchBotMotionSensor,address=%s,name=,rssi=%d,moving=%s,event=report,lastMoveDiff=%d,battery=%d,light=%s", 21 * 8 + 6, getTimeStamp().c_str(), wifi.localIP().toString().c_str(), address.c_str(), rssi, m, t, bat, l);
  syslog.sendTo(msg, dst, port);
  return;
}

// SwitchBot Plug Mini
// https://github.com/OpenWonderLabs/SwitchBotAPI-BLE/blob/latest/devicetypes/plugmini.md
// UUID 105 9
// MAC 96 85 249 45 33 206
// Seq 25
// On/Off 128
// Time 0
// wifi RSSI 0
// Load 1 214  Overload
void checkSwitchBotPlugMini(BLEAdvertisedDevice* d) {
  // Name: , Address: 60:55:f9:2d:21:ce, manufacturer data: 69096055f92d21ceab8000000053, rssi: -42, serviceData: g
  // 69 09 60 55 f9 2d 21 ce ab 80 00 00 00 53
  std::string data = d->getManufacturerData();
  if (data.length() < 14) {
    return;
  }
  const char* sw = data[9] == 0x80 ? "true" : "false";
  const char* over = data[12] & 0x80 == 0x80 ? "true" : "false";
  int load = (data[12] & 0x7f) * 256 + (data[13] & 0x7f);
  std::string address = d->getAddress().toString();
  int rssi = d->getRSSI();
  Serial.printf("plugmini address=%s,rssi=%d,sw=%s,over=%s,laod=%d", address.c_str(), rssi, sw, over, load);
  Serial.println("");
  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: type=SwitchBotPlugMini,address=%s,name=,rssi=%d,sw=%s,over=%s,load=%d", 21 * 8 + 6, getTimeStamp().c_str(), wifi.localIP().toString().c_str(), address.c_str(), rssi, sw, over, load);
  syslog.sendTo(msg, dst, port);
}

void checkSwitchBotEnv(BLEAdvertisedDevice* d) {
  // Name: , Address: df:23:74:a1:96:d8, manufacturer data: 5900df2374a196d8, serviceUUID: cba20d00-224d-11e6-9fb8-0002a5d5c51b, rssi: -71, serviceData: T�	�/
  if (!d->haveServiceData()) {
    return;
  }
  std::string data = d->getServiceData();
  if (data.length() < 5) {
    return;
  }
  // 54 10 e4 05 8b 4e
  int bat = data[2] & 0x7f;
  float temp = (data[3] * 0.1) + ((data[4] & 0x7f) * 1.0) * ((data[4] & 0x80) ? 1.0 : -1.0);
  float hum = (data[5] & 0x7f) * 1.0;
  int rssi = d->getRSSI();
  std::string address = d->getAddress().toString();
  Serial.printf("switchbotenv address=%s,rssi=%d,temp=%.02f,hubr=%.1f,bat=%d", address.c_str(), rssi, temp, hum, bat);
  Serial.println("");
  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: type=SwitchBotEnv,address=%s,name=,rssi=%d,temp=%.02f,hum=%.02f,bat=%d", 21 * 8 + 6, getTimeStamp().c_str(), wifi.localIP().toString().c_str(), address.c_str(), rssi, temp, hum, bat);
  syslog.sendTo(msg, dst, port);
}

//
void checkBlueDevice(BLEAdvertisedDevice* d,int16_t code) {
  std::string address = d->getAddress().toString();
  esp_ble_addr_type_t at = d->getAddressType();
  const char* atStr = at == BLE_ADDR_TYPE_PUBLIC ? "Public" : at == BLE_ADDR_TYPE_RANDOM     ? "Random"
                                                            : at == BLE_ADDR_TYPE_RPA_PUBLIC ? "RPA Public"
                                                                                             : "RPA Random";
  int rssi = d->getRSSI();
  std::string name = d->getName();

  auto e = blueDeviceMap.find(address);
  if (e != blueDeviceMap.end()) {
    BlueDevice* p = e->second.get();
    p->update(rssi, ntp.getEpochTime());
  } else {
    blueDeviceMap[address] = std::unique_ptr<BlueDevice>(new BlueDevice(address,name, rssi, atStr, code, ntp.getEpochTime()));
  }
  e = blueDeviceMap.find(address);
  BlueDevice* p = e->second.get();
  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: %s", 21 * 8 + 6, getTimeStamp().c_str(), WiFi.localIP().toString().c_str(), p->get().c_str());
  syslog.sendTo(msg, dst, port);
  Serial.println(p->get().c_str());
}


void doBlueScan() {
  BLEScanResults foundDevices = pBLEScan->start(5);
  int count = foundDevices.getCount();
  Serial.print("BLEScan count=");
  Serial.println(count);
  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice d = foundDevices.getDevice(i);
    if (d.haveManufacturerData()) {
      std::string data = d.getManufacturerData();
      int code = data[1] << 8 | data[0];
      switch (code) {
        case 0x0969:  // SwiitchBot
          if (data.length() >= 14) {
            checkSwitchBotPlugMini(&d);
          } else {
            checkSwitchBotMotion(&d);
          }
          break;
        case 0x02d5:  // OMRON
          checkOMROM(&d);
          break;
        case 0x0059:
          checkSwitchBotEnv(&d);
        case 0x004c:
        case 0x0060:
          break;
        default:
          Serial.println(d.toString().c_str());
          break;
      }
      checkBlueDevice(&d,code);
    } else {
      Serial.print("no ManufactureData ");
      Serial.println(d.toString().c_str());
      checkBlueDevice(&d,0);
    }
  }
}

// Scan Wifi AP and send info to syslog
void doWifiAPScan() {
  int16_t scan_res = wifi.scanNetworks();
  if (scan_res == 0)
    Serial.println("no wifi ap");
  else {
    // 発見したwifiAPを表示
    Serial.print(scan_res);
    Serial.println(" wifi aps。");
    for (int i = 0; i < scan_res; ++i) {
      AsyncUDPMessage msg;
      std::string bssid = wifi.BSSIDstr(i).c_str();
      std::string ssid = wifi.SSID(i).c_str();
      int32_t rssi = wifi.RSSI(i);
      int32_t channel = wifi.channel(i);
      auto e = wifiApInfoMap.find(bssid);
      if (e != wifiApInfoMap.end()) {
        WifiAPInfo *p = e->second.get();
        p->update(ssid, rssi, channel, ntp.getEpochTime());
      } else {
        wifiApInfoMap[bssid] = std::unique_ptr<WifiAPInfo>(new WifiAPInfo(bssid, ssid, rssi, channel, ntp.getEpochTime()));
      }
      e = wifiApInfoMap.find(bssid);
      WifiAPInfo *p = e->second.get();
      msg.printf("<%d>%s %s twWifiScan: %s", 21 * 8 + 6, getTimeStamp().c_str(), WiFi.localIP().toString().c_str(), p->get().c_str());
      syslog.sendTo(msg, dst, port);
      Serial.print(i + 1);
      Serial.print(":");
      Serial.println(p->get().c_str());
    }
  }
}

int retention = 24;
// cleanup old info
void cleanup() {
  time_t th = ntp.getEpochTime() - 3600 * retention;
  auto itb = blueDeviceMap.begin();
  while (itb != blueDeviceMap.end()){
    BlueDevice *p = itb->second.get();
    if (p->LastTime < th ) {
      Serial.printf("delete %s\n",p->get().c_str());
      blueDeviceMap.erase(itb++);
    } else {
      ++itb;
    }
  }
  auto itw = wifiApInfoMap.begin();
  while (itw != wifiApInfoMap.end()){
    WifiAPInfo *p = itw->second.get();
    if (p->LastTime < th ) {
      Serial.printf("delete %s\n",p->get().c_str());
      wifiApInfoMap.erase(itw++);
    } else {
      ++itw;
    }
  }
  int free = ESP.getFreeHeap();
  if (free < 1024 * 20 && retention > 1) {
    Serial.printf("set retention=%d",retention);
    retention--;
  }
}

// send monitor 
void sendMonitor() {
  AsyncUDPMessage msg;
  float cpu = tick > 0 ?  (tick - idle) * 100.0 / tick : 0.0;
  msg.printf("<%d>%s %s twESP32Sensor: count=%d,mem=%d,free=%d,min=%d,cpu=%.02f", 21 * 8 + 6, getTimeStamp().c_str(), WiFi.localIP().toString().c_str(),
    count,
    ESP.getHeapSize(),
    ESP.getFreeHeap(),
    ESP.getMinFreeHeap(),
    cpu
  );
  syslog.sendTo(msg, dst, port);
  tick=0;
  idle = 0;
}

// Utils 
// get string time stamp
std::string getTimeStamp() {
  char ts[128];
  time_t t;
  struct tm* tm;
  t = ntp.getEpochTime();
  tm = localtime(&t);
  strftime(ts, sizeof(ts), "%FT%X+09:00", tm);
  return std::string(ts);
}

// Input line form serial
String getInput(const char* msg) {
  String in = "";
  while (in == "") {
    Serial.println(msg);
    while (Serial.available() < 1);
    in = Serial.readStringUntil('\n');
    Serial.println(in);
  }
  return in;
}

// check serial input to config mode.
void checkSerial() {
  if (Serial.available() > 5) {
    String in = Serial.readStringUntil('\n');
    Serial.println(in);
    if (in == "config") {
      pref.begin("twESP32Config", false);
      pref.putBool("config", true);
      pref.end();
      Serial.println("Restart now");
      delay(1000);
      ESP.restart();
    }
  }
}
