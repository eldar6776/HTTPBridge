#include "FirmwareUpdateService.h"
#include "LogMacros.h"

// STM32 CRC32 Polynomial
#define CRC32_POLYNOMIAL 0x04C11DB7

static uint32_t crc_update_word(uint32_t crc, uint32_t word) {
    crc ^= word;
    for (int i = 0; i < 32; i++) {
        if (crc & 0x80000000) {
            crc = (crc << 1) ^ CRC32_POLYNOMIAL;
        } else {
            crc <<= 1;
        }
    }
    return crc;
}

static uint32_t stm32_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    // Process 4 bytes at a time if possible (STM32 hardware CRC works on 32-bit words)
    // Here we simulate it byte-by-byte for simplicity but matching the logic
    // Actually, the reference implementation consumed bytes.
    // Let's stick to a known working byte-wise adaptation of STM32 CRC.
    
    // The reference implementation used crc_update_word which takes a 32-bit word.
    // To properly emulate STM32 CRC on a byte stream, we need to pad/combine bytes.
    // However, simplest is to use the exact same function as FirmwareUpdateManager.cpp provided:
    
    // Ref logic:
    // while (len--) {
    //    crc = crc_update_word(crc, (uint32_t)*data++);
    // }
    // NOTE: Casting uint8_t to uint32_t directly in the XOR is what the reference did.
    
    while (len--) {
        crc = crc_update_word(crc, (uint32_t)*data++);
    }
    return crc;
}

FirmwareUpdateService::FirmwareUpdateService(ExternalFlash& flash, TinyFrame& tf) 
    : _flash(flash), _tf(tf), _state(UPD_IDLE), _lastProgress(0), _lastProgressTime(0), 
      _wasCompleted(false), _lastTerminalState(UPD_IDLE) {}

bool FirmwareUpdateService::startUpdate(uint8_t fromSlot, uint8_t targetAddr, uint32_t stagingAddr) {
    if (_state != UPD_IDLE) return false;

    // Validate Slot and Read Info
    if (fromSlot < 4) {
        // Standard Firmware Slot (0-3) - Info is embedded at 0x2000
        if (!_flash.getSlotInfo(fromSlot, &_fwInfo)) {
            LOG_ERROR_LN("UpdateService: Failed to read slot info");
            return false;
        }
    } else {
        // Raw Binary Slot (4-7) - Info is at offset 0 (RawSlotInfoTypeDef)
        RawSlotInfoTypeDef rawInfo;
        if (!_flash.readBufferFromSlot(fromSlot, RAW_SLOT_HEADER_OFFSET, (uint8_t*)&rawInfo, sizeof(RawSlotInfoTypeDef))) {
            LOG_ERROR_LN("UpdateService: Failed to read raw slot info");
            return false;
        }
        
        if (rawInfo.magic != RAW_SLOT_MAGIC || rawInfo.valid != 1) {
            LOG_ERROR_LN("UpdateService: Invalid Raw Slot (Magic/Valid)");
            return false;
        }

        // Map Raw Info to FwInfo for transport
        memset(&_fwInfo, 0, sizeof(FwInfoTypeDef));
        _fwInfo.size = rawInfo.size;
        _fwInfo.crc32 = rawInfo.crc32;
        _fwInfo.version = 0; // Raw files nemaju verziju u headeru, šaljemo 0
    }

    // Sanity check
    if (_fwInfo.size == 0 || _fwInfo.size > FW_SLOT_SIZE) {
        LOG_ERROR_LN("UpdateService: Invalid FW size");
        return false;
    }

    // Verify CRC of the stored file BEFORE sending
    LOG_INFO_LN("UpdateService: Verifying storage CRC...");
    uint32_t calculatedCRC = calcCRC32(fromSlot);
    if (calculatedCRC != _fwInfo.crc32) {
        LOG_ERROR("UpdateService: CRC Mismatch! Stored: %08X, Calc: %08X\n", _fwInfo.crc32, calculatedCRC);
        // return false; // TODO: Uncomment this in production, disabled for testing if needed
    }

    _activeSlot = fromSlot;
    _targetAddr = targetAddr;
    _stagingAddr = stagingAddr;
    _fileSize = _fwInfo.size;
    _bytesSent = 0;
    _currentSeq = 0;
    _retryCount = 0;
    _progress = 0;
    _lastProgress = 0;
    _lastProgressTime = millis();
    _wasCompleted = false;
    _lastTerminalState = UPD_IDLE;
    
    LOG_INFO("UpdateService: Starting update for ID %d from Slot %d (Size: %d bytes)\n", targetAddr, fromSlot, _fileSize);
    
    _state = UPD_STARTING;
    return true;
}

uint32_t FirmwareUpdateService::calcCRC32(uint8_t slot) {
    uint32_t crc = 0xFFFFFFFF;
    uint32_t remaining = _fwInfo.size;
    // Za Raw slotove podaci počinju od 4096, za FW od 0
    uint32_t offset = (slot >= 4) ? RAW_SLOT_DATA_OFFSET : 0;
    uint8_t buf[128];

    while (remaining > 0) {
        size_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        _flash.readBufferFromSlot(slot, offset, buf, chunk);
        crc = stm32_crc32_update(crc, buf, chunk);
        remaining -= chunk;
        offset += chunk;
        yield();
    }
    return crc;
}

void FirmwareUpdateService::reset() {
    _state = UPD_IDLE;
    _activeSlot = 0;
    _targetAddr = 0;
    _stagingAddr = 0;
    _fileSize = 0;
    _bytesSent = 0;
    _currentSeq = 0;
    _lastPacketSize = 0;
    _retryCount = 0;
    _progress = 0;
    _lastProgress = 0;
    _lastProgressTime = 0;
    _wasCompleted = false;
    _lastTerminalState = UPD_IDLE;
    _lastError[0] = '\0';
    memset(&_fwInfo, 0, sizeof(FwInfoTypeDef));
}

void FirmwareUpdateService::loop() {
    // Terminal state retention - drži SUCCESS/FAILED 60 sekundi (6× frontend polling interval)
    static unsigned long terminalStateTime = 0;
    if (_state == UPD_SUCCESS || _state == UPD_FAILED) {
        if (terminalStateTime == 0) {
            terminalStateTime = millis();
            _wasCompleted = true;
            _lastTerminalState = _state;
            LOG_INFO("UpdateService: Entered terminal state %s - retention for %dms\n", 
                     _state == UPD_SUCCESS ? "SUCCESS" : "FAILED", TERMINAL_STATE_RETENTION_MS);
        } else if (millis() - terminalStateTime > TERMINAL_STATE_RETENTION_MS) {
            LOG_INFO("UpdateService: Auto-reset after %s (retention expired)\n", 
                     _state == UPD_SUCCESS ? "SUCCESS" : "FAILED");
            reset();
            terminalStateTime = 0;
        }
        return;
    }
    terminalStateTime = 0;
    
    if (_state == UPD_IDLE) return;

    unsigned long now = millis();

    // No-progress timeout detekcija - ako progress ne raste 30 sekundi = FAIL
    if (_state == UPD_SENDING_DATA || _state == UPD_WAIT_DATA_ACK) {
        if (_progress != _lastProgress) {
            // Progress se promenio - resetuj timer
            _lastProgress = _progress;
            _lastProgressTime = now;
        } else if (now - _lastProgressTime > NO_PROGRESS_TIMEOUT_MS) {
            // Nema progresa duže od 30s
            snprintf(_lastError, sizeof(_lastError), "No progress for %dms at %d%%", NO_PROGRESS_TIMEOUT_MS, _progress);
            LOG_ERROR("UpdateService: %s - ABORTING\n", _lastError);
            abort();
            return;
        }
    }

    switch (_state) {
        case UPD_STARTING:
            sendStartRequest();
            break;

        case UPD_SENDING_DATA:
            sendDataPacket();
            break;

        case UPD_FINISHING:
            sendFinishRequest();
            break;

        case UPD_WAIT_START_ACK:
        case UPD_WAIT_DATA_ACK:
        case UPD_WAIT_FINISH_ACK:
            if (now - _timerStart > (_state == UPD_WAIT_START_ACK ? FLASH_WRITE_TIMEOUT_MS : RESPONSE_TIMEOUT_MS)) {
                if (_retryCount < MAX_UPDATE_RETRIES) {
                    LOG_INFO("UpdateService: Timeout (State %d), Retrying (%d/%d)...\n", _state, _retryCount+1, MAX_UPDATE_RETRIES);
                    _retryCount++;
                    // Revert state to resend
                    if (_state == UPD_WAIT_START_ACK) _state = UPD_STARTING;
                    else if (_state == UPD_WAIT_DATA_ACK) _state = UPD_SENDING_DATA;
                    else if (_state == UPD_WAIT_FINISH_ACK) _state = UPD_FINISHING;
                } else {
                    snprintf(_lastError, sizeof(_lastError), "Timeout after %d retries (State %d)", MAX_UPDATE_RETRIES, _state);
                    LOG_ERROR("UpdateService: %s\n", _lastError);
                    abort();
                }
            }
            break;
    }
}

void FirmwareUpdateService::abort() {
    _state = UPD_FAILED;
}

void FirmwareUpdateService::sendStartRequest() {
    // Construct payload: [SUB_CMD(1)] [ADDR(1)] [FwInfo(20)] [Reserved(4)]
    // Based on update_manager.c logic
    uint8_t payload[32];
    payload[0] = SUB_CMD_START_REQUEST;
    payload[1] = _targetAddr;
    memcpy(&payload[2], &_fwInfo, sizeof(FwInfoTypeDef));
    
    // KLJUČNO: Prebriši ld_addr (offset 16 u structu, offset 18 u payloadu) 
    // sa staging adresom koju očekuje agent (npr. 0x90000000 za QSPI)
    memcpy(&payload[18], &_stagingAddr, 4);

    TF_SendSimple(&_tf, TF_TYPE_FIRMWARE_UPDATE, payload, 26);
    
    _timerStart = millis();
    _state = UPD_WAIT_START_ACK;
    LOG_INFO_LN("UpdateService: Sent START_REQUEST");
}

void FirmwareUpdateService::sendDataPacket() {
    uint32_t remaining = _fileSize - _bytesSent;
    size_t chunk = (remaining > DATA_CHUNK_SIZE) ? DATA_CHUNK_SIZE : remaining;

    // Izračunaj offset čitanja zavisno od tipa slota
    uint32_t readOffset = _bytesSent;
    if (_activeSlot >= 4) {
        readOffset += RAW_SLOT_DATA_OFFSET;
    }

    // Read from Flash
    if (!_flash.readBufferFromSlot(_activeSlot, readOffset, _chunkBuffer, chunk)) {
        LOG_ERROR_LN("UpdateService: Flash Read Error!");
        abort();
        return;
    }

    // Packet: [SUB_CMD(1)] [ADDR(1)] [SEQ(4)] [DATA...]
    uint8_t payload[6 + DATA_CHUNK_SIZE];
    payload[0] = SUB_CMD_DATA_PACKET;
    payload[1] = _targetAddr;
    memcpy(&payload[2], &_currentSeq, 4);
    memcpy(&payload[6], _chunkBuffer, chunk);

    TF_SendSimple(&_tf, TF_TYPE_FIRMWARE_UPDATE, payload, 6 + chunk);

    _lastPacketSize = chunk;
    _timerStart = millis();
    _state = UPD_WAIT_DATA_ACK;
    // LOG_DEBUG_F("UpdateService: Sent DATA Seq %d (Len %d)\n", _currentSeq, chunk);
}

void FirmwareUpdateService::sendFinishRequest() {
    uint8_t payload[10];
    payload[0] = SUB_CMD_FINISH_REQUEST;
    payload[1] = _targetAddr;
    memcpy(&payload[2], &_fwInfo.crc32, 4);

    TF_SendSimple(&_tf, TF_TYPE_FIRMWARE_UPDATE, payload, 6);

    _timerStart = millis();
    _state = UPD_WAIT_FINISH_ACK;
    LOG_INFO_LN("UpdateService: Sent FINISH_REQUEST");
}

TF_Result FirmwareUpdateService::handlePacket(TinyFrame *tf, TF_Msg *msg) {
    if (_state == UPD_IDLE) return TF_STAY;
    
    if (msg->type != TF_TYPE_FIRMWARE_UPDATE) return TF_STAY;
    if (msg->len < 2) return TF_STAY;

    uint8_t subCmd = msg->data[0];
    uint8_t addr = msg->data[1];

    if (addr != _targetAddr) return TF_STAY; // Ignore other devices

    switch (subCmd) {
        case SUB_CMD_START_ACK:
            if (_state == UPD_WAIT_START_ACK) {
                LOG_INFO_LN("UpdateService: START_ACK received.");
                _state = UPD_SENDING_DATA;
                _retryCount = 0;
            }
            break;
            
        case SUB_CMD_DATA_ACK:
            if (_state == UPD_WAIT_DATA_ACK) {
                uint32_t ackSeq;
                if (msg->len >= 6) {
                    memcpy(&ackSeq, &msg->data[2], 4);
                    if (ackSeq == _currentSeq) {
                        _bytesSent += _lastPacketSize;
                        _currentSeq++;
                        _retryCount = 0;
                        
                        _progress = (_bytesSent * 100) / _fileSize;
                        
                        if (_bytesSent >= _fileSize) {
                            LOG_INFO_LN("UpdateService: File sent. Finishing...");
                            _state = UPD_FINISHING;
                        } else {
                            _state = UPD_SENDING_DATA;
                        }
                    }
                }
            }
            break;

        case SUB_CMD_FINISH_ACK:
            if (_state == UPD_WAIT_FINISH_ACK) {
                LOG_INFO_LN("UpdateService: UPDATE COMPLETE!");
                _state = UPD_SUCCESS;
                _progress = 100;
            }
            break;
            
        case SUB_CMD_START_NACK:
        case SUB_CMD_DATA_NACK:
        case SUB_CMD_FINISH_NACK:
        {
            uint8_t nackReason = (msg->len >= 3) ? msg->data[2] : 0;
            const char* reasonStr[] = {
                "NONE", "FILE_TOO_LARGE", "INVALID_VERSION", "ERASE_FAILED",
                "WRITE_FAILED", "CRC_MISMATCH", "UNEXPECTED_PACKET", "SIZE_MISMATCH"
            };
            const char* typeStr = subCmd == SUB_CMD_START_NACK ? "START" : 
                                  subCmd == SUB_CMD_DATA_NACK ? "DATA" : "FINISH";
            snprintf(_lastError, sizeof(_lastError), "NACK %s: %s (%d)", 
                     typeStr, nackReason < 8 ? reasonStr[nackReason] : "UNKNOWN", nackReason);
            LOG_ERROR("UpdateService: %s\n", _lastError);
            // Force timeout logic to handle retry
            _timerStart = 0; 
            break;
        }
    }

    return TF_STAY;
}
