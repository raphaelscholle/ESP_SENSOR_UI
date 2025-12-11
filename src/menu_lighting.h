#pragma once

#include <Arduino.h>

#include "main.h"

inline void appendLightingMenu(String &page, const DeviceConfig &config) {
  page += R"HTML(
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
)HTML";
}
