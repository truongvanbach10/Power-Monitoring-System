# ⚡ Smart Energy Monitoring and Protection System using ESP32

## 📌 Introduction
This project is a smart electrical energy monitoring and protection system developed using ESP32 and PZEM-004T.  
The system is capable of measuring electrical parameters in real-time, displaying data on both LCD and Web Server, calculating electricity cost, and automatically disconnecting the load when overload conditions occur.

This project was developed as a graduation thesis project.

---

## 🎯 Main Features

- 📊 Real-time monitoring of:
  - Voltage (V)
  - Current (A)
  - Power (W)
  - Energy Consumption (kWh)

- 🌐 Local Web Server Monitoring
  - Access using smartphone or computer
  - Real-time data update via WiFi

- 🖥 LCD I2C Display
  - Display electrical parameters directly on hardware

- ⚠️ Smart Protection System
  - Overload warning
  - Automatic relay shutdown when power exceeds threshold
  - Sensor fault detection

- 💰 Electricity Cost Calculation
  - Estimate electricity bill based on energy consumption

- 🔌 Remote Load Control
  - Turn ON/OFF relay through Web Interface

---

## 🛠 Hardware Used

| Component | Quantity |
|---|---|
| ESP32 DevKit V1 | 1 |
| PZEM-004T V3.0 | 1 |
| Relay Module | 1 |
| LCD 16x2 I2C | 1 |
| AC Load (Lamp/Fan) | 1 |
| Power Supply | 1 |

---

## 🧠 System Architecture

```text
AC Source
    │
    ▼
PZEM-004T Sensor
    │
    ▼
ESP32 Controller
 ├── LCD I2C Display
 ├── Relay Protection
 └── Local Web Server