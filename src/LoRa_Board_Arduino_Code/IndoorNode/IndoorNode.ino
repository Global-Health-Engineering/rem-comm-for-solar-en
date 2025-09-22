// ETH Zürich
// Global Health Engineering Lab
// Masterthesis Voeten Jerun
// Indoor Node Skript
// August 2025

#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>
#include <Wire.h>
#include <avr/wdt.h>
#include <math.h>

// Define sensor pin assignments
#define DHTPIN       6       // Pin for DHT22 data line
#define DHTTYPE      DHT22   // Specify DHT sensor type
#define PB_PIN       A0      // Analog pin for battery voltage measurement

const int LEDPinRed   = 9;
const int LEDPinGreen = 10;

// Disable watchdog early during startup (before C runtime begins)
void disableWatchdog() __attribute__((naked)) __attribute__((section(".init3")));
void disableWatchdog() {
  MCUSR = 0;          // Clear MCU status register to reset flags
  wdt_disable();      // Disable the watchdog timer
}

// DHT sensor object for temperature and humidity readings
DHT dht(DHTPIN, DHTTYPE);

// Node configuration and timing for transmission
const uint8_t node = 1;  // Node ID for this device: indoor node
const unsigned long interval = 120000;
unsigned long previousMillis = 0;

// Default values for sensor fallbacks
const int16_t DEF_TEMP   = 250;   // Default encoded temperature (25.0°C)
const uint16_t DEF_HUM   = 500;   // Default encoded humidity (50.0%)
const float DEF_CUR1   = 0;  // Default current ebike 1
const float DEF_CUR2  = 0;     // Default current ebike 2
const uint8_t DEF_BATSOC = 100;   // Default battery state of charge (%)

// Store last successful sensor readings
int16_t lastTemp;   
int16_t lastHum;     
float lastCur1;
float lastCur2;
uint8_t lastBatSOC;

// Define sensor boundaries
int16_t maxDeltaTemp = 200, maxDeltaHum = 400, minTemp = -400, maxTemp = 800, minHum = 0, maxHum = 1000; //values/10 = °C,%
int16_t maxDeltaBat = 40, lowBat = 25, criticalBat = 15; // %

// Retry configuration for various sensors
int DHT_MAX_TRIES  = 4;
int DHT_NEED_GOOD  = 2;
int CUR_MAX_TRIES = 6;
int CUR_NEED_GOOD = 4;
int SOC_MAX_TRIES  = 5;
int SOC_NEED_GOOD  = 3;

// current sensor specs & calib
const float mVperAmp   = 100.0f;      // 20A current sensor sensitivity
const double ACSoffset1 = 2569.0;     // Sensor1 no-load offset in mV
const double ACSoffset2 = 2505.0;     // Sensor2 no-load offset in mV

// Bitmask to indicate which sensors failed (1 = failure)
uint8_t sensorState = 0b0000; // 1st bit (MSB): dht, 2nd bit: smart shunt, 4th bit (LSB): bat soc

// Buffers and flags for SmartShunt telemetry data
float mainBatSOC      = 0.0f;
float mainBatVoltage  = 0.0f;
float mainBatCurrent  = 0.0f;
int32_t mainBatCE     = 0; // consumed Amp Hours 
uint16_t mainBatChargeCycles = 0; // H4
bool hasCE  = false, hasH4 = false;
bool hasSOC = false, hasV  = false, hasI  = false;
char lineBuf[64];   // Buffer for incoming SmartShunt data lines
uint8_t bufIdx = 0;  // Write index for buffer

//----------------------------------------------------------------------------
// CRC-8 calculation (Dallas/Maxim algorithm)
uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  while (len--) {
    uint8_t extract = *data++;
    for (uint8_t tempI = 8; tempI; tempI--) {
      uint8_t sum = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum) crc ^= 0x8C;
      extract >>= 1;
    }
  }
  return crc;
}

//----------------------------------------------------------------------------
// Secure DHT22 reading: averages multiple samples, enforces bounds
bool readDHT22(int16_t &fridgeTemp, int16_t &fridgeHum, bool init = false) {
  int tries = 0, good = 0;
  int32_t sumT = 0, sumH = 0;

  // Attempt multiple readings until enough good ones are obtained
  while (tries < DHT_MAX_TRIES && good < DHT_NEED_GOOD) {
    tries++;
    float t = dht.readTemperature();    // Raw temperature
    float h = dht.readHumidity();       // Raw humidity
    bool ok = false;

    if (!isnan(t) && !isnan(h)) {
      int16_t t_int = (int16_t)roundf(t * 10.0f);
      int16_t h_int = (int16_t)roundf(h * 10.0f);

      // Validate reading ranges
      if (t_int >= minTemp && t_int <= maxTemp && h_int <= maxHum) {
        // Accept if within allowed delta or during initialization
        if (init || (abs(t_int - lastTemp) <= maxDeltaTemp && abs(h_int - lastHum) <= maxDeltaHum)) {
          ok = true;
          sumT += t_int;
          sumH += h_int;
        }
      }
    }

    if (ok) good++;
    wdt_reset();  // Reset watchdog to prevent reset
   
    // Delay between tries, skip if initialization only needs one
    if (!ok || init) delay(2000);
  }

  bool valid = (good >= DHT_NEED_GOOD);
  int16_t avgT = valid ? sumT / DHT_NEED_GOOD : DEF_TEMP;
  int16_t avgH = valid ? sumH / DHT_NEED_GOOD : DEF_HUM;

  if (valid) {
    fridgeTemp = avgT;
    fridgeHum  = avgH;
    lastTemp = avgT;
    lastHum  = avgH;
    sensorState &= ~0b1000;   // Clear DHT error flag
  } else {
    if (init) {
      lastTemp = DEF_TEMP;
      lastHum = DEF_HUM;
      fridgeTemp = DEF_TEMP;
      fridgeHum = DEF_HUM;
    }
    sensorState |= 0b1000;    // Set DHT error flag
  }
  
  return valid;
}

//----------------------------------------------------------------------------
// Read current in amps from analog pin, apply calibration and averaging
bool readCurrent(int analogPin, float &currentA, bool init = false) {

  int tries = 0, good = 0;
  float sumC = 0.0f;

  while (tries < CUR_MAX_TRIES && good < CUR_NEED_GOOD) {
    tries++;
    int raw = analogRead(analogPin);  // Raw ADC value
    
    // Convert ADC to voltage in mV
    float rawVoltage_mV = raw * (3300.0f / 1023.0f);
    // Voltage divider correction
    float voltage_mV = rawVoltage_mV * ((18.0f + 10.0f) / 18.0f); // 18kohm & 10kohm resistors
    // Calculate current: subtract offset, divide by sensitivity
    float c = (voltage_mV - (analogPin == A1 ? ACSoffset1 : ACSoffset2)) / mVperAmp;
    
    // Serial.println(voltage_mV); // usefull for calibration -> ACSoffset
    
    sumC += c;
    good++;
    delay(200);
  }

  bool valid = (good >= CUR_NEED_GOOD);
  float avgC = valid ? sumC / CUR_NEED_GOOD : (analogPin == A1 ? lastCur1 : lastCur2);

  if (valid) {
    currentA = avgC;
    if (analogPin == A1) lastCur1 = avgC;
    else                 lastCur2 = avgC;
  } else {
      if (init) {
        if (analogPin == A1) lastCur1 = DEF_CUR1, currentA = DEF_CUR1;
        else                 lastCur2 = DEF_CUR2, currentA = DEF_CUR2;
      }
    //currentA = (analogPin == A1 ? lastCur1 : lastCur2);
  }

  // Serial.print("Strom A");
  // Serial.print(analogPin - A0);
  // Serial.print(" = ");
  // Serial.println(currentA, 3);
  return valid;
}

//----------------------------------------------------------------------------
// Read battery SOC from analog pin, convert voltage to percentage, update LEDs
bool readBatterySOC(uint8_t &soc, bool init = false) {

  int tries = 0, good = 0;
  int sumB = 0;

  while (tries < SOC_MAX_TRIES && good < SOC_NEED_GOOD) {
    tries++;
    int raw = analogRead(PB_PIN);
    float bVolt = raw * (3.3f / 1023.0f) * ((22.0f + 10.0f) / 22.0f);
    int bSOC = map((int)roundf(bVolt * 100.0f), 300, 420, 0, 100);
    uint8_t bSOC_int = (uint8_t)constrain(bSOC, 0, 100);

    bool ok = false;
    if (init || (abs((int)bSOC_int - (int)lastBatSOC) <= maxDeltaBat)) {
      sumB += bSOC_int;
      ok = true;
    }
    if (ok) good++;
    delay(200);
  }

  bool valid = (good >= SOC_NEED_GOOD);
  uint8_t avgB = valid ? sumB / SOC_NEED_GOOD : DEF_BATSOC;

  if (valid) {
    soc = avgB;
    lastBatSOC = avgB;
    sensorState &= ~0b0001;  // Clear SOC error flag
  } else {
    if (init) {
      lastBatSOC = DEF_BATSOC;
      soc = DEF_BATSOC;
    }
    sensorState |= 0b0001;   // Set SOC error flag
  }

  // LED indication: red <15%, green off >25%
  if (soc > lowBat) {
    digitalWrite(LEDPinRed, LOW);
    digitalWrite(LEDPinGreen, LOW);
  } else if (soc < criticalBat) {
    digitalWrite(LEDPinRed, HIGH);
    digitalWrite(LEDPinGreen, LOW);
  } else {
    digitalWrite(LEDPinRed, HIGH);
    digitalWrite(LEDPinGreen, HIGH);
  }

  return valid;
}

//----------------------------------------------------------------------------
// Parse tab-separated SmartShunt data lines into variables
void parseShunt(const String &line) {
  int idx = line.indexOf('\t');
  if (idx < 0) return;
  String key = line.substring(0, idx);
  String val = line.substring(idx + 1);

  if (key == "SOC") {
    mainBatSOC = val.toFloat() / 10.0f;
    hasSOC = true;
  } else if (key == "V") {
    mainBatVoltage = val.toFloat() / 1000.0f;
    hasV = true;
  } else if (key == "I") {
    mainBatCurrent = val.toFloat() / 1000.0f;
    hasI = true;
  } else if (key == "CE") {
    mainBatCE = val.toInt();
    hasCE = true;
  } else if (key == "H4") {
    mainBatChargeCycles = val.toInt();
    hasH4 = true;
  }
}

//----------------------------------------------------------------------------
// Initialize last valid readings for all sensors at startup
void initializeLastValues() {

  lastTemp     = DEF_TEMP;
  lastHum      = DEF_HUM;
  lastCur1      = DEF_CUR1;
  lastCur2   = DEF_CUR2;
  lastBatSOC      = DEF_BATSOC;

  int16_t t;   int16_t h;
  readDHT22(t, h, true);

  // Initialize eBike current sensors
  float c1, c2;
  readCurrent(A1, c1, true);
  readCurrent(A2, c2, true);

  uint8_t b;
  readBatterySOC(b, true);

  // Adjust retry parameters for normal operation
  DHT_MAX_TRIES  = 2;
  DHT_NEED_GOOD  = 1;
  CUR_MAX_TRIES = 5;
  CUR_NEED_GOOD = 3;
  SOC_MAX_TRIES  = 4;
  SOC_NEED_GOOD  = 2;
}

//----------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  delay(3000);

  Wire.begin();

  Serial1.begin(19200); // smart shunt communication

  randomSeed(analogRead(A5));  // Seed random for jitter, A5: random signal

  pinMode(LEDPinRed,   OUTPUT);
  pinMode(LEDPinGreen, OUTPUT);

  // Initialize LoRa module for 868 MHz
  LoRa.setPins(8, 4, 7);
  if (!LoRa.begin(868E6)) {
    while (1);
  }

  // Start DHT sensor
  dht.begin();

  initializeLastValues();

  digitalWrite(LEDPinRed, LOW);
  digitalWrite(LEDPinGreen, LOW);
  // Enable watchdog with 8s timeout
  wdt_enable(WDTO_8S);
}

//----------------------------------------------------------------------------
void loop() {
  wdt_reset();
  // Read and parse incoming SmartShunt data on Serial1
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (bufIdx > 0) {
        lineBuf[bufIdx] = '\0';
        parseShunt(String(lineBuf));
        bufIdx = 0;
        sensorState &= ~0b0100;
      }
    } else {
      if (bufIdx < sizeof(lineBuf) - 1) {
        lineBuf[bufIdx++] = c;
      } else {
        bufIdx = 0;
        sensorState |= 0b0100;
      }
    }
  }

  wdt_reset();  // Reset watchdog periodically

  unsigned long now = millis();  // Current time in ms

  // Check if it's time to read sensors and send data
  if (now - previousMillis >= interval) {
    
    delay(random(0, 5000));         // Add jitter to avoid collisions
    previousMillis = now;
    wdt_reset();    

    // 1. Read DHT22 sensors
    int16_t inT, inH;
    if (!readDHT22(inT, inH)) {
      inT = lastTemp;
      inH = lastHum;
    }

    wdt_reset();  
    // 2. Read InNode battery SOC
    uint8_t InNodeBatSOC;
    if (!readBatterySOC(InNodeBatSOC)) InNodeBatSOC = lastBatSOC;
    
    wdt_reset();  
    // 3. Read currents from both channels
    float cur1 = 0.0f, cur2 = 0.0f;
    if (!readCurrent(A1, cur1)) cur1 = lastCur1;
    if (!readCurrent(A2, cur2)) cur2 = lastCur2;

    // Determine eBike states: 0=off,1=charging,2=discharging
    uint8_t eBikeState1 = (cur1 >  0.1f ? 1 : (cur1 < -0.1f ? 2 : 0));
    uint8_t eBikeState2 = (cur2 >  0.1f ? 1 : (cur2 < -0.1f ? 2 : 0));
    uint8_t eBikeStatePacked = (eBikeState1 & 0x03) | ((eBikeState2 & 0x03) << 2);

    wdt_reset();  
    
    // prepare payload
    uint8_t signByte = 0;
    if (inT < 0)      signByte |= (1 << 0);
    if (mainBatCurrent < 0.0f) signByte |= (1 << 1);
    if (mainBatCE < 0)         signByte |= (1 << 2);
    uint16_t tempAbs = (uint16_t)abs(inT);
    uint16_t hum = inH;
    uint16_t socMain = (uint16_t)round(mainBatSOC * 10.0f);
    uint16_t volt_enc = (uint16_t)round(mainBatVoltage * 10.0f);
    uint16_t curr_abs = (uint16_t)roundf(fabs(mainBatCurrent) * 10.0f);
    int32_t ceValAbs = abs(mainBatCE); // consumed_mAh

    uint8_t payload[21] = {0};
    // Build LoRa payload byte by byte
    payload[0] = node;  // Node ID
    payload[1] = signByte;
    payload[2] = (tempAbs >> 8) & 0xFF;
    payload[3] =  tempAbs       & 0xFF;
    payload[4] = (hum >> 8) & 0xFF;
    payload[5] =  hum       & 0xFF;
    payload[6] = InNodeBatSOC;
    payload[7] = (socMain >> 8) & 0xFF;
    payload[8] =  socMain       & 0xFF;
    payload[9]  = (volt_enc >> 8) & 0xFF;
    payload[10] =  volt_enc       & 0xFF;
    payload[11] = (curr_abs >> 8) & 0xFF;
    payload[12] =  curr_abs       & 0xFF;    
    payload[13] = (ceValAbs >> 16) & 0xFF;
    payload[14] = (ceValAbs >> 8)  & 0xFF;
    payload[15] =  ceValAbs        & 0xFF;
    payload[16] = (mainBatChargeCycles >> 8) & 0xFF;
    payload[17] =  mainBatChargeCycles       & 0xFF;
    payload[18] = eBikeStatePacked;
    payload[19] = sensorState;
    payload[20] = crc8(payload, 20);

    // Send packet via LoRa
    LoRa.beginPacket();
    LoRa.write(payload, sizeof(payload));
    LoRa.endPacket();

  }
}
