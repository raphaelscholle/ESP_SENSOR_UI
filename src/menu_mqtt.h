#pragma once

#include <Arduino.h>

#include "main.h"

inline void appendMqttMenu(String &page, const DeviceConfig &config) {
  page += R"HTML(
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
)HTML";
}
