#include <SimpleTimer.h>
#include <ModbusMaster.h>

SimpleTimer timer;
ModbusMaster node1;   // Modbus Soil Moisture Sensor

// ================= PIN ESP32 =================
#define RX2 16   // ESP32 RX2 รับข้อมูลจาก TTL485 TXD
#define TX2 17   // ESP32 TX2 ส่งข้อมูลไป TTL485 RXD

// ================= MODBUS SETTING =================
#define SOIL_SLAVE_ID 1      // Slave ID ของ Soil Moisture Sensor
#define MODBUS_BAUD   9600   // Baud rate ของ Sensor

// ================= FUNCTION =================
void readSoilMoisture();
void printModbusError(uint8_t errorCode);

void setup()
{
  Serial.begin(9600);
  delay(1000);

  Serial.println();
  Serial.println("====================================");
  Serial.println("ESP32 + TTL to RS485 Auto Direction");
  Serial.println("Modbus Soil Moisture Sensor");
  Serial.println("Function: 03 Read Holding Registers");
  Serial.println("Slave ID: 1");
  Serial.println("Baudrate: 9600, 8N1");
  Serial.println("====================================");

  // Serial2 ใช้สำหรับ Modbus RTU
  Serial2.begin(MODBUS_BAUD, SERIAL_8N1, RX2, TX2);

  // เริ่มต้น Modbus Master
  node1.begin(SOIL_SLAVE_ID, Serial2);

  // อ่านค่าทุก 1 วินาที
  timer.setInterval(1000L, readSoilMoisture);
}

void loop()
{
  timer.run();
}

void readSoilMoisture()
{
  uint8_t result;

  Serial.println();
  Serial.println("Reading Soil Moisture Sensor...");

  /*
    อ่านค่าแบบเดียวกับ Modbus Poll:
    Slave ID = 1
    Function = 03 Read Holding Registers
    Address = 0
    Quantity = 3

    ค่าความชื้นอยู่ที่ Register ลำดับที่ 2
    ดังนั้นใช้ getResponseBuffer(2)
  */

  result = node1.readHoldingRegisters(0x0000, 3);

  if (result == node1.ku8MBSuccess)
  {
    uint16_t reg0 = node1.getResponseBuffer(0);
    uint16_t reg1 = node1.getResponseBuffer(1);
    uint16_t reg2 = node1.getResponseBuffer(2);

    float soilMoisture = reg2 / 10.0f;

    Serial.print("Register 0: ");
    Serial.println(reg0);

    Serial.print("Register 1: ");
    Serial.println(reg1);

    Serial.print("Register 2 Raw Soil Moisture: ");
    Serial.println(reg2);

    Serial.print("Soil Moisture: ");
    Serial.print(soilMoisture, 1);
    Serial.println(" %");
  }
  else
  {
    Serial.print("Modbus Read Failed. Error Code: ");
    Serial.println(result);
    printModbusError(result);
  }

  Serial.println("------------------------------------");
}

void printModbusError(uint8_t errorCode)
{
  switch (errorCode)
  {
    case node1.ku8MBIllegalFunction:
      Serial.println("Error: Illegal Function");
      break;

    case node1.ku8MBIllegalDataAddress:
      Serial.println("Error: Illegal Data Address");
      break;

    case node1.ku8MBIllegalDataValue:
      Serial.println("Error: Illegal Data Value");
      break;

    case node1.ku8MBSlaveDeviceFailure:
      Serial.println("Error: Slave Device Failure");
      break;

    case node1.ku8MBInvalidSlaveID:
      Serial.println("Error: Invalid Slave ID");
      break;

    case node1.ku8MBInvalidFunction:
      Serial.println("Error: Invalid Function");
      break;

    case node1.ku8MBResponseTimedOut:
      Serial.println("Error: Response Timed Out");
      break;

    case node1.ku8MBInvalidCRC:
      Serial.println("Error: Invalid CRC");
      break;

    default:
      Serial.println("Error: Unknown Modbus Error");
      break;
  }
}
