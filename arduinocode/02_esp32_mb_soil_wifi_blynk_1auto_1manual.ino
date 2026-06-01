#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ModbusMaster.h>
#include <Preferences.h>

// =========================
// Debug
// =========================
#define BLYNK_PRINT Serial

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
const char blynk_server[] = "blynk-local-server-ip";
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
#define VPIN_VALVE1      V1
#define VPIN_SOIL1       V2
#define VPIN_AUTO1       V3
#define VPIN_THRESHOLD1  V4
#define VPIN_VALVE2      V5

// =========================
// Production Timing
// =========================
const unsigned long SENSOR_READ_INTERVAL_MS       = 15000UL;
const unsigned long CONNECTION_CHECK_INTERVAL_MS  = 3000UL;
const unsigned long WIFI_RECONNECT_INTERVAL_MS    = 10000UL;
const unsigned long BLYNK_RECONNECT_INTERVAL_MS   = 10000UL;
const unsigned long BLYNK_CONNECT_TIMEOUT_MS      = 2000UL;
const unsigned long MAX_OFFLINE_TIME_MS           = 10UL * 60UL * 1000UL; // 10 นาที

// =========================
// Sensor / Control Setting
// =========================
const float SOIL_HYSTERESIS = 1.0;   // กันรีเลย์ตัดต่อถี่ เช่น threshold 50 ปิดเมื่อ >= 51
const uint8_t MAX_SENSOR_FAILS_BEFORE_SAFE_OFF = 3;

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

uint8_t sensorFailCount = 0;

unsigned long offlineSince = 0;
unsigned long lastWiFiReconnectAttempt = 0;
unsigned long lastBlynkReconnectAttempt = 0;

uint8_t wifiReconnectCount = 0;

// =========================
// Function Prototypes
// =========================
void startWiFi();
void connectionManager();

void readSoilSensor();
bool readSoilBySlave(uint8_t slaveId, float &value);

void controlValve1Auto();
void syncAllToBlynk();

void safeValve1Off();
void safeBlynkVirtualWrite(uint8_t vpin, int value);
void safeBlynkVirtualWriteFloat(uint8_t vpin, float value);

bool isWiFiOK();
bool isBlynkOK();

void printModbusError(uint8_t errorCode);

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("====================================");
  Serial.println("ESP32 Dev Module Smart Farm");
  Serial.println("Production Stable Version");
  Serial.println("1 Soil Sensor + Relay 2CH Active Low");
  Serial.println("CH1 GPIO18 = Valve1 Auto/Manual");
  Serial.println("CH2 GPIO19 = Valve2 Manual Only");
  Serial.println("GPIO2 Active High = Blynk Indicator");
  Serial.println("====================================");

  // -------------------------
  // Pin Initial State
  // -------------------------
  pinMode(BLYNK_LED_PIN, OUTPUT);
  pinMode(RELAY_CH1, OUTPUT);
  pinMode(RELAY_CH2, OUTPUT);

  digitalWrite(BLYNK_LED_PIN, LED_OFF);
  digitalWrite(RELAY_CH1, RELAY_OFF);
  digitalWrite(RELAY_CH2, RELAY_OFF);

  // -------------------------
  // Serial2 for Modbus RTU
  // -------------------------
  Serial2.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);
  node.begin(1, Serial2);

  // -------------------------
  // Preferences
  // -------------------------
  preferences.begin("sensor_data", false);

  isAutoMode1    = preferences.getBool("auto1", false);
  soilThreshold1 = preferences.getFloat("th1", 50.0);

  // ไม่โหลดค่า soil เก่ามาควบคุมวาล์ว เพื่อความปลอดภัย
  soilMoisture1 = 0.0;

  Serial.println("=== Loaded Preferences ===");
  Serial.printf("Valve1 -> Auto:%d Threshold:%.1f\n", isAutoMode1, soilThreshold1);

  // -------------------------
  // WiFi Stable Setting
  // -------------------------
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  startWiFi();

  // -------------------------
  // Blynk Legacy
  // -------------------------
  Blynk.config(auth, blynk_server, blynk_port);

  if (isWiFiOK()) {
    Serial.println("Connecting to Blynk...");
    Blynk.connect(BLYNK_CONNECT_TIMEOUT_MS);
  }

  if (isBlynkOK()) {
    digitalWrite(BLYNK_LED_PIN, LED_ON);
    Serial.println("Blynk connected.");
  } else {
    digitalWrite(BLYNK_LED_PIN, LED_OFF);
    Serial.println("Blynk not connected.");
  }

  // -------------------------
  // Timers
  // -------------------------
  timer.setInterval(CONNECTION_CHECK_INTERVAL_MS, connectionManager);
  timer.setInterval(SENSOR_READ_INTERVAL_MS, readSoilSensor);

  // อ่านค่า sensor หลัง boot ไม่อ่านทันที
  timer.setTimeout(3000L, readSoilSensor);

  Serial.println("Setup completed.");
}

// =========================
// Start WiFi
// =========================
void startWiFi() {
  Serial.println();
  Serial.print("Starting WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000UL) {
    delay(100);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("WiFi initial connection failed. System will retry automatically.");
  }
}

// =========================
// Connection Manager
// =========================
void connectionManager() {
  bool wifiOK = isWiFiOK();
  bool blynkOK = isBlynkOK();

  // -------------------------
  // Connection OK
  // -------------------------
  if (wifiOK && blynkOK) {
    offlineSince = 0;
    digitalWrite(BLYNK_LED_PIN, LED_ON);
    return;
  }

  // -------------------------
  // Connection Problem
  // -------------------------
  digitalWrite(BLYNK_LED_PIN, LED_OFF);

  if (offlineSince == 0) {
    offlineSince = millis();
  }

  Serial.print("Connection status -> WiFi: ");
  Serial.print(wifiOK ? "OK" : "FAIL");
  Serial.print(" | Blynk: ");
  Serial.println(blynkOK ? "OK" : "FAIL");

  // -------------------------
  // Fail-safe Restart
  // -------------------------
  if (millis() - offlineSince > MAX_OFFLINE_TIME_MS) {
    Serial.println("Offline too long. Restarting ESP32 for recovery...");
    delay(1000);
    ESP.restart();
  }

  // -------------------------
  // WiFi Reconnect
  // -------------------------
  if (!wifiOK) {
    if (millis() - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
      lastWiFiReconnectAttempt = millis();
      wifiReconnectCount++;

      Serial.print("Trying WiFi reconnect... attempt ");
      Serial.println(wifiReconnectCount);

      // ทุก 6 รอบ ให้เริ่ม WiFi ใหม่แบบหนักขึ้น
      if (wifiReconnectCount % 6 == 0) {
        Serial.println("Hard WiFi reconnect...");
        WiFi.disconnect(false);
        delay(100);
        WiFi.begin(ssid, pass);
      } else {
        WiFi.reconnect();
      }
    }

    return;
  }

  // ถ้า WiFi กลับมาแล้ว reset count
  wifiReconnectCount = 0;

  // -------------------------
  // Blynk Reconnect
  // -------------------------
  if (wifiOK && !blynkOK) {
    if (millis() - lastBlynkReconnectAttempt >= BLYNK_RECONNECT_INTERVAL_MS) {
      lastBlynkReconnectAttempt = millis();

      Serial.println("Trying Blynk reconnect...");

      if (Blynk.connect(BLYNK_CONNECT_TIMEOUT_MS)) {
        Serial.println("Blynk reconnected.");
        digitalWrite(BLYNK_LED_PIN, LED_ON);
      } else {
        Serial.println("Blynk reconnect failed.");
        digitalWrite(BLYNK_LED_PIN, LED_OFF);
      }
    }
  }
}

// =========================
// Blynk Connected
// =========================
BLYNK_CONNECTED() {
  Serial.println("Blynk connected! Synchronizing virtual pins...");

  digitalWrite(BLYNK_LED_PIN, LED_ON);

  // Sync เฉพาะค่าตั้งค่าเท่านั้น
  // Auto Mode และ Threshold สามารถดึงจาก App ได้
  Blynk.syncVirtual(VPIN_AUTO1);
  Blynk.syncVirtual(VPIN_THRESHOLD1);

  // ห้าม sync ปุ่ม Relay Manual จาก App หลัง reconnect
  // เพราะอาจดึงค่าสถานะเก่าจาก Server มาทับสถานะจริงของ Relay
  // Blynk.syncVirtual(VPIN_VALVE1);
  // Blynk.syncVirtual(VPIN_VALVE2);

  // ให้รอให้ syncVirtual ของ Auto/Threshold ทำงานก่อน
  // แล้วค่อยส่งสถานะจริงของ Relay กลับไปที่ App
  timer.setTimeout(1000L, syncAllToBlynk);

  // ไม่อ่าน Modbus ตรงนี้ ให้หน่วงไปอ่านทีหลัง
  timer.setTimeout(2000L, readSoilSensor);
}

// =========================
// Sync All Widgets
// =========================
void syncAllToBlynk() {
  if (!isBlynkOK()) return;

  Blynk.virtualWrite(VPIN_AUTO1, isAutoMode1 ? 1 : 0);
  Blynk.virtualWrite(VPIN_THRESHOLD1, soilThreshold1);
  Blynk.virtualWrite(VPIN_SOIL1, soilMoisture1);

  Blynk.virtualWrite(VPIN_VALVE1, digitalRead(RELAY_CH1) == RELAY_ON ? 1 : 0);
  Blynk.virtualWrite(VPIN_VALVE2, digitalRead(RELAY_CH2) == RELAY_ON ? 1 : 0);
}

// =========================
// Valve1 Manual Control
// =========================
BLYNK_WRITE(VPIN_VALVE1) {
  int state = param.asInt();

  if (!isAutoMode1) {
    digitalWrite(RELAY_CH1, state ? RELAY_ON : RELAY_OFF);

    int actualState = digitalRead(RELAY_CH1) == RELAY_ON ? 1 : 0;
    safeBlynkVirtualWrite(VPIN_VALVE1, actualState);

    Serial.print("Valve1 Manual = ");
    Serial.println(actualState ? "ON" : "OFF");
  } else {
    Serial.println("Valve1 is in AUTO mode, manual command ignored.");

    int actualState = digitalRead(RELAY_CH1) == RELAY_ON ? 1 : 0;
    safeBlynkVirtualWrite(VPIN_VALVE1, actualState);
  }
}

// =========================
// Valve2 Manual Only
// =========================
BLYNK_WRITE(VPIN_VALVE2) {
  int state = param.asInt();

  digitalWrite(RELAY_CH2, state ? RELAY_ON : RELAY_OFF);

  int actualState = digitalRead(RELAY_CH2) == RELAY_ON ? 1 : 0;
  safeBlynkVirtualWrite(VPIN_VALVE2, actualState);

  Serial.print("Valve2 Manual Only = ");
  Serial.println(actualState ? "ON" : "OFF");
}

// =========================
// Valve1 Auto Mode
// =========================
BLYNK_WRITE(VPIN_AUTO1) {
  bool newMode = param.asInt();

  if (newMode != isAutoMode1) {
    isAutoMode1 = newMode;
    preferences.putBool("auto1", isAutoMode1);
  }

  Serial.print("Valve1 Mode = ");
  Serial.println(isAutoMode1 ? "Auto" : "Manual");

  if (!isAutoMode1) {
    // ออกจาก Auto แล้วปิด Valve1 ก่อน เพื่อความปลอดภัย
    safeValve1Off();
    safeBlynkVirtualWrite(VPIN_VALVE1, 0);
  } else {
    controlValve1Auto();
  }
}

// =========================
// Threshold Setting
// =========================
BLYNK_WRITE(VPIN_THRESHOLD1) {
  float newThreshold = param.asFloat();

  // จำกัดช่วงค่าที่เหมาะสม
  if (newThreshold < 0.0) newThreshold = 0.0;
  if (newThreshold > 100.0) newThreshold = 100.0;

  if (abs(newThreshold - soilThreshold1) >= 0.1) {
    soilThreshold1 = newThreshold;
    preferences.putFloat("th1", soilThreshold1);
  }

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
    sensorFailCount = 0;

    soilMoisture1 = newValue;

    // ไม่เขียน Preferences ทุกครั้ง เพราะจะทำให้ Flash/NVS เสื่อมและหน่วงระบบ
    safeBlynkVirtualWriteFloat(VPIN_SOIL1, soilMoisture1);

    Serial.print("Soil Moisture = ");
    Serial.print(soilMoisture1, 1);
    Serial.println(" %");

    controlValve1Auto();
  } else {
    sensorFailCount++;

    Serial.print("Soil Sensor read failed! Fail count = ");
    Serial.println(sensorFailCount);

    // Fail-safe: ถ้า Auto อยู่ แล้วอ่าน sensor ไม่ได้หลายครั้ง ให้ปิดวาล์วกันน้ำไหลค้าง
    if (isAutoMode1 && sensorFailCount >= MAX_SENSOR_FAILS_BEFORE_SAFE_OFF) {
      Serial.println("Sensor failed repeatedly. Valve1 AUTO safe OFF.");
      safeValve1Off();
      safeBlynkVirtualWrite(VPIN_VALVE1, 0);
    }
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
  if (!isAutoMode1) return;

  bool valveCurrentlyOn = (digitalRead(RELAY_CH1) == RELAY_ON);
  bool targetValveState = valveCurrentlyOn;

  Serial.print("Valve1 AUTO Check -> Soil = ");
  Serial.print(soilMoisture1, 1);
  Serial.print(" Threshold = ");
  Serial.print(soilThreshold1, 1);
  Serial.print(" Hysteresis = ");
  Serial.println(SOIL_HYSTERESIS, 1);

  // เปิดเมื่อความชื้นต่ำกว่า threshold
  if (soilMoisture1 < soilThreshold1) {
    targetValveState = true;
  }

  // ปิดเมื่อความชื้นสูงกว่า threshold + hysteresis
  if (soilMoisture1 >= soilThreshold1 + SOIL_HYSTERESIS) {
    targetValveState = false;
  }

  if (targetValveState != valveCurrentlyOn) {
    digitalWrite(RELAY_CH1, targetValveState ? RELAY_ON : RELAY_OFF);
    safeBlynkVirtualWrite(VPIN_VALVE1, targetValveState ? 1 : 0);

    Serial.print("Valve1 AUTO -> ");
    Serial.println(targetValveState ? "ON" : "OFF");
  } else {
    Serial.print("Valve1 AUTO -> KEEP ");
    Serial.println(valveCurrentlyOn ? "ON" : "OFF");
  }
}

// =========================
// Safe Valve OFF
// =========================
void safeValve1Off() {
  digitalWrite(RELAY_CH1, RELAY_OFF);
  Serial.println("Valve1 -> SAFE OFF");
}

// =========================
// Safe Blynk Write
// =========================
void safeBlynkVirtualWrite(uint8_t vpin, int value) {
  if (isBlynkOK()) {
    Blynk.virtualWrite(vpin, value);
  }
}

void safeBlynkVirtualWriteFloat(uint8_t vpin, float value) {
  if (isBlynkOK()) {
    Blynk.virtualWrite(vpin, value);
  }
}

// =========================
// Connection Helpers
// =========================
bool isWiFiOK() {
  return WiFi.status() == WL_CONNECTED;
}

bool isBlynkOK() {
  return isWiFiOK() && Blynk.connected();
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
  if (isBlynkOK()) {
    Blynk.run();
  }

  timer.run();
  yield();
}
