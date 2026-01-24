/**
 ******************************************************************************
 * File Name          : rs485.c
 * Date               : 28/02/2016 23:16:19
 * Description        : rs485 communication modul
 ******************************************************************************
 *
 *
 ******************************************************************************
 */

#if (__RS485_H__ != FW_BUILD)
#error "rs485 header version mismatch"
#endif
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "room.h"
#include "rs485.h"
#include "logger.h"
#include "display.h"
#include "LuxNET.h"
#include "stm32746g.h"
#include "stm32746g_ts.h"
#include "stm32746g_qspi.h"
#include "stm32746g_sdram.h"
#include "stm32746g_eeprom.h"
/* Imported Types  -----------------------------------------------------------*/
/* Imported Variables --------------------------------------------------------*/
/* Imported Functions    -----------------------------------------------------*/
/* Private Typedef -----------------------------------------------------------*/
TinyFrame tfapp;
eComStateTypeDef eComState = COM_INIT;
/* Private Define  -----------------------------------------------------------*/
/* Private Variables  --------------------------------------------------------*/
uint32_t rstmr;
uint32_t rsflg;
uint8_t rsbps;
uint8_t rsifa[2];
uint8_t deviceIDRecieve;
uint8_t rsgra[2];
uint8_t rsbra[2];
uint8_t sysid[2]     = {0xAB,0xCD};

bool init_tf = false;
uint32_t rstmr = 0;
static uint32_t wradd = 0;
static uint32_t bcnt = 0;
uint8_t sysid[2];
uint32_t rsflg, tfbps, dlen, etmr;
uint8_t cmd = 0;
uint8_t  ethst, efan,  etsp,  rec, tfifa = 125, tfgra, tfbra, tfgwa;
uint8_t GuestPasswordSend = 1;

/* Retry mehanizam */
volatile bool rs485_msg_acknowledged = false;
#define MAX_RETRIES 5
#define TIMEOUT_MS 100

/* Private macros   ----------------------------------------------------------*/
/* Private Function Prototypes -----------------------------------------------*/
/* Program Code  -------------------------------------------------------------*/

uint8_t responseData[25] = {0}, responseDataLength = 0;
uint8_t sendData[10] = {0};
uint8_t accessTypeID_toSend[4] = {0};


void AttachData(TF_Msg* mess)
{
    mess->data = responseData;
    mess->len = (TF_LEN) responseDataLength;
}

void SendPins(TF_Msg* message)
{

    responseData[responseDataLength++] = PINS;

    switch(message->data[2])
    {
    case READ_PINS:
    {
        responseData[responseDataLength++] = READ_PINS;
        responseData[responseDataLength++] = readPins();
        break;
    }
    case SET_PIN:
    {
        responseData[responseDataLength++] = SET_PIN;
        SetPin(message->data[3], message->data[4]);
        break;
    }
    }

    message->data = responseData;
    message->len = (TF_LEN) responseDataLength;
}


/**
 * @brief Opens the door (RS_OpenDoor).
 *
 * Triggers PersonEnter logic to open the door if not already entered.
 *
 * @param message Pointer to the TinyFrame message structure.
 */
void RS_OpenDoor(TF_Msg* message)
{
    if(!PersonHasEntered())
    {
        PersonEnter(GUEST_ENTER_PASSWORD);
        DISPDoorOpenSet();
        BUZZ_State = BUZZ_DOOR_BELL;
    }
    
    responseData[responseDataLength++] = OPEN_DOOR;
    responseData[responseDataLength++] = ACK;
    AttachData(message);
}

void ReadLog(TF_Msg* message)
{
    responseData[responseDataLength++] = HOTEL_READ_LOG;
    responseData[responseDataLength++] = LOG_DSIZE;
    LOGGER_Read(responseData + responseDataLength);
    responseDataLength += LOG_DSIZE;
    responseData[responseDataLength++] = rsifa[0];
    responseData[responseDataLength++] = rsifa[1];
    AttachData(message);
}

/**
 * @brief Deletes log data.
 *
 * Deletes the last log entry and prepares response with HOTEL_DELETE_LOG command.
 *
 * @param message Pointer to the TinyFrame message structure.
 */
void DeleteLog(TF_Msg* message)
{
    responseData[responseDataLength++] = HOTEL_DELETE_LOG;
    responseData[responseDataLength++] = LOGGER_Delete();
    responseData[responseDataLength++] = rsifa[0];
    responseData[responseDataLength++] = rsifa[1];
    AttachData(message);
}

/**
 * @brief Sets or deletes a user password.
 *
 * Handles setting, updating, or deleting passwords for different user groups (Guest, Maid, Manager, Service).
 * Writes changes to EEPROM.
 *
 * @param message Pointer to the TinyFrame message structure containing password data.
 */
void SetPassword(TF_Msg* message)
{
    uint8_t ebuf[32];
    uint8_t respb = ACK;
    uint32_t temp = 0U;
    int lpwd = 0U;
    char* par;
    RTC_TimeTypeDef tm;
    RTC_DateTypeDef dt;

    responseDataLength = 0;

    if (EE_IsDeviceReady(EE_ADDR, DRV_TRIAL) == HAL_OK)
    {
        par = (char*)(message->data + 2); // set pointer to data
        mem_zero (ebuf, sizeof(ebuf)); // clear temp buffer
        EE_ReadBuffer(ebuf, EE_USRGR_ADD, 16U); // load all permited usergroup to local buffer
        do
        {
            if (strchr((char*)ebuf, *par)) // search usergroup in eeprom data
            {
                if (*par == USERGRP_GUEST) // found guest tag 'G' inside received buffer
                {
                    ++par;
                    GuestPasswordSend = 1;
                    temp = TODEC(*par); // copy guest password ID and set buffer to next byte
                    ++par;
                    if (*par == 'X')    // delete this password
                    {
                        mem_zero (&ebuf[16], 8); // clear temp buffer
                        EE_WriteBuffer(&ebuf[16], EE_USER_PSWRD+((temp-1U)*8U), 8);  // write empty buffer to user eeprom address
                        EE_ReadBuffer (&ebuf[24], EE_USER_PSWRD+((temp-1U)*8U), 8);  // read this buffer again to confirm command
                        if (memcmp(&ebuf[16], &ebuf[24], 8) != 0)// password data not erased
                        {
                            respb = NAK;    // error while erasing password
                            GuestPasswordSend = 0;
                            break;          // exit loop to send negativ response
                        }
                    }
                    else if (*par == ',') // skeep comma to next byte
                    {
                        ++par;
                        lpwd = atoi(par); // convert password to decimal
                        ebuf[16] = temp;
                        ebuf[17] = ((lpwd >> 16) & 0xFF); // password    MSB
                        ebuf[18] = ((lpwd >>  8) & 0xFF);
                        ebuf[19] =  (lpwd        & 0xFF); // password    LSB
                        par = strchr(par, ','); // find password expiry time data
                        if (!par)
                        {
                            respb = NAK;    // invalid password expiry time data
                            GuestPasswordSend = 0;
                            break;          // exit loop to send negativ response
                        }
                        else ++par;
                        tm.Seconds = 0;
                        Str2Hex(par,   &tm.Hours,  2);
                        Str2Hex(par+2, &tm.Minutes,2);
                        Str2Hex(par+4, &dt.Date,   2);
                        Str2Hex(par+6, &dt.Month,  2);
                        Str2Hex(par+8, &dt.Year,   2);


                        lpwd = rtc2unix(&tm, &dt); // convert to unix time
                        ebuf[20] = ((lpwd >> 24) & 0xFF);  // password    MSB
                        ebuf[21] = ((lpwd >> 16) & 0xFF);
                        ebuf[22] = ((lpwd >>  8) & 0xFF);
                        ebuf[23] =  (lpwd        & 0xFF); // password    LSB
                        EE_WriteBuffer (&ebuf[16], EE_USER_PSWRD+((temp-1U)*8U), 8U);  // write empty buffer to user eeprom address
                        EE_ReadBuffer  (&ebuf[24], EE_USER_PSWRD+((temp-1U)*8U), 8U);  // read this buffer again to confirm command
                        if (memcmp(&ebuf[16], &ebuf[24], 8) != 0)
                        {
                            respb = NAK;    // fail to write password
                            break;          // exit loop to send negativ response
                        }
                    }
                }
                else // other users password
                {
                    uint8_t ugroup = *par; // zapamti user group oznaku
                    ++par; // preskoči user group oznaku ('H', 'M', 'S')
                    lpwd = atoi(par); // convert password to decimal
                    ebuf[16] = ((lpwd >> 16) & 0xFF); // password    MSB
                    ebuf[17] = ((lpwd >>  8) & 0xFF);
                    ebuf[18] =  (lpwd        & 0xFF); // password    LSB
                    if      (ugroup == USERGRP_HANDMAID) EE_WriteBuffer(&ebuf[16], EE_MAID_PSWRD,   3);
                    else if (ugroup == USERGRP_MANAGER)  EE_WriteBuffer(&ebuf[16], EE_MNGR_PSWRD,   3);
                    else if (ugroup == USERGRP_SERVICE)  EE_WriteBuffer(&ebuf[16], EE_SRVC_PSWRD,   3);
                }
            }
            par = strchr(par, ','); // set pointer to next coma
            if (par) ++par;
        }
        while(par); // search till end of payload
    }
    else respb = NAK;

    // Pripremi i pošalji odgovor
    responseData[responseDataLength++] = SET_PASSWORD;
    responseData[responseDataLength++] = respb;
    message->data = responseData;
    message->len = (TF_LEN) responseDataLength;
}

/**
  * @brief  Restart Controller - Restartuje kontroler nakon primljene komande
  * @param  message: TinyFrame poruka sa komandom
  * @retval None
  */
void RestartController(TF_Msg* message)
{
    uint8_t respb = ACK;

    // Pripremi odgovor prije restarta
    responseDataLength = 0;
    responseData[responseDataLength++] = RESTART_CTRL;
    responseData[responseDataLength++] = respb;

    // Pošalji odgovor
    message->data = responseData;
    message->len = (TF_LEN) responseDataLength;
    TF_Respond(&tfapp, message);

    // Delay prije restarta da stigne odgovor
    HAL_Delay(100);

    // Izvrši sistem reset
    HAL_NVIC_SystemReset();
}

/**
  * @brief  Get Password - Čita password iz EEPROM-a prema grupi korisnika
  * @param  message: TinyFrame poruka, format: [GET_PASSWORD][user_group][id]
  *         user_group: 'G' - guest, 'H' - handmaid, 'M' - manager, 'S' - service
  *         id: 1-8 za guest passworde, ne treba za ostale
  * @retval None
  */
void GetPassword(TF_Msg* message)
{
    uint8_t ebuf[8];
    uint8_t respb = ACK;
    uint8_t user_group = 0;
    uint8_t pwd_id = 0;
    uint16_t ee_addr = 0;
    uint8_t pwd_len = 0;

    responseDataLength = 0;
    mem_zero(ebuf, sizeof(ebuf));

    // Provjeri dužinu poruke
    if (message->len < 3)
    {
        respb = NAK;
        responseData[responseDataLength++] = GET_PASSWORD;
        responseData[responseDataLength++] = respb;
        message->data = responseData;
        message->len = (TF_LEN) responseDataLength;
        return;
    }

    // Dohvati grupu korisnika
    user_group = message->data[2];

    if (EE_IsDeviceReady(EE_ADDR, DRV_TRIAL) == HAL_OK)
    {
        switch(user_group)
        {
        case USERGRP_GUEST:  // 'G'
            // Za guest passworde potreban je ID (1-8)
            if (message->len < 4)
            {
                respb = NAK;
                break;
            }
            pwd_id = message->data[3];
            if (pwd_id < 1 || pwd_id > 8)
            {
                respb = NAK;
                break;
            }
            // Čitaj guest password i expiry time (8 bytes)
            ee_addr = EE_USER_PSWRD + ((pwd_id - 1) * 8);
            pwd_len = 8;
            EE_ReadBuffer(ebuf, ee_addr, pwd_len);
            break;

        case USERGRP_MAID:  // 'H'
            ee_addr = EE_MAID_PSWRD;
            pwd_len = 3;
            EE_ReadBuffer(ebuf, ee_addr, pwd_len);
            break;

        case USERGRP_MANAGER:  // 'M'
            ee_addr = EE_MNGR_PSWRD;
            pwd_len = 3;
            EE_ReadBuffer(ebuf, ee_addr, pwd_len);
            break;

        case USERGRP_SERVICE:  // 'S'
            ee_addr = EE_SRVC_PSWRD;
            pwd_len = 3;
            EE_ReadBuffer(ebuf, ee_addr, pwd_len);
            break;

        default:
            respb = NAK;  // Nepoznata grupa korisnika
            break;
        }
    }
    else
    {
        respb = NAK;  // EEPROM nije spreman
    }

    // Pripremi odgovor
    responseData[responseDataLength++] = GET_PASSWORD;
    responseData[responseDataLength++] = respb;

    if (respb == ACK)
    {
        // Dodaj password data
        for (uint8_t i = 0; i < pwd_len; i++)
        {
            responseData[responseDataLength++] = ebuf[i];
        }
    }

    message->data = responseData;
    message->len = (TF_LEN) responseDataLength;
}

void RS_SetSysID(TF_Msg* message)
{
    sysid[0] = message->data[2];
    sysid[1] = message->data[3];
    EE_WriteBuffer(sysid, EE_SYSID, 2);
    
    responseData[responseDataLength++] = SET_SYSID;
    responseData[responseDataLength++] = ACK;
    AttachData(message);
}

void RS_GetSysID(TF_Msg* message)
{
    responseData[responseDataLength++] = GET_SYSID;
    responseData[responseDataLength++] = sysid[0];
    responseData[responseDataLength++] = sysid[1];
    AttachData(message);
}

/**
  * @brief TinyFrame ID Listener.
  *
  * Currently just returns TF_CLOSE.
  *
  * @param tf Pointer to TinyFrame instance.
  * @param msg Pointer to TinyFrame message.
  * @return TF_Result TF_CLOSE.
  */
TF_Result ID_Listener(TinyFrame *tf, TF_Msg *msg) {
    // Signaliziraj da je odgovor primljen
    rs485_msg_acknowledged = true;
    return TF_CLOSE;
}

/**
  * @brief Pouzdano slanje poruke sa automatskim retry-em.
  *
  * Šalje poruku i čeka potvrdu. Ako odgovor ne stigne u roku od TIMEOUT_MS,
  * automatski pokušava ponovo (maksimalno MAX_RETRIES puta).
  *
  * @param type Tip TinyFrame poruke (S_DOOR, S_CUSTOM, itd.).
  * @param data Pokazivač na podatke za slanje.
  * @param len Dužina podataka.
  */
void RS485_SendReliable(uint8_t type, uint8_t *data, TF_LEN len) {
    for (int i = 0; i < MAX_RETRIES; i++) {
        rs485_msg_acknowledged = false; // Reset pre slanja

        // Šalji upit - ako stigne odgovor, TinyFrame će pozvati ID_Listener
        TF_QuerySimple(&tfapp, type, data, len, ID_Listener, TIMEOUT_MS);

        // Čekaj odgovor sa timeout-om
        uint32_t start = HAL_GetTick();
        while ((HAL_GetTick() - start) < TIMEOUT_MS) {
            // TinyFrame.Tick() se vrti u pozadini (interrupt ili main loop)
            if (rs485_msg_acknowledged) {
                return; // USPEH! Izlazimo odmah
            }
        }
        // Timeout istekao - petlja se vrti ponovo za retry
    }
    // Komunikacija nije uspela nakon MAX_RETRIES pokušaja
}

/**
  * @brief Firmware Request Listener.
  *
  * Handles firmware update data chunks writing to QSPI flash.
  *
  * @param tf Pointer to TinyFrame instance.
  * @param msg Pointer to TinyFrame message.
  * @return TF_Result TF_STAY.
  */
TF_Result FWREQ_Listener(TinyFrame *tf, TF_Msg *msg) {
    if (IsFwUpdateActiv()) {
        MX_QSPI_Init();
        if (QSPI_Write ((uint8_t*)msg->data, wradd, msg->len) == QSPI_OK) {
            wradd += msg->len;
        } else {
            wradd = 0;
            bcnt = 0;
        }
        MX_QSPI_Init();
        QSPI_MemMapMode();
    }
    TF_Respond(tf, msg);
    rstmr = HAL_GetTick();
    return TF_STAY;
}
/**
  * @brief General Listener for TinyFrame messages.
  *
  * Handles various commands like firmware update requests, log reading, password setting,
  * pin control, etc. Dispatches to specific handler functions.
  *
  * @param tf Pointer to TinyFrame instance.
  * @param msg Pointer to TinyFrame message.
  * @return TF_Result TF_STAY.
  */
TF_Result GEN_Listener(TinyFrame *tf, TF_Msg *msg) {

    if (!IsFwUpdateActiv()) {
        if ((msg->data[9] == ST_FIRMWARE_REQUEST)&& (msg->data[8] == deviceIDRecieve)) {
            wradd = ((msg->data[0]<<24)|(msg->data[1]<<16)|(msg->data[2] <<8)|msg->data[3]);
            bcnt  = ((msg->data[4]<<24)|(msg->data[5]<<16)|(msg->data[6] <<8)|msg->data[7]);
            MX_QSPI_Init();
            if (QSPI_Erase(wradd, wradd + bcnt) == QSPI_OK) {
                StartFwUpdate();
                TF_AddTypeListener(&tfapp, ST_FIRMWARE_REQUEST, FWREQ_Listener);
                TF_Respond(tf, msg);
                rstmr = HAL_GetTick();
            } else {
                wradd = 0;
                bcnt = 0;
            }
            MX_QSPI_Init();
            QSPI_MemMapMode();
        } else if((msg->data[1] == deviceIDRecieve)
              &&  ((msg->data[0] == HOTEL_READ_LOG)
               ||  (msg->data[0] == HOTEL_DELETE_LOG)
               ||  (msg->data[0] == SET_PASSWORD)
               ||  (msg->data[0] == PINS)
               ||  (msg->data[0] == OPEN_DOOR)
               ||  (msg->data[0] == RESTART_CTRL)
               ||  (msg->data[0] == GET_PASSWORD)
               ||  (msg->data[0] == SET_SYSID)
               ||  (msg->data[0] == GET_SYSID))) {
            cmd = msg->data[0];
            if(cmd == HOTEL_READ_LOG)
            {
                ReadLog(msg);
            }
            else if (cmd == HOTEL_DELETE_LOG)
            {
                DeleteLog(msg);
            }
            else if(cmd == SET_PASSWORD)
            {
                SetPassword(msg);
            }
            else if(cmd == PINS)
            {
                SendPins(msg);
            }
            else if(cmd == OPEN_DOOR)
            {
                RS_OpenDoor(msg);
            }
            else if(cmd == RESTART_CTRL)
            {
                RestartController(msg);
            }
            else if(cmd == GET_PASSWORD)
            {
                GetPassword(msg);
            }
            else if(cmd == SET_SYSID)
            {
                RS_SetSysID(msg);
            }
            else if(cmd == GET_SYSID)
            {
                RS_GetSysID(msg);
            }
            HAL_Delay(5);
            TF_Respond(tf, msg);
            ZEROFILL(responseData, sizeof(responseData));
            responseDataLength = 0;
        } else if(msg->data[1] == rsbra[1] + 1) {
            if  (msg->data[0] == SET_RTC_DATE_TIME) {
                rtcdt.WeekDay = msg->data[2];
                rtcdt.Date    = msg->data[3];
                rtcdt.Month   = msg->data[4];
                rtcdt.Year    = msg->data[5];
                rtctm.Hours   = msg->data[6];
                rtctm.Minutes = msg->data[7];
                rtctm.Seconds = msg->data[8];
                HAL_RTC_SetTime(&hrtc, &rtctm, RTC_FORMAT_BCD);
                HAL_RTC_SetDate(&hrtc, &rtcdt, RTC_FORMAT_BCD);
                RtcTimeValidSet();
            }
        }
    }

    return TF_STAY;
}
/**
* @brief  Initialize USART interface for RS485 9-bit receiving.
*         Initializes the state to receive packet control block.
* @param  None
* @retval None
*         Waits to receive:
*         - Packet start address marker SOH or STX (2 bytes, 1 x 9 bit)
*         - Packet receiver address (4 bytes msb + lsb, 2 x 9 bit)
*         - Packet sender address (4 bytes msb + lsb, 2 x 9 bit)
*         - Packet length (4 bytes msb + lsb, 2 x 9 bit)
*/
void RS485_Init(void) {
    if(!init_tf) {
        init_tf = TF_InitStatic(&tfapp, TF_SLAVE); // 1 = master, 0 = slave
        TF_AddGenericListener(&tfapp, GEN_Listener);
    }
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
* @brief  RS485 service function called from main loop.
*         Handles RS485 communication service cycles.
* @param  None
* @retval None
*         Performs receive and send operations on a regular basis.
*/
void RS485_Service(void) {
    if (IsFwUpdateActiv()) {
        if(HAL_GetTick() > rstmr + 5000) {
            TF_RemoveTypeListener(&tfapp, ST_FIRMWARE_REQUEST);
            StopFwUpdate();
            wradd = 0;
            bcnt = 0;
        }
    } else if ((HAL_GetTick() - etmr) >= TF_PARSER_TIMEOUT_TICKS) {
        if (cmd) {
            cmd = 0;
        }
        else if(personSendEntranceSignal)
        {
            personSendEntranceSignal = 0;

            if(PersonEnter_GetEntranceType() == GUEST_ENTER_KEYCARD)
            {
                sendData[0] = GUEST_ENTER_KEYCARD_RS485;
            }
            else if(PersonEnter_GetEntranceType() == GUEST_ENTER_PASSWORD)
            {
                sendData[0] = GUEST_ENTER_PASSWORD_RS485;
            }
            else if(PersonEnter_GetEntranceType() == MAID_ENTER_KEYCARD)
            {
                sendData[0] = MAID_ENTER_KEYCARD_RS485;
            }
            else if(PersonEnter_GetEntranceType() == MAID_ENTER_PASSWORD)
            {
                sendData[0] = MAID_ENTER_PASSWORD_RS485;
            }
            else if(PersonEnter_GetEntranceType() == SERVICE_ENTER_KEYCARD)
            {
                sendData[0] = SERVICE_ENTER_KEYCARD_RS485;
            }
            else if(PersonEnter_GetEntranceType() == SERVICE_ENTER_PASSWORD)
            {
                sendData[0] = SERVICE_ENTER_PASSWORD_RS485;
            }
            else if(PersonEnter_GetEntranceType() == MANAGER_ENTER_KEYCARD)
            {
                sendData[0] = MANAGER_ENTER_KEYCARD_RS485;
            }
            else if(PersonEnter_GetEntranceType() == MANAGER_ENTER_PASSWORD)
            {
                sendData[0] = MANAGER_ENTER_PASSWORD_RS485;
            }
            sendData[1] = pairedDeviceID;
            sendData[2] = accessTypeID_toSend[0];
            sendData[3] = accessTypeID_toSend[1];
            sendData[4] = accessTypeID_toSend[2];
            sendData[5] = accessTypeID_toSend[3];
            sendData[6] = rsifa[0];
            sendData[7] = rsifa[1];
            RS485_SendReliable(S_DOOR, sendData, 8);
            ZEROFILL(accessTypeID_toSend, sizeof(accessTypeID_toSend));
        }
        else if(GuestPasswordSend)
        {
            GuestPasswordSend = 0;

            uint8_t pbuf[8] = {0};
            EE_ReadBuffer(pbuf, EE_USER_PSWRD, 8U);
            sendData[0] = GUEST_PASSWORD_SEND;
            sendData[1] = pairedDeviceID;
            sendData[2] = pbuf[1];
            sendData[3] = pbuf[2];
            sendData[4] = pbuf[3];
            sendData[5] = rsifa[0];
            sendData[6] = rsifa[1];
            RS485_SendReliable(S_CUSTOM, sendData, 7);
        }
    }
}
/**
  * @brief  Calls TinyFrame tick function.
  * @param  None
  * @retval None
  */
void RS485_Tick(void) {
    if (init_tf == true) {
        TF_Tick(&tfapp);
    }
}
/**
  * @brief  Implementation of TinyFrame write function.
  *         Transmits data via UART1.
  * @param  tf Pointer to TinyFrame instance.
  * @param  buff Buffer to transmit.
  * @param  len Length of buffer.
  * @retval None
  */
void TF_WriteImpl(TinyFrame *tf, const uint8_t *buff, uint32_t len) {
    HAL_UART_Transmit(&huart1,(uint8_t*)buff, len, RESP_TOUT);
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
  * @brief  RS485 Receive Complete Callback.
  *         Passes received character to TinyFrame and re-enables interrupt reception.
  * @param  None
  * @retval None
  */
void RS485_RxCpltCallback(void) {
    TF_AcceptChar(&tfapp, rec);
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
* @brief  RS485 Transmit Complete Callback.
* @param  None
* @retval None
*/
void RS485_TxCpltCallback(void) {
}
/**
* @brief  RS485 Error Callback.
*         Clears error flags and re-initializes UART reception.
* @param  None
* @retval None
*/
void RS485_ErrorCallback(void) {
    __HAL_UART_CLEAR_PEFLAG(&huart1);
    __HAL_UART_CLEAR_FEFLAG(&huart1);
    __HAL_UART_CLEAR_NEFLAG(&huart1);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_FLUSH_DRREGISTER(&huart1);
    huart1.ErrorCode = HAL_UART_ERROR_NONE;
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/************************ (C) COPYRIGHT JUBERA D.O.O Sarajevo ************************/
