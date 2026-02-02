#ifndef FIRMWARE_UPDATE_SERVICE_H
#define FIRMWARE_UPDATE_SERVICE_H

#include <Arduino.h>
extern "C" {
    #include "TinyFrame.h"
}
#include "ExternalFlash.h"
#include "FirmwareDefs.h"

// TinyFrame Type ID for update messages
#define TF_TYPE_FIRMWARE_UPDATE  0xC1 

// Sub-commands (Protocol) - USKLAĐENO SA firmware_update_agent.c
#define SUB_CMD_START_REQUEST    0x01
#define SUB_CMD_START_ACK        0x02
#define SUB_CMD_START_NACK       0x03

#define SUB_CMD_DATA_PACKET      0x10
#define SUB_CMD_DATA_ACK         0x11
#define SUB_CMD_DATA_NACK        0x12

#define SUB_CMD_FINISH_REQUEST   0x20
#define SUB_CMD_FINISH_ACK       0x21
#define SUB_CMD_FINISH_NACK      0x22

// Parameters
#define MAX_UPDATE_RETRIES       20
#define DATA_CHUNK_SIZE          128 // Max payload per packet
#define RESPONSE_TIMEOUT_MS      2000
#define FLASH_WRITE_TIMEOUT_MS   10000 // Erase/Write can take time

enum UpdateState {
    UPD_IDLE,
    UPD_STARTING,
    UPD_WAIT_START_ACK,
    UPD_SENDING_DATA,
    UPD_WAIT_DATA_ACK,
    UPD_FINISHING,
    UPD_WAIT_FINISH_ACK,
    UPD_FAILED,
    UPD_SUCCESS
};

class FirmwareUpdateService {
public:
    FirmwareUpdateService(ExternalFlash& flash, TinyFrame& tf);

    // Start a broadcast or single update
    // fromSlot: 0-7
    // targetAddr: 1-254
    bool startUpdate(uint8_t fromSlot, uint8_t targetAddr, uint32_t stagingAddr = 0x90000000);

    void loop(); // Call in main loop
    void reset(); // Reset to IDLE state
    
    // TinyFrame Callback
    TF_Result handlePacket(TinyFrame *tf, TF_Msg *msg);

    bool isActive() { return _state != UPD_IDLE && _state != UPD_SUCCESS && _state != UPD_FAILED; }
    uint8_t getProgress() { return _progress; } // 0-100
    UpdateState getState() { return _state; }
    const char* getLastError() { return _lastError; }

private:
    ExternalFlash& _flash;
    TinyFrame& _tf;

    uint8_t _activeSlot;
    uint8_t _targetAddr;
    uint32_t _stagingAddr;
    UpdateState _state;
    char _lastError[128];
    
    FwInfoTypeDef _fwInfo;
    uint32_t _fileSize;
    uint32_t _bytesSent;
    uint32_t _currentSeq;
    uint32_t _lastPacketSize;
    
    unsigned long _timerStart;
    uint8_t _retryCount;
    uint8_t _progress;

    uint8_t _chunkBuffer[DATA_CHUNK_SIZE + 10]; // Buffer for reading + header

    void sendStartRequest();
    void sendDataPacket();
    void sendFinishRequest();
    void abort();
    
    // STM32 CRC32 Helper
    uint32_t calcCRC32(uint8_t slot);
};

#endif // FIRMWARE_UPDATE_SERVICE_H
