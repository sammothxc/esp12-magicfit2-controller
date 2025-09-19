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

struct Step { int speed; int duration; };
Step workoutSteps[20];
int stepCount = 0;
bool planRunning = false;
bool planPaused = false;
int currentStep = 0;
int elapsedTime = 0;

const int pwmPin = D2;
volatile int dutyPercent = 85;  // 85% = slowest
const int pwmFreq = 63;
volatile bool pwmState = false;
volatile bool motorOn = false;
unsigned long startMillis = 0;
unsigned long accumulatedMillis = 0;
int speedPercent = 0; // 0 = slowest, 100 = fastest

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Vibration Plate">
<link rel="apple-touch-icon" href="icon-192.png">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Vibration Plate</title>
<style>
body { font-family: sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; margin:0; background:#f5f5f5; }
.card-wrapper { display: flex; flex-direction: column; align-items: center; position: relative; }
.card { background:white; padding:30px; border-radius:16px; box-shadow:0 4px 12px rgba(0,0,0,0.15); width:320px; text-align:center; margin-top:20px; }
.tabs { display: flex; width: 380px; position: absolute; top: 0; left: 50%; transform: translateX(-50%); }
.tab { flex: 1; text-align: center; padding: 12px 0; background:#eee; border-bottom: none; border-radius: 8px 8px 0 0; cursor: pointer; font-weight: bold; }
.tab.active { background:white; }
.tab-content { display:none; }
.tab-content.active { display:block; }
h1 { margin-bottom: 25px; font-size: 1.5em; }
button { padding:14px 20px; font-size:18px; border:none; border-radius:10px; color:white; cursor:pointer; margin-top:15px; width: 100%; }
#toggleBtn.start { background:#2196F3; }
#toggleBtn.start:active { background:#1976D2; }
#toggleBtn.stop { background:#f44336; }
#toggleBtn.stop:active { background:#d32f2f; }
#resetBtn { background:#2196F3; }
#startPlanBtn { background: #4CAF50; }
#startPlanBtn.running { background: #2196F3; }
#startPlanBtn.paused { background: #FFC107; }
input[type=range] { width:100%; height: 24px; margin:25px 0; -webkit-appearance: none; background: #ddd; border-radius: 12px; outline: none; }
input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 28px; height: 28px; border-radius: 50%; background: #2196F3; cursor: pointer; }
input[type=range]::-moz-range-thumb { width: 28px; height: 28px; border-radius: 50%; background: #2196F3; cursor: pointer; }
#speedVal, #elapsed, #planElapsed, #planRemaining, #currentStepSpeed { font-size: 1.2em; font-weight: bold; margin-bottom: 10px; }
.step { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; padding: 8px; background: #f0f0f0; border-radius: 8px; gap: 6px; flex-wrap: wrap; }
.step > * { flex: 1 1 auto; }
.step input[type=number] {  width: 50px; padding: 2px 4px; border-radius: 4px; border: 1px solid #ccc; font-size: 0.9em; text-align: center; }
.step button.deleteStep { background: #f44336; color: white; border: none; border-radius: 6px; padding: 2px 6px; font-size: 0.9em; cursor: pointer; flex-shrink: 0; }
</style>
</head>
<body>
<div class="card-wrapper">
  <div class="tabs">
    <div class="tab active" data-tab="1">Control</div>
    <div class="tab" data-tab="2">Workout Plan</div>
  </div>
  <div class="card">

    <div id="tab1" class="tab-content active">
      <h1>Vibration Plate Control</h1>
      <div>Speed: <span id="speedVal">0</span></div>
      <input type="range" min="0" max="100" value="0" id="speedSlider">
      <div style="margin-bottom:15px;"><span id="elapsed">00:00</span></div>
      <button id="toggleBtn" style="margin-bottom:15px;">Start</button>
      <button id="resetBtn">Reset Timer</button>
    </div>

    <div id="tab2" class="tab-content">
      <h1>Workout Plan</h1>
      <div>Current Step Speed: <span id="currentStepSpeed">0</span></div>
      <div style="margin:25px 0; width:100%; background:#ddd; height:24px; border-radius:12px;">
        <div id="planProgress" style="width:0%; height:100%; background:#2196F3; border-radius:12px;"></div>
      </div>
      <div style="margin-bottom:15px;">
        <span id="planElapsed">00:00</span> / <span id="planRemaining">00:00</span>
      </div>
      <button id="startPlanBtn">Start Plan</button>
      <button id="stopPlanBtn" style="margin-top:10px; width:100%; background:#f44336; display:none;">Stop Plan</button>
      <div id="stepsContainer" style="margin-top:15px;"></div>
      <button id="addStepBtn" style="margin-top:15px; background:#2196F3;">Add Step</button>
    </div>
  </div>
</div>

<script>
document.querySelectorAll('.tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    tab.classList.add('active');
    const contentId = "tab" + tab.dataset.tab;
    document.getElementById(contentId).classList.add('active');
  });
});

const toggleBtn = document.getElementById('toggleBtn');
const slider = document.getElementById('speedSlider');
const speedVal = document.getElementById('speedVal');
const elapsed = document.getElementById('elapsed');
const resetBtn = document.getElementById('resetBtn');

toggleBtn.onclick = async () => {
  const resp = await fetch(`/toggle`);
  const data = await resp.json();
  updateToggleBtn(data.state);
};

slider.oninput = async () => {
  speedVal.textContent = slider.value;
  await fetch(`/speed?value=${slider.value}`);
};

resetBtn.onclick = async () => {
  await fetch(`/reset`);
  elapsed.textContent = "00:00";
};

setInterval(async () => {
  const resp = await fetch(`/elapsed`);
  const data = await resp.json();
  elapsed.textContent = data.time;
}, 1000);

function updateToggleBtn(state) {
  if (state) {
    toggleBtn.textContent = 'Stop';
    toggleBtn.classList.remove('start');
    toggleBtn.classList.add('stop');
  } else {
    toggleBtn.textContent = 'Start';
    toggleBtn.classList.remove('stop');
    toggleBtn.classList.add('start');
  }
}

window.onload = async () => {
  const resp = await fetch("/planState");
  const data = await resp.json();

  workoutSteps = data.workoutSteps || [];
  renderSteps();

  planRunning = data.planRunning;
  planPaused = data.planPaused;
  currentStep = data.currentStep || 0;
  let elapsed = data.elapsedTime || 0;

  planElapsed.textContent = formatTime(elapsed);
  const totalTime = workoutSteps.reduce((sum,s)=>sum+s.duration,0);
  planRemaining.textContent = formatTime(totalTime - elapsed);
  planProgress.style.width = ((elapsed/totalTime)*100).toFixed(1)+"%";

  if(planRunning){
    startPlanBtn.textContent = planPaused ? "Resume Plan" : "Pause Plan";
    startPlanBtn.classList.toggle("paused", planPaused);
    startPlanBtn.classList.toggle("running", !planPaused);
    stopPlanBtn.style.display = "block";

    // Activate Workout Plan tab
    document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));
    document.querySelector('.tab[data-tab="2"]').classList.add('active');
    document.getElementById('tab2').classList.add('active');

    runWorkoutPlan(currentStep, elapsed);
  }
};

const stepsContainer = document.getElementById('stepsContainer');
const addStepBtn = document.getElementById('addStepBtn');
const startPlanBtn = document.getElementById('startPlanBtn');
const planElapsed = document.getElementById('planElapsed');
const planRemaining = document.getElementById('planRemaining');
const planProgress = document.getElementById('planProgress');
const currentStepSpeed = document.getElementById('currentStepSpeed');
const stopPlanBtn = document.getElementById('stopPlanBtn');
let planRunning = false;
let planPaused = false;
let workoutSteps = []; // {speed: 0-100, duration: seconds}

addStepBtn.onclick = () => {
  const step = {speed: 50, duration: 30}; // default values
  workoutSteps.push(step);
  renderSteps();
};

function renderSteps() {
  stepsContainer.innerHTML = '';
  workoutSteps.forEach((step, index) => {
    const div = document.createElement('div');
    div.className = 'step';
    div.innerHTML = `
      Speed: <input type="number" min="0" max="100" value="${step.speed}" data-index="${index}" class="speedInput"> 
      Duration: <input type="number" min="1" value="${step.duration}" data-index="${index}" class="durationInput">
      <button class="deleteStep" data-index="${index}">X</button>
    `;
    stepsContainer.appendChild(div);
  });
  // Add event listeners for inputs and delete buttons
  document.querySelectorAll('.speedInput').forEach(input => {
    input.oninput = (e) => {
      const i = e.target.dataset.index;
      workoutSteps[i].speed = parseInt(e.target.value);
    };
  });
  document.querySelectorAll('.durationInput').forEach(input => {
    input.oninput = (e) => {
      const i = e.target.dataset.index;
      workoutSteps[i].duration = parseInt(e.target.value);
      updateRemaining();
    };
  });
  document.querySelectorAll('.deleteStep').forEach(btn => {
    btn.onclick = (e) => {
      const i = e.target.dataset.index;
      workoutSteps.splice(i,1);
      renderSteps();
    };
  });
  updateRemaining();
}

function updateRemaining() {
  const totalTime = workoutSteps.reduce((sum, s) => sum + s.duration, 0);
  planRemaining.textContent = formatTime(totalTime);
  planElapsed.textContent = '00:00';
  planProgress.style.width = '0%';
}

function formatTime(sec) {
  const m = Math.floor(sec/60);
  const s = sec % 60;
  return `${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
}

startPlanBtn.onclick = async () => {
  if (workoutSteps.length === 0) return;

  if (!planRunning) {
    // Start workout
    planRunning = true;
    planPaused = false;
    startPlanBtn.textContent = "Pause Plan";
    startPlanBtn.classList.add("running");
    startPlanBtn.classList.remove("paused");
    stopPlanBtn.style.display = "block"; // show stop button
    runWorkoutPlan();
  } else if (!planPaused) {
    // Pause workout
    planPaused = true;
    startPlanBtn.textContent = "Resume Plan";
    startPlanBtn.classList.add("paused");
    startPlanBtn.classList.remove("running");
  } else {
    // Resume workout
    planPaused = false;
    startPlanBtn.textContent = "Pause Plan";
    startPlanBtn.classList.add("running");
    startPlanBtn.classList.remove("paused");
  }
};

stopPlanBtn.onclick = async () => {
  if (!planRunning) return;

  planRunning = false;
  planPaused = false;
  
  startPlanBtn.textContent = "Start Plan";
  startPlanBtn.classList.remove("paused", "running"); // remove previous classes
  startPlanBtn.style.background = "#4CAF50"; // optional: force green
  stopPlanBtn.style.display = "none"; // hide stop button
  planProgress.style.width = '0%';
  planElapsed.textContent = '00:00';
  planRemaining.textContent = formatTime(
    workoutSteps.reduce((sum, s) => sum + s.duration, 0)
  );

  // Make sure motor is off
  await fetch(`/toggle`);
};


async function runWorkoutPlan(startIndex=0, elapsedTime=0) {
  const totalTime = workoutSteps.reduce((sum,step)=>sum+step.duration,0);

  for(let stepIndex = startIndex; stepIndex < workoutSteps.length; stepIndex++) {
    const step = workoutSteps[stepIndex];
    currentStepSpeed.textContent = step.speed;

    if(!planRunning) break;

    await fetch(`/speed?value=${step.speed}`);
    await fetch(`/toggle`); // motor on

    const stepElapsedStart = (stepIndex === startIndex) ? elapsedTime % step.duration : 0;

    for(let t = stepElapsedStart; t < step.duration; t++) {
      if(!planRunning) break;

      while(planPaused) {
        if(!planRunning) break;
        await new Promise(r=>setTimeout(r,200));
      }

      if(!planRunning) break;

      await new Promise(r=>setTimeout(r,1000));
      elapsedTime++;
      planElapsed.textContent = formatTime(elapsedTime);
      planRemaining.textContent = formatTime(totalTime - elapsedTime);
      planProgress.style.width = ((elapsedTime/totalTime)*100).toFixed(1) + "%";

      // Optional: report progress to server
      fetch(`/updatePlan`, {
        method:'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify({
          currentStep: stepIndex,
          elapsedTime: elapsedTime,
          planRunning,
          planPaused
        })
      });
    }

    await fetch(`/toggle`); // motor off after step
  }

  // Reset at the end
  planProgress.style.width = '0%';
  planElapsed.textContent = '00:00';
  planRemaining.textContent = formatTime(totalTime);
  startPlanBtn.textContent = "Start Plan";
  planRunning = false;
  planPaused = false;
  stopPlanBtn.style.display = "none";

  // Optional: notify server plan finished
  await fetch(`/updatePlan`, {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({planRunning:false, planPaused:false})
  });
}

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
  if (MDNS.begin(hostname)) {
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

  // Reset elapsed timer
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    accumulatedMillis = 0;
    if (motorOn) startMillis = millis();  // restart counting from now if running
    request->send(200, "application/json", "{\"reset\":true}");
  });

  server.on("/state", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"state\":"; 
    json += (motorOn ? "true" : "false");
    json += ",\"speed\":" + String(speedPercent);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Return current plan and progress
  server.on("/planState", HTTP_GET, [](AsyncWebServerRequest *request){
      String json = "{";
      json += "\"workoutSteps\":[";
      for(int i=0;i<stepCount;i++){
          json += "{\"speed\":"+String(workoutSteps[i].speed)+",\"duration\":"+String(workoutSteps[i].duration)+"}";
          if(i<stepCount-1) json += ",";
      }
      json += "],";
      json += "\"planRunning\":" + String(planRunning?"true":"false") + ",";
      json += "\"planPaused\":" + String(planPaused?"true":"false") + ",";
      json += "\"currentStep\":" + String(currentStep) + ",";
      json += "\"elapsedTime\":" + String(elapsedTime);
      json += "}";
      request->send(200,"application/json",json);
  });

  // Update plan/progress from client
  server.on("/updatePlan", HTTP_POST, [](AsyncWebServerRequest *request){
      int params = request->params();
      // We’ll parse JSON from request->getParam("body")->value()
      // For brevity, you can use ArduinoJson to parse and update server-side variables
      request->send(200,"application/json","{\"ok\":true}");
  });

  server.begin();
}

void loop() {
}
