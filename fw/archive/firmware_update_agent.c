/**
 ******************************************************************************
 * @file    firmware_update_agent.c
 * @author  eldar6776@hotmail.com
 * @brief   Implementacija "Firmware Update Agent" modula.
 *
 * @note
 * Sadrži kompletnu implementaciju mašine stanja (State Machine) za pouzdan
 * prijem i validaciju firmvera preko RS485 protokola. Koristi interne tajmere
 * za detekciju grešaka i prekida u komunikaciji.
 * Verzija 2.0: Dodata robusna obrada grešaka sa automatskim čišćenjem QSPI
 * memorije i resetovanjem stanja agenta.
 ******************************************************************************
 */

#include "firmware_update_agent.h"
#include "main.h"
#include "common.h"
#include "rs485.h" // Potrebno za slanje ACK/NACK odgovora
#include "stm32746g_qspi.h"
#include "stm32746g_eeprom.h"

//=============================================================================
// Definicije Vremenskih Ograničenja (Timeouts) i Parametara
//=============================================================================

/**
 * @brief Vrijeme (u ms) koje klijent čeka na sljedeći paket od servera
 * prije nego što samostalno prekine proces ažuriranja.
 */
#define T_INACTIVITY_TIMEOUT 5000 // 5 sekundi

/**
 * @brief Adresa u EEPROM-u gdje se upisuje marker za bootloader.
 * @note  Bootloader pri startu provjerava ovu lokaciju.
 */
#define EE_BOOTLOADER_MARKER_ADDR 0x10 // Primjer, odabrati slobodnu adresu

//=============================================================================
// Definicije za Mašinu Stanja (State Machine)
//=============================================================================

/**
 * @brief Enumeracija koja definiše sve moguće sub-komande unutar FIRMWARE_UPDATE poruke.
 */
typedef enum {
    SUB_CMD_START_REQUEST   = 0x01,
    SUB_CMD_START_ACK       = 0x02,
    SUB_CMD_START_NACK      = 0x03,
    SUB_CMD_DATA_PACKET     = 0x10,
    SUB_CMD_DATA_ACK        = 0x11,
    SUB_CMD_FINISH_REQUEST  = 0x20,
    SUB_CMD_FINISH_ACK      = 0x21,
    SUB_CMD_FINISH_NACK     = 0x22,
} FwUpdate_SubCommand_e;


/**
 * @brief Enumeracija koja definiše moguće razloge za odbijanje (NACK) update-a.
 */
typedef enum {
    NACK_REASON_NONE = 0,
    NACK_REASON_FILE_TOO_LARGE,
    NACK_REASON_INVALID_VERSION,
    NACK_REASON_ERASE_FAILED,
    NACK_REASON_WRITE_FAILED,
    NACK_REASON_CRC_MISMATCH,
    NACK_REASON_UNEXPECTED_PACKET,
    NACK_REASON_SIZE_MISMATCH
} FwUpdate_NackReason_e;

/**
 * @brief Enumeracija koja definiše sva moguća stanja u kojima se Agent može naći.
 */
typedef enum
{
    FSM_IDLE,           /**< Agent je neaktivan i čeka komandu za početak. */
    FSM_RECEIVING,      /**< Agent je prihvatio update, obrisao memoriju i prima pakete. */
} FSM_State_e;


/**
 * @brief Struktura koja čuva sve runtime podatke potrebne za rad Agenta.
 */
typedef struct
{
    FSM_State_e     currentState;           /**< Trenutno stanje mašine. */
    FwInfoTypeDef   fwInfo;                 /**< Metapodaci o firmveru koji se prima. */
    uint32_t        expectedSequenceNum;    /**< Redni broj sljedećeg paketa koji očekujemo. */
    uint32_t        currentWriteAddr;       /**< Trenutna adresa za upis u QSPI. */
    uint32_t        bytesReceived;          /**< Ukupan broj primljenih bajtova. */
    uint32_t        inactivityTimerStart;   /**< Vrijeme kada je primljen posljednji paket. */
} FwUpdateAgent_t;

/**
 * @brief Statička, privatna instanca Agenta. Jedina u sistemu.
 */
static FwUpdateAgent_t agent;
/**
 * @brief Statička, privatna varijabla za čuvanje QSPI adrese.
 * @note  Ova varijabla čuva početnu "staging" QSPI adresu za vrijeme
 * trajanja jedne update sesije.
 */
static uint32_t staging_qspi_addr;

//=============================================================================
// Prototipovi Privatnih Funkcija (Handleri za Stanja)
//=============================================================================
static void HandleMessage_Idle(TinyFrame *tf, TF_Msg *msg);
static void HandleMessage_Receiving(TinyFrame *tf, TF_Msg *msg);
static void Agent_HandleFailure(void); // << NOVO

//=============================================================================
// Implementacija Javnih Funkcija (API)
//=============================================================================

/**
 ******************************************************************************
 * @brief       Inicijalizuje Agent ili ga resetuje na početno stanje.
 * @author      eldar6776@hotmail.com 
 * @note        Ova funkcija postavlja mašinu stanja u FSM_IDLE, resetuje sve
 * brojače i tajmere. Poziva se jednom pri startu sistema, ali i
 * nakon svake neuspješne operacije ažuriranja kako bi se
 * agent pripremio za novi, čist početak.
 ******************************************************************************
 */
void FwUpdateAgent_Init(void)
{
    agent.currentState = FSM_IDLE;
    agent.expectedSequenceNum = 0;
    agent.bytesReceived = 0;
    agent.inactivityTimerStart = 0;
    staging_qspi_addr = 0;
    memset(&agent.fwInfo, 0, sizeof(FwInfoTypeDef));
}

/**
 ******************************************************************************
 * @brief       Servisna funkcija koja upravlja tajmerom za neaktivnost.
 * @author      eldar6776@hotmail.com 
 * @note        Poziva se periodično iz `main()`. Ako je agent u stanju primanja
 * paketa (FSM_RECEIVING) i prođe više vremena od definisanog
 * T_INACTIVITY_TIMEOUT, automatski će se pokrenuti procedura
 * za obradu greške (`Agent_HandleFailure`).
 ******************************************************************************
 */
void FwUpdateAgent_Service(void)
{
    if (agent.currentState == FSM_RECEIVING)
    {
        if ((HAL_GetTick() - agent.inactivityTimerStart) > T_INACTIVITY_TIMEOUT)
        {
            // Server predugo nije poslao paket. Prekidamo proces.
            Agent_HandleFailure();
        }
    }
}

/**
 ******************************************************************************
 * @brief       Glavni dispečer poruka koji poziva odgovarajući handler.
 * @author      eldar6776@hotmail.com 
 * @note        Ovu funkciju poziva `FIRMWARE_UPDATE_Listener` iz `rs485.c`.
 * Prvo provjerava da li je poruka namijenjena ovom uređaju
 * (osim za `START_REQUEST` koji je broadcast), a zatim je
 * prosljeđuje funkciji zaduženoj za trenutno stanje mašine.
 * @param       tf    Pokazivač na TinyFrame instancu.
 * @param       msg   Pokazivač na primljenu TF_Msg poruku.
 ******************************************************************************
 */
void FwUpdateAgent_ProcessMessage(TinyFrame *tf, TF_Msg *msg)
{
    uint8_t target_address = msg->data[1];

    // START_REQUEST je jedina poruka koja se obrađuje iako nije direktno
    // adresirana na nas (kako bi se prikazala poruka na ekranu).
    // Sve ostale poruke se ignorišu ako adresa nije naša.
    if (msg->data[0] != SUB_CMD_START_REQUEST && target_address != tfifa)
    {
        return;
    }

    switch (agent.currentState)
    {
    case FSM_IDLE:
        HandleMessage_Idle(tf, msg);
        break;
    case FSM_RECEIVING:
        HandleMessage_Receiving(tf, msg);
        break;
    default:
        break;
    }
}

/**
 ******************************************************************************
 * @brief       Provjerava da li je proces ažuriranja trenutno u toku.
 * @author      eldar6776@hotmail.com 
 * @note        Ovu funkciju koristi `display.c` modul da bi znao da li treba
 * prikazati poruku "Update in progress..." i blokirati GUI.
 * @retval      bool `true` ako je ažuriranje aktivno, inače `false`.
 ******************************************************************************
 */
bool FwUpdateAgent_IsActive(void)
{
    return (agent.currentState != FSM_IDLE);
}


//=============================================================================
// Implementacija Privatnih Funkcija (Logika Mašine Stanja)
//=============================================================================

/**
 ******************************************************************************
 * @brief       Centralizovana funkcija za obradu svih neuspjeha u transferu.
 * @author      eldar6776@hotmail.com 
 * @note        Ovo je ključna funkcija za robusnost. Kada se pozove, ona
 * briše potencijalno neispravne podatke iz QSPI memorije na
 * "staging" adresi, a zatim resetuje kompletan agent u početno
 * stanje pozivom `FwUpdateAgent_Init()`.
 ******************************************************************************
 */
static void Agent_HandleFailure(void)
{
    // Ako imamo validne informacije o firmveru (veličina i adresa), brišemo QSPI.
    if (staging_qspi_addr != 0 && agent.fwInfo.size > 0)
    {
        MX_QSPI_Init();
        // Brišemo tačno onoliko koliko je trebalo biti upisano.
        QSPI_Erase(staging_qspi_addr, staging_qspi_addr + agent.fwInfo.size);
        MX_QSPI_Init();
        QSPI_MemMapMode();
    }

    // Vraćamo agenta na početne postavke, spreman je za novi pokušaj.
    FwUpdateAgent_Init();
}

/**
 ******************************************************************************
 * @brief       Handler za obradu poruka kada je Agent u IDLE stanju.
 * @author      eldar6776@hotmail.com 
 * @note        U ovom stanju, jedina relevantna poruka je `SUB_CMD_START_REQUEST`.
 * Funkcija vrši sve pred-provjere (veličina, verzija), briše
 * potreban segment QSPI memorije i ako je sve u redu, šalje ACK
 * i prelazi u `FSM_RECEIVING` stanje. U slučaju greške, poziva
 * `Agent_HandleFailure()` i šalje NACK.
 * @param       tf    Pokazivač na TinyFrame instancu.
 * @param       msg   Pokazivač na primljenu TF_Msg poruku.
 ******************************************************************************
 */
static void HandleMessage_Idle(TinyFrame *tf, TF_Msg *msg)
{
    if (msg->data[0] != SUB_CMD_START_REQUEST || msg->data[1] != tfifa) return;

    memcpy(&agent.fwInfo, &msg->data[2], sizeof(FwInfoTypeDef));
    memcpy(&staging_qspi_addr, &msg->data[18], sizeof(uint32_t));

    FwInfoTypeDef currentFwInfo;
    currentFwInfo.ld_addr = RT_APPL_ADDR;
    GetFwInfo(&currentFwInfo);

    if ((agent.fwInfo.size > RT_APPL_SIZE) || (agent.fwInfo.size == 0) || (IsNewFwUpdate(&currentFwInfo, &agent.fwInfo) != 0))
    {
        uint8_t nack_response[] = {SUB_CMD_START_NACK, tfifa, NACK_REASON_INVALID_VERSION};
        TF_SendSimple(tf, FIRMWARE_UPDATE, nack_response, sizeof(nack_response));
        // Ne pozivamo Agent_HandleFailure() jer još ništa nismo ni počeli raditi (npr. brisati memoriju)
        return;
    }

    MX_QSPI_Init();
    if (QSPI_Erase(staging_qspi_addr, staging_qspi_addr + agent.fwInfo.size) != QSPI_OK)
    {
        MX_QSPI_Init();
        QSPI_MemMapMode();
        uint8_t nack_response[] = {SUB_CMD_START_NACK, tfifa, NACK_REASON_ERASE_FAILED};
        TF_SendSimple(tf, FIRMWARE_UPDATE, nack_response, sizeof(nack_response));
        Agent_HandleFailure(); // Greška, očisti i resetuj
        return;
    }
    MX_QSPI_Init();
    QSPI_MemMapMode();

    agent.expectedSequenceNum = 0;
    agent.bytesReceived = 0;
    agent.currentWriteAddr = staging_qspi_addr;
    agent.inactivityTimerStart = HAL_GetTick();

    uint8_t ack_response[] = {SUB_CMD_START_ACK, tfifa};
    TF_SendSimple(tf, FIRMWARE_UPDATE, ack_response, sizeof(ack_response));

    agent.currentState = FSM_RECEIVING;
}

/**
 ******************************************************************************
 * @brief       Handler za obradu poruka kada je Agent u RECEIVING stanju.
 * @author      eldar6776@hotmail.com 
 * @note        Ovdje se obrađuju `SUB_CMD_DATA_PACKET` i `SUB_CMD_FINISH_REQUEST`.
 * Funkcija upisuje podatke u QSPI i na kraju vrši finalnu
 * validaciju. Ako je sve uspješno, upisuje marker za bootloader
 * i restartuje uređaj. U slučaju bilo kakve greške, poziva
 * `Agent_HandleFailure()` i šalje odgovarajući NACK.
 * @param       tf    Pokazivač na TinyFrame instancu.
 * @param       msg   Pokazivač na primljenu TF_Msg poruku.
 ******************************************************************************
 */
static void HandleMessage_Receiving(TinyFrame *tf, TF_Msg *msg)
{
    agent.inactivityTimerStart = HAL_GetTick();

    switch (msg->data[0])
    {
    case SUB_CMD_DATA_PACKET:
    {
        uint32_t receivedSeqNum;
        memcpy(&receivedSeqNum, &msg->data[2], sizeof(uint32_t));

        if (receivedSeqNum == agent.expectedSequenceNum) {
            uint8_t* data_payload = (uint8_t*)&msg->data[6];
            uint16_t data_len = msg->len - 6;

            MX_QSPI_Init();
            if (QSPI_Write(data_payload, agent.currentWriteAddr, data_len) == QSPI_OK) {
                agent.bytesReceived += data_len;
                agent.currentWriteAddr += data_len;
                agent.expectedSequenceNum++;

                uint8_t ack_payload[6];
                ack_payload[0] = SUB_CMD_DATA_ACK;
                ack_payload[1] = tfifa;
                memcpy(&ack_payload[2], &receivedSeqNum, sizeof(uint32_t));
                TF_SendSimple(tf, FIRMWARE_UPDATE, ack_payload, sizeof(ack_payload));
            } else {
                // Greška pri upisu u QSPI!
                Agent_HandleFailure();
            }
            MX_QSPI_Init();
            QSPI_MemMapMode();
        } else if (receivedSeqNum < agent.expectedSequenceNum) {
            // Server je ponovo poslao stari paket, samo šaljemo ACK ponovo.
            uint8_t ack_payload[6];
            ack_payload[0] = SUB_CMD_DATA_ACK;
            ack_payload[1] = tfifa;
            memcpy(&ack_payload[2], &receivedSeqNum, sizeof(uint32_t));
            TF_SendSimple(tf, FIRMWARE_UPDATE, ack_payload, sizeof(ack_payload));
        }
        break;
    }

    case SUB_CMD_FINISH_REQUEST:
    {
        // Provjera da li se broj primljenih bajtova poklapa sa očekivanim.
        if (agent.bytesReceived != agent.fwInfo.size) {
            uint8_t nack_response[] = {SUB_CMD_FINISH_NACK, tfifa, NACK_REASON_SIZE_MISMATCH};
            TF_SendSimple(tf, FIRMWARE_UPDATE, nack_response, sizeof(nack_response));
            Agent_HandleFailure();
            break;
        }

        uint32_t primask_state;
        FwInfoTypeDef receivedFwInfo;
        uint8_t validation_result;

        // Započinjemo kritičnu sekciju da osiguramo stabilno okruženje.
        primask_state = __get_PRIMASK();
        __disable_irq();
        SCB_DisableDCache();

        // =======================================================================
        // === KORAK 1: Privremena rekonfiguracija CRC periferije na WORDS mod ===
        // Deinicijalizujemo drajver da bismo osigurali čisto stanje, zatim ga
        // inicijalizujemo sa FORMAT_WORDS, kako bootloader očekuje.
        // =======================================================================
        HAL_CRC_DeInit(&hcrc);
        hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_WORDS;
        if (HAL_CRC_Init(&hcrc) != HAL_OK) {
            // Ako rekonfiguracija ne uspije, izlazimo sigurno.
            validation_result = 0xFF; // Postavljamo na kod greške
        } else {
            // === KORAK 2: Izvršavanje validacije sa ispravnom konfiguracijom ===
            memset(&receivedFwInfo, 0, sizeof(FwInfoTypeDef));
            receivedFwInfo.ld_addr = staging_qspi_addr;
            validation_result = GetFwInfo(&receivedFwInfo);
        }

        // =======================================================================
        // === KORAK 3: Vraćanje CRC periferije na originalni BYTES mod ===
        // Odmah nakon provjere, vraćamo CRC konfiguraciju na onu koju
        // ostatak aplikacije (npr. EEPROM) očekuje.
        // =======================================================================
        HAL_CRC_DeInit(&hcrc);
        hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
        HAL_CRC_Init(&hcrc); // Ovdje ne provjeravamo grešku jer je ovo originalna, ispravna konfiguracija

        // Završavamo kritičnu sekciju.
        SCB_EnableDCache();
        __set_PRIMASK(primask_state);


        if (validation_result == 0) // Vraća 0 u slučaju uspjeha
        {
            // SVE JE U REDU! Fajl na QSPI je validan.
//                EE_WriteBuffer((uint8_t*)&receivedFwInfo, EE_BOOTLOADER_MARKER_ADDR, sizeof(FwInfoTypeDef));
            uint8_t ack_response[] = {SUB_CMD_FINISH_ACK, tfifa};
            TF_SendSimple(tf, FIRMWARE_UPDATE, ack_response, sizeof(ack_response));
            HAL_Delay(100);
            SYSRestart();
        }
        else
        {
            // Greška se desila ili tokom rekonfiguracije ili tokom same CRC provjere.
            uint8_t nack_response[] = {SUB_CMD_FINISH_NACK, tfifa, NACK_REASON_CRC_MISMATCH};
            TF_SendSimple(tf, FIRMWARE_UPDATE, nack_response, sizeof(nack_response));
            Agent_HandleFailure();
        }
        break;
    }
    default:
        break;
    }
}
