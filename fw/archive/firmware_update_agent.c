/**
 ******************************************************************************
 * @file    firmware_update_agent.c
 * @author  Gemini Agent
 * @brief   Implementacija "Firmware Update Agent" modula prilagođena za korisničke QSPI drajvere.
 ******************************************************************************
 */

#include "firmware_update_agent.h"
#include "main.h"
#include "common.h"
#include "rs485.h" 
#include "stm32746g_qspi.h"

//=============================================================================
// Definicije
//=============================================================================

#define T_INACTIVITY_TIMEOUT 5000 // 5 sekundi

//=============================================================================
// Definicije za Mašinu Stanja (State Machine)
//=============================================================================

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

typedef enum
{
    FSM_IDLE,
    FSM_RECEIVING,
} FSM_State_e;

typedef struct
{
    FSM_State_e     currentState;
    FwInfoTypeDef   fwInfo;
    uint32_t        expectedSequenceNum;
    uint32_t        currentWriteAddr;
    uint32_t        bytesReceived;
    uint32_t        inactivityTimerStart;
} FwUpdateAgent_t;

static FwUpdateAgent_t agent;
static uint32_t staging_qspi_addr;

//=============================================================================
// Prototipovi
//=============================================================================
static void HandleMessage_Idle(TinyFrame *tf, TF_Msg *msg);
static void HandleMessage_Receiving(TinyFrame *tf, TF_Msg *msg);
static void Agent_HandleFailure(void);

//=============================================================================
// Javne Funkcije
//=============================================================================

void FwUpdateAgent_Init(void)
{
    agent.currentState = FSM_IDLE;
    agent.expectedSequenceNum = 0;
    agent.bytesReceived = 0;
    agent.inactivityTimerStart = 0;
    staging_qspi_addr = 0;
    memset(&agent.fwInfo, 0, sizeof(FwInfoTypeDef));
    
    // Reset global update flag if defined in project macros
    StopFwUpdate(); 
}

void FwUpdateAgent_Service(void)
{
    if (agent.currentState == FSM_RECEIVING)
    {
        if ((HAL_GetTick() - agent.inactivityTimerStart) > T_INACTIVITY_TIMEOUT)
        {
            Agent_HandleFailure();
        }
    }
}

void FwUpdateAgent_ProcessMessage(TinyFrame *tf, TF_Msg *msg)
{
    // Check address (assuming msg->data[1] is target address based on protocol analysis)
    // Note: START_REQUEST might be broadcast or specific, checking logic:
    uint8_t target_address = msg->data[1];
    
    // Ako nije za nas i nije broadcast start request (ako se koristi broadcast za start)
    // Ali protokol kaže da je START_REQUEST adresiran.
    if (target_address != deviceIDRecieve)
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

bool FwUpdateAgent_IsActive(void)
{
    return (agent.currentState != FSM_IDLE);
}

//=============================================================================
// Privatne Funkcije
//=============================================================================

static void Agent_HandleFailure(void)
{
    // Pokušaj brisanja neispravnog zapisa ako je moguće
    if (staging_qspi_addr != 0 && agent.fwInfo.size > 0)
    {
        MX_QSPI_Init();
        QSPI_Erase(staging_qspi_addr, staging_qspi_addr + agent.fwInfo.size);
        MX_QSPI_Init();
        QSPI_MemMapMode();
    }

    FwUpdateAgent_Init();
}

static void HandleMessage_Idle(TinyFrame *tf, TF_Msg *msg)
{
    if (msg->data[0] != SUB_CMD_START_REQUEST) return;

    memcpy(&agent.fwInfo, &msg->data[2], sizeof(FwInfoTypeDef));
    memcpy(&staging_qspi_addr, &msg->data[18], sizeof(uint32_t));

    // Razlikujemo firmware fajlove od raw data fajlova po adresi upisa
    bool isFirmwareUpdate = (staging_qspi_addr == RT_APPL_ADDR);
    
    // Osnovna provjera veličine (zajednička za oba tipa)
    if (agent.fwInfo.size == 0)
    {
        uint8_t nack_response[] = {SUB_CMD_START_NACK, deviceIDRecieve, NACK_REASON_INVALID_VERSION};
        TF_Respond(tf, msg); // TF_Respond automatski koristi ID iz poruke
        TF_SendSimple(tf, FIRMWARE_UPDATE, nack_response, sizeof(nack_response));
        return;
    }

    // Specifične provjere za firmware update
    if (isFirmwareUpdate)
    {
        FwInfoTypeDef currentFwInfo;
        currentFwInfo.ld_addr = RT_APPL_ADDR;
        GetFwInfo(&currentFwInfo);
        
        // Provjera verzije i veličine za firmware
        if ((agent.fwInfo.size > RT_APPL_SIZE) || (IsNewFwUpdate(&currentFwInfo, &agent.fwInfo) != 0))
        {
            uint8_t nack_response[] = {SUB_CMD_START_NACK, deviceIDRecieve, NACK_REASON_INVALID_VERSION};
            TF_Respond(tf, msg);
            TF_SendSimple(tf, FIRMWARE_UPDATE, nack_response, sizeof(nack_response));
            return;
        }
    }
    // Za raw data fajlove: samo osnovna provjera veličine je već urađena gore

    MX_QSPI_Init();
    if (QSPI_Erase(staging_qspi_addr, staging_qspi_addr + agent.fwInfo.size) != QSPI_OK)
    {
        MX_QSPI_Init();
        QSPI_MemMapMode();
        uint8_t nack_response[] = {SUB_CMD_START_NACK, deviceIDRecieve, NACK_REASON_ERASE_FAILED};
        TF_SendSimple(tf, FIRMWARE_UPDATE, nack_response, sizeof(nack_response));
        Agent_HandleFailure();
        return;
    }
    MX_QSPI_Init();
    QSPI_MemMapMode();
    
    // Aktiviraj globalni flag
    StartFwUpdate();

    agent.expectedSequenceNum = 0;
    agent.bytesReceived = 0;
    agent.currentWriteAddr = staging_qspi_addr;
    agent.inactivityTimerStart = HAL_GetTick();

    uint8_t ack_response[] = {SUB_CMD_START_ACK, deviceIDRecieve};
    TF_SendSimple(tf, FIRMWARE_UPDATE, ack_response, sizeof(ack_response));

    agent.currentState = FSM_RECEIVING;
}

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
                ack_payload[1] = deviceIDRecieve;
                memcpy(&ack_payload[2], &receivedSeqNum, sizeof(uint32_t));
                TF_SendSimple(tf, FIRMWARE_UPDATE, ack_payload, sizeof(ack_payload));
            } else {
                Agent_HandleFailure();
            }
            MX_QSPI_Init();
            QSPI_MemMapMode();
        } else if (receivedSeqNum < agent.expectedSequenceNum) {
            // Retransmisija ACK-a za stare pakete
            uint8_t ack_payload[6];
            ack_payload[0] = SUB_CMD_DATA_ACK;
            ack_payload[1] = deviceIDRecieve;
            memcpy(&ack_payload[2], &receivedSeqNum, sizeof(uint32_t));
            TF_SendSimple(tf, FIRMWARE_UPDATE, ack_payload, sizeof(ack_payload));
        }
        break;
    }

    case SUB_CMD_FINISH_REQUEST:
    {
        if (agent.bytesReceived != agent.fwInfo.size) {
            uint8_t nack_response[] = {SUB_CMD_FINISH_NACK, deviceIDRecieve, NACK_REASON_SIZE_MISMATCH};
            TF_SendSimple(tf, FIRMWARE_UPDATE, nack_response, sizeof(nack_response));
            Agent_HandleFailure();
            break;
        }

        bool isFirmwareUpdate = (staging_qspi_addr == RT_APPL_ADDR);
        
        // Za firmware fajlove: validacija CRC-a iz firmware headera
        if (isFirmwareUpdate)
        {
            uint32_t primask_state = __get_PRIMASK();
            __disable_irq();
            SCB_DisableDCache();

            HAL_CRC_DeInit(&hcrc);
            hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_WORDS;
            HAL_CRC_Init(&hcrc);

            FwInfoTypeDef receivedFwInfo;
            memset(&receivedFwInfo, 0, sizeof(FwInfoTypeDef));
            receivedFwInfo.ld_addr = staging_qspi_addr;
            
            uint8_t validation_result = GetFwInfo(&receivedFwInfo);

            HAL_CRC_DeInit(&hcrc);
            hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
            HAL_CRC_Init(&hcrc);

            SCB_EnableDCache();
            __set_PRIMASK(primask_state);

            if (validation_result == 0) // Validno
            {
                uint8_t ack_response[] = {SUB_CMD_FINISH_ACK, deviceIDRecieve};
                TF_SendSimple(tf, FIRMWARE_UPDATE, ack_response, sizeof(ack_response));
                HAL_Delay(100);
                HAL_NVIC_SystemReset(); // Reset za primjenu novog firmware-a
            }
            else
            {
                uint8_t nack_response[] = {SUB_CMD_FINISH_NACK, deviceIDRecieve, NACK_REASON_CRC_MISMATCH};
                TF_SendSimple(tf, FIRMWARE_UPDATE, nack_response, sizeof(nack_response));
                Agent_HandleFailure();
            }
        }
        else // Za raw data fajlove: samo prihvatamo transfer bez validacije
        {
            uint8_t ack_response[] = {SUB_CMD_FINISH_ACK, deviceIDRecieve};
            TF_SendSimple(tf, FIRMWARE_UPDATE, ack_response, sizeof(ack_response));
            FwUpdateAgent_Init(); // Resetujemo agenta, ali ne resetujemo sistem
        }
        break;
    }
    default:
        break;
    }
}
