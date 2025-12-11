#pragma once

#include <Arduino.h>

#include "main.h"

inline void appendSensorsMenu(String &page, const DeviceConfig &config) {
  page += R"HTML(
        <section class="page" data-page="sensors">
          <h2>Sunlight sensor bus</h2>
          <div class="grid">
            <div>
              <label>I2C SDA pin</label>
              <input name="sdapin" type="number" value=")HTML";
  page += String(config.i2cSda);
  page += R"HTML(" />
            </div>
            <div>
              <label>I2C SCL pin</label>
              <input name="sclpin" type="number" value=")HTML";
  page += String(config.i2cScl);
  page += R"HTML(" />
            </div>
            <div>
              <label>Base topic</label>
              <input name="suntopic" value=")HTML";
  page += urlEncode(config.sunlightTopic);
  page += R"HTML(" />
            </div>
            <div>
              <label>Sensor count</label>
              <input name="sun_count" type="number" min="1" max="4" value=")HTML";
  page += String(config.sunlightCount);
  page += R"HTML(" />
            </div>
          </div>
          <p>Each sensor can override the bus pins and address below.</p>
        </section>
)HTML";
}

inline void appendSensorDetailMenu(String &page, const DeviceConfig &config, size_t index) {
  const auto &cfg = config.sunlight[index];
  page += "        <section class=\"page\" data-page=\"sensor" + String(index + 1) + "\">\n";
  page += "          <h2>Sensor " + String(index + 1) + "</h2>\n";
  page += "          <div class=\"grid\">\n";
  page += "            <div class=\"switch-row\">\n";
  page += "              <label style=\"width:160px\">Enable sensor</label>\n";
  page += "              <input type=\"checkbox\" name=\"sun_en" + String(index) + "\"" + (cfg.enabled ? " checked" : "") + " />\n";
  page += "            </div>\n";
  page += "            <div>\n";
  page += "              <label>I2C address</label>\n";
  page += "              <input name=\"sun_addr" + String(index) + "\" type=\"number\" value=\"" + String(cfg.address) + "\" />\n";
  page += "            </div>\n";
  page += "            <div>\n";
  page += "              <label>SDA pin</label>\n";
  page += "              <input name=\"sun_sda" + String(index) + "\" type=\"number\" value=\"" + String(cfg.sda) + "\" />\n";
  page += "            </div>\n";
  page += "            <div>\n";
  page += "              <label>SCL pin</label>\n";
  page += "              <input name=\"sun_scl" + String(index) + "\" type=\"number\" value=\"" + String(cfg.scl) + "\" />\n";
  page += "            </div>\n";
  page += "          </div>\n";
  page += "          <p>Live reading and status for this sensor will appear on the Home page.</p>\n";
  page += "        </section>\n";
}
