/**
 ******************************************************************************
 * @file    FirmwareUpdateManager.cpp
 * @author  Gemini Code Assist
 * @brief   Implementacija FirmwareUpdateManager - ISKLJUČIVO za fuf/buf komande.
 *
 * @note
 * Ovaj modul je potpuno odvojen od UpdateManager-a da bi se osiguralo
 * da logika za transfer slika (iuf) ostane netaknuta.
 ******************************************************************************
 */

#include "FirmwareUpdateManager.h"
#include "ProjectConfig.h"
#include "TimeSync.h"
#include "HttpServer.h"  // NAKON ostalih da izbjegnemo FILE_READ konflikt
#include <cstring>

// HttpServer.h može da override-uje FILE_READ macro, pa ga eksplicitno definišemo
#ifndef FILE_READ
#define FILE_READ "r"
#endif

// Globalna konfiguracija (extern)
extern AppConfig g_appConfig;

// ============================================================================
// --- STM32 CRC32 HARDWARE-LIKE SOFTWARE IMPLEMENTATION ---
// Replicating the exact logic from the provided stm32_crc.c file.
// Ove funkcije su kopirane iz UpdateManager.cpp da bi modul bio nezavisan.
// ============================================================================
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
    while (len--) {
        crc = crc_update_word(crc, (uint32_t)*data++);
    }
    return crc;
}

static uint32_t CalculateCRC32(File& file)
{
    uint32_t crc = 0xFFFFFFFF; 
    uint8_t buffer[256];
    file.seek(0);
    while (file.available())
    {
        size_t len = file.read(buffer, sizeof(buffer));
        crc = stm32_crc32_update(crc, buffer, len);
    }
    file.seek(0);
    return crc;
}

FirmwareUpdateManager::FirmwareUpdateManager()
{
    m_session.state = FUF_S_IDLE;
    m_sequence.is_active = false;
    m_rs485_service = NULL;
    m_sd_card_manager = NULL;
    m_http_server = NULL;
}

void FirmwareUpdateManager::Initialize(Rs485Service* pRs485Service, SdCardManager* pSdCardManager)
{
    m_rs485_service = pRs485Service;
    m_sd_card_manager = pSdCardManager;
}

bool FirmwareUpdateManager::IsActive()
{
    return m_sequence.is_active;
}

void FirmwareUpdateManager::StopSequence()
{
    m_sequence.is_active = false;
    m_sequence.current_addr = 0;
    Serial.println(F("[FufManager] Sekvenca zaustavljena."));
    
    // POKRENI SERVER nakon što je cijela sekvenca završena!
    if (m_http_server) {
        m_http_server->Start();
    }
}

void FirmwareUpdateManager::StartFirmwareUpdateSequence(uint16_t first_addr, uint16_t last_addr, FufUpdateType type)
{
    if (m_sequence.is_active) {
        Serial.println("[FufManager] UPOZORENJE: Nova FUF sekvenca zatražena dok je stara aktivna.");
        return;
    }
    m_sequence.is_active = true;
    m_sequence.first_addr = first_addr;
    m_sequence.last_addr = last_addr;
    m_sequence.current_addr = first_addr;
    m_sequence.type = type;
    Serial.printf("[FufManager] FUF sekvenca pokrenuta: Adrese %d-%d, Tip %d\n", first_addr, last_addr, type);
}

bool FirmwareUpdateManager::StartSession(uint16_t clientAddress, FufUpdateType type)
{
    if (m_session.state != FUF_S_IDLE) {
        Serial.println(F("[FufManager] GREŠKA: Sesija već aktivna!"));
        return false;
    }

    if (m_http_server) m_http_server->Stop();

    m_session.clientAddress = clientAddress;
    m_session.bytesSent = 0;
    m_session.currentSequenceNum = 0;
    m_session.retryCount = 0;

    if (type == FUF_TYPE_FIRMWARE) {
        m_session.filename = "/IMG20.RAW";
    } else {
        m_session.filename = "/IMG21.RAW";
    }

    if (!m_sd_card_manager->FileExists(m_session.filename.c_str())) {
        Serial.printf("[FufManager] GREŠKA: Fajl '%s' ne postoji!\n", m_session.filename.c_str());
        if (m_http_server) m_http_server->Start();
        return false;
    }

    m_session.file_handle = m_sd_card_manager->OpenFile(m_session.filename.c_str(), FILE_READ);
    if (!m_session.file_handle) {
        Serial.printf("[FufManager] GREŠKA: Ne mogu otvoriti fajl '%s'\n", m_session.filename.c_str());
        if (m_http_server) m_http_server->Start();
        return false;
    }

    m_session.file_size = m_session.file_handle.size();
    m_session.file_crc = CalculateCRC32(m_session.file_handle);

    Serial.printf("[FufManager] Sesija pokrenuta za klijenta 0x%X, fajl %s\n", clientAddress, m_session.filename.c_str());
    m_session.state = FUF_S_STARTING;
    return true;
}

void FirmwareUpdateManager::Run()
{
    if (!IsActive()) {
        return;
    }

    // Upravljanje sekvencom
    if (m_session.state == FUF_S_IDLE)
    {
        if (m_sequence.current_addr > m_sequence.last_addr) {
            Serial.println("[FufManager] KRAJ SEKVENCIJE: Sve adrese su obrađene.");
            StopSequence();
            return;
        }

        // Pauza između transfera
        if (m_sequence.current_addr > m_sequence.first_addr) {
            Serial.printf("[FufManager] Pauza od %dms prije sljedećeg transfera...\n", APP_START_DEL);
            vTaskDelay(pdMS_TO_TICKS(APP_START_DEL));
        }

        Serial.printf("[FufManager] Pokretanje dijela sekvence: Adresa %d\n", m_sequence.current_addr);
        if (!StartSession(m_sequence.current_addr, m_sequence.type)) {
             Serial.printf("[FufManager] Greška pri pokretanju sesije za adresu %d. Prekidam sekvencu.\n", m_sequence.current_addr);
             StopSequence();
             return;
        }
        m_sequence.current_addr++;
    }

    // Ako sesija nije aktivna, izađi
    if (m_session.state == FUF_S_IDLE) {
        return;
    }

    // Provjera maksimalnog broja pokušaja
    if (m_session.retryCount >= MAX_UPDATE_RETRIES) {
        Serial.println(F("[FufManager] GREŠKA: Previše neuspješnih pokušaja. Update prekinut."));
        CleanupSession(true);
        return;
    }

    uint8_t response_buffer[MAX_PACKET_LENGTH];
    int response_len = 0;
    uint32_t response_timeout = 0;

    // Faza 1: Slanje (ako je potrebno)
    switch (m_session.state)
    {
        case FUF_S_STARTING:
            SendStartRequest();
            // Nakon slanja, stanje se mijenja u FUF_S_WAITING_FOR_START_ACK, pa prelazimo na čekanje
            break;

        case FUF_S_SENDING_DATA:
            SendDataPacket();
            // Nakon slanja, stanje se mijenja u FUF_S_WAITING_FOR_DATA_ACK
            break;

        case FUF_S_SENDING_RESTART_CMD:
            SendRestartCommand();
            m_session.state = FUF_S_PENDING_APP_START;
            // Ne čekamo odgovor, nastavljamo odmah na sljedeće stanje
            return;

        case FUF_S_PENDING_APP_START:
            Serial.printf("[FufManager] Pauza od %dms prije slanja APP_EXE...\n", APP_START_DEL);
            vTaskDelay(pdMS_TO_TICKS(APP_START_DEL));
            m_session.state = FUF_S_SENDING_APP_EXE;
            // Odmah prelazimo na slanje
            return;

        case FUF_S_SENDING_APP_EXE:
            SendAppExeCommand();
            CleanupSession(false); // Ne čekamo odgovor, završavamo
            return;

        default:
            // Ako smo u nekom od stanja čekanja, ne radimo ništa u ovom dijelu
            break;
    }

    // Faza 2: Čekanje odgovora (ako smo u stanju čekanja)
    if (m_session.state == FUF_S_WAITING_FOR_START_ACK)
    {
        response_timeout = IMG_COPY_DEL; // Dugi timeout za brisanje flash-a
        response_len = m_rs485_service->ReceivePacket(response_buffer, MAX_PACKET_LENGTH, response_timeout);
        if (response_len > 0) {
            ProcessResponse(response_buffer, response_len);
        } else {
            OnTimeout();
        }
    }
    else if (m_session.state == FUF_S_WAITING_FOR_DATA_ACK)
    {
        if ((m_session.bytesSent + m_session.read_chunk_size) >= m_session.file_size) {
            response_timeout = IMG_COPY_DEL; // Dugi timeout za CRC verifikaciju
        } else {
            response_timeout = UPDATE_PACKET_TIMEOUT_MS;
        }
        response_len = m_rs485_service->ReceivePacket(response_buffer, MAX_PACKET_LENGTH, response_timeout);
        if (response_len > 0) {
            ProcessResponse(response_buffer, response_len);
        } else {
            OnTimeout();
        }
    }
}

void FirmwareUpdateManager::ProcessResponse(const uint8_t* packet, uint16_t length)
{
    uint8_t ack_nack = packet[0];

    if (m_session.state == FUF_S_WAITING_FOR_START_ACK)
    {
        if (ack_nack == ACK) {
            Serial.println(F("[FufManager] -> Primljen START ACK. Započinjem slanje podataka..."));
            m_session.state = FUF_S_SENDING_DATA;
            m_session.retryCount = 0;
            m_session.currentSequenceNum = 1;
        } else {
            Serial.println(F("[FufManager] -> Primljen START NACK. Pokrećem ponovni pokušaj..."));
            OnTimeout();
        }
    }
    else if (m_session.state == FUF_S_WAITING_FOR_DATA_ACK)
    {
        if (ack_nack == ACK) {
            Serial.printf("[FufManager] -> Primljen DATA ACK za paket #%lu.\n", m_session.currentSequenceNum);
            m_session.retryCount = 0;
            m_session.bytesSent += m_session.read_chunk_size;

            if (m_session.bytesSent >= m_session.file_size) {
                Serial.println(F("[FufManager] Slanje fajla završeno. Prelazim na slanje START_BLDR komande."));
                m_session.state = FUF_S_SENDING_RESTART_CMD;
            } else {
                m_session.currentSequenceNum++;
                m_session.state = FUF_S_SENDING_DATA;
            }
        } else {
            Serial.printf("[FufManager] -> Primljen NACK. Pokušaj %d od %d...\n", m_session.retryCount + 1, MAX_UPDATE_RETRIES);
            m_session.retryCount++;
            m_session.state = FUF_S_SENDING_DATA;
            m_session.file_handle.seek(m_session.bytesSent);
        }
    }
}

void FirmwareUpdateManager::OnTimeout()
{
    m_session.retryCount++;
    Serial.printf("[FufManager] Timeout. Pokušaj %d od %d...\n", m_session.retryCount, MAX_UPDATE_RETRIES);

    switch(m_session.state) {
        case FUF_S_WAITING_FOR_START_ACK:
            m_session.state = FUF_S_STARTING;
            break;
        case FUF_S_WAITING_FOR_DATA_ACK:
            m_session.file_handle.seek(m_session.bytesSent);
            m_session.state = FUF_S_SENDING_DATA;
            break;
        default: break;
    }
}

void FirmwareUpdateManager::SendStartRequest()
{
    FufUpdateSession* s = &m_session;
    uint8_t packet[32];
    uint16_t rsifa = g_appConfig.rs485_iface_addr;
    uint16_t data_len = 11; // Fiksna dužina za file update start

    uint8_t sub_cmd = (s->filename == "/IMG20.RAW") ? DWNLD_FWR_IMG : DWNLD_BLDR_IMG;

    Serial.printf("[FufManager] -> Šaljem START paket (CMD: 0x%02X)...\n", sub_cmd);

    packet[0] = SOH;
    packet[1] = (s->clientAddress >> 8);
    packet[2] = (s->clientAddress & 0xFF);
    packet[3] = (rsifa >> 8);
    packet[4] = (rsifa & 0xFF);
    packet[5] = data_len;
    packet[6] = sub_cmd;

    uint16_t total_packets = (s->file_size + UPDATE_DATA_CHUNK_SIZE - 1) / UPDATE_DATA_CHUNK_SIZE;
    packet[7] = (total_packets >> 8) & 0xFF;
    packet[8] = total_packets & 0xFF;

    packet[9] = (s->file_size >> 24);
    packet[10] = (s->file_size >> 16);
    packet[11] = (s->file_size >> 8);
    packet[12] = (s->file_size & 0xFF);

    packet[13] = (s->file_crc >> 24);
    packet[14] = (s->file_crc >> 16);
    packet[15] = (s->file_crc >> 8);
    packet[16] = (s->file_crc & 0xFF);

    uint16_t checksum = 0;
    for (uint32_t i = 6; i < (6 + data_len); i++) checksum += packet[i];

    packet[17] = (checksum >> 8);
    packet[18] = (checksum & 0xFF);
    packet[19] = EOT;

    if (m_rs485_service->SendPacket(packet, 20)) {
        s->state = FUF_S_WAITING_FOR_START_ACK;
    } else {
        CleanupSession(true);
    }
}

void FirmwareUpdateManager::SendDataPacket()
{
    FufUpdateSession* s = &m_session;
    int16_t bytes_read = s->file_handle.read(s->read_buffer, UPDATE_DATA_CHUNK_SIZE);

    if (bytes_read <= 0) {
        Serial.println(F("[FufManager] GREŠKA: Neočekivan kraj fajla."));
        CleanupSession(true);
        return;
    }

    s->read_chunk_size = (uint16_t)bytes_read;
    Serial.printf("[FufManager] -> Šaljem DATA paket #%lu (%d bajtova)...\n", s->currentSequenceNum, s->read_chunk_size);

    uint8_t packet[MAX_PACKET_LENGTH];
    uint16_t rsifa = g_appConfig.rs485_iface_addr;
    uint16_t data_len = s->read_chunk_size + 2;
    uint16_t total_packet_length = 9 + data_len;

    packet[0] = STX;
    packet[1] = (s->clientAddress >> 8);
    packet[2] = (s->clientAddress & 0xFF);
    packet[3] = (rsifa >> 8);
    packet[4] = (rsifa & 0xFF);
    packet[5] = data_len;
    packet[6] = (s->currentSequenceNum >> 8);
    packet[7] = (s->currentSequenceNum & 0xFF);
    memcpy(&packet[8], s->read_buffer, s->read_chunk_size);

    uint16_t checksum = 0;
    for (uint32_t i = 6; i < (6 + data_len); i++) checksum += packet[i];

    packet[total_packet_length - 3] = (checksum >> 8);
    packet[total_packet_length - 2] = (checksum & 0xFF);
    packet[total_packet_length - 1] = EOT;

    if (m_rs485_service->SendPacket(packet, total_packet_length)) {
        s->state = FUF_S_WAITING_FOR_DATA_ACK;
    } else {
        CleanupSession(true);
    }
}

void FirmwareUpdateManager::SendRestartCommand()
{
    Serial.println(F("[FufManager] Slanje START_BLDR komande..."));
    uint8_t packet[10];
    uint16_t rsifa = g_appConfig.rs485_iface_addr;

    packet[0] = SOH;
    packet[1] = (m_session.clientAddress >> 8);
    packet[2] = (m_session.clientAddress & 0xFF);
    packet[3] = (rsifa >> 8);
    packet[4] = (rsifa & 0xFF);
    packet[5] = 1;
    packet[6] = CMD_START_BLDR;
    uint16_t checksum = CMD_START_BLDR;
    packet[7] = (checksum >> 8);
    packet[8] = (checksum & 0xFF);
    packet[9] = EOT;

    if (!m_rs485_service->SendPacket(packet, 10)) {
        CleanupSession(true);
    }
}

void FirmwareUpdateManager::SendAppExeCommand()
{
    Serial.println(F("[FufManager] Slanje APP_EXE komande..."));
    uint8_t packet[10];
    uint16_t rsifa = g_appConfig.rs485_iface_addr;

    packet[0] = SOH;
    packet[1] = (m_session.clientAddress >> 8);
    packet[2] = (m_session.clientAddress & 0xFF);
    packet[3] = (rsifa >> 8);
    packet[4] = (rsifa & 0xFF);
    packet[5] = 1;
    packet[6] = CMD_APP_EXE;
    uint16_t checksum = CMD_APP_EXE;
    packet[7] = (checksum >> 8);
    packet[8] = (checksum & 0xFF);
    packet[9] = EOT;

    if (!m_rs485_service->SendPacket(packet, 10)) {
        CleanupSession(true);
    }
}

void FirmwareUpdateManager::CleanupSession(bool failed)
{
    // POKRENI SERVER - ALI SAMO AKO NEMA AKTIVNE SEKVENCE!
    if (m_http_server && !m_sequence.is_active) {
        m_http_server->Start();
    }
    
    if (m_session.file_handle) {
        m_session.file_handle.close();
    }

    extern TimeSync g_timeSync;
    g_timeSync.ResetTimer();

    if (m_sequence.is_active) {
        if (failed) {
            Serial.printf("[FufManager] Sesija NEUSPJEŠNA za Adresu %d.\n", m_session.clientAddress);
            StopSequence(); // Prekini celu sekvencu ako jedna adresa ne uspe
        } else {
            Serial.printf("[FufManager] Sesija USPJEŠNA za Adresu %d.\n", m_session.clientAddress);
        }
    } else {
        if (failed) {
            Serial.printf("[FufManager] Sesija (pojedinačna) NEUSPJEŠNA za Adresu %d.\n", m_session.clientAddress);
        } else {
            Serial.printf("[FufManager] Sesija (pojedinačna) USPJEŠNA za Adresu %d.\n", m_session.clientAddress);
        }
    }

    m_session.state = FUF_S_IDLE;
}