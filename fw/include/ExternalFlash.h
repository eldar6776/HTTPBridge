#ifndef EXTERNAL_FLASH_H
#define EXTERNAL_FLASH_H

#include <Arduino.h>
#include <SPI.h>
#include "FirmwareDefs.h"

// W25Q64 Commands
#define W25Q_WRITE_ENABLE        0x06
#define W25Q_SECTOR_ERASE_4K     0x20
#define W25Q_BLOCK_ERASE_32K     0x52
#define W25Q_BLOCK_ERASE_64K     0xD8
#define W25Q_CHIP_ERASE          0xC7
#define W25Q_PAGE_PROGRAM        0x02
#define W25Q_READ_DATA           0x03
#define W25Q_READ_STATUS_1       0x05
#define W25Q_JEDEC_ID            0x9F
#define W25Q_RELEASE_POWER_DOWN  0xAB
#define W25Q_POWER_DOWN          0xB9
#define W25Q_BUSY_MASK           0x01

class ExternalFlash {
public:
    ExternalFlash(int csPin, SPIClass& spiBus);
    
    bool begin();
    uint32_t readJEDECID();
    
    // Slot management
    bool eraseSlot(uint8_t slotIndex);
    bool writeBufferToSlot(uint8_t slotIndex, uint32_t offset, const uint8_t* data, size_t len);
    bool readBufferFromSlot(uint8_t slotIndex, uint32_t offset, uint8_t* buffer, size_t len);
    
    // Firmware metadata inspection
    bool getSlotInfo(uint8_t slotIndex, FwInfoTypeDef* info);
    
    // Low level
    void read(uint32_t addr, uint8_t* buf, size_t len);
    void write(uint32_t addr, const uint8_t* buf, size_t len);
    void eraseSector4K(uint32_t addr);
    void eraseBlock64K(uint32_t addr);

private:
    int _cs;
    SPIClass& _spi;

    void waitUntilReady();
    void writeEnable();
    uint8_t readStatus();
    uint32_t getSlotAddress(uint8_t slotIndex);
};

#endif // EXTERNAL_FLASH_H
