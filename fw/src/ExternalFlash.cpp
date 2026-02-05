#include "ExternalFlash.h"
#include "LogMacros.h"

ExternalFlash::ExternalFlash(int csPin, SPIClass& spiBus) : _cs(csPin), _spi(spiBus) {}

bool ExternalFlash::begin() {
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    delay(100); // Longer initial delay
    
    // Release from Power-Down mode (in case chip was sleeping)
    digitalWrite(_cs, LOW);
    delay(10);
    _spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    _spi.transfer(W25Q_RELEASE_POWER_DOWN);
    _spi.endTransaction();
    delay(10);
    digitalWrite(_cs, HIGH);
    delay(100); // Much longer delay after wake-up
    
    LOG_INFO_LN("ExtFlash: Testing SPI communication...");
    LOG_DEBUG_F("ExtFlash: CS pin state: %d\n", digitalRead(_cs));
    
    // Try to read JEDEC ID multiple times with delays
    for (int attempt = 0; attempt < 5; attempt++) {
        uint32_t jedecId = readJEDECID();
        LOG_DEBUG_F("ExtFlash: Attempt %d - JEDEC ID: 0x%06X\n", attempt + 1, jedecId);
        
        if (jedecId != 0x000000 && jedecId != 0xFFFFFF) {
            LOG_INFO("ExtFlash: Valid device found! Manufacturer: 0x%02X, Type: 0x%02X, Capacity: 0x%02X\n",
                         (jedecId >> 16) & 0xFF, (jedecId >> 8) & 0xFF, jedecId & 0xFF);
            
            // Set QE bit to disable /HOLD and /WP functionality
            // This converts them to IO3/IO2, eliminating floating /HOLD issue
            LOG_INFO_LN("ExtFlash: Setting QE bit to disable /HOLD...");
            writeEnable();
            
            // Write Status Register to set QE=1 (bit 9, in SR2)
            digitalWrite(_cs, LOW);
            delayMicroseconds(10);
            _spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
            _spi.transfer(0x01); // Write Status Register
            _spi.transfer(0x00); // SR1: Clear all protection
            _spi.transfer(0x02); // SR2: Set QE bit (bit 1 of SR2 = bit 9 overall)
            _spi.endTransaction();
            delayMicroseconds(10);
            digitalWrite(_cs, HIGH);
            delay(20); // Wait for write to complete
            
            LOG_INFO_LN("ExtFlash: QE bit set, /HOLD is now disabled");
            
            // Now do global block unlock
            writeEnable();
            
            digitalWrite(_cs, LOW);
            delayMicroseconds(1);
            
            _spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
            _spi.transfer(0x01); // Write Status Register
            _spi.transfer(0x00); // Clear all protection bits (BP0, BP1, BP2...)
            _spi.endTransaction();
            
            delayMicroseconds(1);
            digitalWrite(_cs, HIGH);
            waitUntilReady();
            
            return true;
        }
        delay(50);
    }
    
    LOG_ERROR_LN("ExtFlash: No device found or invalid ID!");
    return false;
}

uint32_t ExternalFlash::readJEDECID() {
    digitalWrite(_cs, LOW);
    delayMicroseconds(10);
    
    _spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    
    // Send JEDEC ID command
    uint8_t cmd_response = _spi.transfer(W25Q_JEDEC_ID);
    LOG_DEBUG_F("ExtFlash: CMD response: 0x%02X\n", cmd_response);
    
    // Read 3 bytes
    uint8_t mfr = _spi.transfer(0x00);
    uint8_t type = _spi.transfer(0x00);
    uint8_t cap = _spi.transfer(0x00);
    
    LOG_DEBUG_F("ExtFlash: Raw bytes: MFR=0x%02X, Type=0x%02X, Cap=0x%02X\n", mfr, type, cap);
    
    _spi.endTransaction();
    
    delayMicroseconds(1);
    digitalWrite(_cs, HIGH);
    
    uint32_t id = (mfr << 16) | (type << 8) | cap;
    return id;
}

void ExternalFlash::waitUntilReady() {
    while (readStatus() & W25Q_BUSY_MASK) {
        yield(); // Allow other tasks to run
    }
}

void ExternalFlash::writeEnable() {
    digitalWrite(_cs, LOW);
    delayMicroseconds(1);
    
    _spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    _spi.transfer(W25Q_WRITE_ENABLE);
    _spi.endTransaction();
    
    delayMicroseconds(1);
    digitalWrite(_cs, HIGH);
    
    // Verify WEL bit
    uint8_t s = readStatus();
    if (!(s & 0x02)) LOG_ERROR("ExtFlash: WriteEnable FAILED! Status: 0x%02X\n", s);
}

uint8_t ExternalFlash::readStatus() {
    digitalWrite(_cs, LOW);
    delayMicroseconds(1);
    
    _spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    _spi.transfer(W25Q_READ_STATUS_1);
    uint8_t status = _spi.transfer(0x00);
    _spi.endTransaction();
    
    delayMicroseconds(1);
    digitalWrite(_cs, HIGH);
    return status;
}

uint32_t ExternalFlash::getSlotAddress(uint8_t slotIndex) {
    if (slotIndex >= FW_SLOT_COUNT) return 0xFFFFFFFF;
    return slotIndex * FW_SLOT_SIZE;
}

bool ExternalFlash::eraseSlot(uint8_t slotIndex) {
    uint32_t startAddr = getSlotAddress(slotIndex);
    if (startAddr == 0xFFFFFFFF) return false;

    // Erase 1MB (16 blocks of 64KB)
    for (int i = 0; i < 16; i++) {
        eraseBlock64K(startAddr + (i * 64 * 1024));
    }
    return true;
}

void ExternalFlash::eraseBlock64K(uint32_t addr) {
    waitUntilReady();
    writeEnable();
    
    // Debug: Check if WEL is set
    // uint8_t s = readStatus();
    // if (!(s & 0x02)) LOG_ERROR("ExtFlash: Erase Block %08X - WEL NOT SET! Status: 0x%02X\n", addr, s);

    digitalWrite(_cs, LOW);
    delayMicroseconds(1);
    
    _spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    _spi.transfer(W25Q_BLOCK_ERASE_64K);
    _spi.transfer((addr >> 16) & 0xFF);
    _spi.transfer((addr >> 8) & 0xFF);
    _spi.transfer(addr & 0xFF);
    _spi.endTransaction();
    
    delayMicroseconds(1);
    digitalWrite(_cs, HIGH);
    
    // LOG_DEBUG_F("ExtFlash: Erase Block %08X command sent.\n", addr);
    waitUntilReady();
}

void ExternalFlash::write(uint32_t addr, const uint8_t* buf, size_t len) {
    // Page Program works on 256 byte pages max, and cannot cross page boundaries
    size_t toWrite = len;
    const uint8_t* ptr = buf;
    uint32_t currentAddr = addr;

    while (toWrite > 0) {
        waitUntilReady();
        writeEnable();

        size_t pageOffset = currentAddr & 0xFF;
        size_t spaceInPage = 256 - pageOffset;
        size_t chunk = (toWrite < spaceInPage) ? toWrite : spaceInPage;

        digitalWrite(_cs, LOW);
        delayMicroseconds(1);
        
        _spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        _spi.transfer(W25Q_PAGE_PROGRAM);
        _spi.transfer((currentAddr >> 16) & 0xFF);
        _spi.transfer((currentAddr >> 8) & 0xFF);
        _spi.transfer(currentAddr & 0xFF);
        for (size_t i = 0; i < chunk; i++) {
            _spi.transfer(ptr[i]);
        }
        _spi.endTransaction();
        
        delayMicroseconds(1);
        digitalWrite(_cs, HIGH);

        currentAddr += chunk;
        ptr += chunk;
        toWrite -= chunk;
    }
    waitUntilReady();
}

void ExternalFlash::read(uint32_t addr, uint8_t* buf, size_t len) {
    waitUntilReady();
    
    digitalWrite(_cs, LOW);
    delayMicroseconds(1);
    
    _spi.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0)); // Read can be faster
    _spi.transfer(W25Q_READ_DATA);
    _spi.transfer((addr >> 16) & 0xFF);
    _spi.transfer((addr >> 8) & 0xFF);
    _spi.transfer(addr & 0xFF);
    for (size_t i = 0; i < len; i++) {
        buf[i] = _spi.transfer(0x00);
    }
    _spi.endTransaction();
    
    delayMicroseconds(1);
    digitalWrite(_cs, HIGH);
}

bool ExternalFlash::writeBufferToSlot(uint8_t slotIndex, uint32_t offset, const uint8_t* data, size_t len) {
    uint32_t baseAddr = getSlotAddress(slotIndex);
    if (baseAddr == 0xFFFFFFFF || (offset + len) > FW_SLOT_SIZE) return false;
    
    write(baseAddr + offset, data, len);
    return true;
}

bool ExternalFlash::readBufferFromSlot(uint8_t slotIndex, uint32_t offset, uint8_t* buffer, size_t len) {
    uint32_t baseAddr = getSlotAddress(slotIndex);
    if (baseAddr == 0xFFFFFFFF || (offset + len) > FW_SLOT_SIZE) return false;

    read(baseAddr + offset, buffer, len);
    return true;
}

bool ExternalFlash::getSlotInfo(uint8_t slotIndex, FwInfoTypeDef* info) {
    return readBufferFromSlot(slotIndex, VERS_INF_OFFSET, (uint8_t*)info, sizeof(FwInfoTypeDef));
}
