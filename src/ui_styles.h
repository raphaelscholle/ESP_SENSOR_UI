#pragma once

#include <Arduino.h>

// Base CSS extracted from the generated HTML. Stored in PROGMEM to reduce RAM
// usage at runtime.
const char kBaseStyles[] PROGMEM = R"CSS(
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
  .pill-nav { display: flex; gap: 10px; flex-wrap: wrap; margin: 0 0 12px; padding: 0; list-style: none; }
  .pill-nav a { text-decoration: none; color: #cdd7ff; padding: 8px 12px; border-radius: 999px; background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.08); display: inline-flex; gap: 8px; align-items: center; }
  .pill-nav a.active, .pill-nav a:hover { background: rgba(255,255,255,0.12); }
  .metric { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08); border-radius: 12px; padding: 14px; display: flex; flex-direction: column; gap: 4px; }
  .metric .value { font-size: 22px; font-weight: 700; color: #fff; }
  .metric small { color: #9ab4ff; }
  .status-dot { width: 10px; height: 10px; border-radius: 50%; background: #f39b39; display: inline-block; }
  .page { display: none; }
  .page.active { display: block; }
  .switch-row { display: flex; align-items: center; gap: 10px; }
)CSS";
