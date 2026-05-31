#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ModbusMaster.h>
#include <Preferences.h>

// =========================
// WiFi Setting
// =========================
const char ssid[] = "แก้ชื่อ Wi-Fi";
const char pass[] = "รหัสการเชื่อมต่อ Wi-Fi";  //Wi-Fi เป็นคลื่น 2.4 GHz เท่านั้น บอร์ด ESP32 Dev ยังไม่รองรับการเชื่อมต่อกับคลื่น 5 GHz

// =========================
// Blynk Auth
// =========================
const char auth[] = "PASTE_YOUR_TOKEN_HERE";  //Token จาก app blynk

// =========================
// Blynk Legacy Server
// =========================
const char blynk_server[] = "blynk-local-server-ip ที่ส่งให้ในเมล์";
const int  blynk_port = 8080;

// =========================
// ESP32 Dev Module Pins
// =========================
#define RX2_PIN 16
#define TX2_PIN 17

#define BLYNK_LED_PIN 2    // Active High

// =========================
// Relay Module 2CH Active Low
// =========================
#define RELAY_CH1 18       // Valve1 Auto/Manual
#define RELAY_CH2 19       // Valve2 Manual Only

#define RELAY_ON  LOW
#define RELAY_OFF HIGH

#define LED_ON    HIGH
#define LED_OFF   LOW

// =========================
// Blynk Virtual Pins
// =========================
// Valve1 / Soil Sensor
#define VPIN_VALVE1      V1
#define VPIN_SOIL1       V2
#define VPIN_AUTO1       V3
#define VPIN_THRESHOLD1  V4

// Valve2 Manual Only
#define VPIN_VALVE2      V5

// =========================
// Global Objects
// =========================
ModbusMaster node;
BlynkTimer timer;
Preferences preferences;

// =========================
// Global Variables
// =========================
bool  isAutoMode1 = false;
float soilThreshold1 = 50.0;
float soilMoisture1 = 0.0;

// =========================
// Function Prototypes
// =========================
void connectWiFi();
void checkConnections();

void readSoilSensor();
bool readSoilBySlave(uint8_t slaveId, float &value);

void controlValve1Auto();
void syncAllToBlynk();

void printModbusError(uint8_t errorCode);

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.println();
  Serial.println("====================================");
  Serial.println("ESP32 Dev Module Smart Farm");
  Serial.println("1 Soil Sensor + Relay 2CH Active Low");
  Serial.println("CH1 GPIO18 = Valve1 Auto/Manual");
  Serial.println("CH2 GPIO19 = Valve2 Manual Only");
  Serial.println("GPIO2 Active High = Blynk Indicator");
  Serial.println("====================================");

  // Serial2 for Modbus RTU
  Serial2.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);

  pinMode(BLYNK_LED_PIN, OUTPUT);
  pinMode(RELAY_CH1, OUTPUT);
  pinMode(RELAY_CH2, OUTPUT);

  // ปิดทั้งหมดตอนเริ่มต้น
  digitalWrite(BLYNK_LED_PIN, LED_OFF);
  digitalWrite(RELAY_CH1, RELAY_OFF);
  digitalWrite(RELAY_CH2, RELAY_OFF);

  // Preferences
  preferences.begin("sensor_data", false);

  isAutoMode1    = preferences.getBool("auto1", false);
  soilThreshold1 = preferences.getFloat("th1", 50.0);
  soilMoisture1  = preferences.getFloat("soil1", 0.0);

  Serial.println("=== Loaded Preferences ===");
  Serial.printf("Valve1 -> Auto:%d Threshold:%.1f Soil:%.1f\n",
                isAutoMode1, soilThreshold1, soilMoisture1);

  // Connect WiFi
  connectWiFi();

  // Blynk Legacy
  Blynk.config(auth, blynk_server, blynk_port);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connecting to Blynk...");
    Blynk.connect(5000);
  }

  if (Blynk.connected()) {
    digitalWrite(BLYNK_LED_PIN, LED_ON);
    Serial.println("Blynk connected.");
  } else {
    digitalWrite(BLYNK_LED_PIN, LED_OFF);
    Serial.println("Blynk not connected.");
  }

  // Modbus Soil Sensor Slave ID 1
  node.begin(1, Serial2);

  timer.setInterval(10000L, checkConnections);
  timer.setInterval(15000L, readSoilSensor);

  // อ่านค่าเร็วหลังบูต
  timer.setTimeout(3000L, readSoilSensor);
}

// =========================
// Connect WiFi
// =========================
void connectWiFi() {
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connection failed!");
  }
}

// =========================
// Blynk Connected
// =========================
BLYNK_CONNECTED() {
  Serial.println("Blynk connected! Synchronizing virtual pins...");

  digitalWrite(BLYNK_LED_PIN, LED_ON);

  // Sync Valve1 Auto/Threshold
  Blynk.syncVirtual(VPIN_AUTO1, VPIN_THRESHOLD1);

  // ถ้า Valve1 ไม่ได้อยู่ Auto ให้ Sync ปุ่ม Manual
  if (!isAutoMode1) {
    Blynk.syncVirtual(VPIN_VALVE1);
  }

  // Valve2 เป็น Manual อย่างเดียว
  Blynk.syncVirtual(VPIN_VALVE2);

  syncAllToBlynk();

  // อ่าน sensor ใหม่ทันทีหลัง Blynk Connect
  readSoilSensor();
}

// =========================
// Sync All Widgets
// =========================
void syncAllToBlynk() {
  Blynk.virtualWrite(VPIN_AUTO1, isAutoMode1);
  Blynk.virtualWrite(VPIN_THRESHOLD1, soilThreshold1);
  Blynk.virtualWrite(VPIN_SOIL1, soilMoisture1);

  Blynk.virtualWrite(VPIN_VALVE1, digitalRead(RELAY_CH1) == RELAY_ON ? 1 : 0);
  Blynk.virtualWrite(VPIN_VALVE2, digitalRead(RELAY_CH2) == RELAY_ON ? 1 : 0);
}

// =========================
// Check Connections
// =========================
void checkConnections() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Reconnecting...");
    digitalWrite(BLYNK_LED_PIN, LED_OFF);

    WiFi.disconnect();
    WiFi.begin(ssid, pass);

    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.println("WiFi reconnected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println();
      Serial.println("WiFi reconnect failed.");
      return;
    }
  }

  if (!Blynk.connected()) {
    Serial.println("Blynk disconnected! Reconnecting...");
    digitalWrite(BLYNK_LED_PIN, LED_OFF);

    Blynk.connect(5000);

    if (Blynk.connected()) {
      Serial.println("Blynk reconnected!");
      digitalWrite(BLYNK_LED_PIN, LED_ON);
    } else {
      Serial.println("Blynk reconnect failed.");
      digitalWrite(BLYNK_LED_PIN, LED_OFF);
    }
  } else {
    digitalWrite(BLYNK_LED_PIN, LED_ON);
  }
}

// =========================
// Valve1 Manual Control
// =========================
BLYNK_WRITE(VPIN_VALVE1) {
  if (!isAutoMode1) {
    int state = param.asInt();

    digitalWrite(RELAY_CH1, state ? RELAY_ON : RELAY_OFF);

    Serial.print("Valve1 Manual = ");
    Serial.println(state ? "ON" : "OFF");
  } else {
    Serial.println("Valve1 is in AUTO mode, manual command ignored.");
    Blynk.virtualWrite(VPIN_VALVE1, digitalRead(RELAY_CH1) == RELAY_ON ? 1 : 0);
  }
}

// =========================
// Valve2 Manual Only
// =========================
BLYNK_WRITE(VPIN_VALVE2) {
  int state = param.asInt();

  digitalWrite(RELAY_CH2, state ? RELAY_ON : RELAY_OFF);

  Serial.print("Valve2 Manual Only = ");
  Serial.println(state ? "ON" : "OFF");
}

// =========================
// Valve1 Auto Mode
// =========================
BLYNK_WRITE(VPIN_AUTO1) {
  isAutoMode1 = param.asInt();
  preferences.putBool("auto1", isAutoMode1);

  Serial.print("Valve1 Mode = ");
  Serial.println(isAutoMode1 ? "Auto" : "Manual");

  if (!isAutoMode1) {
    // ออกจาก Auto แล้วปิด Valve1 ก่อน เพื่อความปลอดภัย
    digitalWrite(RELAY_CH1, RELAY_OFF);
    Blynk.virtualWrite(VPIN_VALVE1, 0);
  } else {
    controlValve1Auto();
  }
}

BLYNK_WRITE(VPIN_THRESHOLD1) {
  soilThreshold1 = param.asFloat();
  preferences.putFloat("th1", soilThreshold1);

  Serial.print("Valve1 Soil Threshold = ");
  Serial.println(soilThreshold1);

  if (isAutoMode1) {
    controlValve1Auto();
  }
}

// =========================
// Read Soil Sensor
// =========================
void readSoilSensor() {
  Serial.println();
  Serial.println("=== Reading Soil Moisture Sensor ===");

  float newValue = 0.0;

  if (readSoilBySlave(1, newValue)) {
    soilMoisture1 = newValue;
    preferences.putFloat("soil1", soilMoisture1);

    Blynk.virtualWrite(VPIN_SOIL1, soilMoisture1);

    Serial.print("Soil Moisture = ");
    Serial.print(soilMoisture1, 1);
    Serial.println(" %");

    controlValve1Auto();
  } else {
    Serial.println("Soil Sensor read failed!");
  }

  Serial.println("====================================");
}

// =========================
// Soil Read Function
// =========================
bool readSoilBySlave(uint8_t slaveId, float &value) {
  node.begin(slaveId, Serial2);

  uint8_t result = node.readHoldingRegisters(0x0000, 3);

  if (result == node.ku8MBSuccess) {
    uint16_t reg0 = node.getResponseBuffer(0);
    uint16_t reg1 = node.getResponseBuffer(1);
    uint16_t reg2 = node.getResponseBuffer(2);

    float tempValue = reg2 / 10.0f;

    Serial.print("Slave ID ");
    Serial.print(slaveId);
    Serial.print(" Register 0 = ");
    Serial.println(reg0);

    Serial.print("Slave ID ");
    Serial.print(slaveId);
    Serial.print(" Register 1 = ");
    Serial.println(reg1);

    Serial.print("Slave ID ");
    Serial.print(slaveId);
    Serial.print(" Register 2 Raw Soil = ");
    Serial.println(reg2);

    Serial.print("Parsed Soil Moisture = ");
    Serial.print(tempValue, 1);
    Serial.println(" %");

    if (tempValue >= 0.0 && tempValue <= 100.0) {
      value = tempValue;
      return true;
    } else {
      Serial.println("Soil value out of range!");
    }
  } else {
    Serial.print("Read Soil failed, Slave ID = ");
    Serial.print(slaveId);
    Serial.print(" Error Code = ");
    Serial.println(result);

    printModbusError(result);
  }

  return false;
}

// =========================
// Valve1 Auto Control
// =========================
void controlValve1Auto() {
  if (isAutoMode1) {
    Serial.print("Valve1 AUTO Check -> Soil = ");
    Serial.print(soilMoisture1);
    Serial.print(" Threshold = ");
    Serial.println(soilThreshold1);

    if (soilMoisture1 < soilThreshold1) {
      digitalWrite(RELAY_CH1, RELAY_ON);
      Blynk.virtualWrite(VPIN_VALVE1, 1);
      Serial.println("Valve1 AUTO -> ON");
    } else {
      digitalWrite(RELAY_CH1, RELAY_OFF);
      Blynk.virtualWrite(VPIN_VALVE1, 0);
      Serial.println("Valve1 AUTO -> OFF");
    }
  }
}

// =========================
// Modbus Error
// =========================
void printModbusError(uint8_t errorCode) {
  switch (errorCode) {
    case node.ku8MBIllegalFunction:
      Serial.println("Error: Illegal Function");
      break;

    case node.ku8MBIllegalDataAddress:
      Serial.println("Error: Illegal Data Address");
      break;

    case node.ku8MBIllegalDataValue:
      Serial.println("Error: Illegal Data Value");
      break;

    case node.ku8MBSlaveDeviceFailure:
      Serial.println("Error: Slave Device Failure");
      break;

    case node.ku8MBInvalidSlaveID:
      Serial.println("Error: Invalid Slave ID");
      break;

    case node.ku8MBInvalidFunction:
      Serial.println("Error: Invalid Function");
      break;

    case node.ku8MBResponseTimedOut:
      Serial.println("Error: Response Timed Out");
      break;

    case node.ku8MBInvalidCRC:
      Serial.println("Error: Invalid CRC");
      break;

    default:
      Serial.println("Error: Unknown Modbus Error");
      break;
  }
}

// =========================
// Loop
// =========================
void loop() {
  if (Blynk.connected()) {
    Blynk.run();
  }

  timer.run();
}
