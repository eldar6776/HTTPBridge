#ifndef FIRMWARE_DEFS_H
#define FIRMWARE_DEFS_H

#include <stdint.h>

// Definicije preuzete iz common.h
#define VERS_INF_OFFSET      0x2000      // Offset adresa za firmware info strukturu

// Struktura metapodataka o firmware-u (5 * 4 = 20 bajtova)
typedef struct
{
	uint32_t size;      // firmware size
	uint32_t crc32;     // firmware crc32
	uint32_t version;   // fw version, fw type
	uint32_t wr_addr;   // firmware write address
	uint32_t ld_addr;   // firmware load address
} FwInfoTypeDef;

// Custom header for raw binary slots (4-7)
#define RAW_SLOT_MAGIC 0xDEADBEEF
typedef struct {
    uint32_t magic;         // Magic number to identify this header
    uint32_t size;          // Actual size of the binary file
    uint32_t crc32;         // CRC32 calculated by the ESP32 during upload
    char     filename[48];  // Original filename from upload
    uint8_t  valid;         // 1 if upload was completed, 0 otherwise
    uint8_t  reserved[3];   // Padding to 64 bytes
} RawSlotInfoTypeDef;

#define RAW_SLOT_HEADER_OFFSET 0 // Stored at the very beginning of the slot
#define RAW_SLOT_DATA_OFFSET 4096 // Start data after the first 4K sector

// Definicije slotova za External Flash (W25Q64 = 8MB)
#define EXT_FLASH_SIZE       (8 * 1024 * 1024)
#define FW_SLOT_SIZE         (1 * 1024 * 1024) // 1MB po slotu
#define FW_SLOT_COUNT        8

#endif // FIRMWARE_DEFS_H
