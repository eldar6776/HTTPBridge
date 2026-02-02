/**
 ******************************************************************************
 * Project          : KuceDzevadova
 ******************************************************************************
 *
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

#if (__COMMON_H__ != FW_TIME)
    #error "common header version mismatch"
#endif

/* Imported Type  ------------------------------------------------------------*/
BRIDGE_TypeDef  COM_Bridge  = BRNONE;
LinkTypeDef     COM_Link    = NOLINK;
RX_TypeDef      COM_State   = RX_INIT;

const uint8_t rtc_months[2][12] ={
    { 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U },
    { 31U, 29U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U }
};
const uint32_t bps[10] ={ 
    2400U, 4800U, 9600U, 19200U, 38400U, 57600U, 115200U, 230400U, 460800U, 921600U 
};
const char *day[] = {NULL,"Ponedjeljak","Utorak","Srijeda","Cetvrtak","Petak","Subota","Nedelja"};
const char *month[] = {NULL,"Januar","Februar","Mart","April","Maj","Juni","Juli","August","Septembar","Oktobar","Novembar","Decembar"};

#if (VECT_TAB_OFFSET == 0x3000)
    #define FWADD ".ARM.__at_0x0800500"
    #define FW_VERS  (FW_TYPE | FW_DATE) // release version is build from date 
#elif     (VECT_TAB_OFFSET == 0x10000)
    #define FWADD ".ARM.__at_0x0801200"
    #define FW_VERS  (FW_TYPE | FW_DATE) // release version is build from date 
#elif   (VECT_TAB_OFFSET == 0x0000)
#define FWADD ".ARM.__at_0x0800200"
#ifdef USE_DEBUGGER
        #define FW_VERS  (FW_TYPE | FW_TIME) // debugg version is build with time or with build number
    #else
#define FW_VERS  (FW_TYPE | FW_DATE) // release version is build from date
#endif
#else
#error "not valid vector table offset"
#endif

#define _2STR(x) FWADD # x   
#define TOSTR(x) _2STR(x)   
#define SIZ_SECT TOSTR(0)   
#define CRC_SECT TOSTR(4)   
#define VER_SECT TOSTR(8) 
#define ADR_SECT TOSTR(C)

const uint32_t size     __attribute__((section(SIZ_SECT),used)) = 0x00000000;   // this firmware size
const uint32_t crc_32   __attribute__((section(CRC_SECT),used)) = 0x00000000;   // this firmware crc32 with crc32 & size set to 0
const uint32_t version  __attribute__((section(VER_SECT),used)) = FW_VERS;      // bits 24~31 = this firmware type, bits 0~23 = this firmware version
const uint32_t address  __attribute__((section(ADR_SECT),used)) = FW_ADDR;      // this firmware write address
/* Imported Variable  --------------------------------------------------------*/
/* Imported Function  --------------------------------------------------------*/
/* Private Type --------------------------------------------------------------*/
/* Private Define ------------------------------------------------------------*/
/* Private Variable ----------------------------------------------------------*/
/* Private Macro -------------------------------------------------------------*/
/* Private Function Prototype ------------------------------------------------*/
/* Program Code  -------------------------------------------------------------*/
/**
 * @brief
 * @param
 * @retval
 */
void DelayMs(__IO uint32_t delay_time){
#ifdef USE_STDPERIPH_DRIVER
    __IO uint32_t init_time = Get_SysTick();    
    while(delay_time){
        if ((Get_SysTick() - init_time) >= delay_time){
            delay_time = 0U;
        }
    }
#elif defined USE_HAL_DRIVER
	__IO uint32_t init_time = HAL_GetTick();
	while (delay_time){
		if ((HAL_GetTick() - init_time) >= delay_time){
			delay_time = 0U;
		}
	}
#endif

}

void substring(char s[], char sub[], int p, int l) {
   int c = 0;
   
   while (c < l) {
      sub[c] = s[p+c-1];
      c++;
   }
   sub[c] = '\0';
}
/**
 * @brief
 * @param
 * @retval
 */
uint8_t Bcd2Dec(uint8_t val)
{
	return(((val >> 4U) * 10U) + (val & 0x0fU));
}
/**
 * @brief
 * @param
 * @retval
 */
uint8_t Dec2Bcd(uint8_t val)
{
	__IO uint32_t r = 0U;
    
	while(val > 9U)
	{
		val -= 10U;
		++r;
	}
	r <<= 4U;
	r += val;
	return (r);
}
/**
 * @brief
 * @param
 * @retval
 */
void CharToBinStr(char *pstr, uint8_t val)
{
	__IO uint32_t cnt = 0U;

	while (cnt < 8U)
	{
		if ((val & (1 << cnt)) != 0U) *pstr = '1';
		else *pstr = '0';
		++cnt;
		++pstr;
	}
}
/**
 * @brief
 * @param
 * @retval
 */
uint32_t GetSize(const uint8_t *pbuf)
{
	const uint8_t *s;

	for (s = pbuf; *s; ++s);

	return (s - pbuf);
}
/**
 * @brief
 * @param
 * @retval
 */
uint32_t BaseToPower(uint16_t base, uint8_t power)
{
	__IO uint32_t result = 1U;

	while (power--) result *= (uint32_t) base;
	return (result);
}
/**
 * @brief
 * @param
 * @retval
 */
uint8_t CalcCRC(const uint8_t *pbuf, uint16_t size)
{
	__IO uint32_t g = 0U;

	do
	{
        g += *pbuf;
		++pbuf;
		--size;
	}
	while (size != 0U);

	return (((~g) & 0xffU) + 1U);
}
/**
 * @brief
 * @param
 * @retval
 */
signed int Str2Int(const char *pstr, uint8_t str_size)
{
	__IO uint32_t result = 0; //--------------------------------store the result
	__IO int32_t sign = 1;   //--------------------------------init sign is positive
	__IO uint32_t scnt = 10U;

	if ((*pstr) == '-')  //--------------------------------------write sign for negative number
	{
		sign = -1;
		++pstr;
	}

	if (str_size != 0U)
	{
		scnt = str_size;
	}

	while (IS09(pstr) && (scnt != 0U)) //------------------------check is string char valid decimal number
	{
		result = ((result * 10U) + (*pstr - '0'));
		++pstr;
		--scnt;
	}

	return (sign * result); //----------------------------------get signed value
}
/**
 * @brief
 * @param
 * @retval
 */
void Int2Str(char *pstr, signed int val, uint8_t str_size)
{
	__IO char tmp[12];
	__IO uint32_t i, t = 0x0U, s = 0x0U; //-------------------------init sign is positive
	__IO uint32_t div = 1000000000U;

	if (val < 0)    //----------------------------------------------check is value negative
	{
		val = -val;     //------------------------------------------make value positive
		s = 1U;     //----------------------------------------------record sign
	}

	for (i = 0U; i < 10U; i++)
	{
		tmp[i] = TOCHAR(val / div);
		val = val % div;
		div /= 10U;

		if (str_size == 0U)     //-------------------------------supress leading zeros conversion
		{
			pstr[t] = tmp[i];

			if (t == 0U)    //-----------------------------------check is writing enabled
			{
				if (tmp[i] != '0')   //---------------------------wait first non zero value to start writing
				{
					if (s == 1U)
					{
						pstr[0] = '-';  //-----------------------write minus sign
						pstr[1] = tmp[i];   //-------------------0x00U terminate string
						t = 0x1U;   //---------------------------enable writing and select next char position
					}
					++t;
				}
			}
			else ++t;   //---------------------------------------select next char position
		}
	}

	if (str_size != 0x0U)    //-----------------------------------if sized result requested
	{
		t = 0x0U;       // add to fix something? 
		if(s == 1U)
		{
			t = 0x1U;   // add to fix something? 
			pstr[0] = '-';  //----------------------------------write minus sign
			pstr[str_size + 1U] = NUL;      //------------------0x00U terminate string
		}
		else pstr[str_size] = NUL;   //-------------------------0x00U terminate string

		while (str_size && i)   //------------------------------resulted string limited to 10 digit number or requested size
		{
			--i;
			--str_size;
			pstr[str_size + t] = tmp[i]; //----------------------copy required size from result converted in local bufer
		}
	}
	else pstr[t] = NUL;     //----------------------------------0x00U terminate string
}
/**
 * @brief
 * @param
 * @retval
 */
void Str2Hex(const char *pstr, uint8_t *phex, uint16_t str_size)
{
	__IO uint32_t scnt = 0U;

	if ((pstr[0] == '0') && ((pstr[1] == 'x') || (pstr[1] == 'X'))) //----check if input string is hex in 0x or 0X format
	{
		pstr += 2U;
	}

	while (str_size) //------------------------------------------convert requested hex chars from string to integer value
	{
		if (!ISVALIDHEX(pstr)) return;

		if (scnt == 0U)
		{
			*phex = CONVERTHEX(pstr);
			++scnt;
		}
		else
		{
			scnt = 0U;
			*phex <<= 4U;
			*phex += CONVERTHEX(pstr);
			++phex;
		}

		++pstr;
		--str_size;
	}
}
/**
 * @brief
 * @param
 * @retval
 */
void Hex2Str(char *pstr, const uint8_t *phex, uint16_t str_size)
{
	__IO uint32_t scnt = 0U;

	while (str_size) //------------------------------------------convert requested hex chars from string to integer value
	{
		if (scnt == 0U)
		{
			uint32_t tmp = ((*phex >> 4) & 0x0FU);  //----------select high nibble
			if (tmp > 0x09U) *pstr = tmp + 0x37U;    //----------convert to upper ascii letter
			else *pstr = tmp + 0x30U;   //----------------------convert to char digit
			++scnt;
		}
		else
		{
			uint32_t tmp = (*phex & 0x0FU); //------------------select low nibble
			if (tmp > 0x09U) *pstr = tmp + 0x37U;    //----------convert to upper ascii letter
			else *pstr = tmp + 0x30U;   //----------------------convert to char digit
			scnt = 0U;
			++phex;
		}

		++pstr;
		*pstr = 0x00U; //----------------------------------------0x00U terminate string
		--str_size;
	}
}
/**
 * @brief
 * @param
 * @retval
 */
/* Copy memory to memory */
void mem_cpy(void *dst, const void *src, uint32_t cnt)
{
	uint8_t *d = (uint8_t*) dst;
	const uint8_t *s = (const uint8_t*) src;

	if (cnt)
	{
		do
		{
			*d++ = *s++;
		}
		while (--cnt);
	}
}
/**
 * @brief
 * @param
 * @retval
 */
/* Fill memory block */
void mem_set(void *dst, int val, uint32_t cnt)
{
	uint8_t *d = (uint8_t*) dst;

	do
	{
		*d++ = (uint8_t) val;
	}
	while (--cnt);
}
/**
 * @brief
 * @param
 * @retval
 */
/* Compare memory block */
uint8_t mem_cmp(const void *dst, const void *src, uint32_t cnt) /* ZR:same, NZ:different */
{
	const uint8_t *d = (const uint8_t*) dst;
	const uint8_t *s = (const uint8_t*) src;
	int r = 0;
	
	do
	{
		r = *d++ - *s++;
	}
	while (--cnt && r == 0);

	if (r == 0) return 0x0U;
	return 0x01U;
}
/**
 * @brief
 * @param
 * @retval
 */
/* Check if chr is contained in the string */
uint8_t chk_chr(const char *str, char chr)
{
	while (*str && *str != chr)
    {
        str++;
    }
	if (*str) return 0x0U;
	return 0x1U;
}
/**
 * @brief
 * @param
 * @retval
 */
void mem_copy(uint8_t *dest, const uint8_t *source, uint32_t len)
{
	if (len == 0) return;
	do
	{
		*dest++ = *source++; /* ??? to be unrolled */
	}
	while (--len != 0);
}
/**
 * @brief
 * @param
 * @retval
 */
int mem_comp(const uint8_t *s1, const uint8_t *s2, uint32_t len)
{
	uint32_t j;

	for (j = 0; j < len; j++)
	{
		if (s1[j] != s2[j]) return 2 * (s1[j] > s2[j]) - 1;
	}
	return 0;
}
/**
 * @brief
 * @param
 * @retval
 */
void mem_zero(uint8_t *dest, uint32_t len)
{
	if (len == 0) return;
	do
	{
		*dest++ = 0; /* ??? to be unrolled */
	}
	while (--len != 0);
}
/**
 * @brief  : set all file info value to zero
 * @param  : pointer to file info struct
 * @retval : none
 */
void ResetFwInfo(FwInfoTypeDef *fw_info)
{
	fw_info->size = 0U;
	fw_info->crc32 = 0U;
	fw_info->version = 0U;
	fw_info->wr_addr = 0U;
	fw_info->ld_addr = 0U;
}
/**
 * @brief  : load file info data from file load address
 * @param  : init load address before call this function
 * @retval : 0 success, >0 error code
 */
uint8_t GetFwInfo(FwInfoTypeDef *fw_info)
{
// check fw size compare to flash app size
// check write address and compare to RT_APPL_ADDR
// check fw type by adding type string type = R+T+B+L
// check postfix
	uint32_t fwcrc32 = 0x0U;
	uint32_t dummy_buf[2];

	if ((fw_info->ld_addr < FLASH_ADDR) || (fw_info->ld_addr > END_LOAD_ADDR)) return 0x1U;
	fw_info->size = (*(uint32_t*) (fw_info->ld_addr + VERS_INF_OFFSET));
	fw_info->crc32 = (*(uint32_t*) (fw_info->ld_addr + VERS_INF_OFFSET + 0x4U));
	fw_info->version = (*(uint32_t*) (fw_info->ld_addr + VERS_INF_OFFSET + 0x8U));
	fw_info->wr_addr = (*(uint32_t*) (fw_info->ld_addr + VERS_INF_OFFSET + 0xCU));
	if ((fw_info->size > FLASH_SIZE) || ((fw_info->size == 0x00000000U))) return 0x2U;
	if ((fw_info->crc32 == 0xFFFFFFFFU) || ((fw_info->crc32 == 0x00000000U))) return 0x3U;
	if ((fw_info->version == 0xFFFFFFFFU) || ((fw_info->version == 0x00000000U))) return 0x4U;
	if ((fw_info->wr_addr < FLASH_ADDR) || (fw_info->wr_addr > (FLASH_END_ADDR + fw_info->size))) return 0x5U;
	dummy_buf[0] = fw_info->size;
	dummy_buf[1] = 0xFFFFFFFFU;
#ifdef USE_STDPERIPH_DRIVER
    CRC_ResetDR();
    CRC_CalcBlockCRC((uint32_t*) fw_info->ld_addr, VERS_INF_OFFSET / 0x4U);
    CRC_CalcBlockCRC((uint32_t*) dummy_buf, 0x2U);
    fwcrc32 = CRC_CalcBlockCRC((uint32_t*)(fw_info->ld_addr + VERS_INF_OFFSET + 0x8U), ((fw_info->size - VERS_INF_OFFSET - 0x8U) / 0x4U));
#elif defined USE_HAL_DRIVER
	fwcrc32 = HAL_CRC_Calculate(&hcrc, (uint32_t*) fw_info->ld_addr, VERS_INF_OFFSET / 0x4U);
	fwcrc32 = HAL_CRC_Accumulate(&hcrc, (uint32_t*) dummy_buf, 0x2);
	fwcrc32 = HAL_CRC_Accumulate(&hcrc, (uint32_t*) (fw_info->ld_addr + VERS_INF_OFFSET + 0x8U),
	        ((fw_info->size - VERS_INF_OFFSET - 0x8U) / 0x4U));
#endif 
	if (fwcrc32 != fw_info->crc32) return 0x6U;
//	if (((fw_info->version & 0xFF000000U) < 0x10000000) || ((fw_info->version & 0xFF000000U) > 0x37000000)) return 0x7U;
	return 0x0U;
}
/**
 * @brief  : validate preloaded file info data
 * @param  : file info struct
 * @retval : 0 success, >0 error code
 */
uint8_t ValidateFwInfo(FwInfoTypeDef *fw_info)
{
// check fw size compare to flash app size
// check write address and compare to RT_APPL_ADDR
// check fw type by adding type string type = R+T+B+L
// check postfix
//    uint32_t fwcrc32 = 0x0U;
//    uint32_t dummy_buf[2];
	if ((fw_info->size      > FLASH_SIZE) || ((fw_info->size    == 0U))) return 0x2U;
	if ((fw_info->crc32   == 0xFFFFFFFFU) || ((fw_info->crc32   == 0U))) return 0x3U;
	if ((fw_info->version == 0xFFFFFFFFU) || ((fw_info->version == 0U))) return 0x4U;
	if ((fw_info->wr_addr   < FLASH_ADDR) ||  (fw_info->wr_addr > (FLASH_END_ADDR + fw_info->size))) return 0x5U;
//    dummy_buf[0] = fw_info->size;
//    dummy_buf[1] = 0xFFFFFFFFU;
//#ifdef USE_STDPERIPH_DRIVER
//    CRC_ResetDR();
//    CRC_CalcBlockCRC((uint32_t*) fw_info->ld_addr, VERS_INF_OFFSET / 0x4U);
//    CRC_CalcBlockCRC((uint32_t*) dummy_buf, 0x2U);
//    fwcrc32 = CRC_CalcBlockCRC((uint32_t*)(fw_info->ld_addr + VERS_INF_OFFSET + 0x8U), ((fw_info->size - VERS_INF_OFFSET - 0x8U) / 0x4U));
//#elif defined USE_HAL_DRIVER
//    fwcrc32 = HAL_CRC_Calculate  (&hcrc,(uint32_t*) fw_info->ld_addr, VERS_INF_OFFSET / 0x4U);
//    fwcrc32 = HAL_CRC_Accumulate (&hcrc,(uint32_t*) dummy_buf, 0x2);
//    fwcrc32 = HAL_CRC_Accumulate (&hcrc,(uint32_t*)(fw_info->ld_addr + VERS_INF_OFFSET + 0x8U), ((fw_info->size - VERS_INF_OFFSET - 0x8U) / 0x4U));
//#endif 
//    if (fwcrc32 != fw_info->crc32) return 0x6U;
//    if (((fw_info->version & 0xFF000000U) < 0x10000000) || ((fw_info->version & 0xFF000000U) > 0x37000000)) return 0x7U;
	return 0x0U;
}
/**
 * @brief  : compare two file info to find if new is update to old
 * @param  : old file info, new file info
 * @retval : 0 new file is update to old, >0 compare result
 */
uint8_t IsNewFwUpdate(FwInfoTypeDef *old_fw, FwInfoTypeDef *new_fw)
{
	if      ((old_fw->version & 0xFF000000U) != (new_fw->version & 0xFF000000U)) return 0x1U;   // fw types doesn't match
    if      ((old_fw->version & 0x00FFFFFFU) == (new_fw->version & 0x00FFFFFFU)) return 0x2U;   // two firmware versions equal
    else if ((old_fw->version & 0x00FFFFFFU)  > (new_fw->version & 0x00FFFFFFU)) return 0x3U;   // newer fw version already written
	return 0x0U;                                                                                // new firmware is update
}
/**
 * @brief
 * @param
 * @retval
 */
uint32_t rtc2unix(void *tm, void *dt)
{
#ifdef HAL_RTC_MODULE_ENABLED
	RTC_TimeTypeDef *tim = (RTC_TimeTypeDef*) tm;
	RTC_DateTypeDef *dat = (RTC_DateTypeDef*) dt;
	uint32_t days = 0x0U, unix = 0x0U, i = 0x0U;
#ifdef USE_STDPERIPH_DRIVER
    uint32_t year = Bcd2Dec(dat->RTC_Year) + 2000U;
    /* Year is below offset year */
	if    (year < UNIX_OFFSET_YEAR) return (0x0U);
    /* Days in back years */
    for (i = UNIX_OFFSET_YEAR; i < year; i++) 
	{
		days += DAYS_IN_YEAR(i);
	}	
    /* Days in current year */
	for (i = 0x1U; i < Bcd2Dec(dat->RTC_Month); i++) 
	{
		days += rtc_months[LEAP_YEAR(year)][i - 0x1U];
	}	
	days += Bcd2Dec(dat->RTC_Date) - 0x1U;
	unix =  days                        * SECONDS_PER_DAY;
	unix += Bcd2Dec(tim->RTC_Hours)     * SECONDS_PER_HOUR;
	unix += Bcd2Dec(tim->RTC_Minutes)   * SECONDS_PER_MINUTE;
	unix += Bcd2Dec(tim->RTC_Seconds);
#elif defined USE_HAL_DRIVER

	uint32_t year = Bcd2Dec(dat->Year) + 2000U;
	/* Year is below offset year */
	if (year < UNIX_OFFSET_YEAR) return (0x0U);
	/* Days in back years */
	for (i = UNIX_OFFSET_YEAR; i < year; i++)
	{
		days += DAYS_IN_YEAR(i);
	}
	/* Days in current year */
	for (i = 0x1U; i < Bcd2Dec(dat->Month); i++)
	{
		days += rtc_months[LEAP_YEAR(year)][i - 0x1U];
	}
	days += Bcd2Dec(dat->Date) - 0x1U;
	unix = days * SECONDS_PER_DAY;
	unix += Bcd2Dec(tim->Hours) * SECONDS_PER_HOUR;
	unix += Bcd2Dec(tim->Minutes) * SECONDS_PER_MINUTE;
	unix += Bcd2Dec(tim->Seconds);
#endif
	return (unix);
#else 
    return 0;
#endif    
}
/************************ (C) COPYRIGHT JUBERA D.O.O Sarajevo ************************/
