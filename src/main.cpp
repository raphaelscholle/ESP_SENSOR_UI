#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SI1145.h>

#include <algorithm>
#include <array>

#include "main.h"
#include "menu_home.h"
#include "menu_lighting.h"
#include "menu_mqtt.h"
#include "menu_nav.h"
#include "menu_sensors.h"
#include "menu_wifi.h"
#include "ui_styles.h"

static const size_t kMaxSunlightSensors = 4;
static DeviceConfig config;
static Preferences preferences;
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static AsyncWebServer server(80);
static Adafruit_NeoPixel pixels(config.wsCount, config.wsPin, NEO_GRB + NEO_KHZ800);
static std::array<Adafruit_SI1145, kMaxSunlightSensors> sunlightSensors;
static std::array<bool, kMaxSunlightSensors> sunlightReady;

static unsigned long lastPublish = 0;
static unsigned long lastSensorRead = 0;
static const unsigned long publishInterval = 10 * 1000UL;
static const unsigned long sensorRefreshInterval = 2000UL;
static const char *apSsid = "ESP-Sensor-UI";
static const char *apPassword = "configureme";

enum class WsMode {
  OFF,
  SOLID,
  RAINBOW,
  BREATHE
};

static WsMode wsMode = WsMode::OFF;
static uint32_t wsColor = 0;
static uint32_t effectStart = 0;

struct SensorReadings {
  float visible = NAN;
  float ir = NAN;
  float uv = NAN;
  unsigned long timestamp = 0;
};

static std::array<SensorReadings, kMaxSunlightSensors> sensorReadings;

template <typename T>
T clampValue(T value, T minVal, T maxVal) {
  if (value < minVal) return minVal;
  if (value > maxVal) return maxVal;
  return value;
}

// Forward declarations
void saveConfig();
void setupWebUi();
void connectWifi();
void connectMqtt();
void handleWsMessage(const String &payload);
void configureSunlightSensors();
void publishSunlight();
void refreshSunlightReadings();
void updatePixels();

String urlEncode(const String &value) {
  String encoded = "";
  for (char c : value) {
    if (isalnum(c)) {
      encoded += c;
    } else {
      encoded += '%';
      char hex[3];
      snprintf(hex, sizeof(hex), "%02X", static_cast<uint8_t>(c));
      encoded += hex;
    }
  }
  return encoded;
}

void loadConfig() {
  preferences.begin("cfg", true);
  config.wifiSsid = preferences.getString("ssid", config.wifiSsid);
  config.wifiPassword = preferences.getString("pass", config.wifiPassword);
  config.mqttHost = preferences.getString("mqttHost", config.mqttHost);
  config.mqttPort = preferences.getUShort("mqttPort", config.mqttPort);
  config.mqttUser = preferences.getString("mqttUser", config.mqttUser);
  config.mqttPassword = preferences.getString("mqttPwd", config.mqttPassword);
  config.baseTopic = preferences.getString("baseTopic", config.baseTopic);
  config.wsPin = preferences.getUChar("wsPin", config.wsPin);
  config.wsCount = preferences.getUShort("wsCnt", config.wsCount);
  config.wsTopic = preferences.getString("wsTopic", config.wsTopic);
  config.sunlightTopic = preferences.getString("sunTopic", config.sunlightTopic);
  config.i2cSda = preferences.getUChar("i2cSda", config.i2cSda);
  config.i2cScl = preferences.getUChar("i2cScl", config.i2cScl);
  config.sunlightCount = clampValue<uint8_t>(preferences.getUChar("sunCnt", config.sunlightCount), 1, kMaxSunlightSensors);

  for (size_t i = 0; i < kMaxSunlightSensors; ++i) {
    String base = "sun" + String(i);
    config.sunlight[i].enabled = preferences.getBool((base + "En").c_str(), i == 0);
    config.sunlight[i].address = preferences.getUChar((base + "Ad").c_str(), 0x60);
    config.sunlight[i].sda = preferences.getUChar((base + "Sda").c_str(), config.i2cSda);
    config.sunlight[i].scl = preferences.getUChar((base + "Scl").c_str(), config.i2cScl);
  }
  preferences.end();
}

void saveConfig() {
  preferences.begin("cfg", false);
  preferences.putString("ssid", config.wifiSsid);
  preferences.putString("pass", config.wifiPassword);
  preferences.putString("mqttHost", config.mqttHost);
  preferences.putUShort("mqttPort", config.mqttPort);
  preferences.putString("mqttUser", config.mqttUser);
  preferences.putString("mqttPwd", config.mqttPassword);
  preferences.putString("baseTopic", config.baseTopic);
  preferences.putUChar("wsPin", config.wsPin);
  preferences.putUShort("wsCnt", config.wsCount);
  preferences.putString("wsTopic", config.wsTopic);
  preferences.putString("sunTopic", config.sunlightTopic);
  preferences.putUChar("i2cSda", config.i2cSda);
  preferences.putUChar("i2cScl", config.i2cScl);
  preferences.putUChar("sunCnt", config.sunlightCount);

  for (size_t i = 0; i < kMaxSunlightSensors; ++i) {
    String base = "sun" + String(i);
    preferences.putBool((base + "En").c_str(), config.sunlight[i].enabled);
    preferences.putUChar((base + "Ad").c_str(), config.sunlight[i].address);
    preferences.putUChar((base + "Sda").c_str(), config.sunlight[i].sda);
    preferences.putUChar((base + "Scl").c_str(), config.sunlight[i].scl);
  }
  preferences.end();
}

void setStatusLED(uint32_t color) {
  pixels.clear();
  pixels.setPixelColor(0, color);
  pixels.show();
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, apPassword);
  Serial.printf("Started config AP '%s' password '%s'\n", apSsid, apPassword);
  setStatusLED(pixels.Color(0, 0, 32));
}

void connectWifi() {
  if (config.wifiSsid.isEmpty()) {
    Serial.println("No WiFi credentials, starting AP mode");
    startAccessPoint();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  Serial.printf("Connecting to WiFi %s...\n", config.wifiSsid.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    setStatusLED(pixels.Color(0, 32, 0));
  } else {
    Serial.println("WiFi connect failed, enabling AP for configuration");
    startAccessPoint();
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }
  String topicStr(topic);
  String wsControlTopic = config.baseTopic + "/" + config.wsTopic + "/set";
  if (topicStr == wsControlTopic) {
    handleWsMessage(message);
  }
}

void connectMqtt() {
  if (config.mqttHost.isEmpty()) {
    return;
  }
  mqttClient.setServer(config.mqttHost.c_str(), config.mqttPort);
  mqttClient.setCallback(mqttCallback);

  if (mqttClient.connected()) {
    return;
  }

  Serial.printf("Connecting to MQTT %s:%u...\n", config.mqttHost.c_str(), config.mqttPort);
  String clientId = "ESP32C3-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqttClient.connect(clientId.c_str(), config.mqttUser.c_str(), config.mqttPassword.c_str())) {
    Serial.println("MQTT connected");
    String wsControlTopic = config.baseTopic + "/" + config.wsTopic + "/set";
    mqttClient.subscribe(wsControlTopic.c_str());
    mqttClient.publish((config.baseTopic + "/status").c_str(), "online", true);
  } else {
    Serial.printf("MQTT connection failed, rc=%d\n", mqttClient.state());
  }
}

uint32_t parseColor(const String &value) {
  String hex = value;
  if (hex.startsWith("#")) {
    hex.remove(0, 1);
  }
  if (hex.length() != 6) {
    return 0;
  }
  uint32_t color = strtoul(hex.c_str(), nullptr, 16);
  return color;
}

void handleWsMessage(const String &payload) {
  String lower = payload;
  lower.toLowerCase();
  if (lower.startsWith("color:")) {
    wsMode = WsMode::SOLID;
    wsColor = parseColor(payload.substring(6));
  } else if (lower.startsWith("rainbow")) {
    wsMode = WsMode::RAINBOW;
    effectStart = millis();
  } else if (lower.startsWith("breathe:")) {
    wsMode = WsMode::BREATHE;
    wsColor = parseColor(payload.substring(8));
    effectStart = millis();
  } else if (lower.startsWith("off")) {
    wsMode = WsMode::OFF;
    wsColor = 0;
  }
  updatePixels();
}

void updatePixels() {
  switch (wsMode) {
    case WsMode::OFF:
      pixels.clear();
      break;
    case WsMode::SOLID:
      uint8_t solidR = (wsColor >> 16) & 0xFF;
      uint8_t solidG = (wsColor >> 8) & 0xFF;
      uint8_t solidB = wsColor & 0xFF;
      uint32_t solidColor = pixels.Color(solidR, solidG, solidB);
      for (uint16_t i = 0; i < pixels.numPixels(); i++) {
        pixels.setPixelColor(i, solidColor);
      }
      break;
    case WsMode::RAINBOW: {
      uint32_t now = millis();
      for (uint16_t i = 0; i < pixels.numPixels(); i++) {
        uint8_t wheelPos = ((i * 256 / pixels.numPixels()) + ((now - effectStart) / 10)) & 255;
        uint32_t color;
        if (wheelPos < 85) {
          color = pixels.Color(wheelPos * 3, 255 - wheelPos * 3, 0);
        } else if (wheelPos < 170) {
          wheelPos -= 85;
          color = pixels.Color(255 - wheelPos * 3, 0, wheelPos * 3);
        } else {
          wheelPos -= 170;
          color = pixels.Color(0, wheelPos * 3, 255 - wheelPos * 3);
        }
        pixels.setPixelColor(i, color);
      }
      break;
    }
    case WsMode::BREATHE: {
      uint32_t now = millis();
      float phase = (sin((now - effectStart) / 1000.0f * PI) + 1) / 2.0f;
      uint8_t r = (uint8_t)(((wsColor >> 16) & 0xFF) * phase);
      uint8_t g = (uint8_t)(((wsColor >> 8) & 0xFF) * phase);
      uint8_t b = (uint8_t)((wsColor & 0xFF) * phase);
      for (uint16_t i = 0; i < pixels.numPixels(); i++) {
        pixels.setPixelColor(i, pixels.Color(r, g, b));
      }
      break;
    }
  }
  pixels.show();
}

void publishSunlight() {
  if (!mqttClient.connected()) {
    return;
  }

  refreshSunlightReadings();
  for (size_t i = 0; i < config.sunlightCount && i < kMaxSunlightSensors; ++i) {
    if (!config.sunlight[i].enabled || !sunlightReady[i]) {
      continue;
    }
    String topicBase = config.baseTopic + "/" + config.sunlightTopic + "/" + String(i + 1);
    mqttClient.publish((topicBase + "/visible").c_str(), String(sensorReadings[i].visible).c_str(), true);
    mqttClient.publish((topicBase + "/ir").c_str(), String(sensorReadings[i].ir).c_str(), true);
    mqttClient.publish((topicBase + "/uv").c_str(), String(sensorReadings[i].uv).c_str(), true);
  }
}

void refreshSunlightReadings() {
  for (size_t i = 0; i < kMaxSunlightSensors; ++i) {
    sensorReadings[i] = {};
  }

  for (size_t i = 0; i < config.sunlightCount && i < kMaxSunlightSensors; ++i) {
    const auto &cfg = config.sunlight[i];
    if (!cfg.enabled || !sunlightReady[i]) {
      continue;
    }
    Wire.begin(cfg.sda, cfg.scl);
    sensorReadings[i].visible = sunlightSensors[i].readVisible();
    sensorReadings[i].ir = sunlightSensors[i].readIR();
    sensorReadings[i].uv = sunlightSensors[i].readUV() / 100.0; // library returns *100
    sensorReadings[i].timestamp = millis();
  }
}

String buildSensorsJson() {
  String json = "{\"sensors\":[";
  for (size_t i = 0; i < config.sunlightCount && i < kMaxSunlightSensors; ++i) {
    if (i > 0) json += ',';
    bool enabled = config.sunlight[i].enabled;
    bool ready = sunlightReady[i];
    json += "{";
    json += "\"index\":" + String(i + 1);
    json += ",\"enabled\":" + String(enabled ? "true" : "false");
    json += ",\"ready\":" + String(ready ? "true" : "false");
    json += ",\"visible\":" + String(sensorReadings[i].visible);
    json += ",\"ir\":" + String(sensorReadings[i].ir);
    json += ",\"uv\":" + String(sensorReadings[i].uv);
    json += ",\"timestamp\":" + String(sensorReadings[i].timestamp);
    json += "}";
  }
  json += "]}";
  return json;
}

String generatePage() {
  String page;
  page.reserve(7000);
  page += R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>ESP32-C3 Sensor UI</title>
<style>
)HTML";
  page += FPSTR(kBaseStyles);
  page += R"HTML(
</style>
</head>
<body>
  <div class="shell">
    <div class="card">
      <h1>ESP32-C3 Sensor Studio</h1>
      )HTML";

  appendNavigation(page, std::min<size_t>(config.sunlightCount, kMaxSunlightSensors));

  page += R"HTML(
      <form id="config-form" method="POST" action="/config">
)HTML";
  appendHomeMenu(page);
  appendWifiMenu(page, config);
  appendMqttMenu(page, config);
  appendLightingMenu(page, config);
  appendSensorsMenu(page, config);

  for (size_t i = 0; i < config.sunlightCount && i < kMaxSunlightSensors; ++i) {
    appendSensorDetailMenu(page, config, i);
  }

  page += R"HTML(
        <div class="actions">
          <button type="submit">Save & Reboot</button>
        </div>
      </form>
    </div>
  </div>
  <script>
    const navLinks = Array.from(document.querySelectorAll('#nav a'));
    const pages = Array.from(document.querySelectorAll('.page'));

    function showPage(pageId) {
      pages.forEach(p => p.classList.toggle('active', p.dataset.page === pageId));
      navLinks.forEach(l => l.classList.toggle('active', l.dataset.page === pageId));
    }

    navLinks.forEach(link => {
      link.addEventListener('click', (e) => {
        e.preventDefault();
        showPage(link.dataset.page);
      });
    });

    // Sensor live data
    const liveGrid = document.getElementById('live-grid');

    function renderSensors(sensors) {
      liveGrid.innerHTML = '';
      sensors.forEach(sensor => {
        const card = document.createElement('div');
        card.className = 'metric';
        const statusColor = !sensor.enabled ? '#64748b' : sensor.ready ? '#4ade80' : '#f59e0b';
        card.innerHTML = `
          <div>Sunlight #${sensor.index} <span class="status-dot" style="background:${statusColor}"></span></div>
          <div class="value">${sensor.enabled && sensor.ready ? sensor.visible.toFixed(1) + ' lux' : '--'}</div>
          <small>IR: ${sensor.enabled && sensor.ready ? sensor.ir.toFixed(1) : '--'} · UV: ${sensor.enabled && sensor.ready ? sensor.uv.toFixed(2) : '--'}</small>
          <small>Updated ${sensor.enabled && sensor.ready ? (sensor.timestamp / 1000).toFixed(1) + 's' : 'n/a'}</small>
        `;
        liveGrid.appendChild(card);
      });
    }

    async function fetchSensors() {
      try {
        const res = await fetch('/api/sensors');
        const json = await res.json();
        renderSensors(json.sensors || []);
      } catch (e) {
        console.error(e);
      }
    }

    const lightSubmit = document.getElementById('light-submit');
    const modeSelect = document.getElementById('mode-select');
    const colorPicker = document.getElementById('color-picker');

    lightSubmit.addEventListener('click', async () => {
      const body = new URLSearchParams();
      body.set('mode', modeSelect.value);
      body.set('color', colorPicker.value);
      try {
        await fetch('/api/light', { method: 'POST', body });
      } catch (err) {
        console.error(err);
      }
    });

    fetchSensors();
    setInterval(fetchSensors, 3000);
  </script>
</body>
</html>
)HTML";
  return page;
}

void setupWebUi() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", generatePage());
  });

  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
    refreshSunlightReadings();
    request->send(200, "application/json", buildSensorsJson());
  });

  server.on("/api/light", HTTP_POST, [](AsyncWebServerRequest *request) {
    auto arg = [&](const String &name) { return request->arg(name); };
    String mode = arg("mode");
    String color = arg("color");

    mode.toLowerCase();
    if (mode == "solid") {
      wsMode = WsMode::SOLID;
      wsColor = parseColor(color);
    } else if (mode == "rainbow") {
      wsMode = WsMode::RAINBOW;
    } else if (mode == "breathe") {
      wsMode = WsMode::BREATHE;
      wsColor = parseColor(color);
    } else {
      wsMode = WsMode::OFF;
      wsColor = 0;
    }
    effectStart = millis();
    updatePixels();

    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    auto arg = [&](const String &name) { return request->arg(name); };
    config.wifiSsid = arg("ssid");
    config.wifiPassword = arg("wifipw");
    config.mqttHost = arg("mqhost");
    config.mqttPort = arg("mqport").toInt();
    config.mqttUser = arg("mquser");
    config.mqttPassword = arg("mqpw");
    config.baseTopic = arg("baset");
    config.wsPin = static_cast<uint8_t>(arg("wspin").toInt());
    config.wsCount = static_cast<uint16_t>(arg("wscount").toInt());
    config.wsTopic = arg("wstopic");
    config.i2cSda = static_cast<uint8_t>(arg("sdapin").toInt());
    config.i2cScl = static_cast<uint8_t>(arg("sclpin").toInt());
    config.sunlightTopic = arg("suntopic");

    uint8_t requestedCount = clampValue<uint8_t>(arg("sun_count").toInt(), 1, kMaxSunlightSensors);
    config.sunlightCount = requestedCount;

    for (size_t i = 0; i < kMaxSunlightSensors; ++i) {
      String idx = String(i);
      String enKey = "sun_en" + idx;
      bool enabled = request->hasArg(enKey.c_str()) && request->arg(enKey.c_str()) == "on";
      config.sunlight[i].enabled = enabled;
      config.sunlight[i].address = static_cast<uint8_t>(arg("sun_addr" + idx).toInt());
      config.sunlight[i].sda = static_cast<uint8_t>(arg("sun_sda" + idx).toInt());
      config.sunlight[i].scl = static_cast<uint8_t>(arg("sun_scl" + idx).toInt());
    }

    saveConfig();
    request->send(200, "text/html", "<html><body><h2>Saved!</h2><p>Rebooting...</p></body></html>");
    delay(1000);
    ESP.restart();
  });

  server.begin();
}

void configureSunlightSensors() {
  sunlightReady.fill(false);
  for (auto &reading : sensorReadings) {
    reading = {};
  }

  for (size_t i = 0; i < config.sunlightCount && i < kMaxSunlightSensors; ++i) {
    const auto &cfg = config.sunlight[i];
    if (!cfg.enabled) {
      continue;
    }
    Wire.end();
    Wire.begin(cfg.sda, cfg.scl);
    sunlightReady[i] = sunlightSensors[i].begin(cfg.address);
    if (sunlightReady[i]) {
      Serial.printf("Sunlight sensor %u ready on addr 0x%02X (SDA %u / SCL %u)\n", static_cast<unsigned>(i + 1), cfg.address, cfg.sda, cfg.scl);
    } else {
      Serial.printf("Sunlight sensor %u not detected. Address 0x%02X\n", static_cast<unsigned>(i + 1), cfg.address);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  loadConfig();
  pixels.updateLength(config.wsCount);
  pixels.setPin(config.wsPin);
  pixels.begin();
  pixels.setBrightness(64);
  setStatusLED(pixels.Color(16, 0, 16));

  connectWifi();
  configureSunlightSensors();
  setupWebUi();
  connectMqtt();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED && WiFi.getMode() != WIFI_AP_STA) {
    connectWifi();
  }
  if (!mqttClient.connected()) {
    connectMqtt();
  }
  mqttClient.loop();

  unsigned long now = millis();
  if (now - lastPublish > publishInterval) {
    lastPublish = now;
    publishSunlight();
  }

  if (now - lastSensorRead > sensorRefreshInterval) {
    lastSensorRead = now;
    refreshSunlightReadings();
  }

  updatePixels();
  delay(10);
}
