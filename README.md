# Lizard Habitat Monitoring System

## Project Overview
This project implements an AI-assisted IoT system that monitors a lizard’s habitat conditions and behavior, including temperature, humidity, and position within the enclosure.

Proper environmental conditions are critical for reptile health. Temperature and humidity must remain within specific ranges, and changes in behavior can indicate stress or health issues. These factors are often monitored manually and inconsistently.

This system provides **continuous, automated monitoring and reporting**, allowing early detection of potential habitat problems.

---

## Problem Statement
Lizard owners typically rely on manual checks to monitor enclosure conditions, which can result in missed environmental issues. Additionally, subtle behavioral changes—such as consistently avoiding a heat source—may go unnoticed.

This project solves that problem by providing **real-time data collection and automated summaries**, improving awareness and enabling earlier intervention.

---

## Final Solution

The completed system:

- Measures **temperature and humidity** using a sensor  
- Tracks **lizard position** (hot side, cool side, or not visible) using a camera  
- Combines environmental and behavioral data  
- Sends **automated email reports** summarizing conditions  

---

## Technologies and Tools

### Hardware
- **ESP32-C6 (XIAO)** — main controller, Wi-Fi, email reporting  
- **ESP32-CAM (AI-Thinker)** — camera-based motion detection  
- **AHT10 Sensor** — temperature and humidity  
- Breadboard and jumper wires  

### Software
- **Arduino IDE** — development and deployment   
- **Email reporting system** (via Wi-Fi)  

### Libraries
- `Adafruit_AHTX0`  
- `ESP_Mail_Client`  
- `esp_camera`  

### AI Tools
- Used for:
  - Code generation and debugging  
  - Hardware setup guidance  
  - Library selection  
  - System design decisions  

---

## System Architecture

- **ESP32-CAM**
  - Captures grayscale frames  
  - Uses frame differencing to detect motion  
  - Determines position (hot vs cool side)  
  - Sends data via UART  

- **ESP32-C6**
  - Reads temperature and humidity every minute  
  - Receives motion data every 10 seconds  
  - Sends hourly email reports over Wi-Fi  

---

## Key Features

- Continuous environmental monitoring  
- Behavior-based analysis (position tracking)  
- Automated email reporting  
- Multi-device communication (UART)  
- Startup handshake system to verify device connection  

---

## Project Scope
This project focuses on **monitoring and reporting** habitat conditions and basic behavioral patterns. It does not diagnose health issues or replace veterinary care.

---

## Final Results

- Successfully collected **temperature and humidity data**  
- Successfully implemented **camera-based motion detection**  
- Established **communication between ESP32-CAM and ESP32-C6**  
- Generated and sent **automated email reports**  
- Demonstrated a working system capable of identifying potential habitat issues  

---

## Project Status
✅ **Completed**

All core features have been implemented, tested, and demonstrated:
- Hardware setup complete  
- Data collection functional  
- Communication between devices established  
- Reporting system operational  

---

## Author
Hank Slaby
