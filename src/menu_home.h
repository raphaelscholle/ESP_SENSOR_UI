#pragma once

#include <Arduino.h>

inline void appendHomeMenu(String &page) {
  page += R"HTML(
        <section class="page active" data-page="home">
          <h2>Live sensor values</h2>
          <div class="grid" id="live-grid"></div>
        </section>
)HTML";
}
