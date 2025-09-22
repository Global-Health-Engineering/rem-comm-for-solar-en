// ETH Zürich
// Global Health Engineering Lab
// Masterthesis Voeten Jerun
// Repeater Node Skript
// August 2025

#include <SPI.h>
#include <LoRa.h>
#include <lorawan.h>
#include <avr/wdt.h>            // for watchdog timer

#define PB_PIN A0  // Analog pin for battery voltage measurement
const int LEDPinRed   = 9;   // Red LED pin
const int LEDPinGreen = 10;  // Green LED pin

// ABP credentials (Activation by Personlalisation)
const char *devAddr = "260BA674";  // Device address for LoRaWAN ABP
const char *nwkSKey = "4FD8ACBB3073482A39E5BD41E2AE0068";  // Network session key
const char *appSKey = "908A674A7FDEE366D891DF24E36FC3CB";  // Application session key

const uint8_t node = 3;  // Node identifier for this device: repeater node

// Pin mapping for RFM module
const sRFM_pins RFM_pins = {
  .CS   = 8,   // Chip Select pin
  .RST  = 4,   // Reset pin
  .DIO0 = 7,   // DIO0 interrupt pin
  .DIO1 = 1,   // DIO1 interrupt pin
  .DIO2 = 2,   // DIO2 interrupt pin
  .DIO5 = 15,  // DIO5 interrupt pin
};

// Repeater node receives p2p messages from node 1 & 2 and sends the data to the Gateway using LoRaWAN
enum Mode { RECV_P2P, SEND_LORAWAN };  // Operation modes: peer-to-peer vs LoRaWAN
Mode mode = RECV_P2P;  // Start in P2P receive mode

// Buffer arrays for data from Node 1 and Node 2
uint8_t p2pBuf[2][22];  // Storage for incoming packets
uint8_t p2pLen[2] = { 0, 0 };  // Lengths of stored packets
// int sameNodeCount = 0;  // Counter for duplicate packets from same node

// Timer configuration for P2P data collection window
const unsigned long COLLECTION_TIMEOUT = 135000UL; // 30,000 ms = 30 seconds // Time allowed for collecting P2P data
unsigned long timerStart = 0;  // Timestamp when timer started
bool timerActive = false;  // Flag indicating timer is running
bool initialization = true;  // Flag for first LoRaWAN send to handle initial conditions
uint8_t lastBatSOC;  // Last reported battery state-of-charge

// Define battery boundaries
int16_t maxDeltaBat = 40, lowBat = 25, criticalBat = 15;

// Disable watchdog early during startup (before C runtime begins)
void disableWatchdog() __attribute__((naked)) __attribute__((section(".init3")));
void disableWatchdog() {
  MCUSR = 0;  // Clear MCU status register
  wdt_disable();  // Turn off watchdog timer
}

void setup() {
  Serial.begin(9600);  // Initialize serial communication
  delay(3000);  // Delay for serial monitor to start

  pinMode(LEDPinRed,   OUTPUT);  // Configure Red LED pin as output
  pinMode(LEDPinGreen, OUTPUT);  // Configure Green LED pin as output

  initP2P();  // Initialize peer-to-peer LoRa settings

  // Enable watchdog with 8-second timeout
  wdt_enable(WDTO_8S);

  // Ensure LEDs are off at start
  digitalWrite(LEDPinRed, LOW);
  digitalWrite(LEDPinGreen, LOW);
}

void loop() {
  wdt_reset();  // Reset watchdog timer at start of loop

  if (mode == RECV_P2P) {
    // Check if collection timer has expired
    if (timerActive && (millis() - timerStart >= COLLECTION_TIMEOUT)) { 
      mode = SEND_LORAWAN;  // Switch to LoRaWAN send mode
    }
    else if (receiveP2P()) {
      // Both P2P packets received successfully
      mode = SEND_LORAWAN;
    }
  }

  if (mode == SEND_LORAWAN) {
    sendAllLoRaWAN();  // Package and send data via LoRaWAN

    // Reset state for next P2P session
    p2pLen[0] = p2pLen[1] = 0;
    //sameNodeCount = 0;
    timerActive = false;
    initP2P();  // Reinitialize P2P for next round
    mode = RECV_P2P;
  }
}

// Initialize LoRa in peer-to-peer mode
void initP2P() {
  LoRa.end();  // Ensure LoRa module is reset
  LoRa.setPins(RFM_pins.CS, RFM_pins.RST, RFM_pins.DIO0);  // Apply pin settings
  if (!LoRa.begin(868E6)) {
    while (1);  // Halt on failure
  }
  // Start collection timer
  timerStart = millis();
  timerActive = true;
}

// Receive data in peer-to-peer mode and store in buffers
bool receiveP2P() {
  int packetSize = LoRa.parsePacket();  // Check for incoming packet
  if (packetSize > 0) {
    wdt_reset();  // Reset watchdog after receiving data

    int nodeID = LoRa.read();  // First byte indicates sending node ID
    if (nodeID < 1 || nodeID > 2) {
      // Invalid node ID, flush buffer and ignore
      while (LoRa.available()) LoRa.read();
      return false;
    }

    int idx = (nodeID == 1) ? 0 : 1;  // Buffer index based on node ID
    int expectedLen = (nodeID == 1) ? 21 : 13;  // Expected packet length for each node

    if (packetSize < expectedLen) {
      while (LoRa.available()) LoRa.read();
      return false;
    }

    uint8_t buf[22];  // Temporary buffer for incoming packet
    buf[0] = nodeID;  // First byte stores node ID
    int count = 1;
    while (LoRa.available() && count < expectedLen) {
      buf[count++] = LoRa.read();  // Read rest of packet
    }
    while (LoRa.available()) LoRa.read();  // Flush any extra data

    if (p2pLen[idx] == 0) {
      // First packet from this node, store it
      memcpy(p2pBuf[idx], buf, expectedLen);
      p2pLen[idx] = expectedLen;
      
    }

    // Check if data from both nodes have arrived
    if (p2pLen[0] > 0 && p2pLen[1] > 0) {
      return true;  // Both packets ready
    }
  }
  return false;  // No complete set yet
}

// Initialize LoRaWAN settings for ABP transmission
void initLoRaWAN() {
  wdt_reset();  // Reset watchdog
  LoRa.end();  // Reset P2P LoRa
  delay(200);
  if (!lora.init()) {
    return;
  }
  lora.setDeviceClass(CLASS_A);  // Use Class A for lowest power
  lora.setDataRate(SF9BW125);  // Set spreading factor and bandwidth
  lora.setChannel(MULTI);  // Use multiple channels
  lora.setNwkSKey(nwkSKey);  // Apply network key
  lora.setAppSKey(appSKey);  // Apply app key
  lora.setDevAddr(devAddr);  // Apply device address
}

// Combine P2P data and battery SOC, then send via LoRaWAN
void sendAllLoRaWAN() {
  wdt_reset();  // Reset watchdog before measurement

  // Measure battery state-of-charge (SOC)
  int tries = 0;
  int good = 0;
  int32_t sumB = 0;
  while (tries < 4 && good < 2) { // 2 out of 4 readings have to be valid
    tries++;
    int PBRawValue = analogRead(PB_PIN);  // Read raw ADC value
    float PBRawVoltage = PBRawValue * (3.3 / 1023.0);  // Convert to voltage
    float PBVoltage = PBRawVoltage * ((22.0 + 10.0) / 22.0);  // Account for voltage divider: one 22kOhm and one 10kOhm resistor are used
    int socRaw = map(PBVoltage * 100, 300, 420, 0, 100);  // Map voltage to SOC percentage. Bat voltage between 3V & 4.2V
    uint8_t repBat = constrain(socRaw, 0, 100);  // Ensure SOC is within 0-100

    if (initialization || (abs((int)repBat - (int)lastBatSOC) <= maxDeltaBat)) {
      sumB += repBat;  // Accumulate valid readings
      good++;
    }
    delay(200);  // Short delay between readings
  }

  uint8_t repNodeBatSOC;
  bool valid = (good >= 2);
  if (valid) {
    repNodeBatSOC = sumB / 2;  // Average of two good readings
    lastBatSOC = repNodeBatSOC;
  } else {
    if (initialization) {
      lastBatSOC = 101;  // Mark invalid initial reading
    }
    repNodeBatSOC = 101;  // Error value for SOC measurement
  }

  initialization = false;  // Clear initialization flag after first run

  // LED status based on SOC level
  if (repNodeBatSOC > lowBat) {
    digitalWrite(LEDPinRed, LOW);
    digitalWrite(LEDPinGreen, LOW);  // Both off: healthy battery
  }
  else if (repNodeBatSOC < criticalBat) {
    digitalWrite(LEDPinRed, HIGH);
    digitalWrite(LEDPinGreen, LOW);  // Red on: low battery warning
  }
  else {
    digitalWrite(LEDPinRed, HIGH);
    digitalWrite(LEDPinGreen, HIGH);  // Yellow (red+green): moderate battery
  }

  initLoRaWAN();  // Setup LoRaWAN for uplink
  delay(500);
  wdt_reset();  // Reset watchdog after init

  // Special case: received no P2P data, send only SOC of node 3
  if (p2pLen[0] == 0 && p2pLen[1] == 0) {
    uint8_t payload[1];
    payload[0]  = repNodeBatSOC;  // Single-byte payload: SOC
    lora.sendUplink(payload, sizeof(payload), 0, 1);  // Send SOC only
    lora.update();  // Process MAC commands

    delay(3000);
    wdt_reset();
    return;  // Done sending
  }

  // Case: only data from node 1 was received, send data of node 1 and SOC of node 3
  if (p2pLen[0] > 0 && p2pLen[1] == 0) {
    uint8_t payload[1];
    payload[0]  = repNodeBatSOC+102;  // Offset SOC to indicate this scenario
    lora.sendUplink(payload, sizeof(payload), 0, 1);
    lora.update();
    delay(3000);
    
    wdt_reset();
    lora.sendUplink(p2pBuf[0], p2pLen[0], 0, 1);  // Send data from node 1
    lora.update();
    delay(3000);
    wdt_reset();
    return;
  }


  // ** // Attention! This feature was not tested due to time constraints.
  // ** //To test it, remove all "// **" in this script and in the JavaScript within ThingsBoard
  // ** //If it works, the status of node 1 will be set to Error whenever it does not transmit.
  // ** // Case: only data from node 2 was received, send data of node 2
  // **if (p2pLen[1] > 0 && p2pLen[0] == 0) {
    
  // **  p2pBuf[1][p2pLen[1]++] = repNodeBatSOC+102; // Offset SOC to indicate this scenario
  
  // **  lora.sendUplink(p2pBuf[1], p2pLen[1], 0, 1);  // Send data from node 2
  // **  lora.update();
  // **  delay(3000);
  // **  wdt_reset();
  // **  return;
  // **}
  


  // General case: send data for each node that has data
  for (int i = 0; i < 2; i++) {
    if (p2pLen[i] > 0) {
      wdt_reset();  // Reset watchdog before each send
      // For node 2, append battery SOC of node 3 to its packet
      if (i == 1 && p2pLen[i] < 22) {
        p2pBuf[i][p2pLen[i]++] = repNodeBatSOC;
      }
      lora.sendUplink(p2pBuf[i], p2pLen[i], 0, 1);  // Send stored packet via LoRaWAN
      lora.update();  // Process MAC commands

      delay(3000);
      wdt_reset();
    }
  }
}

