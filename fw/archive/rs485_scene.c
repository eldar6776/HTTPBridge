/**
 ******************************************************************************
 * Project          : KuceDzevadova
 ******************************************************************************
 *
 *
 ******************************************************************************
 */

#if (__RS485_H__ != FW_BUILD)
#error "rs485 header version mismatch"
#endif
/* Includes ------------------------------------------------------------------*/
#include "png.h"
#include "main.h"
#include "rs485.h"
#include "display.h"
#include "thermostat.h"
#include "stm32746g.h"
#include "stm32746g_ts.h"
#include "stm32746g_qspi.h"
#include "stm32746g_sdram.h"
#include "stm32746g_eeprom.h"
#include "firmware_update_agent.h"
#include "FirmwareDefs.h"
/* Imported Types  -----------------------------------------------------------*/
/* Imported Variables --------------------------------------------------------*/
/* Imported Functions    -----------------------------------------------------*/
/* Private Typedef -----------------------------------------------------------*/
static TinyFrame tfapp;
/* Private Define  -----------------------------------------------------------*/
/* Private Variables  --------------------------------------------------------*/
bool init_tf = false;
static uint32_t rstmr = 0;
uint16_t sysid;
uint32_t rsflg, tfbps, dlen, etmr;
uint8_t lbuf[32], dbuf[32], tbuf[32], lcnt = 0, dcnt = 0, tcnt = 0, cmd = 0;
uint8_t ethst, efan, etsp, rec, tfifa, tfgra, tfbra, tfgwa;
/* Private macros   ----------------------------------------------------------*/
#define MAX_RETRIES 3
#define TIMEOUT_MS 200
#define THERMO_UPDATE_RATE 2000U // Update rate for remote thermostat data in ms
volatile bool rs485_msg_acknowledged = false;
static uint32_t thermo_update_tmr = 0;
volatile bool lang_save_pending = false;
/* Private Function Prototypes -----------------------------------------------*/
uint8_t responseData[32] = {0}, responseDataLength = 0, sendData[10] = {0}, sendDataLength = 0;

/* Program Code  -------------------------------------------------------------*/
/**
 * @brief
 * @param
 * @retval
 */
TF_Result ID_Listener(TinyFrame *tf, TF_Msg *msg)
{
    rs485_msg_acknowledged = true;
    return TF_CLOSE;
}

/**
 * @brief  Listener for GET_ROOM_TEMP response from paired device
 */
TF_Result RemoteTemp_Listener(TinyFrame *tf, TF_Msg *msg)
{
    if (msg->len >= 4 && msg->data[0] == GET_ROOM_TEMP)
    {
        remote_room_temp = msg->data[1]; // Received as integer degrees
        remote_setpoint = msg->data[2];
        remote_fan_speed = msg->data[3];

        remote_data_valid = 1;
        MVUpdateSet();
        // Trigger display updates
        ThstRoomTempUpdateSet();
        ThstActivityUpdateSet();
        ThstSetpointUpdateSet();
    }
    return TF_CLOSE;
}

void RS485_SendReliable(uint8_t type, uint8_t *data, TF_LEN len)
{
    for (int i = 0; i < MAX_RETRIES; i++)
    {
        rs485_msg_acknowledged = false;

        TF_QuerySimple(&tfapp, type, data, len, ID_Listener, TIMEOUT_MS);

        uint32_t start = HAL_GetTick();
        while ((HAL_GetTick() - start) < TIMEOUT_MS)
        {
            if (rs485_msg_acknowledged)
            {
                return;
            }
        }
    }
}

/**
  * @brief Firmware Request Wrapper Listener.
  */
TF_Result FwAgent_Listener(TinyFrame *tf, TF_Msg *msg) {
    FwUpdateAgent_ProcessMessage(tf, msg);
    return TF_STAY;
}
TF_Result GEN_Listener(TinyFrame *tf, TF_Msg *msg)
{
    if (!IsFwUpdateActiv())
    {
        if ((msg->data[1] == tfifa) && ((msg->data[0] == RESTART_CTRL) 
        || (msg->data[0] == LOAD_DEFAULT) 
        || (msg->data[0] == FORMAT_EXTFLASH) 
        || (msg->data[0] == GET_ROOM_TEMP) 
        || (msg->data[0] == SET_ROOM_TEMP) 
        || (msg->data[0] == SET_THST_HEATING) 
        || (msg->data[0] == SET_THST_COOLING) 
        || (msg->data[0] == SET_THST_ON) 
        || (msg->data[0] == SET_THST_OFF) 
        || (msg->data[0] == GET_APPL_STAT) 
        || (msg->data[0] == GET_FAN_DIFFERENCE) 
        || (msg->data[0] == GET_FAN_BAND) 
        || (msg->data[0] == THERMOSTAT_CHANGE_ALL) 
        || (msg->data[0] == SET_GUEST_IN_TEMP) 
        || (msg->data[0] == SET_GUEST_OUT_TEMP) 
        || (msg->data[0] == GET_GUEST_IN_TEMP) 
        || (msg->data[0] == GET_GUEST_OUT_TEMP) 
        || (msg->data[0] == GUEST_ENTER_KEYCARD_RS485) 
        || (msg->data[0] == GUEST_ENTER_PASSWORD_RS485) 
        || (msg->data[0] == GUEST_PASSWORD_SEND) 
        || (msg->data[0] == GET_ROOM_STATUS) 
        || (msg->data[0] == GET_VERSION) 
        || (msg->data[0] == PINS) 
        || (msg->data[0] == SET_LANG) 
        || (msg->data[0] == QR_CODE_SET) 
        || (msg->data[0] == QR_CODE_GET)))
        {
            cmd = msg->data[0];
            if (cmd == SET_ROOM_TEMP)
            {
                thst.sp_temp = msg->data[2];
                ThstSetpointUpdateSet();
                ThstActivityUpdateSet();
                THSTAT_SetpointChanged();
                SaveThermostatController(&thst, EE_THST1);
                responseData[responseDataLength++] = SET_ROOM_TEMP;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_THST_HEATING)
            {
                thst.th_ctrl = 2;
                thst.th_ctrl_old = thst.th_ctrl;  // Sačuvaj trenutni mod
                ThstModeUpdateSet();
                ThstStateUpdateSet();
                ThstRoomTempUpdateSet();
                ThstSetpointUpdateSet();
                ThstActivityUpdateSet();
                SaveThermostatController(&thst, EE_THST1);
                THSTAT_SetpointChanged();
                responseData[responseDataLength++] = SET_THST_HEATING;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_THST_COOLING)
            {
                thst.th_ctrl = 1;
                thst.th_ctrl_old = thst.th_ctrl;  // Sačuvaj trenutni mod
                ThstModeUpdateSet();
                ThstStateUpdateSet();
                ThstRoomTempUpdateSet();
                ThstSetpointUpdateSet();
                ThstActivityUpdateSet();
                SaveThermostatController(&thst, EE_THST1);
                THSTAT_SetpointChanged();
                responseData[responseDataLength++] = SET_THST_COOLING;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_THST_ON)
            {
                EE_ReadBuffer(&thst.th_ctrl_old, EE_THST_CTRL_OLD, 1);  // Učitaj prethodni mod
                thst.th_ctrl = thst.th_ctrl_old;  // Postavi na prethodni mod
                ThstModeUpdateSet();
                ThstStateUpdateSet();
                ThstRoomTempUpdateSet();
                ThstSetpointUpdateSet();
                ThstActivityUpdateSet();
                SaveThermostatController(&thst, EE_THST1);
                THSTAT_SetpointChanged();
                responseData[responseDataLength++] = SET_THST_ON;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_THST_OFF)
            {
                thst.th_ctrl_old = thst.th_ctrl;  // Sačuvaj trenutni mod
                thst.th_ctrl = 0;
                ThstModeUpdateSet();
                ThstStateUpdateSet();
                ThstSetpointUpdateSet();
                ThstRoomTempUpdateSet();
                ThstActivityUpdateSet();
                SaveThermostatController(&thst, EE_THST1);
                THSTAT_SetpointChanged();
                responseData[responseDataLength++] = SET_THST_OFF;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == GET_FAN_DIFFERENCE)
            {
                responseData[responseDataLength++] = GET_FAN_DIFFERENCE;
                responseData[responseDataLength++] = thst.fan_diff;
                msg->data = responseData;
                msg->len = (TF_LEN)responseDataLength;
            }
            else if (cmd == GET_FAN_BAND)
            {
                responseData[responseDataLength++] = GET_FAN_BAND;
                responseData[responseDataLength++] = thst.fan_loband;
                responseData[responseDataLength++] = thst.fan_hiband;
                msg->data = responseData;
                msg->len = (TF_LEN)responseDataLength;
            }
            else if (cmd == GET_ROOM_TEMP)
            {
                responseData[responseDataLength++] = GET_ROOM_TEMP;
                responseData[responseDataLength++] = (thst.mv_temp + thst.mv_offset) / 10;
                responseData[responseDataLength++] = thst.sp_temp;
                msg->data = responseData;
                msg->len = (TF_LEN)responseDataLength;
            }
            else if (cmd == THERMOSTAT_CHANGE_ALL)
            {
                thst.th_ctrl = msg->data[2];
                thst.sp_temp = msg->data[3];
                thst.sp_max = msg->data[4];
                thst.sp_min = msg->data[5];
                thst.fan_ctrl = msg->data[6];
                ThstFullUpdateSet();
                SaveThermostatController(&thst, EE_THST1);
            }
            else if (cmd == SET_GUEST_IN_TEMP)
            {
                EE_WriteBuffer((uint8_t *)(msg->data + 2), EE_GUEST_IN_TEMP, 1);
                EE_ReadBuffer(&guest_in_temperature, EE_GUEST_IN_TEMP, 1);
            }
            else if (cmd == SET_GUEST_OUT_TEMP)
            {
                EE_WriteBuffer((uint8_t *)(msg->data + 2), EE_GUEST_OUT_TEMP, 1);
                EE_ReadBuffer(&guest_out_temperature, EE_GUEST_OUT_TEMP, 1);
            }
            else if (cmd == GET_GUEST_IN_TEMP)
            {
                responseData[responseDataLength++] = cmd;
                responseData[responseDataLength++] = guest_in_temperature;
                msg->data = responseData;
                msg->len = (TF_LEN)responseDataLength;
            }
            else if (cmd == GET_GUEST_OUT_TEMP)
            {
                responseData[responseDataLength++] = cmd;
                responseData[responseDataLength++] = guest_out_temperature;
                msg->data = responseData;
                msg->len = (TF_LEN)responseDataLength;
            }
            else if (cmd == GUEST_ENTER_KEYCARD_RS485)
            {
                GuestEntered(GUEST_ENTER_KEYCARD);
            }
            else if (cmd == GUEST_ENTER_PASSWORD_RS485)
            {
                GuestEntered(GUEST_ENTER_PASSWORD);
            }
            else if (cmd == GUEST_PASSWORD_SEND)
            {
                EE_WriteBuffer((uint8_t *)(msg->data + 2), EE_ROOM_PIN, 3);
                uint32_t pin_val = (msg->data[2] << 16) | (msg->data[3] << 8) | msg->data[4];
                if (pin_val == 0)
                    guest_pin[0] = 0;
                else
                    sprintf((char *)guest_pin, "%d", pin_val);
            }
            else if (cmd == GET_ROOM_STATUS)
            {
                responseData[responseDataLength++] = GET_ROOM_STATUS;
                responseData[responseDataLength++] = IsCardStackerActiv();
                msg->data = responseData;
                msg->len = (TF_LEN)responseDataLength;
            }
            else if (cmd == QR_CODE_SET)
            {
                if (QR_Code_isDataLengthShortEnough(msg->len - 2)) // data[0]=cmd, data[1]=addr, data[2...]=string
                {
                    // Update RAM immediately
                    QR_Code_Set(msg->data + 2);
                    
                    // Prepare for EEPROM write
                    eebuf_qr[0] = msg->len - 2; // Save length
                    memcpy(&eebuf_qr[1], msg->data + 2, eebuf_qr[0]);
                    qr_save_pending = 1; // Set flag for main loop
                    
                    responseData[0] = QR_CODE_SET;
                    responseData[1] = ACK;
                    responseDataLength = 2;
                }
                else
                {
                    responseData[0] = QR_CODE_SET;
                    responseData[1] = 0xFF; // NAK
                    responseDataLength = 2;
                }
                msg->data = responseData;
                msg->len = responseDataLength;
            }
            else if (cmd == QR_CODE_GET)
            {
                msg->data = QR_Code_Get();
                msg->len = strlen((char*)msg->data);
            }
            else if (cmd == SET_LANG)
            {
                language = msg->data[2];
                lang_save_pending = true;
            }
            else if (cmd == GET_VERSION)
            {
                FwInfoTypeDef fwInfo;
                uint8_t result;
                uint32_t version;
                
                HAL_CRC_DeInit(&hcrc);
                hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_WORDS;
                HAL_CRC_Init(&hcrc);

                mem_zero(responseData, sizeof(responseData));
                responseData[0] = GET_VERSION;
                responseDataLength = 21;
                
                // 1. Bootloader version
                fwInfo.ld_addr = RT_BLDR_ADDR;
                result = GetFwInfo(&fwInfo);
                if (result == 0) version = fwInfo.version; else version = 0;
                responseData[1] = (version >> 24) & 0xFF;
                responseData[2] = (version >> 16) & 0xFF;
                responseData[3] = (version >> 8) & 0xFF;
                responseData[4] = version & 0xFF;
                
                // 2. Application version
                fwInfo.ld_addr = RT_APPL_ADDR;
                result = GetFwInfo(&fwInfo);
                if (result == 0) version = fwInfo.version; else version = 0;
                responseData[5] = (version >> 24) & 0xFF;
                responseData[6] = (version >> 16) & 0xFF;
                responseData[7] = (version >> 8) & 0xFF;
                responseData[8] = version & 0xFF;
                
                // 3. Bootloader Backup version
                fwInfo.ld_addr = RT_BLDR_BKP_ADDR;
                result = GetFwInfo(&fwInfo);
                if (result == 0) version = fwInfo.version; else version = 0;
                responseData[9] = (version >> 24) & 0xFF;
                responseData[10] = (version >> 16) & 0xFF;
                responseData[11] = (version >> 8) & 0xFF;
                responseData[12] = version & 0xFF;
                
                // 4. Application Backup version
                fwInfo.ld_addr = RT_APPL_BKP_ADDR;
                result = GetFwInfo(&fwInfo);
                if (result == 0) version = fwInfo.version; else version = 0;
                responseData[13] = (version >> 24) & 0xFF;
                responseData[14] = (version >> 16) & 0xFF;
                responseData[15] = (version >> 8) & 0xFF;
                responseData[16] = version & 0xFF;
                
                // 5. New File version
                fwInfo.ld_addr = RT_NEW_FILE_ADDR;
                result = GetFwInfo(&fwInfo);
                if (result == 0) version = fwInfo.version; else version = 0;
                responseData[17] = (version >> 24) & 0xFF;
                responseData[18] = (version >> 16) & 0xFF;
                responseData[19] = (version >> 8) & 0xFF;
                responseData[20] = version & 0xFF;
                
                HAL_CRC_DeInit(&hcrc);
                hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
                HAL_CRC_Init(&hcrc);
                
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == PINS)
            {
                responseData[responseDataLength++] = PINS;

                switch (msg->data[2])
                {
                case READ_PINS:
                {
                    responseData[responseDataLength++] = READ_PINS;
                    responseData[responseDataLength] = 0;
                    uint8_t pin = 0;

                    for (unsigned short int i = 1U; i < 8; i++)
                    {
                        switch (i)
                        {
                        case 1:
                            pin = Pin1IsSet();
                            break;

                        case 2:
                            pin = Pin2IsSet();
                            break;

                        case 3:
                            pin = Pin3IsSet();
                            break;

                        case 4:
                            pin = Pin4IsSet();
                            break;

                        case 5:
                            pin = Pin5IsSet();
                            break;

                        case 6:
                            pin = Pin6IsSet();
                            break;

                        default:
                            pin = 0;
                            break;
                        }

                        responseData[responseDataLength] = ((responseData[responseDataLength] | pin) << 1U);
                    }

                    pin = 0;
                    responseData[responseDataLength] = (responseData[responseDataLength] | pin);
                    responseDataLength++;
                    break;
                }
                case SET_PIN:
                {
                    responseData[responseDataLength++] = SET_PIN;

                    switch (msg->data[3])
                    {
                    case 1:
                        DISP_RemoteSetLight(msg->data[4]);
                        break;

                    case 2:
                        Pin2SetTo((GPIO_PinState)msg->data[4]);
                        break;

                    case 3:
                        Pin3SetTo((GPIO_PinState)msg->data[4]);
                        break;

                    case 4:
                        //Pin4SetTo((GPIO_PinState)msg->data[4]);
                        break;

                    case 5:
                        Pin5SetTo((GPIO_PinState)msg->data[4]);
                        break;

                    case 6:
                        Pin6SetTo((GPIO_PinState)msg->data[4]);
                        break;
                    }

                    break;
                }
                }

                msg->data = responseData;
                msg->len = (TF_LEN)responseDataLength;
            }
            HAL_Delay(5);
            TF_Respond(tf, msg);
            responseDataLength = 0;
        }
        else if (msg->data[1] == tfbra)
        {
            if (msg->data[0] == SET_RTC_DATE_TIME)
            {
                rtcdt.WeekDay = msg->data[2];
                rtcdt.Date = msg->data[3];
                rtcdt.Month = msg->data[4];
                rtcdt.Year = msg->data[5];
                rtctm.Hours = msg->data[6];
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
 * @brief :  init usart interface to rs485 9 bit receiving
 * @param :  and init state to receive packet control block
 * @retval:  wait to receive:
 *           packet start address marker SOH or STX  2 byte  (1 x 9 bit)
 *           packet receiver address 4 bytes msb + lsb       (2 x 9 bit)
 *           packet sender address msb + lsb 4 bytes         (2 x 9 bit)
 *           packet lenght msb + lsb 4 bytes                 (2 x 9 bit)
 */
void RS485_Init(void)
{
    if (!init_tf)
    {
        init_tf = TF_InitStatic(&tfapp, TF_SLAVE); // 1 = master, 0 = slave
        TF_AddGenericListener(&tfapp, GEN_Listener);
        FwUpdateAgent_Init();
        TF_AddTypeListener(&tfapp, FIRMWARE_UPDATE, FwAgent_Listener);
    }
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
 * @brief  : rs485 service function is  called from every
 * @param  : main loop cycle to service rs485 communication
 * @retval : receive and send on regulary base
 */
void RS485_Service(void)
{
    FwUpdateAgent_Service();
    
    uint8_t i;
    if (IsFwUpdateActiv())
    {
        // Firmware update active - skip other tasks or specific logic if needed
    }
    else if ((HAL_GetTick() - etmr) >= TF_PARSER_TIMEOUT_TICKS)
    {
        if (cmd)
        {
            switch (cmd)
            {
            case LOAD_DEFAULT:
                i = 1;
                EE_WriteBuffer(&i, EE_INIT_ADDR, 1);
            case RESTART_CTRL:
                SYSRestart();
                break;
            case FORMAT_EXTFLASH:
                MX_QSPI_Init();
                QSPI_Erase(0x90000000, 0x90FFFFFF);
                MX_QSPI_Init();
                QSPI_MemMapMode();
                break;
            }
            cmd = 0;
        }
        else if (THSTAT_hasSetpointChanged())
        {
            THSTAT_SetpointChangedReset();
            HAL_Delay(5);
            uint8_t buf[9] = {0};
            buf[0] = THERMOSTAT_CHANGE_ALL;
            buf[1] = pairedDeviceID;
            buf[2] = THSTAT_getTemperatureChangeReason();
            buf[3] = thst.th_ctrl;
            buf[4] = thst.sp_temp;
            buf[5] = thst.sp_max;
            buf[6] = thst.sp_min;
            buf[7] = thst.fan_ctrl;
            RS485_SendReliable(S_TEMP, buf, 8);
            etmr = HAL_GetTick();
        }
        else if (languageHasChanged)
        {
            languageHasChanged = 0;
            HAL_Delay(5);
            uint8_t buf[3];
            buf[0] = SET_LANG;
            buf[1] = pairedDeviceID;
            buf[2] = language;
            RS485_SendReliable(S_INFO, buf, 3);
            etmr = HAL_GetTick();
        }
        else if (lang_save_pending)
        {
            lang_save_pending = false;
            EE_WriteBuffer(&language, EE_LANG, 1);
            etmr = HAL_GetTick();
        }
        else if (qr_save_pending)
        {
            qr_save_pending = 0;
            EE_WriteBuffer(eebuf_qr, EE_QR_CODE1, eebuf_qr[0] + 1);
            etmr = HAL_GetTick();
        }
        //
        //  PERIODIC REMOTE THERMOSTAT QUERY
        //
        else if (thermostat_active)
        {
            if ((HAL_GetTick() - thermo_update_tmr) >= THERMO_UPDATE_RATE)
            {
                thermo_update_tmr = HAL_GetTick();
                remote_data_valid = 0; // Invalidate until new response
                uint8_t buf[10];
                ZEROFILL(buf, sizeof(buf)); // Zero buffer
                buf[0] = GET_ROOM_TEMP;
                buf[1] = pairedDeviceID;    // Target address is required at index 1
                // Send query and expect response handled by RemoteTemp_Listener
                // We use TF_Query to register the listener for the response
                TF_QuerySimple(&tfapp, GET_ROOM_TEMP, buf, 2, RemoteTemp_Listener, 500);
            }
        }
        ZEROFILL(sendData, sizeof(sendData) / sizeof(uint8_t));
        sendDataLength = 0;
    }
}
/**
 * @brief
 * @param
 * @retval
 */
void RS485_Tick(void)
{
    if (init_tf == true)
    {
        TF_Tick(&tfapp);
    }
}
/**
 * @brief
 * @param
 * @retval
 */
void TF_WriteImpl(TinyFrame *tf, const uint8_t *buff, uint32_t len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)buff, len, RESP_TOUT);
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
 * @brief
 * @param
 * @retval
 */
void RS485_RxCpltCallback(void)
{
    TF_AcceptChar(&tfapp, rec);
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
 * @brief : all data send from buffer ?
 * @param : what  should one to say   ? well done,
 * @retval: well done, and there will be more..
 */
void RS485_TxCpltCallback(void)
{
}
/**
 * @brief : usart error occured during transfer
 * @param : clear error flags and reinit usaart
 * @retval: and wait for address mark from master
 */
void RS485_ErrorCallback(void)
{
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
