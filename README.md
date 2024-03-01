# twESP32Sensor
ESP32 based network sensor for TWSNMP

# Build

## Frimware

Open twESP32Sensor or twM5StickCP2Sensor by Arduino IDE
and build it.

## Setup tool

```
$cd twESPSetup
$task clean
$task
```


# Setup tool usage

```
Usage: twESP32Setup [options...] command
command
  list : list setial ports
  monitor : monitor serial port
  config : config ESP32
  write : write firmware to ESP32
  clear : clear config
  reset : reset ESP32
  merge : merge M5SickC Plus2 firmare
  version : show version

options
  -esptool string
    	path to esptool
  -m5
    	M5StickC Plus 2
  -password string
    	wifi password
  -port string
    	serila port name
  -ssid string
    	wifi ssid
  -syslogIP string
    	syslog dst ip
  -syslogPort int
    	syslog dst port (default 514)
```

# about esptool

See

https://docs.espressif.com/projects/esptool/

https://github.com/espressif/esptool/
