# READ_LOG & DELETE_LOG Commands Implementation

## Overview
Ove komande omogućavaju ESP32 HTTP Bridge uređaju da čita i briše log zapise sa IC kontrolera preko RS485 busa.

## Log Structure (16 bytes)

```
Byte  | Field        | Description
------|--------------|------------------------------------------
0-1   | log_id       | Unique log ID (uint16_t, big endian)
2     | log_event    | Event code (0x90-0xF9)
3     | log_type     | Type field (currently unused)
4     | log_group    | Group field (currently unused)
5-9   | card_id      | RFID card ID (5 bytes)
10-12 | date         | Date in BCD format (DD, MM, YY)
13-15 | time         | Time in BCD format (HH, MM, SS)
```

## Command Hex Codes
- `CMD_READ_LOG = 0xCE` (HOTEL_READ_LOG)
- `CMD_DELETE_LOG = 0xCF` (HOTEL_DELETE_LOG)

## IC Controller Response Format

### ReadLog Response
```
[0]    HOTEL_READ_LOG (0xCE)
[1]    LOG_DSIZE (16)
[2-17] Log data (16 bytes)
[18]   Device address high byte
[19]   Device address low byte
```
Total: 20 bytes

### DeleteLog Response
```
[0]    HOTEL_DELETE_LOG (0xCF)
[1]    Status (0=OK, 1=EMPTY)
[2]    Device address high byte
[3]    Device address low byte
```
Total: 4 bytes

## Implementation Steps

### 1. Add to CommandType enum (main.cpp)
```cpp
enum CommandType {
    // ... existing commands ...
    CMD_READ_LOG = 0xCE,        // Read last log from IC controller
    CMD_DELETE_LOG = 0xCF,      // Delete last log from IC controller
    // ... rest of commands ...
};
```

### 2. Add to stringToCommand() function
```cpp
CommandType stringToCommand(const String& cmdStr) {
    // ... existing mappings ...
    if (cmdStr == "READ_LOG") return CMD_READ_LOG;
    if (cmdStr == "DELETE_LOG") return CMD_DELETE_LOG;
    // ... rest of function ...
}
```

### 3. Add helper function for BCD to Decimal conversion
```cpp
/**
 * Convert BCD (Binary Coded Decimal) to decimal
 * @param bcd BCD value (e.g., 0x23 for 23)
 * @return decimal value (e.g., 23)
 */
uint8_t bcdToDec(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
```

### 4. Add helper function for event code to string
```cpp
/**
 * Convert log event code to human-readable string
 * @param eventCode Event code (0x90-0xF9)
 * @return Event name string
 */
String getEventName(uint8_t eventCode) {
    // Driver errors (0x90-0x9C)
    if (eventCode == 0x90) return "SPI_DRIVER_ERROR";
    if (eventCode == 0x91) return "I2C_DRIVER_ERROR";
    if (eventCode == 0x92) return "USART_DRIVER_ERROR";
    if (eventCode == 0x93) return "RTC_DRIVER_ERROR";
    if (eventCode == 0x94) return "TIMER_DRIVER_ERROR";
    if (eventCode == 0x95) return "ETHERNET_DRIVER_ERROR";
    if (eventCode == 0x96) return "CRC_DRIVER_ERROR";
    if (eventCode == 0x97) return "ADC_DRIVER_ERROR";
    if (eventCode == 0x98) return "SYSTEM_CLOCK_ERROR";
    if (eventCode == 0x99) return "SYSTEM_EXCEPTION";
    if (eventCode == 0x9A) return "QSPI_DRIVER_ERROR";
    if (eventCode == 0x9B) return "FLASH_DRIVER_ERROR";
    if (eventCode == 0x9C) return "SOFTWARE_RESET";
    
    // Function errors (0xA0-0xAE)
    if (eventCode == 0xA0) return "CAPACITIVE_SENSOR_ERROR";
    if (eventCode == 0xA1) return "RC522_MIFARE_ERROR";
    if (eventCode == 0xA2) return "ONEWIRE_ERROR";
    if (eventCode == 0xA3) return "RS485_ERROR";
    if (eventCode == 0xA4) return "MAIN_FUNCTION_ERROR";
    if (eventCode == 0xA5) return "DISPLAY_ERROR";
    if (eventCode == 0xA6) return "LOGGER_ERROR";
    if (eventCode == 0xA7) return "DIO_ERROR";
    if (eventCode == 0xA8) return "EEPROM_ERROR";
    if (eventCode == 0xA9) return "ROOM_FUNCTION_ERROR";
    if (eventCode == 0xAA) return "TCPIP_STACK_FAILURE";
    if (eventCode == 0xAB) return "HOTEL_CTRL_FAILURE";
    if (eventCode == 0xAC) return "HTTPD_SERVER_FAILURE";
    if (eventCode == 0xAD) return "THERMOSTAT_FAILURE";
    if (eventCode == 0xAE) return "GENERAL_FAILURE";
    
    // System events (0xB0-0xBE)
    if (eventCode == 0xB0) return "FILE_SYSTEM_OK";
    if (eventCode == 0xB1) return "FILE_SYSTEM_DRIVE_ERROR";
    if (eventCode == 0xB2) return "FILE_SYSTEM_DIR_ERROR";
    if (eventCode == 0xB3) return "FILE_SYSTEM_FILE_ERROR";
    if (eventCode == 0xB4) return "OUT_OF_MEMORY";
    if (eventCode == 0xB5) return "ADDRESS_LIST_ERROR";
    if (eventCode == 0xB6) return "ADDRESS_LIST_SD_ERROR";
    if (eventCode == 0xB7) return "DEVICE_NOT_RESPONDING";
    if (eventCode == 0xB8) return "PIN_RESET";
    if (eventCode == 0xB9) return "POWER_ON_RESET";
    if (eventCode == 0xBA) return "SOFTWARE_RESET";
    if (eventCode == 0xBB) return "IWDG_RESET";
    if (eventCode == 0xBC) return "WWDG_RESET";
    if (eventCode == 0xBE) return "LOW_POWER_RESET";
    
    // Application events (0xC8-0xF9) - most common ones
    if (eventCode == 0xC8) return "RS485_BUS_ERROR";
    if (eventCode == 0xC9) return "RT_RPM_SENSOR_ERROR";
    if (eventCode == 0xCA) return "RT_FANCOIL_NTC_ERROR";
    if (eventCode == 0xCB) return "RT_LOW_TEMP_ERROR";
    if (eventCode == 0xCC) return "RT_HIGH_TEMP_ERROR";
    if (eventCode == 0xCD) return "RT_FREEZE_PROTECTION";
    if (eventCode == 0xCE) return "RT_DISPLAY_NTC_ERROR";
    if (eventCode == 0xCF) return "FIRMWARE_UPDATED";
    if (eventCode == 0xD0) return "FIRMWARE_UPDATE_FAILED";
    if (eventCode == 0xD1) return "BOOTLOADER_UPDATED";
    if (eventCode == 0xD2) return "BOOTLOADER_UPDATE_FAILED";
    if (eventCode == 0xD3) return "IMAGE_UPDATED";
    if (eventCode == 0xD4) return "IMAGE_UPDATE_FAILED";
    if (eventCode == 0xD5) return "FILE_COPY_FAILED";
    if (eventCode == 0xD6) return "FILE_BACKUP_FAILED";
    if (eventCode == 0xD7) return "UNKNOWN_CARD";
    if (eventCode == 0xD8) return "CARD_EXPIRED";
    if (eventCode == 0xD9) return "CARD_INVALID";
    if (eventCode == 0xDA) return "WRONG_ROOM";
    if (eventCode == 0xDB) return "WRONG_SYSTEM_ID";
    if (eventCode == 0xDC) return "GUEST_CARD";          // ✓ Gost ušao RFID karticom
    if (eventCode == 0xDD) return "HANDMAID_CARD";       // ✓ Sobarica ušla RFID karticom
    if (eventCode == 0xDE) return "MANAGER_CARD";        // ✓ Manager ušao RFID karticom
    if (eventCode == 0xDF) return "SERVICE_CARD";        // ✓ Service ušao RFID karticom
    if (eventCode == 0xE0) return "ENTRY_DOOR_OPENED";
    if (eventCode == 0xE1) return "ENTRY_DOOR_CLOSED";
    if (eventCode == 0xE2) return "ENTRY_DOOR_NOT_CLOSED";
    if (eventCode == 0xE3) return "MINIBAR_USED";
    if (eventCode == 0xE4) return "BALCONY_DOOR_OPENED";
    if (eventCode == 0xE5) return "BALCONY_DOOR_CLOSED";
    if (eventCode == 0xE6) return "CARD_STACKER_ON";
    if (eventCode == 0xE7) return "CARD_STACKER_OFF";
    if (eventCode == 0xE8) return "DO_NOT_DISTURB_ON";
    if (eventCode == 0xE9) return "DO_NOT_DISTURB_OFF";
    if (eventCode == 0xEA) return "HANDMAID_SWITCH_ON";
    if (eventCode == 0xEB) return "HANDMAID_SWITCH_OFF";
    if (eventCode == 0xEC) return "HANDMAID_SERVICE_END";
    if (eventCode == 0xED) return "SOS_ALARM_TRIGGERED";
    if (eventCode == 0xEE) return "SOS_ALARM_RESET";
    if (eventCode == 0xEF) return "FIRE_ALARM_TRIGGERED";
    if (eventCode == 0xF0) return "FIRE_ALARM_RESET";
    if (eventCode == 0xF1) return "FLOOD_SENSOR_ACTIVE";
    if (eventCode == 0xF2) return "FLOOD_SENSOR_INACTIVE";
    if (eventCode == 0xF3) return "DOOR_BELL_ACTIVE";
    if (eventCode == 0xF4) return "DOOR_LOCK_USER_OPEN";
    if (eventCode == 0xF5) return "FIRE_EXIT_TRIGGERED";
    if (eventCode == 0xF6) return "FIRE_EXIT_RESET";
    if (eventCode == 0xF7) return "PASSWORD_VALID";      // ✓ Uspješan ulaz PIN-om (Gost/Sobarica/Manager/Service)
    if (eventCode == 0xF8) return "PASSWORD_INVALID";    // ✓ Neuspješan ulaz PIN-om
    if (eventCode == 0xF9) return "ROOM_TIME_POWER_OFF";
    
    return "UNKNOWN_EVENT";
}

/**
 * Get user-friendly access type description based on event code and user group
 * For PASSWORD_VALID event (0xF7), use log_group field to determine user type:
 *   USERGRP_GUEST (1)   = "Gost ušao PIN-om"
 *   USERGRP_MAID (2)    = "Sobarica ušla PIN-om"
 *   USERGRP_MANAGER (3) = "Manager ušao PIN-om"
 *   USERGRP_SERVICE (4) = "Service ušao PIN-om"
 * 
 * @param eventCode Event code (0xDC-0xDF for cards, 0xF7 for PIN)
 * @param userGroup User group (1-4, only used for PASSWORD_VALID event)
 * @return User-friendly access description in Serbian/Bosnian
 */
String getAccessDescription(uint8_t eventCode, uint8_t userGroup) {
    // RFID Card access
    if (eventCode == 0xDC) return "Gost ušao karticom";
    if (eventCode == 0xDD) return "Sobarica ušla karticom";
    if (eventCode == 0xDE) return "Manager ušao karticom";
    if (eventCode == 0xDF) return "Service ušao karticom";
    
    // PIN access (check user group)
    if (eventCode == 0xF7) { // PASSWORD_VALID
        if (userGroup == 1) return "Gost ušao PIN-om";         // USERGRP_GUEST
        if (userGroup == 2) return "Sobarica ušla PIN-om";     // USERGRP_MAID
        if (userGroup == 3) return "Manager ušao PIN-om";      // USERGRP_MANAGER
        if (userGroup == 4) return "Service ušao PIN-om";      // USERGRP_SERVICE
        return "Korisnik ušao PIN-om";
    }
    
    // Other events
    if (eventCode == 0xF8) return "Pogrešan PIN";
    if (eventCode == 0xD7) return "Nepoznata kartica";
    if (eventCode == 0xD8) return "Kartica istekla";
    if (eventCode == 0xD9) return "Nevažeća kartica";
    if (eventCode == 0xDA) return "Pogrešna soba";
    if (eventCode == 0xDB) return "Pogrešan sistem ID";
    
    return getEventName(eventCode);
}
```

### 5. Implement CMD_READ_LOG case in handleSysctrlRequest()
```cpp
case CMD_READ_LOG: {
    // Request format: /sysctrl.cgi?cmd=READ_LOG&id=<device_id>
    // Example: /sysctrl.cgi?cmd=READ_LOG&id=5
    
    if (!request->hasParam("id")) {
        request->send(400, "text/plain", "ERROR: Missing 'id' parameter");
        return;
    }
    
    uint8_t deviceId = request->getParam("id")->value().toInt();
    
    if (deviceId < 1 || deviceId > 254) {
        request->send(400, "text/plain", "ERROR: Invalid device ID (must be 1-254)");
        return;
    }
    
    // Send READ_LOG command to IC controller
    uint8_t txData[2];
    txData[0] = CMD_READ_LOG;  // 0xCE
    txData[1] = deviceId;
    
    TF_Msg msg;
    TF_ClearMsg(&msg);
    msg.type = 0x01;
    msg.data = txData;
    msg.len = 2;
    TF_Send(&tf, &msg);
    
    // Wait for response (max 1000ms)
    unsigned long startTime = millis();
    bool responseReceived = false;
    
    while ((millis() - startTime) < 1000 && !responseReceived) {
        TF_Tick(&tf);
        
        // Check if we received response
        if (lastRxType == 0x02 && lastRxLen >= 20) {
            responseReceived = true;
            break;
        }
        delay(10);
    }
    
    if (!responseReceived) {
        request->send(408, "text/plain", "ERROR: Device timeout - no response from IC controller");
        return;
    }
    
    // Parse response
    if (lastRxData[0] != CMD_READ_LOG) {
        request->send(500, "text/plain", "ERROR: Invalid response command");
        return;
    }
    
    if (lastRxData[1] != 16) {
        request->send(500, "text/plain", "ERROR: Invalid log data size");
        return;
    }
    
    // Parse 16-byte log data (bytes 2-17)
    uint16_t logId = (lastRxData[2] << 8) | lastRxData[3];
    uint8_t logEvent = lastRxData[4];
    uint8_t logType = lastRxData[5];
    uint8_t logGroup = lastRxData[6];
    
    // Card ID (5 bytes) - from RFID card reader
    char cardIdHex[11];
    sprintf(cardIdHex, "%02X%02X%02X%02X%02X", 
            lastRxData[7], lastRxData[8], lastRxData[9], 
            lastRxData[10], lastRxData[11]);
    
    // Date/Time (BCD format) - RTC timestamp when log was created
    uint8_t day = bcdToDec(lastRxData[12]);
    uint8_t month = bcdToDec(lastRxData[13]);
    uint8_t year = bcdToDec(lastRxData[14]);
    uint8_t hour = bcdToDec(lastRxData[15]);
    uint8_t minute = bcdToDec(lastRxData[16]);
    uint8_t second = bcdToDec(lastRxData[17]);
    
    // Format date and time with zero-padding for better display
    char dateStr[12];  // "DD.MM.YYYY\0"
    char timeStr[9];   // "HH:MM:SS\0"
    sprintf(dateStr, "%02d.%02d.20%02d", day, month, year);
    sprintf(timeStr, "%02d:%02d:%02d", hour, minute, second);
    
    // Build JSON response
    String jsonResponse = "{";
    jsonResponse += "\"status\":\"OK\",";
    jsonResponse += "\"device_id\":" + String(deviceId) + ",";
    jsonResponse += "\"log_id\":" + String(logId) + ",";
    jsonResponse += "\"event_code\":\"0x" + String(logEvent, HEX) + "\",";
    jsonResponse += "\"event_name\":\"" + getEventName(logEvent) + "\",";
    jsonResponse += "\"event_description\":\"" + getAccessDescription(logEvent, logGroup) + "\",";
    jsonResponse += "\"type\":" + String(logType) + ",";
    jsonResponse += "\"group\":" + String(logGroup) + ",";
    jsonResponse += "\"card_id\":\"" + String(cardIdHex) + "\",";
    jsonResponse += "\"date\":\"" + String(dateStr) + "\",";
    jsonResponse += "\"time\":\"" + String(timeStr) + "\",";
    jsonResponse += "\"timestamp\":\"" + String(dateStr) + " " + String(timeStr) + "\"";
    jsonResponse += "}";
    
    request->send(200, "application/json", jsonResponse);
    break;
}
```

### 6. Implement CMD_DELETE_LOG case in handleSysctrlRequest()
```cpp
case CMD_DELETE_LOG: {
    // Request format: /sysctrl.cgi?cmd=DELETE_LOG&id=<device_id>
    // Example: /sysctrl.cgi?cmd=DELETE_LOG&id=5
    
    if (!request->hasParam("id")) {
        request->send(400, "text/plain", "ERROR: Missing 'id' parameter");
        return;
    }
    
    uint8_t deviceId = request->getParam("id")->value().toInt();
    
    if (deviceId < 1 || deviceId > 254) {
        request->send(400, "text/plain", "ERROR: Invalid device ID (must be 1-254)");
        return;
    }
    
    // Send DELETE_LOG command to IC controller
    uint8_t txData[2];
    txData[0] = CMD_DELETE_LOG;  // 0xCF
    txData[1] = deviceId;
    
    TF_Msg msg;
    TF_ClearMsg(&msg);
    msg.type = 0x01;
    msg.data = txData;
    msg.len = 2;
    TF_Send(&tf, &msg);
    
    // Wait for response (max 1000ms)
    unsigned long startTime = millis();
    bool responseReceived = false;
    
    while ((millis() - startTime) < 1000 && !responseReceived) {
        TF_Tick(&tf);
        
        // Check if we received response
        if (lastRxType == 0x02 && lastRxLen >= 4) {
            responseReceived = true;
            break;
        }
        delay(10);
    }
    
    if (!responseReceived) {
        request->send(408, "text/plain", "ERROR: Device timeout - no response from IC controller");
        return;
    }
    
    // Parse response
    if (lastRxData[0] != CMD_DELETE_LOG) {
        request->send(500, "text/plain", "ERROR: Invalid response command");
        return;
    }
    
    uint8_t status = lastRxData[1];
    
    // Build JSON response
    String jsonResponse = "{";
    jsonResponse += "\"status\":\"" + String(status == 0 ? "OK" : "EMPTY") + "\",";
    jsonResponse += "\"device_id\":" + String(deviceId) + ",";
    jsonResponse += "\"message\":\"" + String(status == 0 ? "Log deleted successfully" : "Log list is empty") + "\"";
    jsonResponse += "}";
    
    request->send(200, "application/json", jsonResponse);
    break;
}
```

## API Usage Examples

### Read Last Log
```
GET /sysctrl.cgi?cmd=READ_LOG&id=5
```

**Success Response - RFID Card Access (200 OK):**
```json
{
  "status": "OK",
  "device_id": 5,
  "log_id": 123,
  "event_code": "0xDC",
  "event_name": "GUEST_CARD",
  "event_description": "Gost ušao karticom",
  "type": 0,
  "group": 0,
  "card_id": "0A1B2C3D4E",
  "date": "15.03.2024",
  "time": "14:23:45",
  "timestamp": "15.03.2024 14:23:45"
}
```

**PIN Access Response - PASSWORD_VALID (200 OK):**
```json
{
  "status": "OK",
  "device_id": 5,
  "log_id": 124,
  "event_code": "0xF7",
  "event_name": "PASSWORD_VALID",
  "event_description": "Manager ušao PIN-om",
  "type": 0,
  "group": 3,
  "card_id": "0000000000",
  "date": "15.03.2024",
  "time": "15:30:12",
  "timestamp": "15.03.2024 15:30:12"
}
```

**Note about parsed fields:**
- **card_id**: 5-byte RFID card unique identifier (hex format). Shows "0000000000" for PIN access events.
- **date/time**: Exact RTC timestamp when log entry was created on IC controller (parsed from BCD format).
- **timestamp**: Combined date+time for easy display.

**Error Response (408 Timeout):**
```
ERROR: Device timeout - no response from IC controller
```

### Delete Last Log
```
GET /sysctrl.cgi?cmd=DELETE_LOG&id=5
```

**Success Response (200 OK):**
```json
{
  "status": "OK",
  "device_id": 5,
  "message": "Log deleted successfully"
}
```

**Empty Log Response (200 OK):**
```json
{
  "status": "EMPTY",
  "device_id": 5,
  "message": "Log list is empty"
}
```

## Web Interface Integration

### JavaScript Example for Log Display
```javascript with card ID and timestamp
            const logHtml = `
                <div class="log-entry">
                    <h4>Log #${data.log_id}</h4>
                    <p><strong>Događaj:</strong> ${data.event_description}</p>
                    <p><strong>Kartica ID:</strong> ${data.card_id}</p>
                    <p><strong>Vrijeme:</strong> ${data.timestamp}</p>
                    <p><strong>Uređaj:</strong> ${data.device_id
            // Display log entry
            const logHtml = `
                <div class="log-entry">
                    <h4>Log #${data.log_id}</h4>
                    <p><strong>Event:</strong> ${data.event_name}</p>
                    <p><strong>Card ID:</strong> ${data.card_id}</p>
                    <p><strong>Date:</strong> ${data.date} ${data.time}</p>
                </div>
            `;
            document.getElementById('logDisplay').innerHTML = logHtml;
        }
    } catch (error) {
        console.error('Error reading log:', error);
    }
}

async function deleteLog(deviceId) {
    try {
        const response = await fetch(`/sysctrl.cgi?cmd=DELETE_LOG&id=${deviceId}`);
        const data = await response.json();
        
        alert(data.message);
        
        if (data.status === "OK") {
            // Refresh log display
            readLog(deviceId);
        }
    } catch (error) {
        console.error('Error deleting log:', error);
    }
}

// Read multiple logs (loop until empty)
async function readAllLogs(deviceId) {
    const logs = [];
    let isEmpty = false;
    
    while (!isEmpty && logs.length < 100) { // Limit to 100 logs
        try {
            const response = await fetch(`/sysctrl.cgi?cmd=READ_LOG&id=${deviceId}`);
            const data = await response.json();
            
            if (data.status === "OK") {
                logs.push(data);
                
                // Delete to move to next log
                await fetch(`/sysctrl.cgi?cmd=DELETE_LOG&id=${deviceId}`);
                
                // Small delay between requests
                await new Promise(resolve => setTimeout(resolve, 100));
            } else {
                isEmpty = true;
            }
        } catch (error) {
            console.error('Error reading logs:', error);
            break;
        }
    }
    
    return logs;
}
```

## Notes

1. **LIFO Behavior**: Logger uses LIFO (Last In First Out) stack. `READ_LOG` always reads the newest log entry.

2. **Destructive Read**: To read all logs, you must call `READ_LOG` → `DELETE_LOG` in a loop until the log list is empty.

3. **BCD Format**: Date and time are stored in BCD (Binary Coded Decimal) format. Example: 0x23 = 23 decimal.

4. **Card ID**: 5-byte unique identifier from RFID card. Displayed as 10-character hex string.

5. **Event Types**: 
   - 0x90-0x9C: Driver errors (critical)
   - 0xA0-0xAE: Function errors (critical)
   - 0xB0-0xBE: System events (info)
   - 0xC8-0xF9: Application events (info/warning)

6. **Log Capacity**: Determined by EEPROM size on IC controller. Typically 100-500 log entries.

7. **Unused Fields**: `log_type` field is reserved but currently not used in IC controller implementation.

8. **User Group Field (`log_group`)**: Critical for PASSWORD_VALID events to distinguish user types:
   - `1` = USERGRP_GUEST (Gost)
   - `2` = USERGRP_MAID (Sobarica)
   - `3` = USERGRP_MANAGER (Manager)
   - `4` = USERGRP_SERVICE (Service)

9. **Web Display**: Parse event codes into human-readable strings using `getEventName()` function for better UX.

10. **Access Events - Ulaz u sobu (najbitniji događaji)**:
    - **RFID Kartice:**
      - `0xDC` - GUEST_CARD - "Gost ušao karticom"
      - `0xDD` - HANDMAID_CARD - "Sobarica ušla karticom"
      - `0xDE` - MANAGER_CARD - "Manager ušao karticom"
      - `0xDF` - SERVICE_CARD - "Service ušao karticom"
    
    - **PIN Kodovi:**
      - `0xF7` - PASSWORD_VALID - "Uspješan ulaz PIN-om" (provjeriti `log_group` za tip korisnika)
        - group=1: "Gost ušao PIN-om"
        - group=2: "Sobarica ušla PIN-om"
        - group=3: "Manager ušao PIN-om"
        - group=4: "Service ušao PIN-om"
      - `0xF8` - PASSWORD_INVALID - "Pogrešan PIN"

11. **User-Friendly Display**: Use `getAccessDescription(eventCode, userGroup)` helper function for Serbian/Bosnian language display on web interface.
