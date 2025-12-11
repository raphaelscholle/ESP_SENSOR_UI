#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SI1145.h>

#include <array>

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
  :root { color-scheme: dark; }
  body { font-family: 'Inter', system-ui, sans-serif; background: radial-gradient(circle at 10% 20%, #18152a, #0d0a1a 60%); color: #f2f2fb; margin: 0; padding: 0; }
  .shell { max-width: 1200px; margin: 32px auto; padding: 20px; }
  .card { background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08); border-radius: 18px; padding: 20px; box-shadow: 0 20px 60px rgba(0,0,0,0.3); backdrop-filter: blur(12px); }
  h1 { letter-spacing: 0.5px; font-size: 26px; margin-top: 0; display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
  h2 { color: #a7b9ff; text-transform: uppercase; letter-spacing: 1px; font-size: 12px; margin: 24px 0 12px; }
  form { display: grid; gap: 16px; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 14px; }
  label { font-size: 12px; color: #8ea0d0; text-transform: uppercase; letter-spacing: 0.5px; }
  input, select { width: 100%; padding: 12px; border-radius: 12px; border: 1px solid rgba(255,255,255,0.08); background: rgba(255,255,255,0.08); color: #fff; font-size: 14px; box-sizing: border-box; }
  input:focus, select:focus { outline: 2px solid #6b8cff; }
  .accent { color: #6bffdf; }
  .actions { margin-top: 16px; display: flex; gap: 12px; flex-wrap: wrap; }
  button { padding: 12px 18px; border-radius: 12px; border: none; color: #0b0b16; background: linear-gradient(120deg, #6bffdf, #6b8cff); font-weight: 700; letter-spacing: 0.5px; cursor: pointer; box-shadow: 0 12px 30px rgba(107, 143, 255, 0.3); }
  .badge { background: rgba(255,255,255,0.07); padding: 6px 10px; border-radius: 10px; display: inline-block; margin-left: 10px; font-size: 12px; color: #9bd4ff; }
  .pill-nav { display: flex; gap: 10px; flex-wrap: wrap; margin: 0 0 12px; padding: 0; list-style: none; }
  .pill-nav a { text-decoration: none; color: #cdd7ff; padding: 8px 12px; border-radius: 999px; background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.08); display: inline-flex; gap: 8px; align-items: center; }
  .pill-nav a.active, .pill-nav a:hover { background: rgba(255,255,255,0.12); }
  .metric { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08); border-radius: 12px; padding: 14px; display: flex; flex-direction: column; gap: 4px; }
  .metric .value { font-size: 22px; font-weight: 700; color: #fff; }
  .metric small { color: #9ab4ff; }
  .status-dot { width: 10px; height: 10px; border-radius: 50%; background: #f39b39; display: inline-block; }
  .page { display: none; }
  .page.active { display: block; }
  .draggable-area { position: relative; min-height: 340px; border: 1px dashed rgba(255,255,255,0.2); border-radius: 16px; overflow: hidden; padding: 12px; }
  .widget { position: absolute; top: 20px; left: 20px; padding: 12px 14px; background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.14); border-radius: 12px; cursor: grab; user-select: none; }
  .widget:active { cursor: grabbing; }
  .switch-row { display: flex; align-items: center; gap: 10px; }
</style>
</head>
<body>
  <div class="shell">
    <div class="card">
      <h1>ESP32-C3 Sensor Studio <span class="badge">Multi-page UI</span></h1>
      <ul class="pill-nav" id="nav">
        <li><a href="#home" data-page="home" class="active">Home</a></li>
        <li><a href="#wifi" data-page="wifi">Wi-Fi</a></li>
        <li><a href="#mqtt" data-page="mqtt">MQTT</a></li>
        <li><a href="#lighting" data-page="lighting">Lighting</a></li>
        <li><a href="#sensors" data-page="sensors">Sensors</a></li>
)HTML";

  for (size_t i = 0; i < config.sunlightCount && i < kMaxSunlightSensors; ++i) {
    page += "        <li><a href=\"#sensor" + String(i + 1) + "\" data-page=\"sensor" + String(i + 1) + "\">Sunlight " + String(i + 1) + "</a></li>\n";
  }

  page += R"HTML(
      </ul>

      <form id="config-form" method="POST" action="/config">
        <section class="page active" data-page="home">
          <h2>Draggable widget canvas</h2>
          <p>Drop and rearrange sensor summary widgets. Live data updates every few seconds.</p>
          <div class="draggable-area" id="widget-area">
            <div class="widget" draggable="true" style="top:18px; left:18px;" data-widget="summary">Status widget</div>
            <div class="widget" draggable="true" style="top:120px; left:120px;" data-widget="light">Lighting widget</div>
          </div>
          <h2>Live sensor values</h2>
          <div class="grid" id="live-grid"></div>
        </section>

        <section class="page" data-page="wifi">
          <h2>Connectivity</h2>
          <div class="grid">
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
          </div>
        </section>

        <section class="page" data-page="mqtt">
          <h2>MQTT Broker</h2>
          <div class="grid">
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
          </div>
        </section>

        <section class="page" data-page="lighting">
          <h2>WS2812B setup</h2>
          <div class="grid">
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
          </div>
          <h2>LED animations</h2>
          <div id="light-form">
            <div class="grid">
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
            </div>
            <div class="actions">
              <button type="button" id="light-submit">Send to LED</button>
            </div>
          </div>
        </section>

        <section class="page" data-page="sensors">
          <h2>Sensor overview</h2>
          <p>Set how many Grove Sunlight sensors you have connected and provide defaults for addressing.</p>
          <div class="grid">
            <div>
              <label>Number of sensors</label>
              <input name="sun_count" type="number" min="1" max="4" value=")HTML";
  page += String(config.sunlightCount);
  page += R"HTML(" />
            </div>
            <div>
              <label>Default I2C SDA</label>
              <input name="sdapin" type="number" value=")HTML";
  page += String(config.i2cSda);
  page += R"HTML(" />
            </div>
            <div>
              <label>Default I2C SCL</label>
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
          </div>
        </section>
)HTML";

  for (size_t i = 0; i < config.sunlightCount && i < kMaxSunlightSensors; ++i) {
    const auto &cfg = config.sunlight[i];
    page += "        <section class=\"page\" data-page=\"sensor" + String(i + 1) + "\">\n";
    page += "          <h2>Sensor " + String(i + 1) + "</h2>\n";
    page += "          <div class=\"grid\">\n";
    page += "            <div class=\"switch-row\">\n";
    page += "              <label style=\"width:160px\">Enable sensor</label>\n";
    page += "              <input type=\"checkbox\" name=\"sun_en" + String(i) + "\"" + (cfg.enabled ? " checked" : "") + " />\n";
    page += "            </div>\n";
    page += "            <div>\n";
    page += "              <label>I2C address</label>\n";
    page += "              <input name=\"sun_addr" + String(i) + "\" type=\"number\" value=\"" + String(cfg.address) + "\" />\n";
    page += "            </div>\n";
    page += "            <div>\n";
    page += "              <label>SDA pin</label>\n";
    page += "              <input name=\"sun_sda" + String(i) + "\" type=\"number\" value=\"" + String(cfg.sda) + "\" />\n";
    page += "            </div>\n";
    page += "            <div>\n";
    page += "              <label>SCL pin</label>\n";
    page += "              <input name=\"sun_scl" + String(i) + "\" type=\"number\" value=\"" + String(cfg.scl) + "\" />\n";
    page += "            </div>\n";
    page += "          </div>\n";
    page += "          <p>Live reading and status for this sensor will appear on the Home page.</p>\n";
    page += "        </section>\n";
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

    // Draggable widgets
    const area = document.getElementById('widget-area');
    let dragItem = null;
    let offset = { x: 0, y: 0 };

    area.addEventListener('dragstart', (e) => {
      dragItem = e.target.closest('.widget');
      if (!dragItem) return;
      const rect = dragItem.getBoundingClientRect();
      offset.x = e.clientX - rect.left;
      offset.y = e.clientY - rect.top;
    });

    area.addEventListener('dragover', (e) => {
      e.preventDefault();
    });

    area.addEventListener('drop', (e) => {
      e.preventDefault();
      if (!dragItem) return;
      const rect = area.getBoundingClientRect();
      const x = e.clientX - rect.left - offset.x;
      const y = e.clientY - rect.top - offset.y;
      dragItem.style.left = Math.max(0, x) + 'px';
      dragItem.style.top = Math.max(0, y) + 'px';
      dragItem = null;
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
