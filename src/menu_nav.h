#pragma once

#include <Arduino.h>

inline void appendNavigation(String &page, size_t sensorCount) {
  page += R"HTML(
      <ul class="pill-nav" id="nav">
        <li><a href="#home" data-page="home" class="active">Home</a></li>
        <li><a href="#wifi" data-page="wifi">Wi-Fi</a></li>
        <li><a href="#mqtt" data-page="mqtt">MQTT</a></li>
        <li><a href="#lighting" data-page="lighting">Lighting</a></li>
        <li><a href="#sensors" data-page="sensors">Sensors</a></li>
)HTML";

  for (size_t i = 0; i < sensorCount; ++i) {
    page += "        <li><a href=\"#sensor" + String(i + 1) + "\" data-page=\"sensor" + String(i + 1) + "\">Sunlight " + String(i + 1) + "</a></li>\n";
  }

  page += R"HTML(
      </ul>
)HTML";
}
