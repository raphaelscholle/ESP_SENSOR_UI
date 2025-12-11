#pragma once

#include <Arduino.h>

struct SunlightSensorConfig {
  bool enabled = true;
  uint8_t address = 0x60;
  uint8_t sda = 6;
  uint8_t scl = 7;
};

struct DeviceConfig {
  String wifiSsid = "";
  String wifiPassword = "";
  String mqttHost = "";
  uint16_t mqttPort = 1883;
  String mqttUser = "";
  String mqttPassword = "";
  String baseTopic = "esp/sensors";
  uint8_t wsPin = 10;            // Onboard WS2812B pin on ESP32-C3-Zero
  uint16_t wsCount = 1;         // Single on-board pixel
  String wsTopic = "light/ws2812";
  String sunlightTopic = "sunlight";
  uint8_t i2cSda = 3;           // Default pins for ESP32-C3
  uint8_t i2cScl = 2;
  uint8_t sunlightCount = 1;
  SunlightSensorConfig sunlight[4];
};

String urlEncode(const String &value);

