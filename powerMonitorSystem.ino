#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PZEM004Tv30.h>

// ================== WIFI ==================
const char* ssid = "Bach";
const char* password = "1234567890";

// ================== PIN ==================
#define RELAY_PIN 23
#define RXD2 16
#define TXD2 17
#define SDA_PIN 21
#define SCL_PIN 22

// ================== TIME ==================
#define READ_INTERVAL 2000
#define LCD_INTERVAL 3000
#define STARTUP_SAFE_TIME 3000
#define OVERLOAD_DELAY 3000

// ================== OBJECT ==================
WebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2);
PZEM004Tv30 pzem(Serial2, RXD2, TXD2);

// ================== DATA ==================
float vAvg = NAN;
float iAvg = NAN;
float pAvg = NAN;
float eAvg = NAN;

float maxPower = 45.0;
float pricePerKWh = 3000.0;
float minVoltage = 120.0;
float maxVoltage = 260.0;

float pMax = 0;
float pMin = 9999;
float vMaxSeen = 0;
float vMinSeen = 9999;
float iMaxSeen = 0;

float energyOffset = 0;

// ================== STATE ==================
bool relayState = false;
bool warningState = false;
bool overloadFault = false;
bool overloadLock = false;
bool sensorFault = false;
bool manualOff = false;
bool firstStartDone = false;

int faultCode = 0;
int errorCount = 0;
int lcdPage = 0;

unsigned long tRead = 0;
unsigned long tLCD = 0;
unsigned long tStart = 0;
unsigned long overloadStart = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastDataUpdate = 0;

// ================== BASIC ==================
float safeValue(float x) {
  return isnan(x) ? 0 : x;
}

float demoEnergy() {
  float e = safeValue(eAvg) - energyOffset;
  return e < 0 ? 0 : e;
}

float demoCost() {
  return demoEnergy() * pricePerKWh;
}

float loadPercent() {
  if (maxPower <= 0) return 0;
  return (safeValue(pAvg) / maxPower) * 100.0;
}

void setRelay(bool state) {
  relayState = state;
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
}

float smooth(float oldVal, float newVal) {
  if (isnan(newVal)) return oldVal;
  if (isnan(oldVal)) return newVal;
  return oldVal * 0.7 + newVal * 0.3;
}

String detectLoad(float p) {
  if (isnan(p) || p < 2) return "KHONG TAI";
  else if (p < 20) return "DEN";
  else if (p < maxPower) return "QUAT";
  else return "DEN + QUAT";
}

String faultText() {
  if (overloadLock) return "DA KHOA QUA TAI";
  if (faultCode == 0) return "BINH THUONG";
  if (faultCode == 1) return "CANH BAO PZEM";
  if (faultCode == 2) return "QUA CONG SUAT";
  if (faultCode == 4) return "DIEN AP THAP";
  if (faultCode == 5) return "DIEN AP CAO";
  return "KHONG XAC DINH";
}

String wifiText() {
  return WiFi.status() == WL_CONNECTED ? "WIFI OK" : "MAT WIFI";
}

// ================== READ PZEM ==================
void readPZEM() {
  float v = pzem.voltage();
  float i = pzem.current();
  float p = pzem.power();
  float e = pzem.energy();

  if (isnan(v) || isnan(i) || isnan(p) || isnan(e)) {
    errorCount++;
  } else {
    errorCount = 0;

    vAvg = smooth(vAvg, v);
    iAvg = smooth(iAvg, i);
    pAvg = smooth(pAvg, p);
    eAvg = smooth(eAvg, e);

    if (safeValue(pAvg) > pMax) pMax = safeValue(pAvg);
    if (safeValue(pAvg) < pMin) pMin = safeValue(pAvg);
    if (safeValue(vAvg) > vMaxSeen) vMaxSeen = safeValue(vAvg);
    if (safeValue(vAvg) < vMinSeen) vMinSeen = safeValue(vAvg);
    if (safeValue(iAvg) > iMaxSeen) iMaxSeen = safeValue(iAvg);

    lastDataUpdate = millis();
  }

  sensorFault = errorCount >= 10;
}

// ================== PROTECTION ==================
void checkProtect() {
  faultCode = 0;

  if (sensorFault) faultCode = 1;
  if (!isnan(vAvg) && vAvg < minVoltage) faultCode = 4;
  if (!isnan(vAvg) && vAvg > maxVoltage) faultCode = 5;

  if (overloadLock) {
    overloadFault = true;
    warningState = false;
    faultCode = 2;
    setRelay(false);
    return;
  }

  if (!isnan(pAvg) && pAvg > maxPower) {
    faultCode = 2;

    if (!warningState) {
      warningState = true;
      overloadStart = millis();
    }

    if (millis() - overloadStart > OVERLOAD_DELAY) {
      overloadFault = true;
      overloadLock = true;
      warningState = false;
      setRelay(false);
    }
  } else {
    warningState = false;
    overloadFault = false;
  }
}

// ================== WIFI ==================
void checkWiFi() {
  if (millis() - lastWifiCheck >= 5000) {
    lastWifiCheck = millis();

    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
}

// ================== JSON API ==================
String jsonData() {
  float percent = loadPercent();

  String json = "{";
  json += "\"voltage\":" + String(safeValue(vAvg), 1) + ",";
  json += "\"current\":" + String(safeValue(iAvg), 2) + ",";
  json += "\"power\":" + String(safeValue(pAvg), 1) + ",";
  json += "\"energy\":" + String(demoEnergy(), 3) + ",";
  json += "\"cost\":" + String(demoCost(), 0) + ",";
  json += "\"load\":\"" + detectLoad(pAvg) + "\",";
  json += "\"relay\":" + String(relayState ? 1 : 0) + ",";
  json += "\"warning\":" + String(warningState ? 1 : 0) + ",";
  json += "\"overload_lock\":" + String(overloadLock ? 1 : 0) + ",";
  json += "\"manual_off\":" + String(manualOff ? 1 : 0) + ",";
  json += "\"fault_code\":" + String(faultCode) + ",";
  json += "\"fault_text\":\"" + faultText() + "\",";
  json += "\"wifi\":\"" + wifiText() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"max_power\":" + String(maxPower, 0) + ",";
  json += "\"price\":" + String(pricePerKWh, 0) + ",";
  json += "\"percent\":" + String(percent, 0) + ",";
  json += "\"p_max\":" + String(pMax, 1) + ",";
  json += "\"p_min\":" + String(pMin == 9999 ? 0 : pMin, 1) + ",";
  json += "\"v_max\":" + String(vMaxSeen, 1) + ",";
  json += "\"v_min\":" + String(vMinSeen == 9999 ? 0 : vMinSeen, 1) + ",";
  json += "\"i_max\":" + String(iMaxSeen, 2) + ",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"last_update\":" + String(lastDataUpdate / 1000);
  json += "}";
  return json;
}

// ================== WEB PAGE ==================
String webPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Giam sat dien</title>
<style>
body{font-family:Arial;background:#eef2f7;margin:0;padding:14px;text-align:center;color:#111}
h1{color:#0b63ce;margin:10px 0 4px;font-size:36px}
.sub{color:#555;margin-bottom:14px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;max-width:820px;margin:auto}
.card{background:white;border-radius:20px;padding:16px;box-shadow:0 6px 18px rgba(0,0,0,.14)}
.wide{grid-column:1/3}
.title{font-size:13px;color:#666;text-transform:uppercase}
.value{font-size:27px;font-weight:bold;margin-top:7px}
.ok{color:#168a2f}.warn{color:#d98200}.bad{color:#d11a2a}
button{font-size:17px;padding:13px 18px;border:0;border-radius:14px;margin:6px;color:white;min-width:150px}
.btnon{background:#168a2f}.btnoff{background:#d11a2a}.btnreset{background:#0b63ce}.btnwarn{background:#d98200}
input{font-size:19px;padding:12px;border-radius:14px;border:1px solid #c9ced8;width:85%;max-width:280px;text-align:center}
.bar{width:100%;height:22px;background:#e2e6ed;border-radius:20px;overflow:hidden;margin-top:10px}
.fill{height:100%;width:0%;background:#168a2f;transition:.3s}
.fill.warn{background:#d98200}.fill.bad{background:#d11a2a}
canvas{width:100%;max-width:760px;height:220px;background:white;border-radius:20px}
.small{font-size:13px;color:#777;margin-top:12px}
.formline{display:flex;flex-direction:column;align-items:center;gap:7px;margin:12px 0}
</style>
</head>
<body>
<h1>GIAM SAT DIEN</h1>
<div class="sub">ESP32 Smart Energy Monitor - Local Web</div>

<div class="grid">
<div class="card wide"><div class="title">TAI DANG DUNG</div><div id="load" class="value">--</div></div>

<div class="card"><div class="title">DIEN AP</div><div id="voltage" class="value">--</div></div>
<div class="card"><div class="title">DONG DIEN</div><div id="current" class="value">--</div></div>
<div class="card"><div class="title">CONG SUAT</div><div id="power" class="value">--</div></div>
<div class="card"><div class="title">NGUONG TAI</div><div id="maxPower" class="value">--</div></div>

<div class="card wide">
<div class="title">% TAI SO VOI NGUONG</div>
<div id="percent" class="value">--</div>
<div class="bar"><div id="barFill" class="fill"></div></div>
</div>

<div class="card"><div class="title">DIEN NANG</div><div id="energy" class="value">--</div></div>
<div class="card"><div class="title">TIEN DIEN</div><div id="cost" class="value">--</div></div>
<div class="card"><div class="title">RELAY</div><div id="relay" class="value">--</div></div>
<div class="card"><div class="title">TRANG THAI</div><div id="status" class="value">--</div></div>

<div class="card wide">
<div class="title">THONG KE NHANH</div>
<div id="stats" class="value" style="font-size:20px">--</div>
</div>

<div class="card wide">
<a href="/on"><button class="btnon">BAT RELAY / RESET QUA TAI</button></a>
<a href="/reset-fault"><button class="btnwarn">RESET LOI</button></a>
<a href="/off"><button class="btnoff">TAT RELAY</button></a>
<a href="/reset-energy"><button class="btnreset">RESET DIEN NANG</button></a>
<a href="/reset-stats"><button class="btnreset">RESET MIN/MAX</button></a>
</div>

<div class="card wide">
<div class="title">CAI DAT NHANH</div>
<form action="/set" method="GET">
<div class="formline">
<label>Nguong cong suat (W)</label>
<input name="maxp" type="number" step="1" min="1" placeholder="Vi du: 45">
</div>
<div class="formline">
<label>Gia dien (VND/kWh)</label>
<input name="price" type="number" step="100" min="0" placeholder="Vi du: 3000">
</div>
<button class="btnon" type="submit">LUU CAI DAT</button>
</form>
</div>
</div>

<h3>BIEU DO CONG SUAT MINI</h3>
<canvas id="chart" width="720" height="220"></canvas>
<div class="small" id="timeInfo">Dang tai...</div>

<script>
let points = []; const maxPoints = 30;
function fmtTime(s){s=Number(s)||0;let h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;return (h>0?h+'h ':'')+m+'m '+sec+'s'}
async function updateData(){
  try{
    const res = await fetch('/json'); const d = await res.json();
    document.getElementById('load').textContent = d.load;
    document.getElementById('voltage').textContent = d.voltage + ' V';
    document.getElementById('current').textContent = d.current + ' A';
    document.getElementById('power').textContent = d.power + ' W';
    document.getElementById('maxPower').textContent = d.max_power + ' W';
    document.getElementById('energy').textContent = d.energy + ' kWh';
    document.getElementById('cost').textContent = d.cost + ' VND';
    document.getElementById('relay').textContent = d.relay == 1 ? 'DANG BAT' : 'DANG TAT';
    document.getElementById('relay').className = d.relay == 1 ? 'value ok' : 'value bad';
    document.getElementById('status').textContent = d.fault_text;
    document.getElementById('status').className = d.overload_lock == 1 ? 'value bad' : (d.warning == 1 || d.fault_code != 0 ? 'value warn' : 'value ok');
    document.getElementById('percent').textContent = d.percent + '%';
    let fill = document.getElementById('barFill');
    fill.style.width = Math.min(100, Number(d.percent)) + '%';
    fill.className = Number(d.percent) >= 100 ? 'fill bad' : (Number(d.percent) >= 80 ? 'fill warn' : 'fill');
    document.getElementById('stats').textContent = 'Pmax: '+d.p_max+'W | Pmin: '+d.p_min+'W | Vmin/max: '+d.v_min+'/'+d.v_max+'V | Imax: '+d.i_max+'A';
    document.getElementById('timeInfo').textContent = d.wifi + ' | IP: ' + d.ip + ' | Uptime: ' + fmtTime(d.uptime) + ' | Cap nhat cuoi: ' + fmtTime(d.last_update) + ' | API: /json';
    points.push(Number(d.power)); if(points.length > maxPoints) points.shift(); drawChart(Number(d.max_power));
  }catch(e){document.getElementById('timeInfo').textContent='Mat ket noi du lieu'}
}
function drawChart(threshold){
  const c=document.getElementById('chart'),ctx=c.getContext('2d'); ctx.clearRect(0,0,c.width,c.height);
  ctx.strokeStyle='#ddd';ctx.lineWidth=1;for(let y=20;y<c.height;y+=40){ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(c.width,y);ctx.stroke()}
  let maxY=Math.max(threshold+20,...points,60);let thY=c.height-(threshold/maxY)*(c.height-20);
  ctx.strokeStyle='#d11a2a';ctx.setLineDash([6,6]);ctx.beginPath();ctx.moveTo(0,thY);ctx.lineTo(c.width,thY);ctx.stroke();ctx.setLineDash([]);ctx.fillStyle='#d11a2a';ctx.fillText('Nguong '+threshold+'W',10,thY-5);
  if(points.length<2)return;ctx.strokeStyle='#0b63ce';ctx.lineWidth=3;ctx.beginPath();points.forEach((p,i)=>{let x=i*(c.width/(maxPoints-1));let y=c.height-(p/maxY)*(c.height-20);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)});ctx.stroke();
}
setInterval(updateData,2000); updateData();
</script>
</body>
</html>
)rawliteral";
  return html;
}

// ================== LCD ==================
void updateLCD() {
  lcd.clear();

  if (warningState && !overloadLock) {
    lcd.print("CANH BAO TAI!");
    lcd.setCursor(0, 1);
    lcd.print("SAP NGAT RELAY");
    return;
  }

  if (overloadLock) {
    lcd.print("DA KHOA QUA TAI");
    lcd.setCursor(0, 1);
    lcd.print("NHAN BAT DE RS");
    return;
  }

  if (lcdPage == 0) {
    lcd.print("TAI:");
    lcd.print(detectLoad(pAvg));
    lcd.setCursor(0, 1);
    lcd.print("P:");
    lcd.print(safeValue(pAvg), 0);
    lcd.print("W/");
    lcd.print(maxPower, 0);
    lcd.print("W");
  }
  else if (lcdPage == 1) {
    lcd.print("TAI:");
    lcd.print(loadPercent(), 0);
    lcd.print("%");
    lcd.setCursor(0, 1);
    lcd.print("Relay:");
    lcd.print(relayState ? "BAT" : "TAT");
  }
  else if (lcdPage == 2) {
    lcd.print("DIEN AP:");
    lcd.print(safeValue(vAvg), 1);
    lcd.print("V");
    lcd.setCursor(0, 1);
    lcd.print("DONG:");
    lcd.print(safeValue(iAvg), 2);
    lcd.print("A");
  }
  else if (lcdPage == 3) {
    lcd.print("E:");
    lcd.print(demoEnergy(), 3);
    lcd.print("kWh");
    lcd.setCursor(0, 1);
    lcd.print("TIEN:");
    lcd.print(demoCost(), 0);
  }
  else {
    lcd.print("TRANG THAI:");
    lcd.setCursor(0, 1);
    lcd.print(faultText());
  }

  lcdPage = (lcdPage + 1) % 5;
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();

  lcd.print("GIAM SAT DIEN");
  lcd.setCursor(0, 1);
  lcd.print("KHOI DONG...");

  WiFi.begin(ssid, password);
  Serial.print("Dang ket noi WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", []() {
    server.send(200, "text/html", webPage());
  });

  server.on("/json", []() {
    server.send(200, "application/json", jsonData());
  });

  server.on("/on", []() {
    if (safeValue(pAvg) <= maxPower) {
      manualOff = false;
      overloadFault = false;
      warningState = false;
      overloadLock = false;
      setRelay(true);
    }
    server.send(200, "text/html", webPage());
  });

  server.on("/off", []() {
    manualOff = true;
    setRelay(false);
    server.send(200, "text/html", webPage());
  });

  server.on("/reset-fault", []() {
    if (safeValue(pAvg) <= maxPower) {
      overloadFault = false;
      warningState = false;
      overloadLock = false;
      faultCode = 0;
    }
    server.send(200, "text/html", webPage());
  });

  server.on("/reset-energy", []() {
    energyOffset = safeValue(eAvg);
    server.send(200, "text/html", webPage());
  });

  server.on("/reset-stats", []() {
    pMax = 0;
    pMin = 9999;
    vMaxSeen = 0;
    vMinSeen = 9999;
    iMaxSeen = 0;
    server.send(200, "text/html", webPage());
  });

  server.on("/set", []() {
    if (server.hasArg("maxp")) {
      float val = server.arg("maxp").toFloat();
      if (val > 0) maxPower = val;
    }

    if (server.hasArg("price")) {
      float val = server.arg("price").toFloat();
      if (val >= 0) pricePerKWh = val;
    }

    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "");
  });

  server.begin();
  tStart = millis();
}

// ================== LOOP ==================
void loop() {
  unsigned long now = millis();

  checkWiFi();

  if (now - tRead > READ_INTERVAL) {
    tRead = now;

    readPZEM();
    checkProtect();

    if (!firstStartDone &&
        !manualOff &&
        !overloadLock &&
        !overloadFault &&
        !relayState &&
        now - tStart > STARTUP_SAFE_TIME &&
        safeValue(pAvg) <= maxPower) {
      setRelay(true);
      firstStartDone = true;
    }

    // ================== UART MONITOR ==================
    Serial.println();
    Serial.println("====================================");
    Serial.println("      ESP32 SMART ENERGY MONITOR");
    Serial.println("====================================");

    Serial.print("Voltage      : ");
    Serial.print(safeValue(vAvg), 1);
    Serial.println(" V");

    Serial.print("Current      : ");
    Serial.print(safeValue(iAvg), 2);
    Serial.println(" A");

    Serial.print("Power        : ");
    Serial.print(safeValue(pAvg), 2);
    Serial.println(" W");

    Serial.print("Energy       : ");
    Serial.print(demoEnergy(), 3);
    Serial.println(" kWh");

    Serial.print("Electric Cost: ");
    Serial.print(demoCost(), 0);
    Serial.println(" VND");

    Serial.print("Load Type    : ");
    Serial.println(detectLoad(pAvg));

    Serial.print("Load Percent : ");
    Serial.print(loadPercent(), 0);
    Serial.println(" %");

    Serial.print("Relay Status : ");
    Serial.println(relayState ? "BAT" : "TAT");

    Serial.print("Overload Lock: ");
    Serial.println(overloadLock ? "CO" : "KHONG");

    Serial.print("System Status: ");
    Serial.println(faultText());

    Serial.print("WiFi Status  : ");
    Serial.println(wifiText());

    Serial.print("IP Address   : ");
    Serial.println(WiFi.localIP());

    Serial.println("====================================");
  }

  if (now - tLCD > LCD_INTERVAL) {
    tLCD = now;
    updateLCD();
  }

  server.handleClient();
}
