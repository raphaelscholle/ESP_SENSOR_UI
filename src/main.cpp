#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SI1145.h>

struct DeviceConfig {
  String wifiSsid = "";
  String wifiPassword = "";
  String mqttHost = "";
  uint16_t mqttPort = 1883;
  String mqttUser = "";
  String mqttPassword = "";
  String baseTopic = "esp/sensors";
  uint8_t wsPin = 8;            // Onboard WS2812B pin on ESP32-C3-Zero
  uint16_t wsCount = 1;         // Single on-board pixel
  String wsTopic = "light/ws2812";
  String sunlightTopic = "sunlight";
  uint8_t i2cSda = 6;           // Default pins for ESP32-C3
  uint8_t i2cScl = 7;
};

static DeviceConfig config;
static Preferences preferences;
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static AsyncWebServer server(80);
static Adafruit_NeoPixel pixels(config.wsCount, config.wsPin, NEO_GRB + NEO_KHZ800);
static Adafruit_SI1145 sunlightSensor;
static bool sunlightReady = false;
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

static SensorReadings currentReadings;

// Forward declarations
void saveConfig();
void setupWebUi();
void connectWifi();
void connectMqtt();
void handleWsMessage(const String &payload);
void configureSunlightSensor();
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
  String lower = payload; lower.toLowerCase();
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
      for (uint16_t i = 0; i < pixels.numPixels(); i++) {
        pixels.setPixelColor(i, wsColor);
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
  if (!sunlightReady || !mqttClient.connected()) {
    return;
  }
  refreshSunlightReadings();

  String topicBase = config.baseTopic + "/" + config.sunlightTopic;
  mqttClient.publish((topicBase + "/visible").c_str(), String(currentReadings.visible).c_str(), true);
  mqttClient.publish((topicBase + "/ir").c_str(), String(currentReadings.ir).c_str(), true);
  mqttClient.publish((topicBase + "/uv").c_str(), String(currentReadings.uv).c_str(), true);
}

void refreshSunlightReadings() {
  if (!sunlightReady) {
    currentReadings = {};
    return;
  }

  currentReadings.visible = sunlightSensor.readVisible();
  currentReadings.ir = sunlightSensor.readIR();
  currentReadings.uv = sunlightSensor.readUV() / 100.0; // library returns *100
  currentReadings.timestamp = millis();
}

String generatePage() {
  String page;
  page.reserve(4000);
  page += R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>ESP32-C3 Sensor UI</title>
<style>
  body { font-family: 'Inter', system-ui, sans-serif; background: radial-gradient(circle at 10% 20%, #18152a, #0d0a1a 60%); color: #f2f2fb; margin: 0; padding: 0; }
  .shell { max-width: 1080px; margin: 32px auto; padding: 20px; }
  .card { background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08); border-radius: 18px; padding: 20px; box-shadow: 0 20px 60px rgba(0,0,0,0.3); backdrop-filter: blur(12px); }
  h1 { letter-spacing: 0.5px; font-size: 26px; margin-top: 0; display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
  h2 { color: #a7b9ff; text-transform: uppercase; letter-spacing: 1px; font-size: 12px; margin: 24px 0 12px; }
  form { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 14px; }
  label { font-size: 12px; color: #8ea0d0; text-transform: uppercase; letter-spacing: 0.5px; }
  input, select { width: 100%; padding: 12px; border-radius: 12px; border: 1px solid rgba(255,255,255,0.08); background: rgba(255,255,255,0.08); color: #fff; font-size: 14px; box-sizing: border-box; }
  input:focus, select:focus { outline: 2px solid #6b8cff; }
  .accent { color: #6bffdf; }
  .actions { margin-top: 16px; display: flex; gap: 12px; flex-wrap: wrap; }
  button { padding: 12px 18px; border-radius: 12px; border: none; color: #0b0b16; background: linear-gradient(120deg, #6bffdf, #6b8cff); font-weight: 700; letter-spacing: 0.5px; cursor: pointer; box-shadow: 0 12px 30px rgba(107, 143, 255, 0.3); }
  .badge { background: rgba(255,255,255,0.07); padding: 6px 10px; border-radius: 10px; display: inline-block; margin-left: 10px; font-size: 12px; color: #9bd4ff; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 16px; align-items: start; }
  .pill-nav { display: flex; gap: 10px; flex-wrap: wrap; margin: 0 0 12px; padding: 0; list-style: none; }
  .pill-nav a { text-decoration: none; color: #cdd7ff; padding: 8px 12px; border-radius: 999px; background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.08); display: inline-flex; gap: 8px; align-items: center; }
  .pill-nav a:hover { background: rgba(255,255,255,0.12); }
  .metric { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08); border-radius: 12px; padding: 14px; display: flex; flex-direction: column; gap: 4px; }
  .metric .value { font-size: 24px; font-weight: 700; color: #fff; }
  .metric small { color: #9ab4ff; }
  .status-dot { width: 10px; height: 10px; border-radius: 50%; background: #f39b39; display: inline-block; }
</style>
</head>
<body>
  <div class="shell">
    <div class="card">
      <h1>ESP32-C3 Sensor Studio <span class="badge">Web UI</span></h1>
      <ul class="pill-nav">
        <li><a href="#config">Configuration</a></li>
        <li><a href="#live">Live values</a></li>
        <li><a href="#lighting">Lighting</a></li>
      </ul>
      <p>Configure Wi-Fi, MQTT, and pins for the onboard <span class="accent">WS2812B</span> and <span class="accent">Grove Sunlight</span> modules without writing a line of code.</p>
      <h2 id="config">Connectivity</h2>
      <form method="POST" action="/config">
        <div>
          <label>Wi-Fi SSID</label>
          <input name="ssid" value=")HTML";
  page += urlEncode(config.wifiSsid);
  page += R"HTML(" placeholder="MyWiFi" />
        </div>
        <div>
          <label>Wi-Fi Password</label>
          <input name="wifipw" type="password" value=")HTML";
  page += urlEncode(config.wifiPassword);
  page += R"HTML(" placeholder="••••••" />
        </div>
        <div>
          <label>MQTT Host</label>
          <input name="mqhost" value=")HTML";
  page += urlEncode(config.mqttHost);
  page += R"HTML(" placeholder="192.168.1.10" />
        </div>
        <div>
          <label>MQTT Port</label>
          <input name="mqport" type="number" value=")HTML";
  page += String(config.mqttPort);
  page += R"HTML(" />
        </div>
        <div>
          <label>MQTT Username</label>
          <input name="mquser" value=")HTML";
  page += urlEncode(config.mqttUser);
  page += R"HTML(" />
        </div>
        <div>
          <label>MQTT Password</label>
          <input name="mqpw" type="password" value=")HTML";
  page += urlEncode(config.mqttPassword);
  page += R"HTML(" />
        </div>
        <div>
          <label>Base Topic</label>
          <input name="baset" value=")HTML";
  page += urlEncode(config.baseTopic);
  page += R"HTML(" />
        </div>

        <h2>WS2812B</h2>
        <div>
          <label>LED Pin</label>
          <input name="wspin" type="number" value=")HTML";
  page += String(config.wsPin);
  page += R"HTML(" />
        </div>
        <div>
          <label>LED Count</label>
          <input name="wscount" type="number" value=")HTML";
  page += String(config.wsCount);
  page += R"HTML(" />
        </div>
        <div>
          <label>LED Topic</label>
          <input name="wstopic" value=")HTML";
  page += urlEncode(config.wsTopic);
  page += R"HTML(" />
        </div>

        <h2>Grove Sunlight</h2>
        <div>
          <label>I2C SDA Pin</label>
          <input name="sdapin" type="number" value=")HTML";
  page += String(config.i2cSda);
  page += R"HTML(" />
        </div>
        <div>
          <label>I2C SCL Pin</label>
          <input name="sclpin" type="number" value=")HTML";
  page += String(config.i2cScl);
  page += R"HTML(" />
        </div>
        <div>
          <label>Sunlight Topic</label>
          <input name="suntopic" value=")HTML";
  page += urlEncode(config.sunlightTopic);
  page += R"HTML(" />
        </div>
        <div class="actions">
          <button type="submit">Save & Reboot</button>
        </div>
      </form>

      <h2 id="live">Live sensor values</h2>
      <div class="grid" id="sensor-grid">
        <div class="metric">
          <div>Visible light</div>
          <div class="value" id="vis-value">--</div>
          <small>lux</small>
        </div>
        <div class="metric">
          <div>Infrared</div>
          <div class="value" id="ir-value">--</div>
          <small>irradiance</small>
        </div>
        <div class="metric">
          <div>UV Index</div>
          <div class="value" id="uv-value">--</div>
          <small>index</small>
        </div>
        <div class="metric">
          <div>Sensor status</div>
          <div class="value"><span class="status-dot" id="sensor-status"></span> <span id="sensor-label">waiting...</span></div>
          <small>Updated <span id="sensor-time">never</span></small>
        </div>
      </div>

      <h2 id="lighting">LED animations</h2>
      <form id="light-form">
        <div>
          <label>Mode</label>
          <select name="mode" id="mode-select">
            <option value="off">Off</option>
            <option value="solid">Solid color</option>
            <option value="rainbow">Rainbow</option>
            <option value="breathe">Breathe</option>
          </select>
        </div>
        <div>
          <label>Color</label>
          <input type="color" name="color" id="color-picker" value="#6bffdf" />
        </div>
        <div class="actions">
          <button type="submit">Send to LED</button>
        </div>
      </form>
    </div>
  </div>
  <script>
    const statusDot = document.getElementById('sensor-status');
    const statusLabel = document.getElementById('sensor-label');
    const visEl = document.getElementById('vis-value');
    const irEl = document.getElementById('ir-value');
    const uvEl = document.getElementById('uv-value');
    const timeEl = document.getElementById('sensor-time');
    const lightForm = document.getElementById('light-form');
    const modeSelect = document.getElementById('mode-select');
    const colorPicker = document.getElementById('color-picker');

    async function fetchSensors() {
      try {
        const res = await fetch('/api/sensors');
        const json = await res.json();
        if (json.ready) {
          statusDot.style.background = '#4ade80';
          statusLabel.textContent = 'Streaming';
          visEl.textContent = json.visible.toFixed(1);
          irEl.textContent = json.ir.toFixed(1);
          uvEl.textContent = json.uv.toFixed(2);
          timeEl.textContent = (json.timestamp / 1000).toFixed(1) + 's';
        } else {
          statusDot.style.background = '#f59e0b';
          statusLabel.textContent = 'Sensor unavailable';
          visEl.textContent = irEl.textContent = uvEl.textContent = '--';
          timeEl.textContent = 'never';
        }
      } catch (e) {
        statusDot.style.background = '#ef4444';
        statusLabel.textContent = 'Fetch failed';
        console.error(e);
      }
    }

    lightForm.addEventListener('submit', async (e) => {
      e.preventDefault();
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
    String json = "{";
    json += "\"ready\":";
    json += sunlightReady ? "true" : "false";
    json += ",\"visible\":" + String(currentReadings.visible);
    json += ",\"ir\":" + String(currentReadings.ir);
    json += ",\"uv\":" + String(currentReadings.uv);
    json += ",\"timestamp\":" + String(currentReadings.timestamp);
    json += "}";
    request->send(200, "application/json", json);
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

    saveConfig();
    request->send(200, "text/html", "<html><body><h2>Saved!</h2><p>Rebooting...</p></body></html>");
    delay(1000);
    ESP.restart();
  });

  server.begin();
}

void configureSunlightSensor() {
  Wire.end();
  Wire.begin(config.i2cSda, config.i2cScl);
  sunlightReady = sunlightSensor.begin();
  if (!sunlightReady) {
    Serial.println("Sunlight sensor not detected. Check wiring and pins in UI.");
  } else {
    Serial.println("Sunlight sensor ready.");
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
  configureSunlightSensor();
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
