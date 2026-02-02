/**
 ******************************************************************************
 * @file    update_manager.c
 * @author  Gemini & [Vaše Ime]
 * @brief   Asinhroni menadžer za upravljanje višestrukim FW update sesijama.
 *
 * @note
 * Ovaj modul je srce serverske logike za ažuriranje firmvera. Dizajniran je
 * da radi potpuno asinhrono i ne-blokirajuće. Njegova `UpdateManager_Service`
 * funkcija se poziva iz glavne `while(1)` petlje i upravlja stanjima
 * svih aktivnih update sesija, uključujući slanje paketa, praćenje tajmera
 * i ponovno slanje (retransmit).
 ******************************************************************************
 */

#include "update_manager.h" // Ključni include koji je nedostajao
#include "main.h"
#include "ff.h"
#include "common.h"

//=============================================================================
// Definicije
//=============================================================================

/** @brief Maksimalan broj istovremenih update sesija koje menadžer podržava. */
#define MAX_SESSIONS 5
/** @brief Broj ponovnih pokušaja slanja jednog paketa prije odustajanja. */
#define MAX_RETRIES 10
/** @brief Timeout u ms za čekanje na ACK nakon slanja DATA paketa. */
#define T_WAIT_FOR_DATA_ACK 200
/** @brief Timeout u ms za čekanje na START ACK nakon slanja zahtjeva. */
#define T_WAIT_FOR_START_ACK 6000
/** @brief Timeout u ms za čekanje na FINISH ACK nakon slanja finalnog zahtjeva. */
#define T_WAIT_FOR_FINISH_ACK 10000
/** @brief Timeout u ms za čekanje na restart klijenta prije finalne provjere. */
#define T_FINAL_VERIFICATION_DELAY 10000
/** @brief Veličina jednog dijela (chunk) firmvera koji se šalje u jednom paketu. */
#define FW_PACKET_DATA_SIZE 256

/** @brief Niz koji čuva stanje za sve potencijalne sesije. Privatna varijabla modula. */
static UpdateSession_t sessions[MAX_SESSIONS];
static uint8_t active_session_count = 0; 
//=============================================================================
// Prototipovi Privatnih Funkcija
//=============================================================================
static void CleanupSession(UpdateSession_t* s);
static void SendStartRequest(UpdateSession_t* s);
static void SendDataPacket(UpdateSession_t* s);
static void SendFinishRequest(UpdateSession_t* s);
static void SendGetInfoRequest(uint8_t clientAddress);
static const char* NackReasonToString(FwUpdate_NackReason_e reason);

// Pretpostavka je da je ova funkcija dostupna iz drugog modula (npr. display.c)
extern void DISP_UpdateLog(const char *pbuf);

//=============================================================================
// Implementacija Javnih Funkcija
//=============================================================================

/**
 * @brief Inicijalizuje Update Manager.
 * @note  Poziva se jednom pri startu servera. Resetuje sve sesije na IDLE stanje.
 */
void UpdateManager_Init(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        sessions[i].state = S_IDLE;
    }
}

/**
 ******************************************************************************
 * @brief       Pokreće novu sesiju ažuriranja firmvera.
 * @author      Gemini & [Vaše Ime]
 * @note        Ovu funkciju poziva HTTP_CGI_Handler nakon što je već parsirao i
 * validirao osnovne parametre iz HTTP zahtjeva. Funkcija je
 * ne-blokirajuća; ona samo pronalazi slobodan slot, otvara fajl,
 * čita i validira metapodatke ("pečat") i priprema sesiju za
 * početak. Stvarni transfer se odvija u pozadini preko
 * `UpdateManager_Service` funkcije.
 * @param       clientAddress Adresa uređaja (1-254) koji treba ažurirati.
 * @param       filePath      Naziv .BIN fajla na uSD kartici (npr. "IC.BIN").
 * @param       qspiStagingAddress Adresa u QSPI memoriji klijenta gdje će se
 * fajl privremeno upisati (npr. 0x90F00000).
 * @retval      bool          Vraća `true` ako je sesija uspješno inicirana,
 * ili `false` ako je došlo do greške (nema slobodnih
 * slotova, fajl ne postoji, "pečat" u fajlu je neispravan).
 ******************************************************************************
 */
SessionStartResult_e  UpdateManager_StartSession(uint8_t clientAddress, const char* filePath, uint32_t qspiStagingAddress)
{
    // Provjera da li sesija za ovog klijenta već postoji
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].state != S_IDLE && sessions[i].clientAddress == clientAddress) {
            return SESSION_START_ERROR_DUPLICATE;
        }
    }

    // Pronalazak slobodnog slota
    int session_index = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].state == S_IDLE) {
            memset(&sessions[i], 0, sizeof(UpdateSession_t));
            session_index = i;
            break;
        }
    }
    // Ako nema slobodnih slotova, odbij zahtjev. Ovo rješava scenario sa 6. pozivom.
    if (session_index == -1) {
        return SESSION_START_ERROR_NO_SLOTS;
    }

    UpdateSession_t* s = &sessions[session_index];

    // === ISPRAVLJENA LOGIKA ZA FAJL SISTEM ===
    // Ako je ovo prva aktivna sesija (brojač je 0), montiraj drajv.
    if (active_session_count == 0) {
        if (f_mount(&fatfs, "0:", 0) != FR_OK) {
            return SESSION_START_ERROR_FILESYSTEM; // Greška pri montiranju
        }
    }
    
    // Svaka sesija otvara svoj specifični fajl (IC.BIN, ICBL.BIN, LOGO.PNG, itd.).
    if (f_open(&s->fileObject, filePath, FA_READ) != FR_OK) {
        if (active_session_count == 0) f_mount(NULL, "0:", 0); // Demontiraj ako je bila jedina
        return SESSION_START_ERROR_FILESYSTEM;
    }

    // Samo za .BIN fajlove provjeravamo "pečat"
    if (strstr(filePath, ".BIN") != NULL || strstr(filePath, ".bin") != NULL) {
        FRESULT fr;
        UINT bytesRead;
        f_lseek(&s->fileObject, VERS_INF_OFFSET);

        f_read(&s->fileObject, &s->fwInfo.size,    sizeof(uint32_t), &bytesRead);
        f_read(&s->fileObject, &s->fwInfo.crc32,   sizeof(uint32_t), &bytesRead);
        f_read(&s->fileObject, &s->fwInfo.version, sizeof(uint32_t), &bytesRead);
        f_read(&s->fileObject, &s->fwInfo.wr_addr, sizeof(uint32_t), &bytesRead);
        
        f_lseek(&s->fileObject, 0);

        if (ValidateFwInfo(&s->fwInfo) != 0) {
            f_close(&s->fileObject);
            if (active_session_count == 0) f_mount(NULL, "0:", 0);
            return SESSION_START_ERROR_INVALID_FW;  // "Pečat" neispravan
        }
    } else {
        // Za druge fajlove (npr. LOGO.PNG), samo pročitamo veličinu
        s->fwInfo.size = f_size(&s->fileObject);
    }

    s->clientAddress = clientAddress;
    s->qspiStagingAddress = qspiStagingAddress;
    s->bytesSent = 0;
    s->currentSequenceNum = 0;
    s->retryCount = MAX_RETRIES;
    s->state = S_STARTING;
    
    active_session_count++; // Povećaj brojač aktivnih sesija
    
    return SESSION_START_OK;
}

/**
 ******************************************************************************
 * @brief       Glavna servisna funkcija koja upravlja svim aktivnim sesijama.
 * @author      Gemini
 * @note        Ovo je ne-blokirajući drajver mašine stanja. Poziva se iz `main()`.
 * Prolazi kroz sve aktivne sesije i na osnovu njihovog stanja izvršava
 * sljedeći korak (npr. šalje paket, provjerava tajmer, ponovo šalje).
 * Implementira S_PENDING_CLEANUP stanje za sigurno brisanje sesije.
 ******************************************************************************
 */
void UpdateManager_Service(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        UpdateSession_t* s = &sessions[i];
        char logBuffer[128];

        if (s->state == S_IDLE) continue;

        // Provjeravamo da li je sesija završena (OK ili FAILED)
        if (s->state == S_COMPLETED_OK || s->state == S_FAILED) {
            // ODMAH ispisujemo finalnu poruku
            if (s->state == S_COMPLETED_OK) {
                sprintf(logBuffer, "Client %d: Update OK, waiting for restart.", s->clientAddress);
            } else {
                const char* reason_text = NackReasonToString(s->failReason);
                sprintf(logBuffer, "Client %d: FAIL! Reason: %s", s->clientAddress, reason_text);
            }
            DISP_UpdateLog(logBuffer);

            // Zatim, umjesto da odmah zovemo CleanupSession, samo
            // prebacujemo stanje u PENDING_CLEANUP.
            s->state = S_PENDING_CLEANUP;
            
            // Preskačemo ostatak switch-case petlje za ovaj ciklus
            continue; 
        }

        switch (s->state)
        {
            // U sljedećem ciklusu, stanje će biti S_PENDING_CLEANUP.
            // Tek SADA je bezbjedno obrisati sesiju, jer je displej
            // u prethodnom ciklusu imao priliku da sakrije progres bar.
            case S_PENDING_CLEANUP:
                CleanupSession(s);
                break;
            case S_STARTING:
                SendStartRequest(s);
                break;
            case S_SENDING_DATA:
                SendDataPacket(s);
                break;
            case S_FINISHING:
                SendFinishRequest(s);
                break;
            case S_WAITING_FOR_START_ACK:
            case S_WAITING_FOR_DATA_ACK:
            case S_WAITING_FOR_FINISH_ACK:
            {
                uint32_t timeout_duration = 0;
                if(s->state == S_WAITING_FOR_START_ACK)  timeout_duration = T_WAIT_FOR_START_ACK;
                if(s->state == S_WAITING_FOR_DATA_ACK)  timeout_duration = T_WAIT_FOR_DATA_ACK;
                if(s->state == S_WAITING_FOR_FINISH_ACK) timeout_duration = T_WAIT_FOR_FINISH_ACK;

                if ((Get_SysTick() - s->timeoutStart) > timeout_duration) {
                    if (s->retryCount > 0) {
                        s->retryCount--;
                        if (s->state == S_WAITING_FOR_DATA_ACK) {
                            s->state = S_SENDING_DATA;
                        } else if (s->state == S_WAITING_FOR_FINISH_ACK) {
                            s->state = S_FINISHING;
                        } else if (s->state == S_WAITING_FOR_START_ACK) {
                            s->state = S_STARTING;
                        }
                    } else {
                        s->failReason = NACK_REASON_SERVER_TIMEOUT;
                        s->state = S_FAILED;
                    }
                }
                break;
            }
            case S_WAITING_FOR_VERIFICATION:
            {
                if ((Get_SysTick() - s->timeoutStart) > T_FINAL_VERIFICATION_DELAY) {
                    SendGetInfoRequest(s->clientAddress);
                    s->state = S_COMPLETED_OK; 
                }
                break;
            }
            default: break;
        }
    }
}
/**
 * @brief Obrađuje dolazni odgovor (ACK/NACK) od klijenta.
 * @note  Ovu funkciju poziva TinyFrame listener iz `rs485.c` (ili sličnog modula).
 * Ona pronalazi odgovarajuću sesiju na osnovu adrese pošiljaoca iz
 * payload-a i ažurira njeno stanje u mašini stanja.
 */
void UpdateManager_ProcessResponse(TF_Msg *msg)
{
    if (msg->len < 2) return; 

    FwUpdate_SubCommand_e sub_cmd = (FwUpdate_SubCommand_e)msg->data[0];
    uint8_t clientAddress = msg->data[1]; 

    UpdateSession_t* s = NULL;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].state != S_IDLE && sessions[i].clientAddress == clientAddress) {
            s = &sessions[i];
            break;
        }
    }
    if (s == NULL) return;

    switch (sub_cmd)
    {
        case SUB_CMD_START_ACK:
            if (s->state == S_WAITING_FOR_START_ACK) {
                s->state = S_SENDING_DATA;
            }
            break;
        case SUB_CMD_START_NACK:
            if (s->state == S_WAITING_FOR_START_ACK) {
                if(msg->len > 2) s->failReason = (FwUpdate_NackReason_e)msg->data[2];
                s->state = S_FAILED;
            }
            break;
        case SUB_CMD_DATA_ACK:
            if (s->state == S_WAITING_FOR_DATA_ACK) {
                uint32_t ackedSeqNum;
                memcpy(&ackedSeqNum, &msg->data[2], sizeof(uint32_t));
                if (ackedSeqNum == s->currentSequenceNum) {
                    s->bytesSent += s->lastPacketSize; 
                    s->currentSequenceNum++;
                    s->retryCount = MAX_RETRIES; // Resetuj brojač pokušaja za SLJEDEĆI paket.
                    if (s->bytesSent >= s->fwInfo.size) {
                        s->state = S_FINISHING;
                         s->retryCount = MAX_RETRIES; 
                    } else {
                        s->state = S_SENDING_DATA;
                    }
                }
            }
            break;
        case SUB_CMD_FINISH_ACK:
            if (s->state == S_WAITING_FOR_FINISH_ACK) {
//                s->state = S_WAITING_FOR_VERIFICATION;
//                s->timeoutStart = Get_SysTick();
                s->state = S_COMPLETED_OK;
            }
            break;
        case SUB_CMD_FINISH_NACK:
             if (s->state == S_WAITING_FOR_FINISH_ACK) {
                if(msg->len > 2) s->failReason = (FwUpdate_NackReason_e)msg->data[2];
                s->state = S_FAILED;
            }
            break;
        default: 
            break;
    }
}

/**
 * @brief Omogućava 'read-only' pristup podacima o određenoj sesiji.
 */
const UpdateSession_t* UpdateManager_GetSessionInfo(uint8_t session_index)
{
    if (session_index < MAX_SESSIONS) {
        return (const UpdateSession_t*)&sessions[session_index];
    }
    return NULL;
}

//=============================================================================
// Implementacija Privatnih Funkcija
//=============================================================================

/**
 * @brief Pomoćna funkcija za čišćenje i oslobađanje resursa jedne sesije.
 */
static void CleanupSession(UpdateSession_t* s)
{
    f_close(&s->fileObject);
    memset(s, 0, sizeof(UpdateSession_t));
    s->state = S_IDLE;

    // Smanji brojač aktivnih sesija
    if (active_session_count > 0) {
        active_session_count--;
    }
    // Ako je brojač pao na 0, ovo je bila poslednja aktivna sesija -> demontiraj drajv.
    if (active_session_count == 0) {
        f_mount(NULL, "0:", 0);
    }
}

/**
 ******************************************************************************
 * @brief       Sastavlja i šalje SUB_CMD_START_REQUEST poruku.
 * @author      Gemini & [Vaše Ime]
 * @note        Ova funkcija se poziva iz `UpdateManager_Service` kada je sesija
 * u `S_STARTING` stanju. Ona pakuje sve potrebne informacije
 * u jednu poruku: "pečat" (`FwInfoTypeDef`) i privremenu
 * QSPI adresu za upis.
 * @param       s Pokazivač na sesiju za koju se šalje zahtjev.
 ******************************************************************************
 */
static void SendStartRequest(UpdateSession_t* s)
{
    /**
     * @brief Payload za SUB_CMD_START_REQUEST (ukupno 22 bajta):
     * [0]       (1 bajt)  - Sub-komanda (SUB_CMD_START_REQUEST)
     * [1]       (1 bajt)  - Adresa ciljanog klijenta
     * [2 - 17]  (16 bajta)- Originalni, neizmijenjeni FwInfoTypeDef "pečat"
     * [18 - 21] (4 bajta) - QSPI Staging Adresa za upis
     */
    uint8_t payload[2 + sizeof(FwInfoTypeDef) + sizeof(uint32_t)];

    // Pakujemo podatke tačno po definisanom protokolu
    payload[0] = SUB_CMD_START_REQUEST;
    payload[1] = s->clientAddress;
    memcpy(&payload[2], &s->fwInfo, sizeof(FwInfoTypeDef));
    memcpy(&payload[18], &s->qspiStagingAddress, sizeof(uint32_t));

    // Šaljemo sastavljeni paket preko TinyFrame-a
    TF_SendSimple(&tfapp, FIRMWARE_UPDATE, payload, sizeof(payload));

    // Pokrećemo tajmer i prebacujemo sesiju u sljedeće stanje: čekanje na odgovor
    s->timeoutStart = Get_SysTick();
    s->state = S_WAITING_FOR_START_ACK;
}

/**
 * @brief Čita dio fajla, sastavlja i šalje SUB_CMD_DATA_PACKET.
 */
static void SendDataPacket(UpdateSession_t* s)
{
    uint8_t tx_buffer[6 + FW_PACKET_DATA_SIZE]; // SUB(1)+ADR(1)+SEQ(4)+DATA
    UINT bytesToRead, bytesRead;
    
    uint32_t remainingBytes = s->fwInfo.size - s->bytesSent;
    bytesToRead = (remainingBytes > FW_PACKET_DATA_SIZE) ? FW_PACKET_DATA_SIZE : remainingBytes;

    f_lseek(&s->fileObject, s->bytesSent);
    if (f_read(&s->fileObject, &tx_buffer[6], bytesToRead, &bytesRead) != FR_OK || bytesRead != bytesToRead) {
        s->failReason = NACK_REASON_INTERNAL_ERROR;
        s->state = S_FAILED;
        return;
    }

    tx_buffer[0] = SUB_CMD_DATA_PACKET;
    tx_buffer[1] = s->clientAddress;
    memcpy(&tx_buffer[2], &s->currentSequenceNum, sizeof(uint32_t));
    
    // Direktno slanje sastavljenog paketa.
    TF_SendSimple(&tfapp, FIRMWARE_UPDATE, tx_buffer, bytesRead + 6);

    s->lastPacketSize = bytesRead;
    s->timeoutStart = Get_SysTick();
    s->state = S_WAITING_FOR_DATA_ACK;
}

/**
 * @brief Sastavlja i šalje SUB_CMD_FINISH_REQUEST poruku.
 */
static void SendFinishRequest(UpdateSession_t* s)
{
    uint8_t payload[2 + sizeof(uint32_t)];
    payload[0] = SUB_CMD_FINISH_REQUEST;
    payload[1] = s->clientAddress;
    memcpy(&payload[2], &s->fwInfo.crc32, sizeof(uint32_t));

    // Direktno slanje finalne komande.
    TF_SendSimple(&tfapp, FIRMWARE_UPDATE, payload, sizeof(payload));

    s->timeoutStart = Get_SysTick();
    s->state = S_WAITING_FOR_FINISH_ACK;
}

/**
 * @brief Šalje GET_INFO upit za finalnu verifikaciju.
 */
static void SendGetInfoRequest(uint8_t clientAddress)
{
    // TODO: Implementirati slanje GET_APPL_STAT poruke
    // uint8_t payload[] = {GET_APPL_STAT, clientAddress};
    // AddCommand(&neki_red, GET_APPL_STAT, payload, sizeof(payload));
}

/**
 * @brief Pretvara kod greške (NackReason) u tekstualni opis.
 */
static const char* NackReasonToString(FwUpdate_NackReason_e reason)
{
    switch (reason)
    {
        case NACK_REASON_FILE_TOO_LARGE:    return "Error: File too large";
        case NACK_REASON_INVALID_VERSION:   return "Error: Version";
        case NACK_REASON_ERASE_FAILED:      return "Error: Erasing";
        case NACK_REASON_WRITE_FAILED:      return "Error: Write";
        case NACK_REASON_CRC_MISMATCH:      return "Error: CRC";
        case NACK_REASON_UNEXPECTED_PACKET: return "Error: Packet";
        case NACK_REASON_SIZE_MISMATCH:     return "Error: Size";
        case NACK_REASON_SERVER_TIMEOUT:    return "Error: Client Timeout";
        case NACK_REASON_INTERNAL_ERROR:    return "Error: uSD card";
        default:                            return "Error: Unknown";
    }
}
