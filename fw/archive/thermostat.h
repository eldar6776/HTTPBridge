/**
 ******************************************************************************
 * File Name          : thermostat.h
 * Date               : 22/02/2018 07:03:00
 * Description        : temperature controller header
 ******************************************************************************
 */
 
 /* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __TEMP_CTRL_H__
#define __TEMP_CTRL_H__                             FW_BUILD // version
/* Includes ------------------------------------------------------------------*/
#include "stm32f7xx.h"
/* Exported Type  ------------------------------------------------------------*/



typedef struct{
    uint8_t  th_ctrl;           // thermostat control: off,auto,manual,heating,cooling,enabled,disabled,network
    uint8_t  th_ctrl_old;       // return point 
    uint8_t  th_state;          // thermostat status
    int16_t  mv_temp;           // temperature measured value  
    int8_t   mv_offset;         // temperature measured value  offset +/- 10.0°C
    uint16_t mv_ntcref;         // measured value ntc reference & pull up resistance x 0.1kOhm
    uint16_t mv_nctbeta;        // measured value ntc beta constant
    uint8_t  sp_temp;           // temperature setpoint  x 1°C
    uint8_t  sp_diff;           // temperature setpoint difference x 0.1°C
    uint8_t  sp_max;            // temperature setpoint maximum for user x 1°C
    uint8_t  sp_min;            // temperature setpoint minimum for user x 1°C
    uint8_t  fan_ctrl;          // fan control: off,auto,manual 1-2-3,quiet mode   
    uint8_t  fan_speed;         // fan speed actual/set value depend on fan control type
    uint8_t  fan_diff;          // fan speed minimum difference to switch output
    uint8_t  fan_loband;        // fan speed controller low band 
    uint8_t  fan_hiband;        // fan speed controller high band
    uint8_t  fan_quiet_start;   // fan quiet mode start hour
    uint8_t  fan_quiet_end;     // fan quiet mode end hour
    uint8_t  fan_quiet_speed;   // fan quiet mode speed limit
    uint8_t  forward_heating;   // Forward data on change (Heating mode)
    uint8_t  forward_cooling;   // Forward data on change (Cooling mode)
}THERMOSTAT_TypeDef;

extern THERMOSTAT_TypeDef thst;
/* Exported Define  ----------------------------------------------------------*/
#define FANC_NTC_MEASURING_DELAY_TIME               600000U // 10 min. (x60 sec x 1000 ms)      
#define FANC_FAN_MIN_ON_TIME                        500U    // 0,5s between two or on/off fan speed switching
#define FANC_VALVE_MIN_ON_TIME                      5678U	// cooling valve state swith 5,5s min. time	
#define FANC_NTC_UPDATE_TIME                        2345U   // 2.3 second fancoil ntc temperature update rate
#define THSTAT_POWER_ON_DELAY_TIME                  3456U   // 2,5 sec. power on startup time
#define FAN_SWITCH_TIME                             3000

#define THSTAT_TEMP_CHANGE_REASON_OTHER             1
#define THSTAT_TEMP_CHANGE_REASON_GUEST_IN          2
#define THSTAT_TEMP_CHANGE_REASON_GUEST_OUT         3
/* Exported Variable   -------------------------------------------------------*/
extern uint8_t termfl;
extern uint8_t thstat_temperature_change_reason;
/* Exported Macro ------------------------------------------------------------*/
/** ==========================================================================*/
/**                 T H E R M O S T A T         F L A G S                     */
/** ==========================================================================*/
#define IsTempRegSta(x)             (x & (1U<<0)) // remote state config flag
#define IsTempRegMod(x)             (x & (1U<<1)) // remote mode config flag
#define IsTempRegCtr(x)             (x & (1U<<2)) // remote control flag
#define IsTempRegOut(x)             (x & (1U<<3)) // remote output flag
#define IsTempRegNewSta(x)          (x & (1U<<4)) // use remote state config flag 
#define IsTempRegNewMod(x)          (x & (1U<<5)) // use remote mode config flag
#define IsTempRegNewCtr(x)          (x & (1U<<6)) // use remote control flag
#define IsTempRegNewOut(x)          (x & (1U<<7)) // use remote output flag
#define IsTempRegNewCfg(x)          (x & 0xF0) // use at least one remote flag
/** ==========================================================================*/
/**       M E M O R I S E D     A N D     R E S T O R E D     F L A G S       */
/** ==========================================================================*/
#define TempRegOff()                (thst.th_ctrl = 0)
#define TempRegCooling()            (thst.th_ctrl = 1)
#define TempRegHeating()            (thst.th_ctrl = 2)
#define IsTempRegActiv()            (thst.th_ctrl != 0)
#define IsTempRegCooling()          (thst.th_ctrl == 1)
#define IsTempRegHeating()          (thst.th_ctrl == 2)

#define NtcConnected()              (termfl |=  (1U<<0))
#define NtcDisconnected()           (termfl &= ~(1U<<0))
#define IsNtcConnected()            (termfl &   (1U<<0))
#define NtcErrorSet()               (termfl |=  (1U<<1))
#define NtcErrorReset()             (termfl &= ~(1U<<1))
#define IsNtcErrorActiv()           (termfl &   (1U<<1))
/* Exported Function  ------------------------------------------------------- */
void THSTAT_Init(void);
void THSTAT_Service(void);
void THSTAT_SaveSettings(void);
void THSTAT_SetpointChanged(void);
void THSTAT_SetpointChangedReset(void);
uint8_t THSTAT_hasSetpointChanged(void);
void THSTAT_SetTemperature(uint8_t, uint8_t);
uint8_t THSTAT_getTemperatureChangeReason(void);

#endif
/******************************   END OF FILE  ********************************/
