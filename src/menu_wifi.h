#pragma once

#include <Arduino.h>

#include "main.h"

inline void appendWifiMenu(String &page, const DeviceConfig &config) {
  page += R"HTML(
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
)HTML";
}
