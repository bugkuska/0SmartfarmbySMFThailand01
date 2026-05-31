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
#define RX2_PIN 16    // ESP32 RX2 รับข้อมูลจาก TTL485 TXD
#define TX2_PIN 17    // ESP32 TX2 ส่งข้อมูลไป TTL485 RXD

#define BLYNK_LED_PIN 2    // GPIO2 Active High แสดงสถานะ Blynk Connected

// =========================
// Relay Module 2CH Active Low
// =========================
#define RELAY_CH1 18       // CH1 = Valve1
#define RELAY_CH2 19       // CH2 = Valve2

#define RELAY_ON  LOW      // Active Low Relay
#define RELAY_OFF HIGH

#define LED_ON    HIGH     // GPIO2 Active High
#define LED_OFF   LOW

// =========================
// Blynk Virtual Pins
// =========================
// Zone 1
#define VPIN_VALVE1      V2
#define VPIN_SOIL1       V4
#define VPIN_AUTO1       V5
#define VPIN_THRESHOLD1  V6

// Zone 2
#define VPIN_VALVE2      V11
#define VPIN_SOIL2       V12
#define VPIN_AUTO2       V13
#define VPIN_THRESHOLD2  V14

// =========================
// Global Objects
// =========================
ModbusMaster node;
BlynkTimer timer;
Preferences preferences;

// =========================
// Global Variables
// =========================
// Zone 1
bool  isAutoMode1 = false;
float soilThreshold1 = 50.0;
float soilMoisture1 = 0.0;

// Zone 2
bool  isAutoMode2 = false;
float soilThreshold2 = 50.0;
float soilMoisture2 = 0.0;

// =========================
// Function Prototypes
// =========================
void connectWiFi();
void checkConnections();
void readAllSensors();

void readZone1();
void readZone2();

bool readSoilBySlave(uint8_t slaveId, float &value);
void printModbusError(uint8_t errorCode);

void controlZone1Auto();
void controlZone2Auto();

void syncAllToBlynk();

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.println();
  Serial.println("====================================");
  Serial.println("ESP32 Dev Module Smart Farm");
  Serial.println("WiFi SSID/Password Mode");
  Serial.println("Modbus Soil Moisture + Blynk Legacy");
  Serial.println("Relay 2CH Active Low");
  Serial.println("CH1 GPIO18 = Valve1");
  Serial.println("CH2 GPIO19 = Valve2");
  Serial.println("GPIO2 Active High = Blynk Indicator");
  Serial.println("====================================");

  // Serial2 for Modbus RTU
  Serial2.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);

  // GPIO setup
  pinMode(BLYNK_LED_PIN, OUTPUT);
  pinMode(RELAY_CH1, OUTPUT);
  pinMode(RELAY_CH2, OUTPUT);

  // ปิดอุปกรณ์ทั้งหมดตอนเริ่มต้น
  digitalWrite(BLYNK_LED_PIN, LED_OFF);
  digitalWrite(RELAY_CH1, RELAY_OFF);
  digitalWrite(RELAY_CH2, RELAY_OFF);

  // Preferences
  preferences.begin("sensor_data", false);

  isAutoMode1    = preferences.getBool("auto1", false);
  soilThreshold1 = preferences.getFloat("th1", 50.0);
  soilMoisture1  = preferences.getFloat("soil1", 0.0);

  isAutoMode2    = preferences.getBool("auto2", false);
  soilThreshold2 = preferences.getFloat("th2", 50.0);
  soilMoisture2  = preferences.getFloat("soil2", 0.0);

  Serial.println("=== Loaded Preferences ===");
  Serial.printf("Zone1 -> Auto:%d Threshold:%.1f Soil:%.1f\n",
                isAutoMode1, soilThreshold1, soilMoisture1);

  Serial.printf("Zone2 -> Auto:%d Threshold:%.1f Soil:%.1f\n",
                isAutoMode2, soilThreshold2, soilMoisture2);

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

  // ตั้งค่า Modbus เริ่มต้น
  node.begin(1, Serial2);

  // Timers
  timer.setInterval(10000L, checkConnections);
  timer.setInterval(15000L, readAllSensors);

  // อ่านค่าเร็วหลังบูต
  timer.setTimeout(3000L, readAllSensors);
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

  Blynk.syncVirtual(VPIN_AUTO1, VPIN_THRESHOLD1);
  Blynk.syncVirtual(VPIN_AUTO2, VPIN_THRESHOLD2);

  if (!isAutoMode1) {
    Blynk.syncVirtual(VPIN_VALVE1);
  }

  if (!isAutoMode2) {
    Blynk.syncVirtual(VPIN_VALVE2);
  }

  syncAllToBlynk();

  // อ่าน sensor ใหม่และคุมทันทีหลัง Blynk Connect
  readAllSensors();
}

// =========================
// Sync All Widgets
// =========================
void syncAllToBlynk() {
  Blynk.virtualWrite(VPIN_AUTO1, isAutoMode1);
  Blynk.virtualWrite(VPIN_THRESHOLD1, soilThreshold1);
  Blynk.virtualWrite(VPIN_SOIL1, soilMoisture1);
  Blynk.virtualWrite(VPIN_VALVE1, digitalRead(RELAY_CH1) == RELAY_ON ? 1 : 0);

  Blynk.virtualWrite(VPIN_AUTO2, isAutoMode2);
  Blynk.virtualWrite(VPIN_THRESHOLD2, soilThreshold2);
  Blynk.virtualWrite(VPIN_SOIL2, soilMoisture2);
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
// Manual Controls
// =========================
BLYNK_WRITE(VPIN_VALVE1) {
  if (!isAutoMode1) {
    int state = param.asInt();

    digitalWrite(RELAY_CH1, state ? RELAY_ON : RELAY_OFF);

    Serial.print("Valve1 Manual = ");
    Serial.println(state ? "ON" : "OFF");
  } else {
    Serial.println("Zone1 is in AUTO mode, manual command ignored.");
    Blynk.virtualWrite(VPIN_VALVE1, digitalRead(RELAY_CH1) == RELAY_ON ? 1 : 0);
  }
}

BLYNK_WRITE(VPIN_VALVE2) {
  if (!isAutoMode2) {
    int state = param.asInt();

    digitalWrite(RELAY_CH2, state ? RELAY_ON : RELAY_OFF);

    Serial.print("Valve2 Manual = ");
    Serial.println(state ? "ON" : "OFF");
  } else {
    Serial.println("Zone2 is in AUTO mode, manual command ignored.");
    Blynk.virtualWrite(VPIN_VALVE2, digitalRead(RELAY_CH2) == RELAY_ON ? 1 : 0);
  }
}

// =========================
// Zone 1 Auto Mode
// =========================
BLYNK_WRITE(VPIN_AUTO1) {
  isAutoMode1 = param.asInt();
  preferences.putBool("auto1", isAutoMode1);

  Serial.print("Zone1 Mode = ");
  Serial.println(isAutoMode1 ? "Auto" : "Manual");

  if (!isAutoMode1) {
    digitalWrite(RELAY_CH1, RELAY_OFF);
    Blynk.virtualWrite(VPIN_VALVE1, 0);
  } else {
    controlZone1Auto();
  }
}

BLYNK_WRITE(VPIN_THRESHOLD1) {
  soilThreshold1 = param.asFloat();
  preferences.putFloat("th1", soilThreshold1);

  Serial.print("Zone1 Threshold = ");
  Serial.println(soilThreshold1);

  if (isAutoMode1) {
    controlZone1Auto();
  }
}

// =========================
// Zone 2 Auto Mode
// =========================
BLYNK_WRITE(VPIN_AUTO2) {
  isAutoMode2 = param.asInt();
  preferences.putBool("auto2", isAutoMode2);

  Serial.print("Zone2 Mode = ");
  Serial.println(isAutoMode2 ? "Auto" : "Manual");

  if (!isAutoMode2) {
    digitalWrite(RELAY_CH2, RELAY_OFF);
    Blynk.virtualWrite(VPIN_VALVE2, 0);
  } else {
    controlZone2Auto();
  }
}

BLYNK_WRITE(VPIN_THRESHOLD2) {
  soilThreshold2 = param.asFloat();
  preferences.putFloat("th2", soilThreshold2);

  Serial.print("Zone2 Threshold = ");
  Serial.println(soilThreshold2);

  if (isAutoMode2) {
    controlZone2Auto();
  }
}

// =========================
// Read All Sensors
// =========================
void readAllSensors() {
  Serial.println();
  Serial.println("=== Reading All Soil Sensors ===");

  readZone1();
  delay(300);

  readZone2();
  delay(300);

  Serial.println("================================");
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

    Serial.print("Slave ID ");
    Serial.print(slaveId);
    Serial.print(" Soil Moisture = ");
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
// Zone 1
// =========================
void readZone1() {
  float newValue = 0.0;

  if (readSoilBySlave(1, newValue)) {
    soilMoisture1 = newValue;
    preferences.putFloat("soil1", soilMoisture1);

    Blynk.virtualWrite(VPIN_SOIL1, soilMoisture1);

    Serial.print("Zone1 Soil Moisture = ");
    Serial.println(soilMoisture1);

    controlZone1Auto();
  } else {
    Serial.println("Zone1 read failed!");
  }
}

void controlZone1Auto() {
  if (isAutoMode1) {
    Serial.print("Zone1 AUTO Check -> Soil = ");
    Serial.print(soilMoisture1);
    Serial.print(" Threshold = ");
    Serial.println(soilThreshold1);

    if (soilMoisture1 < soilThreshold1) {
      digitalWrite(RELAY_CH1, RELAY_ON);
      Blynk.virtualWrite(VPIN_VALVE1, 1);
      Serial.println("Zone1 AUTO -> Valve1 ON");
    } else {
      digitalWrite(RELAY_CH1, RELAY_OFF);
      Blynk.virtualWrite(VPIN_VALVE1, 0);
      Serial.println("Zone1 AUTO -> Valve1 OFF");
    }
  }
}

// =========================
// Zone 2
// =========================
void readZone2() {
  float newValue = 0.0;

  if (readSoilBySlave(2, newValue)) {
    soilMoisture2 = newValue;
    preferences.putFloat("soil2", soilMoisture2);

    Blynk.virtualWrite(VPIN_SOIL2, soilMoisture2);

    Serial.print("Zone2 Soil Moisture = ");
    Serial.println(soilMoisture2);

    controlZone2Auto();
  } else {
    Serial.println("Zone2 read failed!");
  }
}

void controlZone2Auto() {
  if (isAutoMode2) {
    Serial.print("Zone2 AUTO Check -> Soil = ");
    Serial.print(soilMoisture2);
    Serial.print(" Threshold = ");
    Serial.println(soilThreshold2);

    if (soilMoisture2 < soilThreshold2) {
      digitalWrite(RELAY_CH2, RELAY_ON);
      Blynk.virtualWrite(VPIN_VALVE2, 1);
      Serial.println("Zone2 AUTO -> Valve2 ON");
    } else {
      digitalWrite(RELAY_CH2, RELAY_OFF);
      Blynk.virtualWrite(VPIN_VALVE2, 0);
      Serial.println("Zone2 AUTO -> Valve2 OFF");
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
