// ETH Zürich
// Global Health Engineering Lab
// Masterthesis Voeten Jerun
// Outdoor Node Skript
// August 2025

#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>
#include <Wire.h>
#include <avr/wdt.h>

// Define sensor pin assignments and constants
#define DHT_PIN       6       // Pin for DHT22 data line
#define DHT_TYPE      DHT22   // Specify DHT sensor type
#define PB_PIN        A0      // Analog pin for battery voltage measurement
#define WIND_PIN      A1      // Pin for wind speed sensor
#define LIGHT_ADDR    0x23    // I2C address for light sensor

const int LEDPinRed   = 9;    // Red LED pin
const int LEDPinGreen = 10;   // Green LED pin

// Disable watchdog early during startup (before C runtime begins)
void disableWatchdog() __attribute__((naked)) __attribute__((section(".init3")));
void disableWatchdog() {
  MCUSR = 0;                // Clear MCU status register
  wdt_disable();            // Disable the watchdog timer
}

// Instantiate the DHT sensor object
DHT dht(DHT_PIN, DHT_TYPE);

// Node configuration and timing for transmission
const uint8_t node       = 2;            // Node ID for this device: outdoor node
const unsigned long interval = 120000;    // Interval between measurements (ms)
unsigned long previousMillis = 0;         // Timestamp of last measurement

// Default values for sensor fallbacks
const int16_t DEF_TEMP   = 250;   // Default encoded temperature (25.0°C)
const uint16_t DEF_HUM   = 500;   // Default encoded humidity (50.0%)
const uint16_t DEF_LUX   = 1000;  // Default illuminance (lux)
const uint16_t DEF_WIND  = 0;     // Default wind speed (m/s)
const uint8_t DEF_BATSOC = 100;   // Default battery state of charge (%)

// Store last successful sensor readings
int16_t lastTemp;
int16_t lastHum;
uint16_t lastLux;
uint16_t lastWindspeed;
uint8_t  lastBatSOC;

// Define sensor boundaries
int16_t maxDeltaTemp = 200, maxDeltaHum = 400, minTemp = -400, maxTemp = 800, minHum = 0, maxHum = 1000; //values/10 = °C,%
uint16_t maxLux = 54612; // lux
int16_t maxDeltaWind = 300, maxWind = 500; // values / 10 = m/s
int16_t maxDeltaBat = 40, lowBat = 25, criticalBat = 15; // %

// Retry configuration for various sensors
int MAX_TRIES = 5;
int NEED_GOOD = 3;
int DHT_MAX_TRIES = 4;
int DHT_NEED_GOOD = 2;

// Bitmask to indicate which sensors failed (1 = failure)
uint8_t sensorState = 0b0000; // 1st bit (MSB): dht, 2nd: light sensor, 3rd: wind sensor, 4th (LSB): bat soc

//----------------------------------------------------------------------------
// Compute CRC-8 checksum for given data buffer
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
// Read a block of registers from the light sensor over I2C
uint8_t readReg(uint8_t reg, uint8_t *pBuf, size_t size) {
  Wire.beginTransmission((uint8_t)LIGHT_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0;  // Transmission failed
  delay(20);                                  // Wait for sensor
  Wire.requestFrom((uint8_t)LIGHT_ADDR, (uint8_t)size);
  for (uint8_t i = 0; i < size; i++) {
    pBuf[i] = Wire.read();                    // Read data bytes
  }
  return size;
}

//----------------------------------------------------------------------------
// Read temperature and humidity from the DHT22 with multiple tries and validation
bool readDHT22(int16_t &tVal, uint16_t &hVal, bool init = false) {
  int tries = 0, good = 0;
  int32_t sumT = 0, sumH = 0;

  // Attempt multiple readings until enough good ones are obtained
  while (tries < DHT_MAX_TRIES && good < DHT_NEED_GOOD) {
    tries++;
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    bool ok = false;
    if (!isnan(t) && !isnan(h)) {
      int16_t t_int = (int16_t)roundf(t * 10.0f);
      int16_t h_int = (int16_t)roundf(h * 10.0f);

      // Validate against expected ranges and last readings
      if (t_int >= minTemp && t_int <= maxTemp && h_int <= maxHum) {
        if (init || (abs(t_int - lastTemp) <= maxDeltaTemp && abs(h_int - lastHum) <= maxDeltaHum)) {
          ok = true;
          sumT += t_int;
          sumH += h_int;
        }
      }
    }

    if (ok) good++;
    wdt_reset();                            // Reset watchdog
    if (!ok || init) delay(2000);          // Delay between attempts
  }

  bool valid = (good >= DHT_NEED_GOOD);
  int16_t avgT = valid ? sumT / DHT_NEED_GOOD : DEF_TEMP;
  uint16_t avgH = valid ? sumH / DHT_NEED_GOOD : DEF_HUM;

  if (valid) {
    tVal = avgT;
    hVal = avgH;
    lastTemp = avgT;
    lastHum = avgH;
    sensorState &= ~0b1000;                // Clear humidity/temperature error flag
  } else {
    if (init) {
      lastTemp = DEF_TEMP;
      lastHum = DEF_HUM;
      tVal = DEF_TEMP;
      hVal = DEF_HUM;
    }
    sensorState |= 0b1000;                 // Set error flag on failure
  }

  return valid;
}

//----------------------------------------------------------------------------
// Read ambient illuminance (lux) with retries and averaging
bool readIlluminance(uint16_t &luxVal, bool init = false) {

  int tries = 0, good = 0;
  int32_t sumL = 0;

  while (tries < MAX_TRIES && good < NEED_GOOD) {
    tries++;
    uint8_t buf[2];
    if (readReg(0x10, buf, 2) == 2) {
      uint16_t raw = (buf[0] << 8) | buf[1];
      uint16_t l = (uint16_t)(raw / 1.2f);

      if (l <= maxLux) {
        sumL += l;
        good++;
      }
    }
    delay(200);
  }

  bool valid = (good >= NEED_GOOD);
  uint16_t avgL = valid ? sumL / NEED_GOOD : DEF_LUX;

  if (valid) {
    luxVal = avgL;
    lastLux = avgL;
    sensorState &= ~0b0100;                // Clear illuminance error flag
  } else {
    if (init) {
      lastLux = DEF_LUX;
      luxVal = DEF_LUX;
    }
    sensorState |= 0b0100;                 // Set error flag
  }

  return valid;
}

//----------------------------------------------------------------------------
// Read wind speed using analog input and calculate in tenths of m/s
bool readWindspeed(uint16_t &windVal, bool init = false) {

  int tries = 0, good = 0;
  int32_t sumW = 0;

  while (tries < MAX_TRIES && good < NEED_GOOD) {
    tries++;
    int raw = analogRead(WIND_PIN);
    float w = raw * (3.3f / 1023.0f);
    uint16_t w_int = (uint16_t)((w / 2.5f) * 45.0f * 10.0f); // formula from datasheet

    if (w_int <= maxWind) {
      if (init || abs((int)w_int - (int)lastWindspeed) <= maxDeltaWind) {
        sumW += w_int;
        good++;
      }
    }
    delay(200);
  }

  bool valid = (good >= NEED_GOOD);
  uint16_t avgW = valid ? sumW / NEED_GOOD : DEF_WIND;
  constrain(avgW, 0, 500);                  // Ensure within bounds

  if (valid) {
    windVal = avgW;
    lastWindspeed = avgW;
    sensorState &= ~0b0010;                // Clear wind error flag
  } else {
    if (init) {
      lastWindspeed = DEF_WIND;
      windVal = DEF_WIND;
    }
    sensorState |= 0b0010;                 // Set error flag
  }

  return valid;
}

//----------------------------------------------------------------------------
// Read battery voltage and convert to state-of-charge percentage
bool readBatterySOC(uint8_t &socVal, bool init = false) {

  int tries = 0, good = 0;
  int32_t sumB = 0;

  while (tries < MAX_TRIES && good < NEED_GOOD) {
    tries++;
    int raw = analogRead(PB_PIN);
    float bVolt = raw * (3.3f / 1023.0f) * ((22.0f + 10.0f) / 22.0f);
    int bSOC = map((int)roundf(bVolt * 100.0f), 300, 420, 0, 100);
    uint8_t bSOC_int = (uint8_t)constrain(bSOC, 0, 100);

    bool ok = false;
    if (init || (abs((int)bSOC_int - (int)lastBatSOC) <= maxDeltaBat)) {
      ok = true;
      sumB += bSOC_int;
    }
    if (ok) good++;
    delay(200);
  }

  bool valid = (good >= NEED_GOOD);
  uint8_t avgB = valid ? sumB / NEED_GOOD : DEF_BATSOC;

  if (valid) {
    socVal = avgB;
    lastBatSOC = avgB;
    sensorState &= ~0b0001;                // Clear battery error flag
  } else {
    if (init) {
      lastBatSOC = DEF_BATSOC;
      socVal = DEF_BATSOC;
    }
    sensorState |= 0b0001;                 // Set error flag
  }

  // Update LED indicators based on battery SOC thresholds
  if (socVal > lowBat) {
    digitalWrite(LEDPinRed, LOW);
    digitalWrite(LEDPinGreen, LOW);
  } else if (socVal < criticalBat) {
    digitalWrite(LEDPinRed, HIGH);
    digitalWrite(LEDPinGreen, LOW);
  } else {
    digitalWrite(LEDPinRed, HIGH);
    digitalWrite(LEDPinGreen, HIGH);
  }

  return valid;
}

//----------------------------------------------------------------------------
// Initialize baseline sensor values on startup to fill last* variables
void initializeLastValues() {

  lastTemp     = DEF_TEMP;
  lastHum      = DEF_HUM;
  lastLux      = DEF_LUX;
  lastWindspeed   = DEF_WIND;
  lastBatSOC      = DEF_BATSOC;

  int16_t t;   uint16_t h;
  readDHT22(t, h, true);         // Initial read for temperature/humidity
  uint16_t lux;
  readIlluminance(lux, true);    // Initial read for illuminance
  uint16_t w;
  readWindspeed(w, true);        // Initial read for wind speed
  uint8_t b;
  readBatterySOC(b, true);       // Initial read for battery SOC

  // Adjust retry parameters for normal operation
  MAX_TRIES = 4;
  NEED_GOOD = 2;
  DHT_MAX_TRIES = 2;
  DHT_NEED_GOOD = 1;
}

//----------------------------------------------------------------------------
// Arduino setup: initialize serial, sensors, LoRa, and watchdog
void setup() {
  Serial.begin(9600);            // Start UART for debugging
  delay(3000);                   // Allow time for serial monitor attachment
  Wire.begin();                  // Initialize I2C bus
  dht.begin();                   // Start DHT sensor
  randomSeed(analogRead(A5));    // Seed random for jitter, A5: random signal

  pinMode(LEDPinRed,   OUTPUT);
  pinMode(LEDPinGreen, OUTPUT);

  // Configure LoRa module pins and frequency
  LoRa.setPins(8, 4, 7);
  if (!LoRa.begin(868E6)) while (1);

  initializeLastValues();        // Perform first sensor reads

  digitalWrite(LEDPinRed, LOW);
  digitalWrite(LEDPinGreen, LOW);

  wdt_enable(WDTO_8S);           // Enable watchdog with 8s timeout
}

//----------------------------------------------------------------------------
// Main loop: perform periodic sensor readings and send via LoRa
void loop() {
  wdt_reset();                   // watchdog reset
  unsigned long now = millis();
  if (now - previousMillis < interval) return;  // Enforce interval delay

  delay(random(0, 5000));         // Add jitter to avoid collisions
  previousMillis = now;
  wdt_reset();                    // Reset watchdog again

  int16_t  outT;
  uint16_t outH, lux, wind;
  uint8_t  soc;

  // Attempt readings, fallback to last values on failure
  if (!readDHT22(outT, outH)) {
    outT = lastTemp;
    outH = (uint16_t)lastHum;
  }
  wdt_reset();
  if (!readIlluminance(lux)) lux = lastLux;
  wdt_reset();
  if (!readWindspeed(wind)) wind = lastWindspeed;
  wdt_reset();
  if (!readBatterySOC(soc)) soc = lastBatSOC;
  wdt_reset();
  // Build payload array for LoRa transmission, encode data fields and CRC
  uint8_t payload[13] = {
    node,
    (outT < 0),
    (uint8_t)((abs(outT) >> 8) & 0xFF),
    (uint8_t)(abs(outT) & 0xFF),
    (uint8_t)((outH >> 8) & 0xFF),
    (uint8_t)(outH & 0xFF),
    (uint8_t)((lux >> 8) & 0xFF),
    (uint8_t)(lux & 0xFF),
    (uint8_t)((wind >> 8) & 0xFF),
    (uint8_t)(wind & 0xFF),
    soc,
    sensorState,
    crc8(payload, 12)
  };

  // Send the packet over LoRa
  LoRa.beginPacket();
  LoRa.write(payload, sizeof(payload));
  LoRa.endPacket();

}
