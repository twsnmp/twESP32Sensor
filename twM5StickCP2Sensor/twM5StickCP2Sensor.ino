#include <M5StickCPlus2.h>
#include <map>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <AsyncUDP.h>
#include <Preferences.h>
#include <esp_freertos_hooks.h>

#include "NTPClient.h"
#include "BLEDevice.h"
#include "BlueDevice.h"
#include "WifiAPInfo.h"

#if defined(CONFIG_ESP32_DEFAULT_CPU_FREQ_240)
static const uint64_t MaxIdleCalls = 1855000;
#elif defined(CONFIG_ESP32_DEFAULT_CPU_FREQ_160)
static const uint64_t MaxIdleCalls = 1233100;
#else
#error "Unsupported CPU frequency"
#endif

// Define constant
#define VERSION "1.0.0"
#define STATE_START 0
#define STATE_SETUP 1
#define STATE_WIFI  2
#define STATE_READY 3

// Function prottypes
std::string getTimeStamp();
void cleanup();
void sendMonitor();
void doWifiAPScan();
void doBuleScan();
String getInput(const char *msg,bool require = true);
void checkSerial();
void updateLCD(int state);

WiFiClass wifi;
String ssid;
String password;
WiFiUDP udp;

NTPClient ntp(udp, "ntp.nict.jp", 32400, 60000);
AsyncUDP syslog;
IPAddress syslog_dst;
String syslog_ip;
int syslog_port = 514;

BLEScan *pBLEScan;
Preferences pref;

std::map<std::string, std::unique_ptr<BlueDevice>> blueDeviceMap;
std::map<std::string, std::unique_ptr<WifiAPInfo>> wifiApInfoMap;

uint32_t count = 0;
uint32_t idle = 0;
uint32_t tick = 0;

int blueCount  = 0;
int wifiAPCount = 0;
int omronCount = 0;
int switchBotCount = 0;
float cpu = 0.0;
float mem = 0.0;

bool IRAM_ATTR idleHook(void) {
  idle++;
  return true;
}

void setup() {
  Serial.begin(115200);
  esp_register_freertos_idle_hook_for_cpu(&idleHook, 0);
  esp_register_freertos_idle_hook_for_cpu(&idleHook, 1);
  M5.begin();
  updateLCD(STATE_START);
  Serial.printf("twM5StickCP2Sensor v%s\n", VERSION);
  Serial.println("setup start");
  pref.begin("twESP32Config", true);
  bool needConfig = pref.getBool("config", true);
  if (needConfig) {
    updateLCD(STATE_SETUP);
    pref.end();
    pref.begin("twESP32Config", false);
    String ssid_in  = getInput("enter ssid:");
    String password_in = getInput("enter password:",false);
    String syslog_in =  "";
    do {
      syslog_in = getInput("enter syslog ip:");
    } while(!syslog_dst.fromString(syslog_in.c_str()));
    int port_in = -1;
    while(true) {
      String in = getInput("enter syslog port:",false);
      if (in == "") {
        break;
      }
      int p = atoi(in.c_str());
      if (p > 0 && p < 0xffff) {
        port_in = p;
        break;
      }
    }
    pref.putString("ssid", ssid_in);
    pref.putString("password", password_in);
    pref.putString("syslog", syslog_in);
    if (port_in > 0 && port_in < 0xffff) {
      pref.putInt("port", port_in);
    }
    pref.putBool("config", false);
  }
  ssid = pref.getString("ssid", "");
  password = pref.getString("password", "");
  syslog_ip = pref.getString("syslog", "");
  syslog_port = pref.getInt("port", 514);
  syslog_dst.fromString(syslog_ip.c_str());

  Serial.printf("Config ssid=%s,syslog=%s,port=%d\n", ssid.c_str(), syslog_ip.c_str(), syslog_port);
  pref.end();
  updateLCD(STATE_WIFI);
  wifi.mode(WIFI_STA);
  wifi.setAutoConnect(false);
  wifi.begin(ssid, password);
  Serial.printf("Connecting to %s", ssid.c_str());
  delay(3000);
  int n = 0;
  while (wifi.status() != WL_CONNECTED) {
    delay(1000);
    M5.Lcd.print(".");
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
  Serial.println("setup end");
  updateLCD(STATE_READY);
}

void loop() {
  tick += 2 * MaxIdleCalls;
  ntp.update();
  checkSerial();
  if (count % 60 == 0) {
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
  delay(1000);
  M5.update();
  count++;
}

uint16_t oldColor = 0;

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
void checkOMROM(BLEAdvertisedDevice *d) {
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

  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: type=OMRONEnv,address=%s,name=,rssi=%d,seq=%d,temp=%.02f,hum=%.02f,lx=%d,press=%.02f,sound=%.02f,eTVOC=%d,co2=%d", 21 * 8 + 6, getTimeStamp().c_str(), wifi.localIP().toString().c_str(), address.c_str(), rssi, seq, temp, hum, lx, press, sound, eTVOC, co2);
  syslog.sendTo(msg, syslog_dst, syslog_port);
}

void checkSwitchBotMotion(BLEAdvertisedDevice *d) {
  // Name: , Address: d7:bb:ea:e7:cf:58, manufacturer data: 6909d7bbeae7cf58922c0045, rssi: -60, serviceData: s
  if (!d->haveServiceData()) {
    return;
  }
  std::string data = d->getServiceData();
  if (data.length() < 6 || data[0] != 's') {
    return;
  }
  // 73 40 e4 00 1c 02
  const char *m = data[1] & 0x40 ? "true" : "false";
  const char *l = data[5] & 0x02 ? "true" : "false";
  int bat = data[2];
  int t = (data[3] * 256 + data[4]) + ((data[5] & 0x80) ? 0x10000 : 0);
  std::string address = d->getAddress().toString();
  int rssi = d->getRSSI();
  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: type=SwitchBotMotionSensor,address=%s,name=,rssi=%d,moving=%s,event=report,lastMoveDiff=%d,battery=%d,light=%s", 21 * 8 + 6, getTimeStamp().c_str(), wifi.localIP().toString().c_str(), address.c_str(), rssi, m, t, bat, l);
  syslog.sendTo(msg, syslog_dst, syslog_port);
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
void checkSwitchBotPlugMini(BLEAdvertisedDevice *d) {
  // Name: , Address: 60:55:f9:2d:21:ce, manufacturer data: 69096055f92d21ceab8000000053, rssi: -42, serviceData: g
  // 69 09 60 55 f9 2d 21 ce ab 80 00 00 00 53
  std::string data = d->getManufacturerData();
  if (data.length() < 14) {
    return;
  }
  const char *sw = data[9] == 0x80 ? "true" : "false";
  const char *over = data[12] & 0x80 == 0x80 ? "true" : "false";
  int load = (data[12] & 0x7f) * 256 + (data[13] & 0x7f);
  std::string address = d->getAddress().toString();
  int rssi = d->getRSSI();
  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: type=SwitchBotPlugMini,address=%s,name=,rssi=%d,sw=%s,over=%s,load=%d", 21 * 8 + 6, getTimeStamp().c_str(), wifi.localIP().toString().c_str(), address.c_str(), rssi, sw, over, load);
  syslog.sendTo(msg, syslog_dst, syslog_port);
}

void checkSwitchBotEnv(BLEAdvertisedDevice *d) {
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
  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: type=SwitchBotEnv,address=%s,name=,rssi=%d,temp=%.02f,hum=%.02f,bat=%d", 21 * 8 + 6, getTimeStamp().c_str(), wifi.localIP().toString().c_str(), address.c_str(), rssi, temp, hum, bat);
  syslog.sendTo(msg, syslog_dst, syslog_port);
}

//
void checkBlueDevice(BLEAdvertisedDevice *d, int16_t code) {
  std::string address = d->getAddress().toString();
  esp_ble_addr_type_t at = d->getAddressType();
  const char *atStr = at == BLE_ADDR_TYPE_PUBLIC ? "Public" : at == BLE_ADDR_TYPE_RANDOM     ? "Random"
                                                            : at == BLE_ADDR_TYPE_RPA_PUBLIC ? "RPA Public"
                                                                                             : "RPA Random";
  int rssi = d->getRSSI();
  std::string name = d->getName();

  auto e = blueDeviceMap.find(address);
  if (e != blueDeviceMap.end()) {
    BlueDevice *p = e->second.get();
    p->update(rssi, ntp.getEpochTime());
  } else {
    blueDeviceMap[address] = std::unique_ptr<BlueDevice>(new BlueDevice(address, name, rssi, atStr, code, ntp.getEpochTime()));
  }
  e = blueDeviceMap.find(address);
  BlueDevice *p = e->second.get();
  AsyncUDPMessage msg;
  msg.printf("<%d>%s %s twBlueScan: %s", 21 * 8 + 6, getTimeStamp().c_str(), WiFi.localIP().toString().c_str(), p->get().c_str());
  syslog.sendTo(msg, syslog_dst, syslog_port);
}

void doBlueScan() {
  BLEScanResults foundDevices = pBLEScan->start(5);
  blueCount = foundDevices.getCount();
  omronCount = 0;
  switchBotCount = 0;
  for (int i = 0; i < blueCount; i++) {
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
          switchBotCount++;
          break;
        case 0x02d5:  // OMRON
          checkOMROM(&d);
          omronCount++;
          break;
        case 0x0059:
          checkSwitchBotEnv(&d);
          switchBotCount++;
          break;
        case 0x004c:
        case 0x0060:
          break;
        default:
          break;
      }
      checkBlueDevice(&d, code);
    } else {
      checkBlueDevice(&d, 0);
    }
  }
  Serial.printf("%s BlueScan device=%d omron=%d switchBot=%d\n",getTimeStamp().c_str(),blueCount,omronCount,switchBotCount);
  updateLCD(STATE_READY);
}

// Scan Wifi AP and send info to syslog
void doWifiAPScan() {
  wifiAPCount = wifi.scanNetworks();
  Serial.printf("%s WifiApScan count=%d\n",getTimeStamp().c_str(),wifiAPCount);
  updateLCD(STATE_READY);
  for (int i = 0; i < wifiAPCount; ++i) {
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
    syslog.sendTo(msg, syslog_dst, syslog_port);
  }
}

int retention = 24;
// cleanup old info
void cleanup() {
  time_t th = ntp.getEpochTime() - 3600 * retention;
  auto itb = blueDeviceMap.begin();
  int delCountBlue = 0;
  while (itb != blueDeviceMap.end()) {
    BlueDevice *p = itb->second.get();
    if (p->LastTime < th) {
      blueDeviceMap.erase(itb++);
      delCountBlue++;
    } else {
      ++itb;
    }
  }
  auto itw = wifiApInfoMap.begin();
  int delCountWifi = 0;
  while (itw != wifiApInfoMap.end()) {
    WifiAPInfo *p = itw->second.get();
    if (p->LastTime < th) {
      wifiApInfoMap.erase(itw++);
      delCountWifi++;
    } else {
      ++itw;
    }
  }
  if(delCountBlue > 0 || delCountWifi > 0) {
    Serial.printf("%s delete device blue=%d wifi=%d\n",getTimeStamp().c_str(),delCountBlue,delCountWifi);
  }
  int free = ESP.getFreeHeap();
  if (free < 1024 * 20 && retention > 1) {
    Serial.printf("%s dec retention=%d\n",getTimeStamp().c_str(), retention);
    retention--;
  }
}

// send monitor
void sendMonitor() {
  uint32_t free = ESP.getFreeHeap();
  uint32_t total = ESP.getHeapSize();
  cpu = 100.0 - (100.0 * idle) / tick;
  mem = total > 0 ? (100.0 - (100.0 * free) / total) : 0.0;
  tick = 0;
  idle = 0;
  AsyncUDPMessage msg;

  msg.printf("<%d>%s %s twESP32Sensor: count=%d,total=%d,free=%d,min=%d,mem=%.2f,cpu=%.2f", 21 * 8 + 6, getTimeStamp().c_str(), WiFi.localIP().toString().c_str(),
             count,
             total,
             free,
             ESP.getMinFreeHeap(),
             mem,
             cpu);
  syslog.sendTo(msg, syslog_dst, syslog_port);
  tick = 0;
  idle = 0;
  Serial.printf("%s monitor count=%d cpu=%.2f mem=%.2f\n",getTimeStamp().c_str(),count,cpu,mem);
  updateLCD(STATE_READY);
}

// Utils
// get string time stamp
std::string getTimeStamp() {
  char ts[128];
  time_t t;
  struct tm *tm;
  t = ntp.getEpochTime();
  tm = localtime(&t);
  strftime(ts, sizeof(ts), "%FT%X+09:00", tm);
  return std::string(ts);
}

// Input line form serial
String getInput(const char *msg,bool require) {
  String in = "";
  do {
    Serial.println(msg);
    while (Serial.available() < 1);
    in = Serial.readStringUntil('\n');
    in.trim();
    Serial.println(in.c_str());
  } while( in == "" && require);
  if( in == "cancel") {
    pref.begin("twESP32Config", false);
    pref.putBool("config", false);
    pref.end();
    Serial.printf("%s setup cancel and restart now\n",getTimeStamp().c_str());
    delay(1000);
    ESP.restart();
  }
  if( in == "clear!") {
    pref.begin("twESP32Config", false);
    pref.clear();
    pref.end();
    Serial.printf("%s config clear and restart now\n",getTimeStamp().c_str());
    delay(1000);
    ESP.restart();
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
      Serial.printf("%s restart now\n",getTimeStamp().c_str());
      delay(1000);
      ESP.restart();
    }
  }
}

void updateLCD(int state) {
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(5, 4);
  M5.Lcd.printf("twM5StickCP2Sensor v%s",VERSION);
  if (state == STATE_START) {
    M5.Lcd.setTextColor(SKYBLUE,BLACK);
    M5.Lcd.setCursor(5, 20);
    M5.Lcd.print("status:start");
    return;
  }
  if (state == STATE_SETUP) {
    M5.Lcd.setTextColor(YELLOW,BLACK);
    M5.Lcd.setCursor(5, 20);
    M5.Lcd.print("status:setup form serial");
    return;
  }
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setCursor(5, 36);
  M5.Lcd.printf("ssid  :%s",ssid.c_str());
  if (state == STATE_WIFI) {
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.setCursor(5, 20);
    M5.Lcd.print("status:connecting to wifi");
    return;
  }
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setCursor(5, 52);
  M5.Lcd.printf("ip    :%s",WiFi.localIP().toString().c_str());
  M5.Lcd.setCursor(5, 68);
  M5.Lcd.printf("syslog:%s:%d",syslog_ip.c_str(),syslog_port);
  M5.Lcd.setTextColor(SKYBLUE);
  M5.Lcd.setCursor(5, 20);
  M5.Lcd.print("status:ready");
  M5.Lcd.setTextColor(GREEN);

  M5.Lcd.setCursor(5, 84);
  M5.Lcd.printf("bledev:%d(omron=%d,swb=%d)",blueCount,omronCount,switchBotCount);
  M5.Lcd.setCursor(5, 100);
  M5.Lcd.printf("wifiap:%d",wifiAPCount);

  M5.Lcd.setCursor(5, 116);
  M5.Lcd.printf("mon   :cpu=%.2f%%,mem=%.2f%%",cpu,mem);
}