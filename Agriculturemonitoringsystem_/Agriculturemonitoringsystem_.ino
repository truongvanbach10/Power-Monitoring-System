#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#define DHTPIN 14
#define DHTTYPE DHT11
#define SOIL_PIN 34
#define RELAY_PIN 26
#define RELAY_ON HIGH
#define RELAY_OFF LOW
const char* ssid = "Bach";
const char* password = "1234567890";
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
float temp = 0;
float hum = 0;
int soilPercent = 0;
bool dhtOK = false;
bool pumpState = false;
bool autoMode = true;
const int SOIL_DRY_LIMIT = 40;
const int SOIL_WET_LIMIT = 60;
unsigned long lastRead = 0;
String chartLabel = "";
String chartTemp = "";
String chartHum = "";
String chartSoil = "";
int dataCount = 0;
void setPump(bool state) {
  pumpState = state;
  digitalWrite(RELAY_PIN, state ? RELAY_ON : RELAY_OFF);
}
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h)) {

    temp = t;
    hum = h;

    dhtOK = true;

  } else {

    dhtOK = false;
  }

  // ===== SOIL =====
  int soilRaw = analogRead(SOIL_PIN);

  soilPercent = map(soilRaw, 4095, 1500, 0, 100);

  soilPercent = constrain(soilPercent, 0, 100);

  // ===== AUTO MODE =====
  if (autoMode) {

    // Đất khô → bật bơm
    if (soilPercent < SOIL_DRY_LIMIT && !pumpState) {

      setPump(true);

      Serial.println("AUTO: BAT BOM");
    }

    // Đủ ẩm → tắt bơm
    if (soilPercent > SOIL_WET_LIMIT && pumpState) {

      setPump(false);

      Serial.println("AUTO: TAT BOM");
    }
  }

  // ===== SERIAL =====
  Serial.println("==========");

  Serial.print("IP WEB: http://");
  Serial.println(WiFi.localIP());

  if (dhtOK) {

    Serial.print("Nhiet do: ");
    Serial.print(temp);
    Serial.println(" *C");

    Serial.print("Do am KK: ");
    Serial.print(hum);
    Serial.println(" %");

  } else {

    Serial.println("LOI DOC DHT11");
  }

  Serial.print("Do am dat: ");
  Serial.print(soilPercent);
  Serial.println(" %");

  Serial.print("Trang thai bom: ");
  Serial.println(pumpState ? "ON" : "OFF");

  Serial.print("Che do: ");
  Serial.println(autoMode ? "AUTO" : "MANUAL");

  // ===== CHART =====
  if (dataCount > 0) {

    chartLabel += ",";
    chartTemp += ",";
    chartHum += ",";
    chartSoil += ",";
  }

  chartLabel += "'" + String(dataCount) + "'";

  chartTemp += String(temp, 1);

  chartHum += String(hum, 1);

  chartSoil += String(soilPercent);

  dataCount++;

  // reset chart sau 20 diem
  if (dataCount > 20) {

    chartLabel = "";
    chartTemp = "";
    chartHum = "";
    chartSoil = "";

    dataCount = 0;
  }
}

// ========= HTML =========
String htmlPage() {

  String page = R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<meta charset="UTF-8">

<title>IoT Smart Farm</title>

<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

<style>

body{
font-family:Arial;
background:#eef2f3;
margin:0;
text-align:center;
}

.header{
background:#1e88e5;
color:white;
padding:18px;
font-size:24px;
}

.card{
background:white;
margin:15px auto;
padding:18px;
border-radius:15px;
width:90%;
max-width:520px;
box-shadow:0 3px 10px #aaa;
}

.value{
font-size:32px;
font-weight:bold;
color:#1e88e5;
}

.btn{
padding:12px 25px;
margin:8px;
border:none;
border-radius:8px;
font-size:16px;
color:white;
}

.on{background:#43a047;}
.off{background:#e53935;}
.auto{background:#1e88e5;}
.manual{background:#fb8c00;}

.error{
color:red;
font-weight:bold;
}

</style>

</head>

<body>

<div class="header">

HỆ THỐNG GIÁM SÁT MÔI TRƯỜNG IOT

</div>

<div class="card">

<h3>Nhiệt độ</h3>

<div class="value" id="temp">--</div>

<div id="dhtError"></div>

</div>

<div class="card">

<h3>Độ ẩm không khí</h3>

<div class="value" id="hum">--</div>

</div>

<div class="card">

<h3>Độ ẩm đất</h3>

<div class="value" id="soil">--</div>

</div>

<div class="card">

<h3>Trạng thái bơm</h3>

<div class="value" id="pump">--</div>

</div>

<div class="card">

<h3>Chế độ</h3>

<div class="value" id="mode">--</div>

<button class="btn auto" onclick="setMode('AUTO')">

AUTO

</button>

<button class="btn manual" onclick="setMode('MANUAL')">

MANUAL

</button>

</div>

<div class="card">

<h3>Điều khiển bơm</h3>

<button class="btn on" onclick="setPump('ON')">

BẬT BƠM

</button>

<button class="btn off" onclick="setPump('OFF')">

TẮT BƠM

</button>

</div>

<div class="card">

<h3>Biểu đồ realtime</h3>

<canvas id="myChart"></canvas>

</div>

<script>

function updateData(){

fetch('/data')

.then(res => res.json())

.then(data => {

document.getElementById('temp').innerHTML =
data.dht_ok ? data.temperature + ' °C' : '--';

document.getElementById('hum').innerHTML =
data.dht_ok ? data.humidity + ' %' : '--';

document.getElementById('soil').innerHTML =
data.soil + ' %';

document.getElementById('pump').innerHTML =
data.pump;

document.getElementById('mode').innerHTML =
data.mode;

if(!data.dht_ok){

document.getElementById('dhtError').innerHTML =
'<span class="error">Lỗi DHT11</span>';

}else{

document.getElementById('dhtError').innerHTML = '';
}

});

}

function setPump(state){

fetch('/pump?state=' + state)

.then(res => res.text())

.then(() => {

updateData();

});

}

function setMode(mode){

fetch('/mode?value=' + mode)

.then(res => res.text())

.then(() => {

updateData();

});

}

setInterval(updateData,2000);

updateData();

const ctx = document.getElementById('myChart');

new Chart(ctx, {

type: 'line',

data: {

labels: [)rawliteral";

  page += chartLabel;

  page += R"rawliteral(],

datasets: [

{
label: 'Nhiệt độ',
data: [)rawliteral";

  page += chartTemp;

  page += R"rawliteral(],
borderWidth:2
},

{
label: 'Độ ẩm KK',
data: [)rawliteral";

  page += chartHum;

  page += R"rawliteral(],
borderWidth:2
},

{
label: 'Độ ẩm đất',
data: [)rawliteral";

  page += chartSoil;

  page += R"rawliteral(],
borderWidth:2
}

]

}

});

</script>

</body>
</html>

)rawliteral";

  return page;
}

// ========= ROOT =========
void handleRoot() {

  server.send(200, "text/html", htmlPage());
}

// ========= JSON =========
void handleData() {

  String json = "{";

  json += "\"temperature\":";
  json += String(temp,1);
  json += ",";

  json += "\"humidity\":";
  json += String(hum,1);
  json += ",";

  json += "\"dht_ok\":";
  json += dhtOK ? "true" : "false";
  json += ",";

  json += "\"soil\":";
  json += String(soilPercent);
  json += ",";

  json += "\"pump\":\"";
  json += pumpState ? "ON" : "OFF";
  json += "\",";

  json += "\"mode\":\"";
  json += autoMode ? "AUTO" : "MANUAL";
  json += "\"";

  json += "}";

  server.send(200, "application/json", json);
}

// ========= PUMP =========
void handlePump() {

  String state = server.arg("state");

  // Bấm tay → chuyển MANUAL
  autoMode = false;

  if (state == "ON") {

    setPump(true);
  }

  if (state == "OFF") {

    setPump(false);
  }

  server.send(200, "text/plain", "OK");
}

// ========= MODE =========
void handleMode() {

  String value = server.arg("value");

  if (value == "AUTO") {

    autoMode = true;
  }

  if (value == "MANUAL") {

    autoMode = false;
  }

  server.send(200, "text/plain", "OK");
}

// ========= SETUP =========
void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println("===== ESP32 BAT DAU =====");

  pinMode(RELAY_PIN, OUTPUT);

  setPump(false);

  dht.begin();

  delay(2000);

  // ===== WIFI =====
  WiFi.begin(ssid, password);

  Serial.print("Dang ket noi WiFi");

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  Serial.println("WiFi da ket noi");

  Serial.print("IP WEB: http://");

  Serial.println(WiFi.localIP());

  // ===== SERVER =====
  server.on("/", handleRoot);

  server.on("/data", handleData);

  server.on("/pump", handlePump);

  server.on("/mode", handleMode);

  server.begin();

  Serial.println("WEB SERVER READY");
}

// ========= LOOP =========
void loop() {

  server.handleClient();

  if (millis() - lastRead >= 2000) {

    lastRead = millis();

    readSensors();
  }
}
