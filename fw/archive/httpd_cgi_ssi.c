/**
 ******************************************************************************
 * @file    httpd_cg_ssi.c
 * @author  MCD Application Team
 * @version V1.0.0
 * @date    31-October-2011
 * @brief   Webserver SSI and CGI handlers
 ******************************************************************************
 * @attention
 *
 * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
 * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
 * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
 * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
 * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
 * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 * <h2><center>&copy; Portions COPYRIGHT 2011 STMicroelectronics</center></h2>
 ******************************************************************************
 */

#if (__COMMON_H__ != FW_TIME)
#error "common header version mismatch"
#endif
/* Includes ------------------------------------------------------------------*/
#include "ff.h"
#include "rtc.h"
#include "main.h"
#include "gpio.h"
#include "uart.h"
#include "i2cee.h"
#include "httpd.h"
#include "fs5206.h"
#include "buzzer.h"
#include "eth_bsp.h"
#include "netconf.h"
#include "netbios.h"
#include "display.h"
#include "wiegand.h"
#include "spi_flash.h"
#include "tftpserver.h"
#include "stm32f4x7_eth.h"
#include "stm32f429i_lcd.h"
#include "sdio_sd.h"
/* Imported Types  -----------------------------------------------------------*/
/* Imported Variables --------------------------------------------------------*/
/* Imported Functions    -----------------------------------------------------*/
/* Private Typedef -----------------------------------------------------------*/
/* Private Define  -----------------------------------------------------------*/
#define ADDRESS_ERROR                   190
#define VALUE_TYPE_ERROR                191
#define VALUE_MISSING_ERROR             192
#define VALUE_OUTOFRANGE_ERROR          193
#define RECEIVER_ADDRESS_ERROR          194
#define WRITE_ADDRESS_ERRROR            195
#define FILE_TRANSFER_ERRROR            196
#define RESPONSE_TIMEOUT                197
#define COMMAND_FAIL                    198
#define TIME_INFO                       199
//#define DINSTATE                        199

#define NEW_FILE_EXTFLASH_ADDRESS       0x90F00000              // default 1MByte storage address for new firmware & bootloader file
#define TempRegOn(x)                    (x |=  (0x1U << 0))     // config On: controll loop is executed periodicaly
#define TempRegOff(x)                   (x &=(~(0x1U << 0)))    // config Off:controll loop stopped,
#define IsTempRegActiv(x)               (x &   (0x1U << 0))
#define TempRegHeating(x)               (x |=  (0x1U << 1))     // config Heating: output activ for setpoint value 
#define TempRegCooling(x)               (x &=(~(0x1U << 1)))    // config Cooling: opposite from heating
#define IsTempRegHeating(x)             (x &   (0x1U << 1))
#define TempRegEnable(x)                (x |=  (0x1U << 2))     // controll flag Enable: controll loop set output state
#define TempRegDisable(x)               (x &=(~(0x1U << 2)))    // controll flag Disable:output is forced to inactiv state, controll loop cannot change outpu
#define IsTempRegEnabled(x)             (x &   (0x1U << 2))
#define TempRegOutOn(x)                 (x |=  (0x1U << 3))     // status On: output demand for actuator to inject energy in to system
#define TempRegOutOff(x)                (x &=(~(0x1U << 3)))    // status Off:stop demanding energy for controlled system, setpoint is reached
#define IsTempRegOutActiv(x)            (x &   (0x1U << 3))
#define TempRegNewStaSet(x)             (x |=  (0x1U << 4))
#define TempRegNewModSet(x)             (x |=  (0x1U << 5))
#define TempRegNewCtrSet(x)             (x |=  (0x1U << 6))
#define TempRegNewOutSet(x)             (x |=  (0x1U << 7))
/* Private Variables  --------------------------------------------------------*/
static bool init_tf = false;
TinyFrame tfapp;
static int retval, rdy = 0;

tCGI CGI_TAB[12]        = { 0 };    // Cgi call table, only one CGI used
const char  *TAGCHAR    = {"t"};    // use character "t" as tag for CGI */
const char **TAGS       = &TAGCHAR;
const char  *weblog     = {"/log.html"};
const char  *webctrl    = {"/sysctrl.html"};

uint8_t log_FromListener[6] = {0}, logs_FromListener_counter = 0;

/* Private macros   ----------------------------------------------------------*/
/* Private Function Prototypes -----------------------------------------------*/
void RS485_Tick(void);
void TINYFRAME_Init(void);
uint8_t SendFwInfo(void);
uint8_t HTTP2RS485(void);
void RS485_Receive(uint16_t rxb);
TF_Result ID_Listener(TinyFrame *tf, TF_Msg *msg);
uint8_t HC2RT_Link(uint8_t *txbuf, uint8_t *rxbuf);
const char  *HTTP_CGI_Handler(int iIndex, int iNumParams, char **pcParam, char **pcValue); // CGI handler for incoming http request */
const tCGI   HTTP_CGI   = {"/sysctrl.cgi", HTTP_CGI_Handler};   // Html request for "/sysctrl.cgi" will start HTTP_CGI_Handler

/* Test variables and functions   ---------------------------------------------*/

#define REPLY_TEMPERATURE 172U

uint8_t replyData[25] = {0};

TF_LEN replyDataLength = 0;

/* Program Code  -------------------------------------------------------------*/
u16_t HTTP_ResponseHandler(int iIndex, char *pcInsert, int iInsertLen)
{
    int len;
    int i;
    uint8_t tmp;
    char retbuf[LWIP_HTTPD_MAX_TAG_INSERT_LEN]; // buffer iste veličine kao i max. definisan u lwip httpd.h
    ZEROFILL(retbuf, COUNTOF(retbuf));          // obriši buffer

    if(iIndex == 0)
    {
        if(retval == FS_DRIVE_ERR)
        {
            strcpy(pcInsert, "DRIVE_ERROR");
        }
        else if(retval == FS_DIRECTORY_ERROR)
        {
            strcpy(pcInsert, "DIRECTORY_ERROR");
        }
        else if(retval == FS_FILE_ERROR)
        {
            strcpy(pcInsert, "FILE_ERROR");
        }
        else if(retval == TIME_INFO)
        {
            RTC_GetDate(RTC_Format_BCD, &rtc_date);
            RTC_GetTime(RTC_Format_BCD, &rtc_time);
            sprintf(pcInsert, "TIME=%02d:%02d", BCD2DEC(rtc_time.RTC_Hours), BCD2DEC(rtc_time.RTC_Minutes));
            len = strlen(pcInsert);
            sprintf(&pcInsert[len], ";DATE=%02d.%02d.20%02d", BCD2DEC(rtc_date.RTC_Date), BCD2DEC(rtc_date.RTC_Month),BCD2DEC(rtc_date.RTC_Year));
            len = strlen(pcInsert);
            sprintf(&pcInsert[len], ";TIMER=%02d:%02d/%02d:%02d", BCD2DEC(timer[0]),BCD2DEC(timer[1]),BCD2DEC(timer[2]),BCD2DEC(timer[3]));

        }
        else if(retval == ADDRESS_ERROR)
        {
            strcpy(pcInsert, "ADDRESS_ERROR");
        }
        else if(retval == VALUE_TYPE_ERROR)
        {
            strcpy(pcInsert, "VALUE_TYPE_ERROR");
        }
        else if(retval == VALUE_MISSING_ERROR)
        {
            strcpy(pcInsert, "VALUE_MISSING_ERROR");
        }
        else if(retval == VALUE_OUTOFRANGE_ERROR)
        {
            strcpy(pcInsert, "VALUE_OUTOFRANGE_ERROR");
        }
        else if(retval == FILE_TRANSFER_ERRROR)
        {
            strcpy(pcInsert, "FILE_TRANSFER_ERRROR");
        }
        else if(retval == RESPONSE_TIMEOUT)
        {
            strcpy(pcInsert, "RESPONSE_TIMEOUT");
        }
        else if(retval == COMMAND_FAIL)
        {
            strcpy(pcInsert, "COMMAND_FAIL");
        }
        else if((retval == DOUT_SET) || (retval == DOUT_RESET) || (retval == GET_DOUT_STATE))
        {
            if(GPIO_ReadOutputDataBit(DOUT0_GPIO_PORT, DOUT0_GPIO_PIN) == Bit_SET)   strcpy(pcInsert,   "1");
            else strcpy(pcInsert,   "0");
            if(GPIO_ReadOutputDataBit(DOUT1_GPIO_PORT, DOUT1_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 1, "1");
            else strcpy(pcInsert + 1, "0");
            if(GPIO_ReadOutputDataBit(DOUT2_GPIO_PORT, DOUT2_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 2, "1");
            else strcpy(pcInsert + 2, "0");
            if(GPIO_ReadOutputDataBit(DOUT3_GPIO_PORT, DOUT3_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 3, "1");
            else strcpy(pcInsert + 3, "0");
            if(GPIO_ReadOutputDataBit(DOUT4_GPIO_PORT, DOUT4_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 4, "1");
            else strcpy(pcInsert + 4, "0");
            if(GPIO_ReadOutputDataBit(DOUT5_GPIO_PORT, DOUT5_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 5, "0");
            else strcpy(pcInsert + 5, "1");
            if(GPIO_ReadOutputDataBit(DOUT6_GPIO_PORT, DOUT6_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 6, "0");
            else strcpy(pcInsert + 6, "1");
            if(GPIO_ReadOutputDataBit(DOUT7_GPIO_PORT, DOUT7_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 7, "0");
            else strcpy(pcInsert + 7, "1");
            if(GPIO_ReadOutputDataBit(DOUT8_GPIO_PORT, DOUT8_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 8, "0");
            else strcpy(pcInsert + 8, "1");
            if(GPIO_ReadOutputDataBit(DOUT9_GPIO_PORT, DOUT9_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 9, "0");
            else strcpy(pcInsert + 9, "1");
            if(GPIO_ReadOutputDataBit(DOUT10_GPIO_PORT, DOUT10_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 10, "0");
            else strcpy(pcInsert + 10, "0");
            if(GPIO_ReadOutputDataBit(DOUT11_GPIO_PORT, DOUT11_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 11, "0");
            else strcpy(pcInsert + 11, "1");
            if(GPIO_ReadOutputDataBit(DOUT12_GPIO_PORT, DOUT12_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 12, "0");
            else strcpy(pcInsert + 12, "1");
            if(GPIO_ReadOutputDataBit(DOUT13_GPIO_PORT, DOUT13_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 13, "0");
            else strcpy(pcInsert + 13, "1");
            if(GPIO_ReadOutputDataBit(DOUT14_GPIO_PORT, DOUT14_GPIO_PIN) == Bit_SET)   strcpy(pcInsert + 14, "0");
            else strcpy(pcInsert + 14, "1");
            strcpy(pcInsert + 15, "_");
        }
        else if(retval == GET_RELAY_STATE)
        {
            for(len = 0; len < replyDataLength; len++)
            {
                tmp = ~replyData[len];
                CharToBinStr(retbuf + (len * 8), tmp);
            }
            strcpy(pcInsert, retbuf);
        }
        else if(retval == FS_FILE_OK)
        {

//            strcpy (pcInsert, "OKAY");
//            sprintf(pcInsert, "%s%d%s%d", "RT", replyData[0], "--SP", replyData[1]);

            switch(replyData[0])
            {
            case GET_ROOM_TEMP:
            {
                if(((replyData[1] < 10) && (replyData[2] < 10)) || ((replyData[1] > 9) && (replyData[1] > 9))) sprintf(pcInsert, "RT%d--SP%d", replyData[1], replyData[2]);
                else sprintf(pcInsert, "RT%d--SP%d", replyData[1], replyData[2]);

                break;

            }

            case GET_FAN_DIFFERENCE:
            {
                if(replyData[1] < 10)
                    sprintf(pcInsert, "FAN_DIFFERENCE-%d", replyData[1]);
                else
                    sprintf(pcInsert, "FAN_DIFFERENCE--%d", replyData[1]);

                break;
            }

            case GET_FAN_BAND:
            {
                if(((replyData[1] < 10) && (replyData[2] < 10)) || (((replyData[1] > 9) && (replyData[1] < 100)) && ((replyData[2] > 9) && (replyData[2] < 100))))
                    sprintf(pcInsert, "LOW_BAND-%d_HIGH_BAND-%d", replyData[1], replyData[2]);
                else
                    sprintf(pcInsert, "LOW_BAND-%d__HIGH_BAND-%d", replyData[1], replyData[2]);

                break;
            }

            case SELECT_RELAY:
            {
                if(replyDataLength > 3)
                {
                    sprintf(pcInsert, "RELAY%d-%d-%d-%d", replyData[1], replyData[2], replyData[3], replyData[4]);
                }
                else
                {
                    sprintf(pcInsert, "RELAY%d-%d", replyData[1], replyData[2]);
                }

                break;
            }

            case PINS:
            {
                switch(replyData[1])
                {
                case READ_PINS:
                {
//                            sprintf(pcInsert, "PINS--READ");

                    uint8_t valueOfPins[8] = {0};

                    for(i = 1; i < 9; i++)
                    {
                        valueOfPins[9 - i - 1] = replyData[2] % 2;

                        replyData[2] = replyData[2] / 2;
                    }

                    sprintf(pcInsert, "PIN_VALUES--%d%d%d%d%d%d%d%d", valueOfPins[0], valueOfPins[1], valueOfPins[2], valueOfPins[3], valueOfPins[4], valueOfPins[5], valueOfPins[6], valueOfPins[7]);

                    break;
                }
                case SET_PIN:
                {
                    sprintf(pcInsert, "PINS_SET_SUCCESSFULLY!");

                    break;
                }
                }
                break;
            }
            case RET:
            {
                uint8_t valueOfPins[8] = {0};

                for(i = 1; i < 9; i++)
                {
                    valueOfPins[9 - i - 1] = replyData[1] % 2;

                    replyData[1] = replyData[1] / 2;
                }

                sprintf(pcInsert, "PIN_VALUES--%d%d%d%d%d%d%d%d-RT-%d-SP-%d-OFF/COOL/HEAT-%d", valueOfPins[0], valueOfPins[1], valueOfPins[2], valueOfPins[3], valueOfPins[4], valueOfPins[5], valueOfPins[6], valueOfPins[7], replyData[2], replyData[3], replyData[4]);

                break;
            }
            case GET_SP_MAX:
            {
                if(replyData[1] < 10) sprintf(pcInsert, "%d_", replyData[1]);
                else sprintf(pcInsert, "%d", replyData[1]);

                break;
            }
//                case SET_SP_MAX:
//                {
//                    sprintf(pcInsert, "%d", replyData[1]);
//
//                    break;
//                }
            case GET_SP_MIN:
            {
                if(replyData[1] < 10) sprintf(pcInsert, "%d_", replyData[1]);
                else sprintf(pcInsert, "%d", replyData[1]);

                break;
            }
//                case SET_SP_MIN:
//                {
//                    sprintf(pcInsert, "%d", replyData[1]);
//
//                    break;
//                }
            case GET_FAN_SPEED_CONTROL_MODE:
            {
                sprintf(pcInsert, "%d_", replyData[1]);
                break;
            }
            case HOTEL_READ_LOG:
            {
                sprintf(retbuf, "ID-%d,EV-%d,TY-%d,GR-%d,CR-%d,DY-%d,MO-%d,YR-%d,HR-%d,MN-%d,SC-%d",
                        (replyData[2] << 8) | replyData[3], replyData[4], replyData[5],
                        replyData[6], replyData[7] + replyData[8] + replyData[9] + replyData[10],
                        replyData[11], replyData[12], replyData[13], replyData[14], replyData[15], replyData[16]);
                // ako je neparan broj karaktera u buferu dodaj još jedan
                // ovo se može dodat i na kraj za sve odgovore zajedno
                i = strlen(retbuf);
                if(i & 1) retbuf[i] = '_';
                strcpy(pcInsert, retbuf);
                break;
            }
            case HOTEL_DELETE_LOG:
            {
                strcpy(pcInsert, "DELETED\n");
                break;
            }
            case GET_GUEST_IN_TEMP:
            {
                sprintf(pcInsert, "GUEST_IN_TEMP_%d", replyData[1]);
                break;
            }
            case GET_GUEST_OUT_TEMP:
            {
                sprintf(pcInsert, "GUEST_OUT_TEMP_%d", replyData[1]);
                break;
            }
            case GET_PIN:
            {
                sprintf(pcInsert, "PIN_VALUE_%d", replyData[1]);
                break;
            }
            case HOTEL_GET_PIN:
            {
                sprintf(pcInsert, "PIN_VALUE_%d", replyData[1]);
                break;
            }
            default:

                strcpy(pcInsert, "OK");
                break;
            }

            if(strlen(pcInsert) % 2) strncat(pcInsert, "_", 1);

            replyDataLength = 0;

        }
        DISP_FileTransferState(retval);
    }
    len = strlen(pcInsert);
    if(!len) len = iInsertLen;
    return len;
}
/**
 * @brief  CGI handler for HTTP request
 */
const char *HTTP_CGI_Handler(int iIndex, int iNumParams, char **pcParam, char **pcValue)
{
    uint8_t buf[512], sta = 0, ifadd = 0;
    uint32_t dr = 0, wradd = 0, remain = 0;

    TF_LEN length = 2;
    buf[3] = 0;
    retval = 0;
    if(iIndex == 0)
    {
        if(!strcmp(pcParam[0], "GETTIME"))  // SET DIMMER STATE
        {
            retval = TIME_INFO;
        }
        else if(!strcmp(pcParam[0], "RELAY"))  // SET RELAY STATE
        {
            if(ISVALIDDEC(*pcValue[0]))
            {
                if(!strcmp(pcParam[1], "VALUE"))
                {
                    if(ISVALIDDEC(*pcValue[1]))
                    {
                        buf[0] = atoi(pcValue[0]);
                        buf[1] = atoi(pcValue[1]);
                        TF_QuerySimple(&tfapp, S_BINARY, buf, 2, ID_Listener, TF_PARSER_TIMEOUT_TICKS * 4);
                        rdy = TF_PARSER_TIMEOUT_TICKS * 2;
                        do
                        {
                            --rdy;
                            DelayMs(1);
#ifdef USE_WATCHDOG
                            IWDG_ReloadCounter();
#endif
                        }
                        while(rdy > 0);
                        if(rdy == 0)
                        {
                            retval = RESPONSE_TIMEOUT;
                        }
                        else retval = FS_FILE_OK;
                    }
                    else retval = VALUE_TYPE_ERROR;
                }
                else if(!strcmp(pcParam[1], "STATE"))
                {
                    if(ISVALIDDEC(*pcValue[1]))
                    {
                        buf[0] = atoi(pcValue[0]);
                        TF_QuerySimple(&tfapp, V_STATUS, buf, 1, ID_Listener, TF_PARSER_TIMEOUT_TICKS * 4);
                        rdy = TF_PARSER_TIMEOUT_TICKS * 2;
                        do
                        {
                            --rdy;
                            DelayMs(1);
#ifdef USE_WATCHDOG
                            IWDG_ReloadCounter();
#endif
                        }
                        while(rdy > 0);
                        if(rdy == 0)
                        {
                            retval = RESPONSE_TIMEOUT;
                        }
                        else retval = GET_RELAY_STATE;
                    }
                    else retval = VALUE_TYPE_ERROR;
                }
                else  retval = VALUE_MISSING_ERROR;
            }
            else retval = VALUE_TYPE_ERROR;
        }
        else if(!strcmp(pcParam[0], "DIMMER"))  // SET DIMMER STATE
        {
            if(ISVALIDDEC(*pcValue[0]))
            {
                if(!strcmp(pcParam[1], "VALUE"))
                {
                    if(ISVALIDDEC(*pcValue[1]))
                    {
                        buf[0] = atoi(pcValue[0]);
                        buf[1] = atoi(pcValue[1]);
                        TF_QuerySimple(&tfapp, S_DIMMER, buf, 2, ID_Listener, TF_PARSER_TIMEOUT_TICKS * 4);
                        rdy = TF_PARSER_TIMEOUT_TICKS * 2;
                        do
                        {
                            --rdy;
                            DelayMs(1);
#ifdef USE_WATCHDOG
                            IWDG_ReloadCounter();
#endif
                        }
                        while(rdy > 0);
                        if(rdy == 0)
                        {
                            retval = RESPONSE_TIMEOUT;
                        }
                        else retval = FS_FILE_OK;
                    }
                    else retval = VALUE_TYPE_ERROR;
                }
                else  retval = VALUE_MISSING_ERROR;
            }
            else retval = VALUE_TYPE_ERROR;
        }
        else if(!strcmp(pcParam[0], "DATE"))    // SET DATE & TIME
        {
            if(ISVALIDDEC(*pcValue[0]))
            {
                if(!strcmp(pcParam[1], "TIME"))
                {
                    if(ISVALIDDEC(*pcValue[1]))
                    {
                        RTC_TimeTypeDef tm;
                        RTC_DateTypeDef dt;
                        dt.RTC_WeekDay = CONVERTDEC(pcValue[0]);
                        if(!rtc_date.RTC_WeekDay)dt.RTC_WeekDay = 7;
                        Str2Hex(pcValue[0] + 1, &dt.RTC_Date,    2);
                        Str2Hex(pcValue[0] + 3, &dt.RTC_Month,   2);
                        Str2Hex(pcValue[0] + 5, &dt.RTC_Year,    2);
                        Str2Hex(pcValue[1],     &tm.RTC_Hours,   2);
                        Str2Hex(pcValue[1] + 2, &tm.RTC_Minutes, 2);
                        Str2Hex(pcValue[1] + 4, &tm.RTC_Seconds, 2);
                        PWR_BackupAccessCmd(ENABLE);
                        rdy  = RTC_SetTime(RTC_Format_BCD, &tm);
                        rdy += RTC_SetDate(RTC_Format_BCD, &dt);
                        PWR_BackupAccessCmd(DISABLE);
                        RTC_State = RTC_VALID;
                        DISP_UpdateTimeSet();
                        if(rdy == 0)
                        {
                            retval = COMMAND_FAIL;
                        }
                        else retval = FS_FILE_OK;
                    }
                    else retval = VALUE_TYPE_ERROR;
                }
                else  retval = VALUE_MISSING_ERROR;
            }
            else retval = VALUE_TYPE_ERROR;
        }
        else if(!strcmp(pcParam[0], "TIMERON"))    // SET DATE & TIME
        {
            if(ISVALIDDEC(*pcValue[0]))
            {
                if(!strcmp(pcParam[1], "TIMEROFF"))
                {
                    if(ISVALIDDEC(*pcValue[1]))
                    {
                        Str2Hex(pcValue[0],     &timer[0], 2);
                        Str2Hex(pcValue[0] + 2, &timer[1], 2);
                        Str2Hex(pcValue[1],     &timer[2], 2);
                        Str2Hex(pcValue[1] + 2, &timer[3], 2);
                        if(I2CEE_WriteBytes16(I2CEE_PAGE_0, EE_TIMER_ADD, timer, 4) != 0)   ErrorHandler(MAIN_FUNC, I2C_DRV);
                        DelayMs(I2CEE_WRITE_DELAY);
                        retval = FS_FILE_OK;
                    }
                    else retval = VALUE_TYPE_ERROR;
                }
                else  retval = VALUE_MISSING_ERROR;
            }
            else retval = VALUE_TYPE_ERROR;
        }
        else if(!strcmp(pcParam[0], "CMD"))     // EXE COMMAND
        {
            if(!strcmp(pcParam[1], "ADDRESS"))
            {
                if(ISVALIDDEC(*pcValue[1]))
                {
                    buf[0] = 0;
                    buf[1] = atoi(pcValue[1]);
                    if(strcmp(pcValue[0], "RESTART") == 0)
                    {
                        buf[0] = RESTART_CTRL;
                    }
                    else if(strcmp(pcValue[0], "DOUTSET") == 0)
                    {
                        if((buf[1] > 0) && (buf[1] < 16))
                        {
                            switch(buf[1])
                            {
                            case 1:
                                GPIO_SetBits(DOUT0_GPIO_PORT, DOUT0_GPIO_PIN);
                                break;
                            case 2:
                                GPIO_SetBits(DOUT1_GPIO_PORT, DOUT1_GPIO_PIN);
                                break;
                            case 3:
                                GPIO_SetBits(DOUT2_GPIO_PORT, DOUT2_GPIO_PIN);
                                break;
                            case 4:
                                GPIO_SetBits(DOUT3_GPIO_PORT, DOUT3_GPIO_PIN);
                                break;
                            case 5:
                                GPIO_SetBits(DOUT4_GPIO_PORT, DOUT4_GPIO_PIN);
                                break;
                            case 6:
                                GPIO_ResetBits(DOUT5_GPIO_PORT, DOUT5_GPIO_PIN);
                                break;
                            case 7:
                                GPIO_ResetBits(DOUT6_GPIO_PORT, DOUT6_GPIO_PIN);
                                break;
                            case 8:
                                GPIO_ResetBits(DOUT7_GPIO_PORT, DOUT7_GPIO_PIN);
                                break;
                            case 9:
                                GPIO_ResetBits(DOUT8_GPIO_PORT, DOUT8_GPIO_PIN);
                                break;
                            case 10:
                                GPIO_ResetBits(DOUT9_GPIO_PORT, DOUT9_GPIO_PIN);
                                break;
                            case 11:
//                                    GPIO_SetBits(DOUT10_GPIO_PORT, DOUT10_GPIO_PIN);
                                break;
                            case 12:
                                GPIO_ResetBits(DOUT11_GPIO_PORT, DOUT11_GPIO_PIN);
                                break;
                            case 13:
                                GPIO_ResetBits(DOUT12_GPIO_PORT, DOUT12_GPIO_PIN);
                                break;
                            case 14:
                                GPIO_ResetBits(DOUT13_GPIO_PORT, DOUT13_GPIO_PIN);
                                break;
                            case 15:
                                GPIO_ResetBits(DOUT14_GPIO_PORT, DOUT14_GPIO_PIN);
                                break;
                            default:
                                break;
                            }
                            retval = DOUT_SET;
                        }
                    }
                    else if(strcmp(pcValue[0], "DOUTRESET") == 0)
                    {
                        if((buf[1] > 0) && (buf[1] < 16))
                        {
                            switch(buf[1])
                            {
                            case 1:
                                GPIO_ResetBits(DOUT0_GPIO_PORT, DOUT0_GPIO_PIN);
                                break;
                            case 2:
                                GPIO_ResetBits(DOUT1_GPIO_PORT, DOUT1_GPIO_PIN);
                                break;
                            case 3:
                                GPIO_ResetBits(DOUT2_GPIO_PORT, DOUT2_GPIO_PIN);
                                break;
                            case 4:
                                GPIO_ResetBits(DOUT3_GPIO_PORT, DOUT3_GPIO_PIN);
                                break;
                            case 5:
                                GPIO_ResetBits(DOUT4_GPIO_PORT, DOUT4_GPIO_PIN);
                                break;
                            case 6:
                                GPIO_SetBits(DOUT5_GPIO_PORT, DOUT5_GPIO_PIN);
                                break;
                            case 7:
                                GPIO_SetBits(DOUT6_GPIO_PORT, DOUT6_GPIO_PIN);
                                break;
                            case 8:
                                GPIO_SetBits(DOUT7_GPIO_PORT, DOUT7_GPIO_PIN);
                                break;
                            case 9:
                                GPIO_SetBits(DOUT8_GPIO_PORT, DOUT8_GPIO_PIN);
                                break;
                            case 10:
                                GPIO_SetBits(DOUT9_GPIO_PORT, DOUT9_GPIO_PIN);
                                break;
                            case 11:
//                                    GPIO_ResetBits(DOUT10_GPIO_PORT, DOUT10_GPIO_PIN);
                                break;
                            case 12:
                                GPIO_SetBits(DOUT11_GPIO_PORT, DOUT11_GPIO_PIN);
                                break;
                            case 13:
                                GPIO_SetBits(DOUT12_GPIO_PORT, DOUT12_GPIO_PIN);
                                break;
                            case 14:
                                GPIO_SetBits(DOUT13_GPIO_PORT, DOUT13_GPIO_PIN);
                                break;
                            case 15:
                                GPIO_SetBits(DOUT14_GPIO_PORT, DOUT14_GPIO_PIN);
                                break;
                            default:
                                break;
                            }
                            retval = DOUT_RESET;
                        }
                    }
                    else if(strcmp(pcValue[0], "DOUTSTATE") == 0)
                    {
                        if((buf[1] > 0) && (buf[1] < 16))
                        {
                            retval = GET_DOUT_STATE;
                        }
                    }
                    else if(strcmp(pcValue[0], "DEFAULT") == 0)
                    {
                        buf[0] = LOAD_DEFAULT;
                    }
                    else if(strcmp(pcValue[0], "EXTFLASH") == 0)
                    {
                        buf[0] = FORMAT_EXTFLASH;
                    }
                    else if(strcmp(pcValue[0], "GETSTATE") == 0)
                    {
                        buf[0] = GET_APPL_STAT;
                    }
                    else if(strcmp(pcValue[0], "TEMPERATURE") == 0)
                    {
                        buf[0] = GET_ROOM_TEMP;
                    }
                    else if(strcmp(pcValue[0], "COOLING") == 0)
                    {
                        buf[0] = SET_THST_COOLING;
                    }
                    else if(strcmp(pcValue[0], "HEATING") == 0)
                    {
                        buf[0] = SET_THST_HEATING;
                    }
                    else if(strcmp(pcValue[0], "TERMO_ON") == 0)
                    {
                        buf[0] = SET_THST_ON;
                    }
                    else if(strcmp(pcValue[0], "TERMO_OFF") == 0)
                    {
                        buf[0] = SET_THST_OFF;
                    }
                    else if(strcmp(pcValue[0], "GET_FAN_DIFFERENCE") == 0)
                    {
                        buf[0] = GET_FAN_DIFFERENCE;
                    }
                    else if(strcmp(pcValue[0], "GET_FAN_BAND") == 0)
                    {
                        buf[0] = GET_FAN_BAND;
                    }
                    else if(strstr(pcValue[0], "RELAY"))
                    {
                        buf[0] = SELECT_RELAY;
                        buf[2] = atoi(strstr(pcValue[0], "RELAY") + 6);
                        length = 4;

                        if(strstr(pcValue[0], "MAIN"))
                        {
                            buf[3] = GET_MAIN;
                        }
                        else if(strstr(pcValue[0], "LED"))
                        {
                            buf[3] = GET_LED;
//                            buf[4] = atoi(strstr(pcValue[0], "LED") + 4);
                        }
                        else if(strstr(pcValue[0], "OUT"))
                        {
                            buf[3] = GET_OUT;
                        }

                        length++;
                    }
                    else if(strstr(pcValue[0], "PINS"))
                    {
                        buf[0] = PINS;
                        length = 3;

                        if(strstr(pcValue[0], "READPINS"))
                        {
                            buf[2] = READ_PINS;
                        }
                        else if(strstr(pcValue[0], "SETPIN"))
                        {
                            buf[2] = SET_PIN;
                            buf[length++] = atoi(strstr(pcValue[0], "SETPINS") + 8);
                            buf[length++] = atoi(strstr(pcValue[0], "VALUE") + 6);
                        }//http://192.168.0.199:8021/sysctrl.cgi?CMD=SETPINS=1VALUE=0&ADDRESS=200
                    }
                    else if(strcmp(pcValue[0], "GET_SP_MAX") == 0)
                    {
                        buf[0] = GET_SP_MAX;
                    }
                    else if(strstr(pcValue[0], "SET_SP_MAX"))
                    {
                        buf[0] = SET_SP_MAX;
                        buf[2] = atoi(pcValue[0] + 17);

                        length = 3;
                    }
                    else if(strcmp(pcValue[0], "GET_SP_MIN") == 0)
                    {
                        buf[0] = GET_SP_MIN;
                    }
                    else if(strstr(pcValue[0], "SET_SP_MIN"))
                    {
                        buf[0] = SET_SP_MIN;
                        buf[2] = atoi(pcValue[0] + 17);

                        length = 3;
                    }
                    else if(strstr(pcValue[0], "SET_FAN_DIFFERENCE"))
                    {
                        buf[0] = SET_FAN_DIFFERENCE;
                        buf[2] = atoi(pcValue[0] + 19);

                        length = 3;
                    }
                    else if(strstr(pcValue[0], "SET_FAN_LOBAND"))
                    {
                        buf[0] = SET_FAN_LOBAND;
                        buf[2] = atoi(pcValue[0] + 15);

                        length = 3;
                    }
                    else if(strstr(pcValue[0], "SET_FAN_HIBAND"))
                    {
                        buf[0] = SET_FAN_HIBAND;
                        buf[2] = atoi(pcValue[0] + 15);

                        length = 3;
                    }
                    else if(strstr(pcValue[0], "GET_FAN_SPEEDMODE"))
                    {
                        buf[0] = GET_FAN_SPEED_CONTROL_MODE;
                    }
                    else if(strstr(pcValue[0], "SET_FAN_SPEED_CONTROL_MODE_ON_OFF"))
                    {
                        buf[0] = SET_FAN_HIBAND;
                    }
                    else if(strstr(pcValue[0], "SET_FAN_SPEED_CONTROL_MODE_3SPEED"))
                    {
                        buf[0] = SET_FAN_HIBAND;
                    }
                    else if(strstr(pcValue[0], "PCA9685_SetValue"))
                    {
                        buf[0] = PCA9685_SetValue;
                        buf[2] = atoi(pcValue[2]);
                        buf[3] = atoi(pcValue[3]);

                        length = 4;
                    }
                    else if(strstr(pcValue[0], "HOTEL_READ_LOG"))
                    {
                        buf[0] = HOTEL_READ_LOG;
                        length = 2;
                    }
                    else if(strstr(pcValue[0], "HOTEL_DELETE_LOG"))
                    {
                        buf[0] = HOTEL_DELETE_LOG;
                        length = 2;
                    }
                    else if(strstr(pcValue[0], "SET_PASSWORD"))
                    {
                        buf[0] = SET_PASSWORD;
                        buf[1] = atoi(pcValue[1]) >> 8;
                        buf[2] = atoi(pcValue[1]) & 0xff;
                        strcpy(((char*)buf) + 3, pcValue[2]);
//                        memcpy(buf + 2, pcValue[2], 7);

                        length = 3 + strlen(pcValue[2]) + 1;
                    }
                    else if(strstr(pcValue[0], "SET_GUEST_IN_TEMP"))
                    {
                        buf[0] = SET_GUEST_IN_TEMP;
                        buf[2] = atoi(pcValue[2]);

                        length = 3;
                    }
                    else if(strstr(pcValue[0], "SET_GUEST_OUT_TEMP"))
                    {
                        buf[0] = SET_GUEST_OUT_TEMP;
                        buf[2] = atoi(pcValue[2]);

                        length = 3;
                    }
                    else if(strstr(pcValue[0], "GET_GUEST_IN_TEMP"))
                    {
                        buf[0] = GET_GUEST_IN_TEMP;
                        length = 2;
                    }
                    else if(strstr(pcValue[0], "GET_GUEST_OUT_TEMP"))
                    {
                        buf[0] = GET_GUEST_OUT_TEMP;
                        length = 2;
                    }
                    else if(!strcmp(pcValue[0], "SETPINv2"))
                    {
                        buf[0] = SET_PIN_V2;
                        buf[2] = *(pcValue[2]);
                        buf[3] = atoi(pcValue[3]);
                        buf[4] = atoi(pcValue[4]);

                        length = 5;
                    }
                    else if(!strcmp(pcValue[0], "GETPIN"))
                    {
                        buf[0] = GET_PIN;
                        buf[2] = *(pcValue[2]);
                        buf[3] = atoi(pcValue[3]);

                        length = 4;
                    }
                    else if(strstr(pcValue[0], "HOTEL_SETPINv2"))
                    {
                        buf[0] = HOTEL_SET_PIN_V2;
                        buf[1] = atoi(pcValue[1]) >> 8;
                        buf[2] = atoi(pcValue[1]) & 0xff;
                        buf[3] = *(pcValue[2]);
                        buf[4] = atoi(pcValue[3]);
                        buf[5] = atoi(pcValue[4]);

                        length = 6;
                    }
                    else if(strstr(pcValue[0], "HOTEL_GETPIN"))
                    {
                        buf[0] = HOTEL_GET_PIN;
                        buf[1] = atoi(pcValue[1]) >> 8;
                        buf[2] = atoi(pcValue[1]) & 0xff;
                        buf[3] = *(pcValue[2]);
                        buf[4] = atoi(pcValue[3]);

                        length = 5;
                    }
                    else retval = VALUE_TYPE_ERROR;

                    if(buf[0])
                    {
                        TF_QuerySimple(&tfapp, S_CUSTOM, buf, length, ID_Listener, TF_PARSER_TIMEOUT_TICKS * 4);
                        rdy = TF_PARSER_TIMEOUT_TICKS * 2;
                        do
                        {
                            --rdy;
                            DelayMs(1);
#ifdef USE_WATCHDOG
                            IWDG_ReloadCounter();
#endif
                        }
                        while(rdy > 0);
                        if(rdy == 0)
                        {
                            retval = RESPONSE_TIMEOUT;
                        }
                        else retval = FS_FILE_OK;
                    }
                    else if((retval != DIN_STATE)
                            && (retval != DOUT_SET)
                            && (retval != DOUT_RESET)
                            && (retval != GET_DOUT_STATE)
                            && (retval != GET_RELAY_STATE)) retval = VALUE_TYPE_ERROR;
                }
                else retval = VALUE_TYPE_ERROR;
            }
            else  retval = VALUE_MISSING_ERROR;
        }
        else if(strcmp(pcParam[0], "SETPOINT") == 0)    // THERMOSTAT SETPOINT
        {
            if(ISVALIDDEC(*pcValue[0]))
            {
                if(strcmp(pcParam[1], "ADDRESS") == 0)
                {
                    if(ISVALIDDEC(*pcValue[1]))
                    {
                        buf[0] = SET_ROOM_TEMP;
                        buf[1] = atoi(pcValue[1]);
                        buf[2] = atoi(pcValue[0]);
                        TF_QuerySimple(&tfapp, S_TEMP, buf, 3, ID_Listener, TF_PARSER_TIMEOUT_TICKS * 4);
                        rdy = TF_PARSER_TIMEOUT_TICKS * 2;
                        do
                        {
                            --rdy;
                            DelayMs(1);
#ifdef USE_WATCHDOG
                            IWDG_ReloadCounter();
#endif
                        }
                        while(rdy > 0);
                        if(rdy == 0)
                        {
                            retval = RESPONSE_TIMEOUT;
                        }
                        else retval = FS_FILE_OK;
                    }
                    else retval = VALUE_TYPE_ERROR;
                }
                else  retval = VALUE_MISSING_ERROR;
            }
            else retval = VALUE_TYPE_ERROR;
        }
        else if(!strcmp(pcParam[0], "FILE"))    // UPDATE FIRMWARE
        {
            if(f_mount(&fatfs, "0:", 0) == FR_OK)
            {
                if(f_opendir(&dir_1, "/") == FR_OK)
                {
                    if(f_open(&file_SD, pcValue[0], FA_READ) == FR_OK)
                    {
                        if(!strcmp(pcParam[1], "WRITE"))
                        {
                            if(!strcmp(pcParam[2], "ADDRESS"))
                            {
                                wradd = Str2Int(pcValue[1], 0);
                                ifadd = Str2Int(pcValue[2], 0);
                            }
                            else retval = VALUE_MISSING_ERROR;
                        }
                        else if(!strcmp(pcValue[0], "IC.BIN") || !strcmp(pcValue[0], "ICBL.BIN"))
                        {
                            if(!strcmp(pcParam[1], "ADDRESS"))
                            {
                                wradd = 0x90F00000;
                                ifadd = Str2Int(pcValue[1], 0);
                            }
                            else retval = VALUE_MISSING_ERROR;
                        }
                        else if(!strcmp(pcValue[0], "EXT.BIN"))
                        {
                            if(!strcmp(pcParam[1], "ADDRESS"))
                            {
                                wradd = 0x90000000;
                                ifadd = Str2Int(pcValue[1], 0);
                            }
                            else retval = VALUE_MISSING_ERROR;
                        }
                        else retval = VALUE_TYPE_ERROR;
                        if((wradd > 0x08000000) && (wradd < 0x91000000))
                        {
                            if((ifadd > 0) && (ifadd < 0xFF))
                            {
                                retval = FS_FILE_OK;
                            }
                            else retval = VALUE_OUTOFRANGE_ERROR;
                        }
                        else retval = ADDRESS_ERROR;
                    }
                    else retval = FS_FILE_ERROR;
                }
                else retval = FS_DIRECTORY_ERROR;
            }
            else retval = FS_DRIVE_ERR;

            if(retval == FS_FILE_OK)
            {
                buf[0] = wradd >> 24;
                buf[1] = wradd >> 16;
                buf[2] = wradd >> 8;
                buf[3] = wradd;
                buf[4] = file_SD.obj.objsize >> 24;
                buf[5] = file_SD.obj.objsize >> 16;
                buf[6] = file_SD.obj.objsize >> 8;
                buf[7] = file_SD.obj.objsize;
                buf[8] = ifadd;
                buf[9] = ST_FIRMWARE_REQUEST;
                remain = file_SD.obj.objsize;
                DISP_ProgbarSetNewState(1);
                DISP_FileTransferState(FW_UPDATE_RUN);
                TF_QuerySimple(&tfapp, ST_FIRMWARE_REQUEST, buf, 10, ID_Listener, 30000);
                rdy = 29000;
                while(--rdy > 0)
                {
                    DelayMs(1);
#ifdef USE_WATCHDOG
                    IWDG_ReloadCounter();
#endif
                }
                while(remain > 0)
                {
                    f_read(&file_SD, buf, sizeof(buf), &dr);
                    remain -= dr;
                    sta = (100 - ((remain * 100) / file_SD.obj.objsize));
                    if(sta > 1) DISP_ProgbarSetNewState(sta);
                    TF_QuerySimple(&tfapp, ST_FIRMWARE_REQUEST, buf, dr, ID_Listener, TF_PARSER_TIMEOUT_TICKS * 4);
                    rdy = TF_PARSER_TIMEOUT_TICKS * 2;
                    do
                    {
                        --rdy;
                        DelayMs(1);
#ifdef USE_WATCHDOG
                        IWDG_ReloadCounter();
#endif
                    }
                    while(rdy > 0);
                    if(rdy == 0)
                    {
                        retval = FILE_TRANSFER_ERRROR;
                        DISP_ProgbarSetNewState(0);
                        return weblog;
                    }
                }
                DISP_ProgbarSetNewState(0);
            }
            else f_mount(NULL, "0:", 0);
            return weblog;
        }
    }
    if(retval) return weblog;
    else        return webctrl;
}
/**
 * @brief  uljucuje LED
 * @param  1 - LED1
 * @param  2 - LED2
 * @param  3 - LED1 &L ED2
 * @retval None
 */
void led_set(uint8_t led)
{
    if(led == 0U) GPIO_ResetBits(LED_GPIO_PORT, LED1_GPIO_PIN | LED2_GPIO_PIN);
    else if(led == 1U) GPIO_ResetBits(LED_GPIO_PORT, LED1_GPIO_PIN);
    else if(led == 2U) GPIO_ResetBits(LED_GPIO_PORT, LED2_GPIO_PIN);
}
/**
 * @brief  iskljucuje LED
 * @param  0 - LED1 &L ED2
 * @param  1 - LED1
 * @param  2 - LED2
 * @retval None
 */
void led_clr(uint8_t led)
{
    if(led == 0U) GPIO_SetBits(LED_GPIO_PORT, LED1_GPIO_PIN | LED2_GPIO_PIN);
    else if(led == 1U) GPIO_SetBits(LED_GPIO_PORT, LED1_GPIO_PIN);
    else if(led == 2U) GPIO_SetBits(LED_GPIO_PORT, LED2_GPIO_PIN);
}
/**
 * @brief  change LED state to oposite 1-0-1-0...
 * @param  0 - LED1 &L ED2
 * @param  1 - LED1
 * @param  2 - LED2
 * @retval None
 */
void led_tgl(uint8_t led)
{
    if(led == 0U) GPIO_ToggleBits(LED_GPIO_PORT, LED1_GPIO_PIN | LED2_GPIO_PIN);
    else if(led == 1U) GPIO_ToggleBits(LED_GPIO_PORT, LED1_GPIO_PIN);
    else if(led == 2U) GPIO_ToggleBits(LED_GPIO_PORT, LED2_GPIO_PIN);
}
/**
 * @brief  insert data to HTTP response
 * @param  iIndex       = 0
 * @param  pcInsert     = pointer to response data buffer
 * @param  iInsertLen   = max. length of response data in bytes
 * @retval length in bytes of response data set by handler
 */
TF_Result ID_Listener(TinyFrame *tf, TF_Msg *msg)
{
    short int i;
//    uint8_t command = msg->data[0];
//
//    switch(command)
//    {
//        case GET_ROOM_TEMP:
//
//            replyData[0] = msg->data[1];
//            replyData[1] = msg->data[2];
//
//            break;
//
//        case SELECT_RELAY:
//
//
//
//            break;
//    }

    replyDataLength = msg->len;

    for(i = 0; i < msg->len; i++)
    {
        replyData[i] = msg->data[i];
    }

    rdy = -1;
    return TF_CLOSE;
}
/**
  * @brief
  * @param
  * @retval
  */
TF_Result GEN_Listener(TinyFrame *tf, TF_Msg *msg)
{
    char buf[128];

    sprintf(buf, "RECf:TYPE=%d:ID=%d:LEN=%d%s:BUSID=%d", msg->type, msg->frame_id, msg->len, msg->is_response == true ? ":RESPONSE" : "\0", (msg->data[2] << 24) | (msg->data[3] << 16) | (msg->data[4] << 8) | (msg->data[5]) /*(msg->data[msg->len - 2] << 8) | msg->data[msg->len - 1])*/);

    DISP_UpdateLog(buf);

    ++logs_FromListener_counter;
    log_FromListener[0] = msg->type;
    log_FromListener[1] = msg->frame_id;
    log_FromListener[2] = msg->len >> 8;
    log_FromListener[3] = msg->len & 0xff;
    log_FromListener[4] = msg->data[msg->len - 2];
    log_FromListener[5] = msg->data[msg->len - 1];

    return TF_STAY;
}
void httpd_ssi_init(void)
{
    http_set_ssi_handler(HTTP_ResponseHandler, (char const **) TAGS, 1);
}
void httpd_cgi_init(void)
{
    CGI_TAB[0] = HTTP_CGI;
    http_set_cgi_handlers(CGI_TAB, 1);
}
void RS485_Tick(void)
{
    if(init_tf == true)
    {
        TF_Tick(&tfapp);
    }
}
void RS485_Receive(uint16_t rxb)
{
    uint8_t tfrxb = rxb;
    TF_AcceptChar(&tfapp, tfrxb);
}
void TF_WriteImpl(TinyFrame *tf, const uint8_t *buff, uint32_t len)
{
    RS485_Send((uint8_t*)buff, len);
}
void TINYFRAME_Init(void)
{

    init_tf = TF_InitStatic(&tfapp, TF_MASTER); // 1 = master, 0 = slave
    TF_AddGenericListener(&tfapp, GEN_Listener);
}
/************************ (C) COPYRIGHT JUBERA D.O.O Sarajevo ************************/
