package main

import (
	"flag"
	"fmt"
	"io"
	"log"
	"os/exec"
	"strings"
	"time"

	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

var version = "vx.x.x"
var commit = ""

var esptool = ""
var serialPort = ""
var ssid = ""
var password = ""
var syslogIP = ""
var m5stick = false
var syslogPort = 0

func init() {
	flag.StringVar(&esptool, "esptool", "", "path to esptool")
	flag.StringVar(&serialPort, "port", "", "serila port name")
	flag.StringVar(&ssid, "ssid", "", "wifi ssid")
	flag.StringVar(&password, "password", "", "wifi password")
	flag.StringVar(&syslogIP, "syslogIP", "", "syslog dst ip")
	flag.IntVar(&syslogPort, "syslogPort", 514, "syslog dst port")
	flag.BoolVar(&m5stick, "m5", false, "M5StickC Plus 2")
	flag.Parse()
}

func main() {
	cmd := flag.Arg(0)
	switch cmd {
	case "list":
		listSerialPort()
	case "monitor":
		if err := monitorESP32(); err != nil {
			log.Fatalln(err)
		}
	case "config":
		if err := configESP32(); err != nil {
			log.Fatalln(err)
		}
	case "clear":
		if err := clearESP32(); err != nil {
			log.Fatalln(err)
		}
	case "write":
		if err := writeESP32(); err != nil {
			log.Fatalln(err)
		}
	default:
		fmt.Println("invalid command(list|monitor|config|write|clear)")
		flag.Usage()
	}
}

func listSerialPort() {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		log.Fatal(err)
	}
	for _, port := range ports {
		if port.IsUSB {
			fmt.Printf("%s (%s/%s:%s)\n", port.Name, port.VID, port.PID, port.SerialNumber)
		}
	}
}

// monitor ESP32 form serial port
func monitorESP32() error {
	mode := &serial.Mode{
		BaudRate: 115200,
	}
	p, err := serial.Open(serialPort, mode)
	if err != nil {
		return err
	}
	defer p.Close()
	for {
		line, err := readLine(p)
		if err != nil {
			return err
		}
		fmt.Printf("'%s'\n", line)
	}
}

// config ESP32 form serial port
func configESP32() error {
	if ssid == "" {
		return fmt.Errorf("no ssid")
	}
	if syslogIP == "" {
		return fmt.Errorf("no syslog ip")
	}
	mode := &serial.Mode{
		BaudRate: 115200,
	}
	p, err := serial.Open(serialPort, mode)
	if err != nil {
		return err
	}
	defer p.Close()
	p.SetReadTimeout(time.Second * 10)
	done := false
	for i := 0; i < 5; {
		line, err := readLine(p)
		if err != nil {
			return err
		}
		fmt.Println(line)
		switch line {
		case "":
			i++
		case "enter ssid:":
			p.Write([]byte(ssid + "\n"))
		case "enter password:":
			p.Write([]byte(password + "\n"))
		case "enter syslog ip:":
			p.Write([]byte(syslogIP + "\n"))
		case "enter syslog port:":
			p.Write([]byte(fmt.Sprintf("%d\n", syslogPort)))
			done = true
		default:
			if line == "setup end" || strings.HasPrefix(line, "Config ") {
				if done {
					return nil
				}
				time.Sleep(time.Second * 2)
				p.Write([]byte("config\n"))
				i++
			}
		}
	}
	return nil
}

// clear ESP32 config form serial port
func clearESP32() error {
	mode := &serial.Mode{
		BaudRate: 115200,
	}
	p, err := serial.Open(serialPort, mode)
	if err != nil {
		return err
	}
	defer p.Close()
	p.SetReadTimeout(time.Second * 10)
	for i := 0; i < 5; {
		line, err := readLine(p)
		if err != nil {
			return err
		}
		fmt.Println(line)
		switch line {
		case "":
			i++
		case "enter ssid:", "enter password:":
			p.Write([]byte("clear!\n"))
			return nil
		default:
			if line == "setup end" || strings.HasPrefix(line, "Config") {
				p.Write([]byte("config\n"))
				i++
			}
		}
	}
	return nil
}

func readLine(p serial.Port) (string, error) {
	buff := make([]byte, 1)
	r := ""
	for {
		n, err := p.Read(buff)
		if err != nil {
			return "", err
		}
		if n == 0 {
			return r, nil
		}
		if buff[0] == 0x0a || buff[0] == 0x0d {
			if r != "" {
				return r, nil
			}
			continue
		}
		r += string(buff[:n])
	}
}

// write firmware to ESP32
func writeESP32() error {
	if esptool == "" {
		esptool = findESPTool()
		if esptool == "" {
			return fmt.Errorf("esptool not found")
		}
	}
	name := esptool
	args := []string{}
	if strings.HasSuffix(esptool, ".py") {
		if p, err := exec.LookPath("python"); err == nil {
			name = p
		} else {
			if p, err := exec.LookPath("python3"); err == nil {
				name = p
			} else {
				return fmt.Errorf("python not found")
			}
		}
		args = append(args, esptool)
	}
	/*
	   ESP32
	   --chip esp32 --port "/dev/cu.wchusbserial146440" --baud 115200
	   --before default_reset --after hard_reset write_flash  -z --flash_mode dio
	   --flash_freq 80m --flash_size 4MB
	   0x1000 "/private/var/folders/v5/znj_wgb92xld9h4yjbtyhzwr0000gn/T/arduino/sketches/43AA7BA82766B413A228A093B0F982B3/twESP32Sensor.ino.bootloader.bin"
	   0x8000 "/private/var/folders/v5/znj_wgb92xld9h4yjbtyhzwr0000gn/T/arduino/sketches/43AA7BA82766B413A228A093B0F982B3/twESP32Sensor.ino.partitions.bin"
	   0xe000 "/Users/ymimacmini/Library/Arduino15/packages/esp32/hardware/esp32/2.0.14/tools/partitions/boot_app0.bin"
	   0x10000 "/private/var/folders/v5/znj_wgb92xld9h4yjbtyhzwr0000gn/T/arduino/sketches/43AA7BA82766B413A228A093B0F982B3/twESP32Sensor.ino.bin"

	   M5Stick
	   --chip esp32 --port "/dev/cu.wchusbserial57710041991" --baud 1500000
	   --before default_reset --after hard_reset write_flash -z --flash_mode dio
	   --flash_freq 80m --flash_size 8MB
	   0x10000 "/private/var/folders/v5/znj_wgb92xld9h4yjbtyhzwr0000gn/T/arduino/sketches/B8728CE2975CDDA79B8A77C0D5308567/twM5StickCP2Sensor.ino.bin"
	*/

	args = append(args, "--chip")
	args = append(args, "esp32")
	args = append(args, "--port")
	args = append(args, serialPort)
	if m5stick {
		args = append(args, "--baud")
		args = append(args, "1500000")
	} else {
		args = append(args, "--baud")
		args = append(args, "115200")
	}
	args = append(args, "--before")
	args = append(args, "default_reset")
	args = append(args, "--after")
	args = append(args, "hard_reset")
	args = append(args, "write_flash")
	args = append(args, "-z")
	args = append(args, "--flash_mode")
	args = append(args, "dio")
	args = append(args, "--flash_freq")
	args = append(args, "80m")
	if m5stick {
		args = append(args, "--flash_size")
		args = append(args, "8MB")
		args = append(args, "0x10000")
		args = append(args, "./twM5StickCP2Sensor.ino.bin")
	} else {
		args = append(args, "--flash_size")
		args = append(args, "4MB")
		args = append(args, "0x1000")
		args = append(args, "./twESP32Sensor.ino.bootloader.bin")
		args = append(args, "0x8000")
		args = append(args, "./twESP32Sensor.ino.partitions.bin")
		args = append(args, "0xe000")
		args = append(args, "./boot_app0.bin")
		args = append(args, "0x10000")
		args = append(args, "./twESP32Sensor.ino.bin")
	}
	log.Println(name, args)
	cmd := exec.Command(name, args...)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return err
	}
	err = cmd.Start()
	if err != nil {
		return err
	}
	go dumpOutput(stdout)
	go dumpOutput(stderr)
	cmd.Wait()
	return nil
}

// dumpOutput show stdout and stderr
func dumpOutput(r io.ReadCloser) {
	buff := make([]byte, 1024)
	for {
		n, err := r.Read(buff)
		if err != nil {
			return
		}
		if n > 0 {
			fmt.Print(string(buff[:n]))
		}
	}
}

func findESPTool() string {
	if p, err := exec.LookPath("esptool"); err == nil {
		return p
	}
	if p, err := exec.LookPath("./esptool"); err == nil {
		return p
	}
	return ""
}