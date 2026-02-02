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
extern uint8_t language;
extern uint8_t languageHasChanged;
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
uint8_t  ethst, efan,  etsp,  rec, tfifa, tfgra, tfbra, tfgwa;
/* Private macros   ----------------------------------------------------------*/
#define MAX_RETRIES 3
#define TIMEOUT_MS 100
volatile bool rs485_msg_acknowledged = false;
/* Private Function Prototypes -----------------------------------------------*/

/* Test variables and functions ----------------------------------------------*/
static uint32_t fwd_data_tmr = 0;
static bool fwd_data_req = false;
static bool sos_req = false;

uint8_t responseData[32] = {0}, responseDataLength = 0;

/* Program Code  -------------------------------------------------------------*/
/**
  * @brief  ID Listener function for TinyFrame messages.
  * @param  tf: Pointer to TinyFrame instance.
  * @param  msg: Pointer to TinyFrame message.
  * @retval TF_Result indicating how to handle the message.
  */
TF_Result ID_Listener(TinyFrame *tf, TF_Msg *msg) {
    // Signaliziraj da je odgovor primljen
    rs485_msg_acknowledged = true;
    return TF_CLOSE;
}
/**
 * @brief  Firmware Update Agent Listener wrapper for TinyFrame messages.
 * @param  tf: Pointer to TinyFrame instance.
 * @param  msg: Pointer to TinyFrame message.
 * @retval TF_Result indicating how to handle the message.
 */
TF_Result FwAgent_Listener(TinyFrame *tf, TF_Msg *msg) {
    FwUpdateAgent_ProcessMessage(tf, msg);
    return TF_STAY;
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
 * @brief  Triggers the Forward Data mechanism.
 * @param  delay: Delay in milliseconds before sending.
 */
void RS485_TriggerForwardData(uint32_t delay)
{
    fwd_data_req = true;
    fwd_data_tmr = HAL_GetTick() + delay;
    if (delay == 0) fwd_data_tmr = HAL_GetTick(); // Ensure it's ready immediately
}

/**
 * @brief  Triggers the SOS message sending in RS485 service.
 */
void RS485_TriggerSOS(void)
{
    sos_req = true;
}

/**
 * @brief  Generic Listener function for TinyFrame messages.
 * @param  tf: Pointer to TinyFrame instance.
 * @param  msg: Pointer to TinyFrame message.
 * @retval TF_Result indicating how to handle the message.
 */ 
TF_Result GEN_Listener(TinyFrame *tf, TF_Msg *msg) {
    if (!IsFwUpdateActiv()) {
        if((msg->data[1] == tfifa)
                  &&  ((msg->data[0] == RESTART_CTRL)
                       ||  (msg->data[0] == LOAD_DEFAULT)
                       ||  (msg->data[0] == FORMAT_EXTFLASH)
                       ||  (msg->data[0] == GET_ROOM_TEMP)
                       ||  (msg->data[0] == SET_ROOM_TEMP)
                       ||  (msg->data[0] == SET_THST_HEATING)
                       ||  (msg->data[0] == SET_THST_COOLING)
                       ||  (msg->data[0] == SET_THST_ON)
                       ||  (msg->data[0] == SET_THST_OFF)
                       ||  (msg->data[0] == GET_APPL_STAT)
                       ||  (msg->data[0] == GET_FAN_DIFFERENCE)
                       ||  (msg->data[0] == GET_FAN_BAND)
                       ||  (msg->data[0] == SELECT_RELAY)
                       ||  (msg->data[0] == THERMOSTAT_CHANGE_ALL)
                       ||  (msg->data[0] == SET_LANG)
                       ||  (msg->data[0] == SET_FWD_HEATING)
                       ||  (msg->data[0] == SET_FWD_COOLING)
                       ||  (msg->data[0] == SET_ENABLE_HEATING)
                       ||  (msg->data[0] == SET_ENABLE_COOLING)
                       ||  (msg->data[0] == GET_VERSION)
                       ||  (msg->data[0] == PINS))) {
            cmd = msg->data[0];
            if      (cmd == SET_ROOM_TEMP) {
                thst.sp_temp = msg->data[2];
                ThstSetpointUpdateSet();  // Update only setpoint (no flickering)
                ThstActivityUpdateSet();  // Update activity indicator
                Thermostat_SetpointChanged();  // Notify remote device of setpoint change
                SaveThermostatController(&thst, EE_THST1);
                responseData[responseDataLength++] = SET_ROOM_TEMP;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_THST_HEATING) {
                thst.th_ctrl = 2;
                thst.th_ctrl_old = thst.th_ctrl;  // Sačuvaj trenutni mod
                ThstModeUpdateSet();   // Update mode to HEATING
                ThstStateUpdateSet();  // Update ON/OFF state
                ThstRoomTempUpdateSet(); // Update room temp display position
                ThstSetpointUpdateSet(); // Osvježi setpoint
                ThstActivityUpdateSet(); // Osvježi indikator aktivnosti
                Thermostat_SetpointChanged();
                SaveThermostatController(&thst, EE_THST1);
                responseData[responseDataLength++] = SET_THST_HEATING;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_THST_COOLING) {
                thst.th_ctrl = 1;
                thst.th_ctrl_old = thst.th_ctrl;  // Sačuvaj trenutni mod
                ThstModeUpdateSet();   // Update mode to COOLING
                ThstStateUpdateSet();  // Update ON/OFF state
                ThstRoomTempUpdateSet(); // Update room temp display position
                ThstSetpointUpdateSet(); // Osvježi setpoint
                ThstActivityUpdateSet(); // Osvježi indikator aktivnosti
                Thermostat_SetpointChanged();
                SaveThermostatController(&thst, EE_THST1);
                responseData[responseDataLength++] = SET_THST_COOLING;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_THST_ON) {
                EE_ReadBuffer(&thst.th_ctrl_old, EE_THST_CTRL_OLD, 1);  // Učitaj prethodni mod
                thst.th_ctrl = thst.th_ctrl_old;  // Postavi na prethodni mod
                ThstModeUpdateSet();   // Update mode display
                ThstStateUpdateSet();  // Update ON/OFF state
                ThstRoomTempUpdateSet(); // Update room temp display
                ThstSetpointUpdateSet(); // Osvježi setpoint
                ThstActivityUpdateSet(); // Osvježi indikator aktivnosti
                Thermostat_SetpointChanged();
                SaveThermostatController(&thst, EE_THST1);
                responseData[responseDataLength++] = SET_THST_ON;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_THST_OFF) {
                thst.th_ctrl_old = thst.th_ctrl;  // Sačuvaj trenutni mod
                thst.th_ctrl = 0;
                ThstModeUpdateSet();       // Osvježi mode (sakrij HEATING/COOLING)
                ThstStateUpdateSet();      // Update ON/OFF state
                ThstSetpointUpdateSet();   // Update setpoint (shows "-")
                ThstRoomTempUpdateSet();   // Clear room temp
                ThstActivityUpdateSet();   // Osvježi indikator aktivnosti
                Thermostat_SetpointChanged();
                SaveThermostatController(&thst, EE_THST1);
                responseData[responseDataLength++] = SET_THST_OFF;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == GET_FAN_DIFFERENCE) {
                responseData[responseDataLength++] = GET_FAN_DIFFERENCE;
                responseData[responseDataLength++] = thst.fan_diff;
                msg->data=responseData;
                msg->len=(TF_LEN) responseDataLength;
            }
            else if (cmd == GET_FAN_BAND) {
                responseData[responseDataLength++] = GET_FAN_BAND;
                responseData[responseDataLength++] = thst.fan_loband;
                responseData[responseDataLength++] = thst.fan_hiband;
                msg->data=responseData;
                msg->len=(TF_LEN) responseDataLength;
            }
            else if (cmd == GET_ROOM_TEMP) {
                responseData[responseDataLength++] = GET_ROOM_TEMP;
                responseData[responseDataLength++] = (thst.mv_temp + thst.mv_offset) / 10;
                responseData[responseDataLength++] = thst.sp_temp;
                responseData[responseDataLength++] = thst.fan_speed;
                responseData[responseDataLength++] = thst.th_ctrl;
                responseData[responseDataLength++] = thst.sp_max;
                responseData[responseDataLength++] = thst.sp_min;
                responseData[responseDataLength++] = thst.fan_ctrl;
                responseData[responseDataLength++] = thst.forward_heating;
                responseData[responseDataLength++] = thst.forward_cooling;    
                responseData[responseDataLength++] = thst_enable_heating;
                responseData[responseDataLength++] = thst_enable_cooling;                  
                msg->data=responseData;
                msg->len=(TF_LEN) responseDataLength;
            }
            else if(msg->data[0] == THERMOSTAT_CHANGE_ALL) {
                thstat_temperature_change_reason = msg->data[2];
                thst.th_ctrl = msg->data[3];
                thst.sp_temp = msg->data[4];
                thst.sp_max = msg->data[5];
                thst.sp_min = msg->data[6];
				thst.fan_ctrl = msg->data[7];
                ThstFullUpdateSet();  // Update all thermostat elements
                SaveThermostatController(&thst, EE_THST1);
                if(THSTAT_getTemperatureChangeReason() == THSTAT_TEMP_CHANGE_REASON_GUEST_OUT) {
                    DISP_RemoteSetLight(0);
                    Light2Off();
                }
            }
            else if (cmd == SET_LANG)
            {
                language = msg->data[2];
                languageHasChanged = 1;
                DISP_LanguageUpdate();
            }
            else if (cmd == SET_FWD_HEATING)
            {
                thst.forward_heating = msg->data[2];
                SaveThermostatController(&thst, EE_THST1);
                responseData[responseDataLength++] = SET_FWD_HEATING;
                responseData[responseDataLength++] = thst.forward_heating;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_FWD_COOLING)
            {
                thst.forward_cooling = msg->data[2];
                SaveThermostatController(&thst, EE_THST1);
                responseData[responseDataLength++] = SET_FWD_COOLING;
                responseData[responseDataLength++] = thst.forward_cooling;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_ENABLE_HEATING)
            {
                thst_enable_heating = msg->data[2];
                EE_WriteBuffer(&thst_enable_heating, EE_THST1 + 23, 1);
                responseData[responseDataLength++] = SET_ENABLE_HEATING;
                responseData[responseDataLength++] = thst_enable_heating;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            else if (cmd == SET_ENABLE_COOLING)
            {
                thst_enable_cooling = msg->data[2];
                EE_WriteBuffer(&thst_enable_cooling, EE_THST1 + 22, 1);
                responseData[responseDataLength++] = SET_ENABLE_COOLING;
                responseData[responseDataLength++] = thst_enable_cooling;
                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
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
            // Trigger Forward Data if enabled and command is relevant
            if (cmd == SET_ROOM_TEMP || cmd == SET_THST_HEATING || cmd == SET_THST_COOLING || 
                cmd == SET_THST_ON || cmd == SET_THST_OFF || cmd == THERMOSTAT_CHANGE_ALL)
            {
                uint8_t allow_fwd = 0;
                if (thst.th_ctrl == 2 && thst.forward_heating) allow_fwd = 1;
                else if (thst.th_ctrl == 1 && thst.forward_cooling) allow_fwd = 1;
                else if (thst.th_ctrl == 0) {
                    if (thst.th_ctrl_old == 2 && thst.forward_heating) allow_fwd = 1;
                    else if (thst.th_ctrl_old == 1 && thst.forward_cooling) allow_fwd = 1;
                }

                if (allow_fwd)
                {
                    RS485_TriggerForwardData(2000); // 2 seconds delay
                }
            }
            
            else if(cmd == PINS) {

                responseData[responseDataLength++] = PINS;

                switch(msg->data[2])
                {
                case READ_PINS:
                {
                    responseData[responseDataLength++] = READ_PINS;
                    responseData[responseDataLength] = 0;
                    uint8_t pin = 0;

                    for(unsigned short int i = 1U; i < 8; i++)
                    {
                        switch(i)
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

                    pin = 0; // Pin8IsSet();
                    responseData[responseDataLength] = (responseData[responseDataLength] | pin);
                    responseDataLength++;

                    break;
                }
                case SET_PIN:
                {
                    responseData[responseDataLength++] = SET_PIN;

                    switch(msg->data[3])
                    {
                    case 1:
                        DISP_RemoteSetLight(msg->data[4]);
                        break;

                    case 2:
                        Pin2SetTo((GPIO_PinState)msg->data[4]);
                        break;

                    case 3:
                        if(msg->data[4])FanOff();
                        Pin3SetTo((GPIO_PinState)msg->data[4]);
                        break;

                    case 4:
                        Pin4SetTo((GPIO_PinState)msg->data[4]);
                        break;

                    case 5:
                        if(msg->data[4])FanOff();
                        Pin5SetTo((GPIO_PinState)msg->data[4]);
                        break;

                    case 6:
                        if(msg->data[4])FanOff();
                        Pin6SetTo((GPIO_PinState)msg->data[4]);
                        break;
                    }

                    break;
                }
                }

                msg->data = responseData;
                msg->len = (TF_LEN) responseDataLength;
            }
            HAL_Delay(5);
            TF_Respond(tf, msg);
            responseDataLength = 0;
        }
        else if(msg->data[1] == tfbra)
        {
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
* @brief :  init usart interface to rs485 9 bit receiving
* @param :  and init state to receive packet control block
* @retval:  wait to receive:
*           packet start address marker SOH or STX  2 byte  (1 x 9 bit)
*           packet receiver address 4 bytes msb + lsb       (2 x 9 bit)
*           packet sender address msb + lsb 4 bytes         (2 x 9 bit)
*           packet lenght msb + lsb 4 bytes                 (2 x 9 bit)
*/
void RS485_Init(void) {
    if(!init_tf) {
        init_tf = TF_InitStatic(&tfapp, TF_SLAVE); // 1 = master, 0 = slave
        TF_AddGenericListener(&tfapp, GEN_Listener);
        TF_AddTypeListener(&tfapp, FIRMWARE_UPDATE, FwAgent_Listener);
        FwUpdateAgent_Init();
    }
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
* @brief  : rs485 service function is  called from every
* @param  : main loop cycle to service rs485 communication
* @retval : receive and send on regulary base
*/
void RS485_Service(void) {
    uint8_t i;
    FwUpdateAgent_Service();
    if ((HAL_GetTick() - etmr) >= TF_PARSER_TIMEOUT_TICKS) {
        if (cmd) {
            switch (cmd) {
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
            case SET_ROOM_TEMP:
                break;
            }
            cmd = 0;
        }
        else if (Thermostat_hasSetpointChanged())
        {
            Thermostat_SetpointChangedReset();
			HAL_Delay(5);
            uint8_t buf[7];
            buf[0] = THERMOSTAT_CHANGE_ALL;
            buf[1] = pairedDeviceID;
            buf[2] = thst.th_ctrl;
            buf[3] = thst.sp_temp;
            buf[4] = thst.sp_max;
            buf[5] = thst.sp_min;
			buf[6] = thst.fan_ctrl;
            RS485_SendReliable(S_TEMP, buf, 7);
            etmr = HAL_GetTick();
        }
        else if (fwd_data_req)  // Forward Data Service
        {
            uint8_t allow_fwd = 0;
            if (thst.th_ctrl == 2 && thst.forward_heating) allow_fwd = 1;
            else if (thst.th_ctrl == 1 && thst.forward_cooling) allow_fwd = 1;
            else if (thst.th_ctrl == 0) {
                if (thst.th_ctrl_old == 2 && thst.forward_heating) allow_fwd = 1;
                else if (thst.th_ctrl_old == 1 && thst.forward_cooling) allow_fwd = 1;
            }

            if (!allow_fwd)
            {
                fwd_data_req = false;
            }
            else if (HAL_GetTick() >= fwd_data_tmr)
            {
                fwd_data_req = false;
                uint8_t buf[8];
                buf[0] = thst.th_ctrl;
                buf[1] = thst.th_state;
                buf[2] = (uint8_t)(thst.mv_temp >> 8);
                buf[3] = (uint8_t)(thst.mv_temp & 0xFF);
                buf[4] = thst.mv_offset;
                buf[5] = thst.sp_temp;
                buf[6] = thst.fan_ctrl;
                buf[7] = thst.fan_speed;
                RS485_SendReliable(S_IR, buf, 8);
                etmr = HAL_GetTick();
            }
        }
        else if (sos_req) // SOS Service
        {
            sos_req = false;
            uint8_t buf[1];
            buf[0] = 1; // Dummy payload
            HAL_Delay(5);
            RS485_SendReliable(S_SOS, buf, 1);
            etmr = HAL_GetTick();
        }
        else if (languageHasChanged)
        {
            EE_WriteBuffer(&language, EE_LANG, 1);
            languageHasChanged = 0;
            HAL_Delay(5);
            uint8_t buf[3];
            buf[0] = SET_LANG;
            buf[1] = pairedDeviceID;
            buf[2] = language;
            RS485_SendReliable(S_INFO, buf, 3);
            etmr = HAL_GetTick();
        }
    }
}
/** 
 * @brief  RS485 Tick function to be called periodically.
 * @param  None
 * @retval None     
 */
void RS485_Tick(void) {
    if (init_tf == true) {
        TF_Tick(&tfapp);
    }
}
/**
  * @brief  Implementation of TinyFrame write function using UART.
  * @param  tf: Pointer to TinyFrame instance.
  * @param  buff: Pointer to data buffer to send.
  * @param  len: Length of data to send.
  * @retval None    
  */
void TF_WriteImpl(TinyFrame *tf, const uint8_t *buff, uint32_t len) {
    HAL_UART_Transmit(&huart1,(uint8_t*)buff, len, RESP_TOUT);
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
  * @brief  RS485 Receive Complete Callback function.
  * @param  None
  * @retval None    
  */
void RS485_RxCpltCallback(void) {
    TF_AcceptChar(&tfapp, rec);
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
* @brief : all data send from buffer ?
* @param : what  should one to say   ? well done,
* @retval: well done, and there will be more..
*/
void RS485_TxCpltCallback(void) {
}
/**
* @brief : usart error occured during transfer
* @param : clear error flags and reinit usaart
* @retval: and wait for address mark from master
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
