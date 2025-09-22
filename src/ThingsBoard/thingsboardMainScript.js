


//Für den finalen test konnte folgendes feature aus zeitgründen nicht mehr eingebaut werden. repeater node: falls nur node 2 ankommt, könnte soc repeater + 100 zum zeigen, dass node 1 nicht angekommen.
//Weil der soc der repeater node jeweils an die outdoor node payload angehängt wird, wurde es so implementiert, dass die repeater node seinen eigenen soc separat sendet, wenn das packet der outdoor node oder kein packet bei dem repeater angekommen ist. Zudem wurden die einzelnen daten der repeater node so codiert, dass erkenntlich ist ob keine daten oder nur die der indoor node angekommen sind. Damit konnte der zustand der indoor node als fehlerhaft markiert werden. Es gibt einen vorschlag zur codeanpassung in der repeater node und für thingsboard, welche auch falls nur outdoor node daten ankommen angezeigt wird, ob die indoor node angekommen ist oder nicht.



// ETH Zürich
// Global Health Engineering Lab
// Masterthesis Voeten Jerun
// Gateway script
// August 2025

// incoming data: hex
function hexToBytes(hex) {
    var bytes = [];
    for (var i = 0; i < hex.length; i += 2) {
      bytes.push(parseInt(hex.substr(i, 2), 16));
    }
    return bytes;
  }
  
  // compare to received CRC
  function computeCRC8(data) {
    let crc = 0x00;
    for (let b of data) {
      let extract = b;
      for (let bit = 0; bit < 8; bit++) {
        let sum = (crc ^ extract) & 0x01;
        crc = (crc >>> 1) & 0x7F;       // unsigned shift
        if (sum) crc ^= 0x8C;
        extract = (extract >>> 1) & 0x7F;
      }
    }
    return crc;
  }
  
  // ebike state
  function decodeState(s) {
      if      (s === 1) return "Charging";
      else if (s === 2) return "Discharging";
      else if (s === 0) return "Not Docked";
      else              return "Error";
  }
  
  // check payload type
  var raw = msg.payloadHex !== undefined
          ? msg.payloadHex
          : msg.payload;
  var hexPayload;
  if (typeof raw === 'string') {
    hexPayload = raw;
  } else if (Array.isArray(raw)) {
    hexPayload = raw.map(b => ('0'+b.toString(16)).slice(-2)).join('');
  } else if (typeof raw === 'number') {
    hexPayload = raw.toString();
  } else {
    throw new Error("Unsupported payload type: " + typeof raw);
  }
  
  // remove spaces
  hexPayload = hexPayload.replace(/\s+/g, '');
  
  if (!hexPayload) {
    throw new Error("No Payload found in msg.payload");
  }
  
  var bytes = hexToBytes(hexPayload);
  
  if (bytes.length === 0) {
    throw new Error("Empty Payload.");
  }
  
  var node = bytes[0]; // first byte indicates node
  var data;
  
  if (bytes.length === 1)  { // node 1 or 1 and 2 are not sending
     node = 3;
  }
  
  // indoor node
  if (node === 1) {
    
    if (bytes.length < 21) {
      throw new Error("Payload is to short. expected: 21 Bytes, arrived: " + bytes.length);
    }
  
    // calculate CRC from Byte 0 - 19 
    var computedCRC = computeCRC8(bytes.slice(0, 20));
    var receivedCRC = bytes[20];
    if (computedCRC !== receivedCRC) {
      throw new Error("CRC not vaild Node 1: received=" + receivedCRC + ", computed=" + computedCRC);
    }
  
    var signByte      = bytes[1];
    var tempSign      = (signByte & 0x01) ? -1 : 1; // fridge temp sign
    var currentSign   = (signByte & 0x02) ? -1 : 1;
    var ceSign        = (signByte & 0x04) ? -1 : 1; // consumed_mAh sign
    
    // fridge temperature: byte 2,3
    var tempMagRaw    = (bytes[2] << 8) | bytes[3];   
    var fridgeTemp    = tempSign * (tempMagRaw / 10.0); //  235 → 23.5 °C
  
    // fridge humidity: byte 4,5
    var humRaw        = (bytes[4] << 8) | bytes[5];  
    var fridgeHum     = humRaw / 10.0;
  
    var InNodeBatSOC  = bytes[6];  // 0–100 %
  
    // stationary battery soc, byte: 7,8
    var mainBatSOC_raw = (bytes[7] << 8) | bytes[8];  
    var mainBatSOC     = mainBatSOC_raw / 10.0;
  
    // stationary battery voltage, byte: 9,10
    var voltRaw        = (bytes[9] << 8) | bytes[10];
    var mainBatVoltage = voltRaw / 10.0;
  
    // stationary battery current, byte: 11,12
    var currRaw        = (bytes[11] << 8) | bytes[12];
    var mainBatCurrent = currentSign * (currRaw / 10.0);
  
    // stationary battery consumed_mAh, byte: 13,14 
    var CE_raw         = (bytes[13] << 16) | (bytes[14] << 8) | (bytes[15]); 
    var consumed_mAh = ceSign * CE_raw;           // in mAh
  
    // // stationary battery chargeCycles, byte: 15,16 
    var chargeCycles   = (bytes[16] << 8) | bytes[17]; 
  
    // ebike state, byte: 18
    var packed      = bytes[18];
    var eb1raw      =  (packed        & 0x03);        // Bits 0–1
    var eb2raw      = ((packed >> 2)  & 0x03);        // Bits 2–3
    var eb3raw      = ((packed >> 4)  & 0x03);        // Bits 4–5
    var eb4raw      = ((packed >> 6)  & 0x03);        // Bits 6–7
  
    var eBikeState1 = decodeState(eb1raw);
    var eBikeState2 = decodeState(eb2raw);
    var eBikeState3 = decodeState(eb3raw);
    var eBikeState4 = decodeState(eb4raw);
  
    // sensor states, byte: 19
    var sensorState = bytes[19];
    var InDHTStatus   = (sensorState & 0b1000) ? "Error" : "OK";
    var SmartShuntStatus   = (sensorState & 0b0100) ? "Error" : "OK";
    var InNodeBatStatus   = (sensorState & 0b0001) ? "Error" : "OK";
    
  
    data = {
      node: node,
      fridgeTemp:    fridgeTemp,   
      fridgeHum:     fridgeHum,   
      InNodeBatSOC:  InNodeBatSOC, 
  
      // SmartShunt-data
      mainBatSOC:         mainBatSOC,         
      mainBatVoltage:     mainBatVoltage,     
      mainBatCurrent:     mainBatCurrent,     
      consumed_mAh:   consumed_mAh,   
      chargeCycles:       chargeCycles,   
  
      // eBike states
      eBikeState1:        eBikeState1,        // "Charging" | "Discharging" | "Not Docked" | "Error"
      eBikeState2:        eBikeState2,
      eBikeState3:        eBikeState3,
      eBikeState4:        eBikeState4,
  
      // Sensor-Status
      InDHTStatus:          InDHTStatus,      
      SmartShuntStatus: SmartShuntStatus,
      InNodeBatStatus: InNodeBatStatus
    };
  }
    
  // outdoor node
  else if (node === 2) {
    
    if (bytes.length < 14) {
      throw new Error("Payload is to short. expected: 14 Bytes, arrived: " + bytes.length);
    }
  
    // calculate CRC from Byte 0 - 11 
    var computedCRC = computeCRC8(bytes.slice(0, 12));
    var receivedCRC = bytes[12];
    if (computedCRC !== receivedCRC) {
      throw new Error("CRC not vaild Node 2: received=" + receivedCRC + ", computed=" + computedCRC);
    }
  
    // fridge temperature: byte 1,2,3
    var tempSign    = bytes[1];                               // 0 = positiv, 1 = negativ
    var tempMagRaw  = (bytes[2] << 8) | bytes[3];             
    var tempValue   = tempMagRaw / 10.0;                       
    if (tempSign === 1) tempValue = -tempValue;
  
    // fridge humidity: byte 4,5
    var humRaw      = (bytes[4] << 8) | bytes[5];             
    var humValue    = humRaw / 10.0;
  
    // Illuminance (lux)  byte: 6,7
    var illuminance = (bytes[6] << 8) | bytes[7];             
  
    // Windspeed (m/s): byte 8,9
    var windRaw     = (bytes[8] << 8) | bytes[9];              
    var windValue   = windRaw / 10.0;
  
    var OutNodeBatSOC = bytes[10];
  
    // Sensor-Status
    var sensorState = bytes[11];
    var dhtStatus   = (sensorState & 0b1000) ? "Error" : "OK";
    var lightStatus = (sensorState & 0b0100) ? "Error" : "OK";
    var windStatus  = (sensorState & 0b0010) ? "Error" : "OK";
    var outNodeBatStatus  = (sensorState & 0b001) ? "Error" : "OK";
      
    var repNodeBatSOC = bytes[13];
    var repNodeBatStatus = "OK";


    // ** // Attention! This feature was not tested due to time constraints.
    // ** //To test it, remove all "// **" in this script and in the RepeaterNode.ino script
    // ** //If it works, the status of node 1 will be set to Error whenever it does not transmit.

    // ** var InNodeBatStatus   = "";
    // ** // Case: only data from node 2 was received
    // **if (repNodeBatSOC >= 102 && repNodeBatSOC <= 203) { 
        
    // **    InNodeBatStatus   = "Error";
    // **    repNodeBatSOC = repNodeBatSOC-102;
    // **}

    if (repNodeBatSOC === 101) { 
      repNodeBatStatus = "Error";  
    }

    data = {
      node: node,
      outTemp: tempValue,
      outHum: humValue,
      illuminance: illuminance,
      windspeed: windValue,
      OutNodeBatSOC: OutNodeBatSOC,
      OutDHTStatus: dhtStatus,
      OutLightStatus: lightStatus,
      OutWindStatus: windStatus,
      outNodeBatStatus: outNodeBatStatus,
      repNodeBatSOC: repNodeBatSOC,
      repNodeBatStatus: repNodeBatStatus // ** ,
      
      // ** InNodeBatStatus: InNodeBatStatus
      
    };
  
  } 
  
  // repeater node
  else if (node === 3) {
      EncRepNodeBatSOC = bytes[0];
      
      // data from node 1 & 2 did not arrive at the repeater node. repeater sends its own soc
      if (EncRepNodeBatSOC >= 0 && EncRepNodeBatSOC <= 101) {
          var repNodeBatStatus = "OK";
          var InNodeBatStatus   = "Error";
          var outNodeBatStatus  = "Error";
          var repNodeBatSOC = EncRepNodeBatSOC;
          if (repNodeBatSOC === 101) {
              repNodeBatStatus = "Error";  
          }
      }
      // data from node 2 did not arrive at the repeater node. repeater sends its own soc
      else if (EncRepNodeBatSOC >= 102 && EncRepNodeBatSOC <= 203) {
          var repNodeBatStatus = "OK";
          var InNodeBatStatus   = "OK";
          var outNodeBatStatus  = "Error";
          var repNodeBatSOC = EncRepNodeBatSOC-102;
          if (repNodeBatSOC === 101) {
              repNodeBatStatus = "Error"; 
          }
      }   else {
        throw new Error("wrong payload from node 3"); 
      }
      
      data = {
          node: node,
          repNodeBatSOC: repNodeBatSOC,
          outNodeBatStatus: outNodeBatStatus,
          InNodeBatStatus: InNodeBatStatus,
          repNodeBatStatus: repNodeBatStatus
      };
  
  
  }  else {
      throw new Error("Unknown Node-ID: " + node);
  }
  
  return {
    msg: data,
    metadata: metadata,
    msgType: "POST_TELEMETRY_REQUEST"
  };
  
  
  