#include "esp12-magicfit2-controller_secrets.h"
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>

const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;
const char* hostname = "vibeplate";

AsyncWebServer server(80);
ESP8266WiFiMulti WiFiMulti;
Ticker pwmTicker;

const int pwmPin = D2;
volatile int dutyPercent = 85;  // 85% = slowest
const int pwmFreq = 63;
volatile bool pwmState = false;
volatile bool motorOn = false;
unsigned long startMillis = 0;
unsigned long accumulatedMillis = 0;
int speedPercent = 0; // 0 = slowest, 100 = fastest

// HTML page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Vibration Plate</title>
<style>
body { font-family: sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; margin:0; background:#f5f5f5; }
.card { background:white; padding:20px; border-radius:12px; box-shadow:0 4px 10px rgba(0,0,0,0.1); text-align:center; width:300px; }
h1 { margin-bottom: 10px; }
button { padding:10px 20px; font-size:16px; border:none; border-radius:8px; background:#2196F3; color:white; cursor:pointer; margin-bottom:20px;}
button:active { background:#1976D2; }
input[type=range] { width:100%; margin-bottom:10px;}
#speedVal { font-weight:bold; }
</style>
</head>
<body>
<div class="card">
  <h1>Vibration Plate</h1>
  <button id="toggleBtn">Start</button>
  <div>
    <input type="range" min="0" max="100" value="0" id="speedSlider">
    <div>Speed: <span id="speedVal">0</span></div>
  </div>
  <div>Elapsed: <span id="elapsed">00:00</span></div>
</div>

<script>
const toggleBtn = document.getElementById('toggleBtn');
const slider = document.getElementById('speedSlider');
const speedVal = document.getElementById('speedVal');
const elapsed = document.getElementById('elapsed');

toggleBtn.onclick = async () => {
  const resp = await fetch(`/toggle`);
  const data = await resp.json();
  toggleBtn.textContent = data.state ? 'Stop' : 'Start';
};

slider.oninput = async () => {
  speedVal.textContent = slider.value;
  await fetch(`/speed?value=${slider.value}`);
};

// Poll elapsed time every second
setInterval(async () => {
  const resp = await fetch(`/elapsed`);
  const data = await resp.json();
  elapsed.textContent = data.time;
}, 1000);
</script>
</body>
</html>
)rawliteral";

void IRAM_ATTR pwmTick() {
  static uint32_t highMs, lowMs;

  if (!motorOn) { // motor off
    digitalWrite(pwmPin, LOW);
    pwmTicker.once_ms(100, pwmTick);
    return;
  }

  if (pwmState) {
    digitalWrite(pwmPin, LOW);
    pwmState = false;
    pwmTicker.once_ms(lowMs, pwmTick);
  } else {
    digitalWrite(pwmPin, HIGH);
    pwmState = true;
    pwmTicker.once_ms(highMs, pwmTick);
  }

  int periodMs = 1000 / pwmFreq;
  highMs = periodMs * (100 - dutyPercent) / 100; // inverted for 2N3904
  lowMs  = periodMs - highMs;
}

void setDutyFromUI(int uiVal) {
  uiVal = constrain(uiVal,0,100);
  dutyPercent = map(uiVal,0,100,85,50); // 0=slowest,100=fastest
}

void startMotor() {
  if (!motorOn) {
    motorOn = true;
    startMillis = millis();
  }
}

void stopMotor() {
  if (motorOn) {
    motorOn = false;
    accumulatedMillis += millis() - startMillis;
  }
}

String formatElapsed(unsigned long ms) {
  unsigned long totalSec = ms / 1000;
  unsigned int minutes = totalSec / 60;
  unsigned int seconds = totalSec % 60;
  char buf[6];
  sprintf(buf,"%02u:%02u",minutes,seconds);
  return String(buf);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFiMulti.addAP(ssid, password);
  Serial.println();
  Serial.print("Connecting to WiFi");
  while (WiFiMulti.run() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  if (MDNS.begin(hostname)) { // <-- the hostname you want
    Serial.print("mDNS responder started: http://");
    Serial.print(hostname);
    Serial.println(".local");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
  delay(500);
  pinMode(pwmPin, OUTPUT);
  digitalWrite(pwmPin, LOW);
  pwmTicker.once_ms(0, pwmTick);

  // Serve page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Update speed
  server.on("/speed", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("value")){
      int val = request->getParam("value")->value().toInt();
      speedPercent = val;
      setDutyFromUI(speedPercent);
    }
    request->send(200,"application/json","{\"speed\":"+String(speedPercent)+"}");
  });

  // Toggle motor
  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
    if (motorOn) stopMotor();
    else startMotor();
    request->send(200,"application/json","{\"state\":"+(motorOn?String("true"):String("false"))+"}");
  });

  // Elapsed time
  server.on("/elapsed", HTTP_GET, [](AsyncWebServerRequest *request){
    unsigned long elapsedTime = accumulatedMillis;
    if (motorOn) elapsedTime += millis()-startMillis;
    request->send(200,"application/json","{\"time\":\""+formatElapsed(elapsedTime)+"\"}");
  });

  server.begin();
}

void loop() {
}
