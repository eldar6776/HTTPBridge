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
#include "dio.h"
#include "room.h"
#include "rs485.h"
#include "logger.h"
#include "display.h"
#include "onewire.h"
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
uint8_t rsgra[2];
uint8_t rsbra[2];
uint8_t sysid[2]     = {0xAB,0xCD};

bool init_tf = false;
static uint8_t ud1, ud2;
uint32_t rstmr = 0;
static uint32_t wradd = 0;
static uint32_t bcnt = 0;
uint8_t sysid[2];
uint32_t rsflg, tfbps, dlen, etmr;
uint8_t lbuf[32], dbuf[32], tbuf[32], lcnt = 0, dcnt = 0, tcnt = 0, cmd = 0;
uint8_t  ethst, efan,  etsp,  rec, tfifa = 125, tfgra, tfbra, tfgwa;
uint8_t GuestPasswordSend = 1;
//uint8_t *lctrl1 =(uint8_t*) &LIGHT_Ctrl1.Main1;
//uint8_t *lctrl2 =(uint8_t*) &LIGHT_Ctrl2.Main1;
/* Private macros   ----------------------------------------------------------*/
/* Private Function Prototypes -----------------------------------------------*/

/* Test variables and functions ----------------------------------------------*/
uint8_t rd = 0;
uint8_t responseData[25] = {0}, responseDataLength = 0;
uint8_t sendData[10] = {0};
uint8_t accessTypeID_toSend[4] = {0};
/* Program Code  -------------------------------------------------------------*/

void AttachData(TF_Msg* mess)
{
    mess->data = responseData;
    mess->len = (TF_LEN) responseDataLength;
}

void Send_LED_Temp(TF_Msg* message)
{
    responseData[responseDataLength++] = RET;
    responseData[responseDataLength++] = readPins();
    message->data = responseData;
    message->len = (TF_LEN) responseDataLength;
}

void SetRoomTemp(TF_Msg* message)
{
//    thst.sp_temp = message->data[2];
//    menu_thst = 0;
}

void SetThstHeating()
{
//    thst.th_ctrl = 2;
}

void SetThstCooling()
{
//    thst.th_ctrl = 1;
}

void SetThstOn()
{
//    thst.th_ctrl = 1;
}

void SetThstOff()
{
//    thst.th_ctrl = 0;
}

void SendFanDiff(TF_Msg* message)
{
    responseData[responseDataLength++] = GET_FAN_DIFFERENCE;
    AttachData(message);
}

void SendFanBand(TF_Msg* message)
{
    responseData[responseDataLength++] = GET_FAN_BAND;
    message->data=responseData;
    message->len=(TF_LEN) responseDataLength;
}

void SendRoomTemp(TinyFrame* tinyframe, TF_Msg* message)
{
    tinyframe->userdata = &ud1;
    tfapp.data[0] = '1';
    tfapp.data[1] = '2';
    tfapp.data[2] = '3';
    
    responseData[responseDataLength++] = GET_ROOM_TEMP;
    message->data=responseData;
    message->len=(TF_LEN) responseDataLength;
}

void SendRelay(TF_Msg* message)
{
    responseData[responseDataLength++] = SELECT_RELAY;
    responseData[responseDataLength++] = message->data[2];
    message->data = responseData;
    message->len = (TF_LEN) responseDataLength;
}

void SendPins(TF_Msg* message)
{
//                    uint8_t pin = 0;

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

void RS_SetPin(TF_Msg* message)
{
    SetPin(message->data[2], message->data[3]);
}

void RS_SetPinv2(TF_Msg* message)
{
    if((message->data[3] == 'C') && (message->data[4] == 8) && message->data[5] && (!PersonHasEntered()))   PersonEnter(GUEST_ENTER_PASSWORD);
    else SetPinv2(message->data[3], message->data[4], message->data[5]);
}

void RS_GetPin(TF_Msg* message)
{
    responseData[responseDataLength++] = HOTEL_GET_PIN;
    responseData[responseDataLength++] = GetPin(message->data[3], message->data[4]);
    
    AttachData(message);
}

void Send_SP_Max(TF_Msg* message)
{
    responseData[responseDataLength++] = GET_SP_MAX;
    AttachData(message);
}

void Set_SP_Max(TF_Msg* message)
{
//    thst.sp_max = message->data[2];
}

void Send_SP_Min(TF_Msg* message)
{
    responseData[responseDataLength++] = GET_SP_MIN;    
    AttachData(message);
}

void Set_SP_Min(TF_Msg* message)
{
//    thst.sp_min = message->data[2];
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

void DeleteLog(TF_Msg* message)
{
    responseData[responseDataLength++] = HOTEL_DELETE_LOG;
    responseData[responseDataLength++] = LOGGER_Delete();
    responseData[responseDataLength++] = rsifa[0];
    responseData[responseDataLength++] = rsifa[1];
    AttachData(message);
}

void SetPassword(TF_Msg* message)
{
    uint8_t ebuf[32];
    uint8_t respb = ACK;
    uint32_t temp = 0U;
    int lpwd = 0U;
    char* par;
    RTC_TimeTypeDef tm;
    RTC_DateTypeDef dt;
    if (EE_IsDeviceReady(EE_ADDR, DRV_TRIAL) == HAL_OK)
    {
        par = (char*)(message->data + 3); // set pointer to data
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
                        
                        /*GUI_GotoXY(20, 76);
                        GUI_DispDec(tm.Hours, 2);
                        GUI_GotoXY(20, 106);
                        GUI_DispDec(tm.Minutes, 2);
                        GUI_GotoXY(20, 136);
                        GUI_DispDec(dt.Date, 2);
                        GUI_GotoXY(20, 166);
                        GUI_DispDec(dt.Month, 2);
                        GUI_GotoXY(20, 196);
                        GUI_DispDec(dt.Year, 2);
                        
                        GUI_GotoXY(300, 166);
                        GUI_DispString(par);*/
                        
                        
                        //par += 10U;
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
                    lpwd = atoi(par); // convert password to decimal
                    ebuf[16] = ((lpwd >> 16) & 0xFF); // password    MSB 
                    ebuf[17] = ((lpwd >>  8) & 0xFF); 
                    ebuf[18] =  (lpwd        & 0xFF); // password    LSB
                    if      (*par == USERGRP_HANDMAID) EE_WriteBuffer(&ebuf[16], EE_MAID_PSWRD,   3); 
                    else if (*par == USERGRP_MANAGER)  EE_WriteBuffer(&ebuf[16], EE_MNGR_PSWRD,   3); 
                    else if (*par == USERGRP_SERVICE)  EE_WriteBuffer(&ebuf[16], EE_SRVC_PSWRD,   3);
                }
            }
            par = strchr(par, ','); // set pointer to next coma
            if (par) ++par;
        }
        while(par); // search till end of payload 
    }
    else respb = NAK;
    
}


/**
  * @brief
  * @param
  * @retval
  */
TF_Result ID_Listener(TinyFrame *tf, TF_Msg *msg){
    return TF_CLOSE;
}
TF_Result FWREQ_Listener(TinyFrame *tf, TF_Msg *msg){          
    if (IsFwUpdateActiv()){
        MX_QSPI_Init();
        if (QSPI_Write ((uint8_t*)msg->data, wradd, msg->len) == QSPI_OK){
            wradd += msg->len; 
        }else{
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


TF_Result GEN_Listener(TinyFrame *tf, TF_Msg *msg){
    
//    rd++;
//    
//    receivedData[0] = msg->data[0];
//    receivedData[1] = msg->data[1];
//    receivedDataLength = 2;
//    
//    if(rd == 1)
//    {
//        GUI_GotoXY(20, 136);
//        GUI_DispDec(msg->data[0], 3);
//        GUI_GotoXY(20, 166);
//        GUI_DispDec(msg->data[1], 3);
//        GUI_GotoXY(20, 196);
//        GUI_DispDec(msg->data[2], 3);
//    }
//    else if(rd == 3)
//    {
//        rd = 0;
//    }
//    else
//    {
//        return TF_CLOSE;
//    }
    
//    GUI_GotoXY(20, 136);
//    GUI_DispDec(receivedData[0], 3);
//    GUI_GotoXY(20, 166);
//    GUI_DispDec(receivedData[1], 3);
    
//    GUI_GotoXY(20, 166);
//    GUI_DispDec(rsbra[1], 3);
    
    if (!IsFwUpdateActiv()){
        if ((msg->data[9] == ST_FIRMWARE_REQUEST)&& (msg->data[8] == ((rsifa[0] << 8) | rsifa[1]))){
            wradd = ((msg->data[0]<<24)|(msg->data[1]<<16)|(msg->data[2] <<8)|msg->data[3]);
            bcnt  = ((msg->data[4]<<24)|(msg->data[5]<<16)|(msg->data[6] <<8)|msg->data[7]);
            MX_QSPI_Init();
            if (QSPI_Erase(wradd, wradd + bcnt) == QSPI_OK){
                StartFwUpdate();
                TF_AddTypeListener(&tfapp, ST_FIRMWARE_REQUEST, FWREQ_Listener);
                TF_Respond(tf, msg);
                rstmr = HAL_GetTick();
            }else{
                wradd = 0;
                bcnt = 0;
            }
            MX_QSPI_Init();
            QSPI_MemMapMode();
        }else if((((msg->data[1] << 8) | msg->data[2])  == ((rsifa[0] << 8) | rsifa[1]))
            &&  ((msg->data[0] == SET_THST_OFF)
            ||  (msg->data[0] == HOTEL_READ_LOG)
            ||  (msg->data[0] == HOTEL_DELETE_LOG)
            ||  (msg->data[0] == THERMOSTAT_CHANGE_ALL)
            ||  (msg->data[0] == SET_PASSWORD)
            ||  (msg->data[0] == PINS)
            ||  (msg->data[0] == SET_PIN)
            ||  (msg->data[0] == HOTEL_SET_PIN_V2)
            ||  (msg->data[0] == HOTEL_GET_PIN)
            /*||  (msg->data[1] == RESTART_CTRL)
            ||  (msg->data[1] == LOAD_DEFAULT)
            ||  (msg->data[1] == FORMAT_EXTFLASH)
            ||  (msg->data[1] == GET_ROOM_TEMP)
            ||  (msg->data[1] == SET_ROOM_TEMP)
            ||  (msg->data[1] == SET_THST_HEATING)
            ||  (msg->data[1] == SET_THST_COOLING)
            ||  (msg->data[1] == SET_THST_ON)
            ||  (msg->data[1] == GET_APPL_STAT)
            ||  (msg->data[1] == GET_FAN_DIFFERENCE)
            ||  (msg->data[1] == GET_FAN_BAND)
            ||  (msg->data[1] == SELECT_RELAY)
            ||  (msg->data[1] == RET)
            ||  (msg->data[1] == RET_TO_PRIMARY)
            ||  (msg->data[1] == GET_SP_MAX)
            ||  (msg->data[1] == SET_SP_MAX)
            ||  (msg->data[1] == GET_SP_MIN)
            ||  (msg->data[1] == SET_SP_MIN)
            ||  (msg->data[1] == 1)*/)){
                cmd = msg->data[0];
                if(cmd == HOTEL_READ_LOG)
                {
                    ReadLog(msg);
                }
                else if (cmd == HOTEL_DELETE_LOG)
                {
                    DeleteLog(msg);
                }
                else if (cmd == THERMOSTAT_CHANGE_ALL)
                {
                    
                }
                else if(cmd == SET_PASSWORD)
                {
                    SetPassword(msg);
                }
                else if(cmd == PINS)
                {
                    SendPins(msg);
                }
                else if(cmd == SET_PIN)
                {
                    RS_SetPin(msg);
                }
                else if(cmd == HOTEL_SET_PIN_V2)
                {
                    RS_SetPinv2(msg);
                }
                else if(cmd == HOTEL_GET_PIN)
                {
                    RS_GetPin(msg);
                }
                else if(cmd == RET)
                {
                    if(msg->data[msg->len - 1] == 1)
                    {
                        responseData[0] = RET_TO_PRIMARY;
                        responseData[1] = 10;
                        responseData[2] = readPins();
                        TF_QuerySimple(&tfapp, S_CUSTOM, responseData, 3, ID_Listener, TF_PARSER_TIMEOUT_TICKS*4);
                    }
                    else
                    {
                        Send_LED_Temp(msg);
                    }
                }
                else if(cmd == RET_TO_PRIMARY)
                {
                    uint8_t pins = msg->data[3], valueOfPins[8] = {0}, response[60];
                    
                    for(unsigned short int i = 1; i < 9; i++)
                    {
                        valueOfPins[9 - i - 1] = pins % 2;                        
                        pins = pins / 2;
                    }
                    
                    sprintf((char*)response, "PIN_VAL%d--%d%d%d%d%d%d%d%d-RT-%d-SP-%d-OFF/COOL/HEAT-%d",msg->data[2], valueOfPins[0], valueOfPins[1], valueOfPins[2], valueOfPins[3], valueOfPins[4], valueOfPins[5], valueOfPins[6], valueOfPins[7], msg->data[4], msg->data[5], msg->data[6]);
                    HAL_UART_Transmit(&huart2, response, strlen((char*)response), 100);
                }
                else if (cmd == SET_ROOM_TEMP){
                    SetRoomTemp(msg);
                }
                else if (cmd == SET_THST_HEATING){
                    SetThstHeating();
                }else if (cmd == SET_THST_COOLING){
                    SetThstCooling();
                }
                else if (cmd == SET_THST_ON){
                    SetThstOn();
                }
                else if (cmd == SET_THST_OFF){
                    SetThstOff();
                }
                else if (cmd == GET_FAN_DIFFERENCE){
                    SendFanDiff(msg);
                }
                else if (cmd == GET_FAN_BAND){
                    SendFanBand(msg);
                }
                else if (cmd == GET_ROOM_TEMP){
                    SendRoomTemp(tf, msg);
                }
                else if(cmd == SELECT_RELAY)
                {
                    SendRelay(msg);
                }
                else if(cmd == GET_SP_MAX)
                {
                    Send_SP_Max(msg);
                }
                else if(cmd == SET_SP_MAX)
                {
                    Set_SP_Max(msg);
                }
                else if(cmd == GET_SP_MIN)
                {
                    Send_SP_Min(msg);
                }
                else if(cmd == SET_SP_MIN)
                {
                    Set_SP_Min(msg);
                }
                
                TF_Respond(tf, msg);
                ZEROFILL(responseData, sizeof(responseData));
                responseDataLength = 0;
                
        }else if(msg->data[1] == rsbra[1] + 1){
            if  (msg->data[0] == SET_RTC_DATE_TIME){
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
void RS485_Init(void){
    if(!init_tf){
        init_tf = TF_InitStatic(&tfapp, TF_SLAVE); // 1 = master, 0 = slave
        TF_AddGenericListener(&tfapp, GEN_Listener);
    }
	HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
* @brief  : rs485 service function is  called from every
* @param  : main loop cycle to service rs485 communication
* @retval : receive and send on regulary base 
*/
void RS485_Service(void){
    uint8_t i; 
    if (IsFwUpdateActiv()){
        if(HAL_GetTick() > rstmr + 5000){
            TF_RemoveTypeListener(&tfapp, ST_FIRMWARE_REQUEST);
            StopFwUpdate();
            wradd = 0;
            bcnt = 0;
        }
    } else if ((HAL_GetTick() - etmr) >= TF_PARSER_TIMEOUT_TICKS){
        if (cmd){
//            switch (cmd){
//                case LOAD_DEFAULT:
//                    i = 1;
//                    EE_WriteBuffer(&i, EE_INIT_ADDR, 1);
//                case RESTART_CTRL:
//                    SYSRestart();
//                    break;
//                case FORMAT_EXTFLASH:
//                    MX_QSPI_Init();
//                    QSPI_Erase(0x90000000, 0x90FFFFFF);
//                    MX_QSPI_Init();
//                    QSPI_MemMapMode();
//                    break;
//                case GET_APPL_STAT:
//                    PresentSystem();
//                    HAL_UART_Receive_IT(&huart1, &rec, 1);
//                    break;
//                case GET_ROOM_TEMP:                   
//                    tbuf[0] = thst.mv_temp >> 8;
//                    tbuf[1] = thst.mv_temp & 0xFF;
//                    tcnt = 2;
//                    break;
//                case SET_ROOM_TEMP:
//                    break;
//            }

            // provjeri zašto ulazi ovdje ako se šalje poruka iz ovog softvera ?
            // cmd bi trebao biti formiran samo na ispravan upit iz primljene poruke
            
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
            TF_QuerySimple(&tfapp, S_DOOR, sendData, 8, ID_Listener, TF_PARSER_TIMEOUT_TICKS);
            etmr = HAL_GetTick();            
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
            TF_QuerySimple(&tfapp, S_CUSTOM, sendData, 7, ID_Listener, TF_PARSER_TIMEOUT_TICKS);
            etmr = HAL_GetTick();
        }
        else if (tcnt) {
            TF_QuerySimple(&tfapp, S_TEMP, tbuf, tcnt, ID_Listener, TF_PARSER_TIMEOUT_TICKS);
            etmr = HAL_GetTick();
            tcnt = 0;
        } else if (lcnt) {
            TF_QuerySimple(&tfapp, S_BINARY, lbuf, lcnt, ID_Listener, TF_PARSER_TIMEOUT_TICKS);
            etmr = HAL_GetTick();
            lcnt = 0;
        } else if (dcnt){
            TF_QuerySimple(&tfapp, S_DIMMER, dbuf, dcnt, ID_Listener, TF_PARSER_TIMEOUT_TICKS);
            etmr = HAL_GetTick();
            dcnt = 0;
        } else {
            lcnt = 0;
            dcnt = 0;
            tcnt = 0;
            ZEROFILL(tbuf,COUNTOF(tbuf));
            ZEROFILL(lbuf,COUNTOF(lbuf));
            ZEROFILL(dbuf,COUNTOF(dbuf));
        }
    }
}
/**
  * @brief
  * @param
  * @retval
  */
void RS485_Tick(void){
    if (init_tf == true) {
        TF_Tick(&tfapp);
    }
}
/**
  * @brief
  * @param
  * @retval
  */
void TF_WriteImpl(TinyFrame *tf, const uint8_t *buff, uint32_t len){
//    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_SET);
    HAL_UART_Transmit(&huart1,(uint8_t*)buff, len, RESP_TOUT);
//    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
  * @brief  
  * @param  
  * @retval 
  */
void RS485_RxCpltCallback(void){
    TF_AcceptChar(&tfapp, rec);
	HAL_UART_Receive_IT(&huart1, &rec, 1);
}
/**
* @brief : all data send from buffer ?
* @param : what  should one to say   ? well done,   
* @retval: well done, and there will be more..
*/
void RS485_TxCpltCallback(void){
}
/**
* @brief : usart error occured during transfer
* @param : clear error flags and reinit usaart
* @retval: and wait for address mark from master 
*/
void RS485_ErrorCallback(void){
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
