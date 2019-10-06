# WT0122-MQTT
WT0122 pool thermometer MQTT gateway

The goal of the project to integrate the WT0122 pool thermometer with Home Assistant.

# Needed components
 - WT0122 pool thermoeter:  it is using ASK/OOK to transmit the temperature value to the receiver(s).
 - Arduino Uno or ESP8266 (NodeMCU) - the code was tested with Arduino
 - 433 Mhz RF receiver - e.g. RXB08
 - 433Mhz antenna
 - Housing - e.g.: https://www.thingiverse.com/thing:2418114 
 - 5V power supply
 
# Connections
- connect the rf receiver's Data pin to the MCU IO pin (Arduino digital IO pin 3)
- connect the rf receiver's VCC pin to the MCU 5V pin
- connect the rf receiver's GND pin to the MCU GND pin
- connect the rf receiver's ANT pin to the antenna

 Before uploading the code please modify the network settings (IP, MAC, SSID, PWD) according to your notwork.
