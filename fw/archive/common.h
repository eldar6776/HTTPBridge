/**
 ******************************************************************************
 * File Name          : common.h
 * Date               : 21/08/2016 20:59:16
 * Description        : usefull function and macros set header file
 ******************************************************************************
 *
 *
 ******************************************************************************
 */
#ifndef __COMMON_H__
#define __COMMON_H__					        FW_TIME	// version
/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
/*  firmware build is updated every time with firmware using this header is compiled date   */
/*  time and buld number are result of executing batch files and vb script	is compiled	    */
/* 	                                                                                        */
/**/
#define FW_DATE                                	0x00260123
#define FW_TIME                        			0x00121915 
#define FW_BUILD_NUMBER                          12038

/* select application if preprocessor symbol no defined -----------------*/
//#define HOTEL_CONTROLLER
//#define ROOM_CONTROLLER
//#define ROOM_THERMOSTAT
//#define CARD_STACKER
//#define CARD_RW
//#define INTEGRATED_CONTROLLER

/* select type of application if preprocessor symbol not defined ------- */
//#define APPLICATION
//#define BOOTLOADER
//#define DEFAULT_LOADER

/* select options if preprocessor symbol not defined ------- */
//#define USE_FULL_ASSERT       // use for full assert and debug of hal parameters 
//#define USE_DEBUGGER          // if using serial wire debugger remap PA13 & PA14 to swdio & swclk, disable watchdog
//#define DEMO_MODE             // used for room controller demo presentation
//#define INIT_DEFAULT          // used to write hotel controller default value to eeprom
//#define USE_CONSTANT_IP       // use constant define ip addresse when first time initialize hotel controller dragon hardware
//#define USE_WATCHDOG          // enable watchdog timer
//#define OW_DS18B20            // enable Dallas DS18B20 onewire temperature sensor 
//#define GARAGE_ACCESS         // configure room controller as garage access controller

/* Configuration - Enable/Disable persistence per pin ------------------------*/
//#define BACKUP_PIN_1_ENABLE         1   // Enable persistence for Pin1 (GPIOC_12)
//#define BACKUP_PIN_2_ENABLE         1   // Enable persistence for Pin2 (GPIOD_2)
//#define BACKUP_PIN_3_ENABLE         1   // Enable persistence for Pin3 (GPIOC_8)
//#define BACKUP_PIN_4_ENABLE         1   // Enable persistence for Pin4 (GPIOD_4)
//#define BACKUP_PIN_5_ENABLE         1   // Enable persistence for Pin5 (GPIOC_11)
//#define BACKUP_PIN_6_ENABLE         1   // Enable persistence for Pin6 (GPIOC_10)

/* firmware switch */
#define VERS_INF_OFFSET                         0x2000      // address offset for firmware version info

/* hotel controller option switch  */
#ifdef  HOTEL_CONTROLLER
    #ifdef APPLICATION
        #ifdef USE_DEBUGGER
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #else
            #define VECT_TAB_OFFSET             0x10000 // offset for vector tabele remap
        #endif
        #define FW_TYPE                         HC_FW_APPL1
        #define FW_ADDR                         HC_APPL_ADDR
    #elif defined BOOTLOADER
        #define VECT_TAB_OFFSET                 0x0000 // offset for vector tabele remap
        #define FW_TYPE                         HC_FW_BLDR
        #define FW_ADDR                         HC_BLDR_ADDR
    #elif defined DEFAULT_LOADER
        #ifdef USE_DEBUGGER
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #else
            #define VECT_TAB_OFFSET             0x10000 // offset for vector tabele remap
        #endif
        #define FW_TYPE                         HC_FW_DLDR
        #define FW_ADDR                         HC_APPL_ADDR
    #endif
/* hotel controller constante  */
typedef enum    
{   /*  hotel controller firmware type versions */
    HC_FW_DBG       = 0x10000000, // version used with debugger tools, no watchdog, no vector offset 
    HC_FW_APPL1     = 0x11000000, // application firmware type 1, cannot be replaced by other four
    HC_FW_APPL2     = 0x12000000, // application firmware type 2, cannot be replaced by other four
    HC_FW_APPL3     = 0x13000000, // application firmware type 3, cannot be replaced by other four
    HC_FW_APPL4     = 0x14000000, // application firmware type 4, cannot be replaced by other four
    HC_FW_APPL5     = 0x14000000, // application firmware type 5, cannot be replaced by other four
    HC_FW_BLDR      = 0x15000000, // base bootloader used only to start next valid firmware by order, cannot be updated
    HC_FW_BLDR1     = 0x16000000, // bootloader firmware used to rewrite internal flash and update application
    HC_FW_DLDR      = 0x17000000, // default loader = fw type to write initial default values to eeprom, flash...
    HC_FW_DIAG      = 0x18000000, // service and diagnostic firmware used to check system components
    HC_FW_EXFL      = 0x19000000  // external flash binary 

}HCFW_TypeDef;

#define FLASH_ADDR                              0x08000000 // internal flash first memory address
#define FLASH_SIZE                              0x00100000 // STM32F429IG  internal flash 1MB
#define FLASH_END_ADDR                          0x08100000 // internal flash last memory address
#define EXT_RAM_ADDR                            0xD0000000 // external sram first memory address
#define EXT_RAM_SIZE                            0x00800000 // MT48LC4M32B2 external sram  8MB
#define EXT_RAM_END_ADDR                        0xD0800000 // external ram last memory address
#define EXT_FLASH_ADDR                          0x00000000 // external spi flash W25Q64 first memory address
#define EXT_FLASH_SIZE                          0x00800000 // external spi flash W25Q64 memory size  
#define EXT_FLASH_END_ADDR                      0x00800000 // external spi flash W25Q64 last memory address
#define END_LOAD_ADDR                           0x08100000 // last possible load address for firmware
#define HC_BLDR_ADDR                            0x08000000 // room controller bootloader start address
#define HC_BLDR_SIZE		                    0x00010000 // hotel controller bootloader size 32KB
#define HC_APPL_ADDR 		                    0x08010000 // hotel controller application start address
#define HC_APPL_SIZE      		                0x000F0000 // hotel controller application max. size

#define ADDR_FLASH_SECTOR_0                     ((uint32_t)0x08000000) /* Base @ of Sector 0, 16 Kbyte */
#define ADDR_FLASH_SECTOR_1                     ((uint32_t)0x08004000) /* Base @ of Sector 1, 16 Kbyte */
#define ADDR_FLASH_SECTOR_2                     ((uint32_t)0x08008000) /* Base @ of Sector 2, 16 Kbyte */
#define ADDR_FLASH_SECTOR_3                     ((uint32_t)0x0800C000) /* Base @ of Sector 3, 16 Kbyte */
#define ADDR_FLASH_SECTOR_4                     ((uint32_t)0x08010000) /* Base @ of Sector 4, 64 Kbyte */
#define ADDR_FLASH_SECTOR_5                     ((uint32_t)0x08020000) /* Base @ of Sector 5, 128 Kbyte */
#define ADDR_FLASH_SECTOR_6                     ((uint32_t)0x08040000) /* Base @ of Sector 6, 128 Kbyte */
#define ADDR_FLASH_SECTOR_7                     ((uint32_t)0x08060000) /* Base @ of Sector 7, 128 Kbyte */
#define ADDR_FLASH_SECTOR_8                     ((uint32_t)0x08080000) /* Base @ of Sector 8, 128 Kbyte */
#define ADDR_FLASH_SECTOR_9                     ((uint32_t)0x080A0000) /* Base @ of Sector 9, 128 Kbyte */
#define ADDR_FLASH_SECTOR_10                    ((uint32_t)0x080C0000) /* Base @ of Sector 10, 128 Kbyte */
#define ADDR_FLASH_SECTOR_11                    ((uint32_t)0x080E0000) /* Base @ of Sector 11, 128 Kbyte */
/* room controller option switch  */      
#elif defined  ROOM_CONTROLLER
    #ifdef APPLICATION
        #ifdef USE_DEBUGGER
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #else
            #define VECT_TAB_OFFSET             0x3000 // offset for vector tabele remap
        #endif
        #define FW_TYPE                         RC_FW_APPL1
        #define FW_ADDR                         RC_APPL_ADDR
    #elif defined BOOTLOADER
        #define VECT_TAB_OFFSET                 0x0000 // offset for vector tabele remap
        #define FW_TYPE                         RC_FW_BLDR
        #define FW_ADDR                         RC_BLDR_ADDR
    #elif defined DEFAULT_LOADER
        #ifdef USE_DEBUGGER
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #else
            #define VECT_TAB_OFFSET             0x3000 // offset for vector tabele remap
        #endif
        #define FW_TYPE                         RC_FW_DLDR
        #define FW_ADDR                         RC_APPL_ADDR
    #endif
/* room controller constante  */  
typedef enum    
{   /*  room controller firmware type versions */
    RC_FW_DBG       = 0x20000000, // version used with debugger tools, no watchdog, no vector offset 
    RC_FW_APPL1     = 0x21000000, // application firmware type 1, cannot be replaced by other four
    RC_FW_APPL2     = 0x22000000, // application firmware type 2, cannot be replaced by other four
    RC_FW_APPL3     = 0x23000000, // application firmware type 3, cannot be replaced by other four
    RC_FW_APPL4     = 0x24000000, // application firmware type 4, cannot be replaced by other four
    RC_FW_APPL5     = 0x24000000, // application firmware type 5, cannot be replaced by other four
    RC_FW_BLDR      = 0x25000000, // base bootloader used only to start next valid firmware by order, cannot be updated
    RC_FW_BLDR1     = 0x26000000, // bootloader firmware used to rewrite internal flash and update application
    RC_FW_DLDR      = 0x27000000, // default loader = fw type to write initial default values to eeprom, flash...
    RC_FW_DIAG      = 0x28000000, // service and diagnostic firmware used to check system components
    RC_FW_EXFL      = 0x29000000  // external flash binary 

}RCFW_TypeDef;

#define FLASH_ADDR                              0x08000000 // internal rom base addres
#define FLASH_SIZE                              0x00010000 // stm32f102  internal flash 64KB
#define FLASH_END_ADDR                          (FLASH_ADDR | FLASH_SIZE)
#define EXT_FLASH_ADDR                          0x00000000  // ext. flash start address
#define EXT_FLASH_SIZE                          0x08000000  // ext. flash 8 MByte available memory
#define EXT_FLASH_END_ADDR                      0x07FFFFFF  // ext. flash last address
#define END_LOAD_ADDR                           FLASH_END_ADDR	
#define RC_BLDR_ADDR                            0x08000000  // bootloader flash start address
#define RC_BLDR_SIZE		                    0x00003000  // bootloader flash size 12kB allocated
#define RC_APPL_ADDR 		                    0x08003000  // room controller application flash start address
#define RC_APPL_SIZE      		                0x0000D000  // room controller application flash size 52kB
#define SPIFL_IMG1_ADD                          ((uint32_t)0x00000000U)	// ROOM NUMBER      image 1
#define SPIFL_IMG2_ADD                          ((uint32_t)0x00030000U)	// DND ACTIV        image 2
#define SPIFL_IMG3_ADD				            ((uint32_t)0x00060000U)	// BEDDING  REQ     image 3
#define SPIFL_IMG4_ADD				            ((uint32_t)0x00090000U)	// CLEANING REQ     image 4
#define SPIFL_IMG5_ADD				            ((uint32_t)0x000C0000U)	// GENERAL  REQ     image 5
#define SPIFL_IMG6_ADD				            ((uint32_t)0x000F0000U)	// CARD VALID	    image 6
#define SPIFL_IMG7_ADD				            ((uint32_t)0x00120000U)	// CARD INVALID     image 7
#define SPIFL_IMG8_ADD				            ((uint32_t)0x00150000U)	// WRONG ROOM       image 8
#define SPIFL_IMG9_ADD				            ((uint32_t)0x00180000U)	// TIME EXPIRED     image 9
#define SPIFL_IMG10_ADD                         ((uint32_t)0x001B0000U)	// FIRE ALARM       image 10
#define SPIFL_IMG11_ADD                         ((uint32_t)0x001E0000U)	// FIRE EXIT        image 11
#define SPIFL_IMG12_ADD                         ((uint32_t)0x00210000U)	// MINIBAR USED     image 12
#define SPIFL_IMG13_ADD                         ((uint32_t)0x00240000U)	// ROOM UNUSABLE    image 13
#define SPIFL_IMG14_ADD                         ((uint32_t)0x00270000u)	// SOS ALARM        image 14
#define SPIFL_IMG15_ADD                         ((uint32_t)0x002A0000U)	// SPI flash - new  image 15
#define SPIFL_IMG16_ADD                         ((uint32_t)0x002D0000U)	// SPI flash - new  image 16
#define SPIFL_IMG17_ADD                         ((uint32_t)0x00300000U)	// SPI flash - new  image 17
#define SPIFL_IMG18_ADD                         ((uint32_t)0x00330000U)	// SPI flash - new  image 18
#define SPIFL_IMG19_ADD                         ((uint32_t)0x00360000U)	// SPI flash - new  image 19
#define RC_NEW_APPL_ADDR     	                ((uint32_t)0x00390000U) // NEW FIRMWARE     image 20
#define RC_NEW_BLDR_ADDR                        ((uint32_t)0x003C0000U) // NEW BOOTLOADER   image 21
#define RC_SML_FONT_ADDR                        ((uint32_t)0x003F0000U)	// SMALL SIZE FONTS  198kB
#define RC_MID_FONT_ADDR                        ((uint32_t)0x00420000U)	// MIDDLE SIZE FONTS 64kB
#define RC_BIG_DIGIT_ADDR                       ((uint32_t)0x00440000U)	// BIG SIZE DIGITS   128kB
#define RC_BIG_FONT_ADDR                        ((uint32_t)0x00450000U)	// BIG SIZE FONTS    198kB
#define RC_NEW_IMAGE_ADD                        ((uint32_t)0x00480000U)	// NEW DISPLAY IMAGE image 1 ~ image 19
#define RC_BKP_APPL_ADDR                        ((uint32_t)0x004B0000U)	// FIRMWARE BACKUP   save beforre erase 
#define RC_BKP_BLDR_ADDR                        ((uint32_t)0x004E0000U)	// BOOTLOADER BACKUP save beforre erase 
#define RC_DEF_APPL_ADDR                        ((uint32_t)0x00510000U)	// DEFAULT FIRMWARE   copy of first firmware for recovery of chatastrofic update failure 
#define RC_DEF_BLDR_ADDR                        ((uint32_t)0x00540000U)	// DEFAULT BOOTLOADER copy of first bootloader for recovery of chatastrofic bootloader failure 
#define RC_BLDR_VERS_ADDR                       (RC_BLDR_ADDR     | VERS_INF_OFFSET)  // running bootloader version info address
#define RC_APPL_VERS_ADDR                       (RC_APPL_ADDR     | VERS_INF_OFFSET)  // running firmware version info address
#define RC_DEF_BLDR_VERS_ADDR                   (RC_DEF_BLDR_ADDR | VERS_INF_OFFSET)  // default bootloader version info address
#define RC_DEF_APPL_VERS_ADDR                   (RC_DEF_APPL_ADDR | VERS_INF_OFFSET)  // running firmware version info address
#define RC_BKP_BLDR_VERS_ADDR                   (RC_BKP_BLDR_ADDR | VERS_INF_OFFSET)  // new bootloader version info address
#define RC_BKP_APPL_VERS_ADDR                   (RC_BKP_APPL_ADDR | VERS_INF_OFFSET)  // default firmware version info address
#define RC_NEW_BLDR_VERS_ADDR                   (RC_NEW_BLDR_ADDR | VERS_INF_OFFSET)  // new bootloader version info address
#define RC_NEW_APPL_VERS_ADDR                   (RC_NEW_APPL_ADDR | VERS_INF_OFFSET)  // new firmware version info address
/* Base address of the Flash sectors */
#define ADDR_FLASH_PAGE_0                       ((uint32_t)0x08000000) /* Base @ of Page 0, 1 Kbytes */
#define ADDR_FLASH_PAGE_1                       ((uint32_t)0x08000400) /* Base @ of Page 1, 1 Kbytes */
#define ADDR_FLASH_PAGE_2                       ((uint32_t)0x08000800) /* Base @ of Page 2, 1 Kbytes */
#define ADDR_FLASH_PAGE_3                       ((uint32_t)0x08000C00) /* Base @ of Page 3, 1 Kbytes */
#define ADDR_FLASH_PAGE_4                       ((uint32_t)0x08001000) /* Base @ of Page 4, 1 Kbytes */
#define ADDR_FLASH_PAGE_5                       ((uint32_t)0x08001400) /* Base @ of Page 5, 1 Kbytes */
#define ADDR_FLASH_PAGE_6                       ((uint32_t)0x08001800) /* Base @ of Page 6, 1 Kbytes */
#define ADDR_FLASH_PAGE_7                       ((uint32_t)0x08001C00) /* Base @ of Page 7, 1 Kbytes */
#define ADDR_FLASH_PAGE_8                       ((uint32_t)0x08002000) /* Base @ of Page 8, 1 Kbytes */
#define ADDR_FLASH_PAGE_9                       ((uint32_t)0x08002400) /* Base @ of Page 9, 1 Kbytes */
#define ADDR_FLASH_PAGE_10                      ((uint32_t)0x08002800) /* Base @ of Page 10, 1 Kbytes */
#define ADDR_FLASH_PAGE_11                      ((uint32_t)0x08002C00) /* Base @ of Page 11, 1 Kbytes */
#define ADDR_FLASH_PAGE_12                      ((uint32_t)0x08003000) /* Base @ of Page 12, 1 Kbytes */
#define ADDR_FLASH_PAGE_13                      ((uint32_t)0x08003400) /* Base @ of Page 13, 1 Kbytes */
#define ADDR_FLASH_PAGE_14                      ((uint32_t)0x08003800) /* Base @ of Page 14, 1 Kbytes */
#define ADDR_FLASH_PAGE_15                      ((uint32_t)0x08003C00) /* Base @ of Page 15, 1 Kbytes */
#define ADDR_FLASH_PAGE_16                      ((uint32_t)0x08004000) /* Base @ of Page 16, 1 Kbytes */
#define ADDR_FLASH_PAGE_17                      ((uint32_t)0x08004400) /* Base @ of Page 17, 1 Kbytes */
#define ADDR_FLASH_PAGE_18                      ((uint32_t)0x08004800) /* Base @ of Page 18, 1 Kbytes */
#define ADDR_FLASH_PAGE_19                      ((uint32_t)0x08004C00) /* Base @ of Page 19, 1 Kbytes */
#define ADDR_FLASH_PAGE_20                      ((uint32_t)0x08005000) /* Base @ of Page 20, 1 Kbytes */
#define ADDR_FLASH_PAGE_21                      ((uint32_t)0x08005400) /* Base @ of Page 21, 1 Kbytes */
#define ADDR_FLASH_PAGE_22                      ((uint32_t)0x08005800) /* Base @ of Page 22, 1 Kbytes */
#define ADDR_FLASH_PAGE_23                      ((uint32_t)0x08005C00) /* Base @ of Page 23, 1 Kbytes */
#define ADDR_FLASH_PAGE_24                      ((uint32_t)0x08006000) /* Base @ of Page 24, 1 Kbytes */
#define ADDR_FLASH_PAGE_25                      ((uint32_t)0x08006400) /* Base @ of Page 25, 1 Kbytes */
#define ADDR_FLASH_PAGE_26                      ((uint32_t)0x08006800) /* Base @ of Page 26, 1 Kbytes */
#define ADDR_FLASH_PAGE_27                      ((uint32_t)0x08006C00) /* Base @ of Page 27, 1 Kbytes */
#define ADDR_FLASH_PAGE_28                      ((uint32_t)0x08007000) /* Base @ of Page 28, 1 Kbytes */
#define ADDR_FLASH_PAGE_29                      ((uint32_t)0x08007400) /* Base @ of Page 29, 1 Kbytes */
#define ADDR_FLASH_PAGE_30                      ((uint32_t)0x08007800) /* Base @ of Page 30, 1 Kbytes */
#define ADDR_FLASH_PAGE_31                      ((uint32_t)0x08007C00) /* Base @ of Page 31, 1 Kbytes */
#define ADDR_FLASH_PAGE_32                      ((uint32_t)0x08008000) /* Base @ of Page 32, 1 Kbytes */
#define ADDR_FLASH_PAGE_33                      ((uint32_t)0x08008400) /* Base @ of Page 33, 1 Kbytes */
#define ADDR_FLASH_PAGE_34                      ((uint32_t)0x08008800) /* Base @ of Page 34, 1 Kbytes */
#define ADDR_FLASH_PAGE_35                      ((uint32_t)0x08008C00) /* Base @ of Page 35, 1 Kbytes */
#define ADDR_FLASH_PAGE_36                      ((uint32_t)0x08009000) /* Base @ of Page 36, 1 Kbytes */
#define ADDR_FLASH_PAGE_37                      ((uint32_t)0x08009400) /* Base @ of Page 37, 1 Kbytes */
#define ADDR_FLASH_PAGE_38                      ((uint32_t)0x08009800) /* Base @ of Page 38, 1 Kbytes */
#define ADDR_FLASH_PAGE_39                      ((uint32_t)0x08009C00) /* Base @ of Page 39, 1 Kbytes */
#define ADDR_FLASH_PAGE_40                      ((uint32_t)0x0800A000) /* Base @ of Page 40, 1 Kbytes */
#define ADDR_FLASH_PAGE_41                      ((uint32_t)0x0800A400) /* Base @ of Page 41, 1 Kbytes */
#define ADDR_FLASH_PAGE_42                      ((uint32_t)0x0800A800) /* Base @ of Page 42, 1 Kbytes */
#define ADDR_FLASH_PAGE_43                      ((uint32_t)0x0800AC00) /* Base @ of Page 43, 1 Kbytes */
#define ADDR_FLASH_PAGE_44                      ((uint32_t)0x0800B000) /* Base @ of Page 44, 1 Kbytes */
#define ADDR_FLASH_PAGE_45                      ((uint32_t)0x0800B400) /* Base @ of Page 45, 1 Kbytes */
#define ADDR_FLASH_PAGE_46                      ((uint32_t)0x0800B800) /* Base @ of Page 46, 1 Kbytes */
#define ADDR_FLASH_PAGE_47                      ((uint32_t)0x0800BC00) /* Base @ of Page 47, 1 Kbytes */
#define ADDR_FLASH_PAGE_48                      ((uint32_t)0x0800C000) /* Base @ of Page 48, 1 Kbytes */
#define ADDR_FLASH_PAGE_49                      ((uint32_t)0x0800C400) /* Base @ of Page 49, 1 Kbytes */
#define ADDR_FLASH_PAGE_50                      ((uint32_t)0x0800C800) /* Base @ of Page 50, 1 Kbytes */
#define ADDR_FLASH_PAGE_51                      ((uint32_t)0x0800CC00) /* Base @ of Page 51, 1 Kbytes */
#define ADDR_FLASH_PAGE_52                      ((uint32_t)0x0800D000) /* Base @ of Page 52, 1 Kbytes */
#define ADDR_FLASH_PAGE_53                      ((uint32_t)0x0800D400) /* Base @ of Page 53, 1 Kbytes */
#define ADDR_FLASH_PAGE_54                      ((uint32_t)0x0800D800) /* Base @ of Page 54, 1 Kbytes */
#define ADDR_FLASH_PAGE_55                      ((uint32_t)0x0800DC00) /* Base @ of Page 55, 1 Kbytes */
#define ADDR_FLASH_PAGE_56                      ((uint32_t)0x0800E000) /* Base @ of Page 56, 1 Kbytes */
#define ADDR_FLASH_PAGE_57                      ((uint32_t)0x0800E400) /* Base @ of Page 57, 1 Kbytes */
#define ADDR_FLASH_PAGE_58                      ((uint32_t)0x0800E800) /* Base @ of Page 58, 1 Kbytes */
#define ADDR_FLASH_PAGE_59                      ((uint32_t)0x0800EC00) /* Base @ of Page 59, 1 Kbytes */
#define ADDR_FLASH_PAGE_60                      ((uint32_t)0x0800F000) /* Base @ of Page 60, 1 Kbytes */
#define ADDR_FLASH_PAGE_61                      ((uint32_t)0x0800F400) /* Base @ of Page 61, 1 Kbytes */
#define ADDR_FLASH_PAGE_62                      ((uint32_t)0x0800F800) /* Base @ of Page 62, 1 Kbytes */
#define ADDR_FLASH_PAGE_63                      ((uint32_t)0x0800FC00) /* Base @ of Page 63, 1 Kbytes */
#define ADDR_FLASH_PAGE_64                      ((uint32_t)0x08010000) /* Base @ of Page 64, 1 Kbytes */
#define ADDR_FLASH_PAGE_65                      ((uint32_t)0x08010400) /* Base @ of Page 65, 1 Kbytes */
#define ADDR_FLASH_PAGE_66                      ((uint32_t)0x08010800) /* Base @ of Page 66, 1 Kbytes */
#define ADDR_FLASH_PAGE_67                      ((uint32_t)0x08010C00) /* Base @ of Page 67, 1 Kbytes */
#define ADDR_FLASH_PAGE_68                      ((uint32_t)0x08011000) /* Base @ of Page 68, 1 Kbytes */
#define ADDR_FLASH_PAGE_69                      ((uint32_t)0x08011400) /* Base @ of Page 69, 1 Kbytes */
#define ADDR_FLASH_PAGE_70                      ((uint32_t)0x08011800) /* Base @ of Page 70, 1 Kbytes */
#define ADDR_FLASH_PAGE_71                      ((uint32_t)0x08011C00) /* Base @ of Page 71, 1 Kbytes */
#define ADDR_FLASH_PAGE_72                      ((uint32_t)0x08012000) /* Base @ of Page 72, 1 Kbytes */
#define ADDR_FLASH_PAGE_73                      ((uint32_t)0x08012400) /* Base @ of Page 73, 1 Kbytes */
#define ADDR_FLASH_PAGE_74                      ((uint32_t)0x08012800) /* Base @ of Page 74, 1 Kbytes */
#define ADDR_FLASH_PAGE_75                      ((uint32_t)0x08012C00) /* Base @ of Page 75, 1 Kbytes */
#define ADDR_FLASH_PAGE_76                      ((uint32_t)0x08013000) /* Base @ of Page 76, 1 Kbytes */
#define ADDR_FLASH_PAGE_77                      ((uint32_t)0x08013400) /* Base @ of Page 77, 1 Kbytes */
#define ADDR_FLASH_PAGE_78                      ((uint32_t)0x08013800) /* Base @ of Page 78, 1 Kbytes */
#define ADDR_FLASH_PAGE_79                      ((uint32_t)0x08013C00) /* Base @ of Page 79, 1 Kbytes */
#define ADDR_FLASH_PAGE_80                      ((uint32_t)0x08014000) /* Base @ of Page 80, 1 Kbytes */
#define ADDR_FLASH_PAGE_81                      ((uint32_t)0x08014400) /* Base @ of Page 81, 1 Kbytes */
#define ADDR_FLASH_PAGE_82                      ((uint32_t)0x08014800) /* Base @ of Page 82, 1 Kbytes */
#define ADDR_FLASH_PAGE_83                      ((uint32_t)0x08014C00) /* Base @ of Page 83, 1 Kbytes */
#define ADDR_FLASH_PAGE_84                      ((uint32_t)0x08015000) /* Base @ of Page 84, 1 Kbytes */
#define ADDR_FLASH_PAGE_85                      ((uint32_t)0x08015400) /* Base @ of Page 85, 1 Kbytes */
#define ADDR_FLASH_PAGE_86                      ((uint32_t)0x08015800) /* Base @ of Page 86, 1 Kbytes */
#define ADDR_FLASH_PAGE_87                      ((uint32_t)0x08015C00) /* Base @ of Page 87, 1 Kbytes */
#define ADDR_FLASH_PAGE_88                      ((uint32_t)0x08016000) /* Base @ of Page 88, 1 Kbytes */
#define ADDR_FLASH_PAGE_89                      ((uint32_t)0x08016400) /* Base @ of Page 89, 1 Kbytes */
#define ADDR_FLASH_PAGE_90                      ((uint32_t)0x08016800) /* Base @ of Page 90, 1 Kbytes */
#define ADDR_FLASH_PAGE_91                      ((uint32_t)0x08016C00) /* Base @ of Page 91, 1 Kbytes */
#define ADDR_FLASH_PAGE_92                      ((uint32_t)0x08017000) /* Base @ of Page 92, 1 Kbytes */
#define ADDR_FLASH_PAGE_93                      ((uint32_t)0x08017400) /* Base @ of Page 93, 1 Kbytes */
#define ADDR_FLASH_PAGE_94                      ((uint32_t)0x08017800) /* Base @ of Page 94, 1 Kbytes */
#define ADDR_FLASH_PAGE_95                      ((uint32_t)0x08017C00) /* Base @ of Page 95, 1 Kbytes */
#define ADDR_FLASH_PAGE_96                      ((uint32_t)0x08018000) /* Base @ of Page 96, 1 Kbytes */
#define ADDR_FLASH_PAGE_97                      ((uint32_t)0x08018400) /* Base @ of Page 97, 1 Kbytes */
#define ADDR_FLASH_PAGE_98                      ((uint32_t)0x08018800) /* Base @ of Page 98, 1 Kbytes */
#define ADDR_FLASH_PAGE_99                      ((uint32_t)0x08018C00) /* Base @ of Page 99, 1 Kbytes */
#define ADDR_FLASH_PAGE_100                     ((uint32_t)0x08019000) /* Base @ of Page 100, 1 Kbytes */
#define ADDR_FLASH_PAGE_101                     ((uint32_t)0x08019400) /* Base @ of Page 101, 1 Kbytes */
#define ADDR_FLASH_PAGE_102                     ((uint32_t)0x08019800) /* Base @ of Page 102, 1 Kbytes */
#define ADDR_FLASH_PAGE_103                     ((uint32_t)0x08019C00) /* Base @ of Page 103, 1 Kbytes */
#define ADDR_FLASH_PAGE_104                     ((uint32_t)0x0801A000) /* Base @ of Page 104, 1 Kbytes */
#define ADDR_FLASH_PAGE_105                     ((uint32_t)0x0801A400) /* Base @ of Page 105, 1 Kbytes */
#define ADDR_FLASH_PAGE_106                     ((uint32_t)0x0801A800) /* Base @ of Page 106, 1 Kbytes */
#define ADDR_FLASH_PAGE_107                     ((uint32_t)0x0801AC00) /* Base @ of Page 107, 1 Kbytes */
#define ADDR_FLASH_PAGE_108                     ((uint32_t)0x0801B000) /* Base @ of Page 108, 1 Kbytes */
#define ADDR_FLASH_PAGE_109                     ((uint32_t)0x0801B400) /* Base @ of Page 109, 1 Kbytes */
#define ADDR_FLASH_PAGE_110                     ((uint32_t)0x0801B800) /* Base @ of Page 110, 1 Kbytes */
#define ADDR_FLASH_PAGE_111                     ((uint32_t)0x0801BC00) /* Base @ of Page 111, 1 Kbytes */
#define ADDR_FLASH_PAGE_112                     ((uint32_t)0x0801C000) /* Base @ of Page 112, 1 Kbytes */
#define ADDR_FLASH_PAGE_113                     ((uint32_t)0x0801C400) /* Base @ of Page 113, 1 Kbytes */
#define ADDR_FLASH_PAGE_114                     ((uint32_t)0x0801C800) /* Base @ of Page 114, 1 Kbytes */
#define ADDR_FLASH_PAGE_115                     ((uint32_t)0x0801CC00) /* Base @ of Page 115, 1 Kbytes */
#define ADDR_FLASH_PAGE_116                     ((uint32_t)0x0801D000) /* Base @ of Page 116, 1 Kbytes */
#define ADDR_FLASH_PAGE_117                     ((uint32_t)0x0801D400) /* Base @ of Page 117, 1 Kbytes */
#define ADDR_FLASH_PAGE_118                     ((uint32_t)0x0801D800) /* Base @ of Page 118, 1 Kbytes */
#define ADDR_FLASH_PAGE_119                     ((uint32_t)0x0801DC00) /* Base @ of Page 119, 1 Kbytes */
#define ADDR_FLASH_PAGE_120                     ((uint32_t)0x0801E000) /* Base @ of Page 120, 1 Kbytes */
#define ADDR_FLASH_PAGE_121                     ((uint32_t)0x0801E400) /* Base @ of Page 121, 1 Kbytes */
#define ADDR_FLASH_PAGE_122                     ((uint32_t)0x0801E800) /* Base @ of Page 122, 1 Kbytes */
#define ADDR_FLASH_PAGE_123                     ((uint32_t)0x0801EC00) /* Base @ of Page 123, 1 Kbytes */
#define ADDR_FLASH_PAGE_124                     ((uint32_t)0x0801F000) /* Base @ of Page 124, 1 Kbytes */
#define ADDR_FLASH_PAGE_125                     ((uint32_t)0x0801F400) /* Base @ of Page 125, 1 Kbytes */
#define ADDR_FLASH_PAGE_126                     ((uint32_t)0x0801F800) /* Base @ of Page 126, 1 Kbytes */
#define ADDR_FLASH_PAGE_127                     ((uint32_t)0x0801FC00) /* Base @ of Page 127, 1 Kbytes */
/* Define bitmap representing user flash area that could be write protected (check restricted to pages 16-63 = app_code). */
#define RC_FL_PAGE2PROT     (OB_WRP_PAGES16TO19 | OB_WRP_PAGES20TO23 | OB_WRP_PAGES24TO27 | OB_WRP_PAGES28TO31 | \
                             OB_WRP_PAGES32TO35 | OB_WRP_PAGES36TO39 | OB_WRP_PAGES40TO43 | OB_WRP_PAGES44TO47 | \
                             OB_WRP_PAGES48TO51 | OB_WRP_PAGES52TO55 | OB_WRP_PAGES56TO59 | OB_WRP_PAGES60TO63)  
/* room thermostat option switch  */   
#elif defined  ROOM_THERMOSTAT
    #ifdef APPLICATION
        #ifdef USE_DEBUGGER
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #else
            #define VECT_TAB_OFFSET             0x10000 // offset for vector tabele remap
        #endif
        #define FW_TYPE                         RT_FW_APPL1
        #define FW_ADDR                         RT_APPL_ADDR
    #elif defined BOOTLOADER
        #define VECT_TAB_OFFSET                 0x0000 // offset for vector tabele remap
        #define FW_TYPE                         RT_FW_BLDR
        #define FW_ADDR                         RT_BLDR_ADDR
    #elif defined DEFAULT_LOADER
        #ifdef USE_DEBUGGER
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #else
            #define VECT_TAB_OFFSET             0x10000 // offset for vector tabele remap
        #endif
        #define FW_TYPE                         RT_FW_DLDR
        #define FW_ADDR                         RT_APPL_ADDR
    #endif
/* room thermostat constante  */
typedef enum    
{   /*  room thermostat firmware type versions */
    RT_FW_DBG       = 0x30000000, // version used with debugger tools, no watchdog, no vector offset 
    RT_FW_APPL1     = 0x31000000, // application firmware type 1, cannot be replaced by other four
    RT_FW_APPL2     = 0x32000000, // application firmware type 2, cannot be replaced by other four
    RT_FW_APPL3     = 0x33000000, // application firmware type 3, cannot be replaced by other four
    RT_FW_APPL4     = 0x34000000, // application firmware type 4, cannot be replaced by other four
    RT_FW_APPL5     = 0x34000000, // application firmware type 5, cannot be replaced by other four
    RT_FW_BLDR      = 0x35000000, // base bootloader used only to start next valid firmware by order, cannot be updated
    RT_FW_BLDR1     = 0x36000000, // bootloader firmware used to rewrite internal flash and update application
    RT_FW_DLDR      = 0x37000000, // default loader = fw type to write initial default values to eeprom, flash...
    RT_FW_DIAG      = 0x38000000, // service and diagnostic firmware used to check system components
    RT_FW_EXFL      = 0x39000000  // external flash binary 

}RTFW_TypeDef;

#define FLASH_ADDR                              0x08000000 // internal rom base addres
#define FLASH_SIZE                              0x00100000 // STM32F746ZG  internal flash 1MB
#define FLASH_END_ADDR                          (FLASH_ADDR | FLASH_SIZE) // room thermostat stm32f746 flash end address 1MB
#define RAM_ADDR                                0x20000000 // internal ram base address
#define RAM_SIZE                                0x00050000 // STM32F746ZG  internal sram  320kB
#define RAM_END_ADDR                            (RAM_ADDR | RAM_SIZE)
#define EXT_FLASH_ADDR                          0x90000000 // external rom base address
#define EXT_FLASH_SIZE                          0x01000000 // N25Q128 QSPI external flash 16MB
#define EXT_FLASH_END_ADDR                      (EXT_FLASH_ADDR | EXT_FLASH_SIZE)
#define EXT_RAM_ADDR                            0xC0000000 // external sram base address
#define EXT_RAM_SIZE                            0x00800000 // MT48LC4M32B2 external sram  8MB
#define EXT_RAM_END_ADDR                        (EXT_RAM_ADDR | EXT_RAM_SIZE)
#define END_LOAD_ADDR                           EXT_FLASH_END_ADDR
#define RT_BLDR_ADDR                            0x08000000 // room thermostat bootloader start address
#define RT_BLDR_SIZE                            0x00010000  // bootloader max. size 64kB
#define RT_APPL_ADDR                            0x08010000 // room thermostat application start address
#define RT_APPL_SIZE                            0x000F0000 // application firmware max. size 960kB
#define RT_LOGO_ADDR                            0x90D80000 // room thermostat user logo.png start address
#define RT_LOGO_SIZE                            0x00080000 // logo image max. size 144,000‬ bytes    (360px x 100px x 32bpp)
#define RT_DSPIMG_SIZE                          0x00080000 // display image max. size 524,288‬ bytes (480px x 272px x 16bpp)
#define RT_BLDR_BKP_ADDR                        0x90E00000 // room thermostat bootloader backup start address
#define RT_APPL_BKP_ADDR                        0x90E20000 // room thermostat application backup start address
#define RT_NEW_FILE_ADDR                        0x90F00000 // room thermostat new file storage start address
#define RT_DEF_BLDR_ADDR                        RT_BLDR_BKP_ADDR    // copy of first programmed bootloader for last recovery option
#define RT_DEF_APPL_ADDR                        RT_APPL_BKP_ADDR    // copy of firts programmed application for last recovery option
#define RT_BLDR_VERS_ADDR                       (RT_BLDR_ADDR     | VERS_INF_OFFSET)  // bootloader version info address
#define RT_APPL_VERS_ADDR                       (RT_APPL_ADDR     | VERS_INF_OFFSET)  // application version info address
#define RT_DEF_BLDR_VERS_ADDR                   (RT_DEF_BLDR_ADDR | VERS_INF_OFFSET)  // default bootloader version info address
#define RT_DEF_APPL_VERS_ADDR                   (RT_DEF_APPL_ADDR | VERS_INF_OFFSET)  // running firmware version info address
#define RT_BLDR_BKP_VERS_ADDR                   (RT_BLDR_BKP_ADDR | VERS_INF_OFFSET)  // new bootloader version info address
#define RT_APPL_BKP_VERS_ADDR                   (RT_APPL_BKP_ADDR | VERS_INF_OFFSET)  // default firmware version info address
#define RT_NEW_FILE_VERS_ADDR                   (RT_NEW_FILE_ADDR | VERS_INF_OFFSET)  // new firmware version info address
/* Base address of the Flash sectors */
#define RT_ADDR_FLSECT_0                        0x08000000 /* Base address of Sector 0, 32 Kbytes */
#define RT_ADDR_FLSECT_1                        0x08008000 /* Base address of Sector 1, 32 Kbytes */
#define RT_ADDR_FLSECT_2                        0x08010000 /* Base address of Sector 2, 32 Kbytes */
#define RT_ADDR_FLSECT_3                        0x08018000 /* Base address of Sector 3, 32 Kbytes */
#define RT_ADDR_FLSECT_4                        0x08020000 /* Base address of Sector 4, 128 Kbytes */
#define RT_ADDR_FLSECT_5                        0x08040000 /* Base address of Sector 5, 256 Kbytes */
#define RT_ADDR_FLSECT_6                        0x08080000 /* Base address of Sector 6, 256 Kbytes */
#define RT_ADDR_FLSECT_7                        0x080C0000 /* Base address of Sector 7, 256 Kbytes */
/* Define bitmap representing user flash area that could be write protected (check restricted to pages 8-39). */
#define RT_FL_SECT4PROT     (OB_WRP_SECTOR_0 | OB_WRP_SECTOR_1 | OB_WRP_SECTOR_2 | OB_WRP_SECTOR_3 |\
                             OB_WRP_SECTOR_4 | OB_WRP_SECTOR_5 | OB_WRP_SECTOR_6 | OB_WRP_SECTOR_7)
/* room card stacker option switch  */
#elif defined  CARD_STACKER
    #ifdef APPLICATION
        #ifdef USE_DEBUGGER
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #else
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #endif
        #define FW_TYPE                         CS_FW_APPL1
        #define FW_ADDR                         CS_APPL_ADDR
    #elif defined BOOTLOADER
        #define VECT_TAB_OFFSET                 0x0000 // offset for vector tabele remap
        #define FW_TYPE                         CS_FW_BLDR
        #define FW_ADDR                         CS_BLDR_ADDR
    #elif defined DEFAULT_LOADER
        #ifdef USE_DEBUGGER
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #else
            #define VECT_TAB_OFFSET             0x0000 // offset for vector tabele remap
        #endif
        #define FW_TYPE                         CS_FW_DLDR
        #define FW_ADDR                         CS_APPL_ADDR
    #endif
/* card stacker constante  */
typedef enum
{ /*  card stacker firmware type versions */
	CS_FW_DBG   = 0x40000000, // version used with debugger tools, no watchdog, no vector offset
	CS_FW_APPL1 = 0x41000000, // application firmware type 1, cannot be replaced by other four
	CS_FW_APPL2 = 0x42000000, // application firmware type 2, cannot be replaced by other four
	CS_FW_APPL3 = 0x43000000, // application firmware type 3, cannot be replaced by other four
	CS_FW_APPL4 = 0x44000000, // application firmware type 4, cannot be replaced by other four
	CS_FW_APPL5 = 0x44000000, // application firmware type 5, cannot be replaced by other four
	CS_FW_BLDR  = 0x45000000, // bootloader to start next valid firmware by order, cannot be updated
	CS_FW_BLDR1 = 0x46000000, // bootloader firmware used to rewrite internal flash and update application
	CS_FW_DLDR  = 0x47000000, // default loader = fw type to write initial default values to eeprom, flash...
	CS_FW_DIAG  = 0x48000000, // service and diagnostic firmware used to check system components
	CS_FW_EXFL  = 0x49000000  // external flash binary

} CSFW_TypeDef;
#ifdef VERS_INF_OFFSET
#undef VERS_INF_OFFSET
#endif
#define VERS_INF_OFFSET	0xFF0

#define FLASH_ADDR                              0x08000000 	// internal rom base addres
#define FLASH_SIZE                              0x00004000 	// STM32F030x6  internal flash 16kB
#define FLASH_END_ADDR                          0x08004000 	// room thermostat STM32F030x6 flash end address 16kB
#define RAM_ADDR                                0x20000000 	// internal ram base address
#define RAM_SIZE                                0x00001000 	// STM32F030x6  internal sram  4kB
#define RAM_END_ADDR                            0x20001000
#define CS_BLDR_ADDR                            0x08000000	// card stacker bootloader start address
#define CS_BLDR_SIZE                            0x00001000	// bootloader max. size 4kB
#define CS_APPL_ADDR                            0x08000000 	// card stacker application start address
#define CS_APPL_SIZE                            0x00004000 	// application firmware max. size 16kB
#define CS_BLDR_VERS_ADDR                       (CS_BLDR_ADDR     | VERS_INF_OFFSET)  // bootloader version info address
#define CS_APPL_VERS_ADDR                       (CS_APPL_ADDR     | VERS_INF_OFFSET)  // application version info address
#define END_LOAD_ADDR                           FLASH_END_ADDR
/* switch error  */
#else
    #error "application not selected"
#endif
/* Exported constants --------------------------------------------------------*/
/** ============================================================================*/
/**         T C P I P       I N T E R F A C E       D E F A U L T               */
/** ============================================================================*/
#define NETBIOS_LWIP_NAME               "HOTEL_CTRL1"    // netbios network name http://HOTEL_CTRL to find device on same network
#define DEF_SYSID                       43962   // default system id 0xabba hex
#define DEF_PASSW                       43962   // default password  0xabba hex
#define DEF_SRVC_PSWRD                  43962   // default service password  0xabba hex
#define DEF_MAID_PSWRD                  1548    // default handmaid password
#define DEF_MNGR_PSWRD                  2637    // default manager password
#define IPADDR0                         192     // ip address msb
#define IPADDR1                         168     // ip address
#define IPADDR2                         0U      // ip address
#define IPADDR3                         200U    // ip address lsb 
#define SUBNET0                         255U    // ip subnet mask msb
#define SUBNET1                         255U    // ip subnet mask
#define SUBNET2                         255U    // ip subnet mask
#define SUBNET3                         0U      // ip subnet mask lsb
#define GWADDR0                         192U    // ip gateway address msb
#define GWADDR1                         168U    // ip gateway address
#define GWADDR2                         0U      // ip gateway address
#define GWADDR3                         1U      // ip gateway address lsb
#define MACADDR0                        2U      // Ethernet interface hardware MAC address msb  
#define MACADDR1                        1U      // MAC address
#define MACADDR2                        2U      // MAC address
#define MACADDR3                        0U      // MAC address
#define MACADDR4                        0U      // MAC address
#define MACADDR5                        0U      // Ethernet interface hardware MAC address lsb  
/** ============================================================================*/
/**             R S 4 8 5       I N T E R F A C E       D E F A U L T           */
/** ============================================================================*/
#define DEF_RSBPS                       115200U // default rs485 interface baudrate
#define DEF_RSBRA                       255U  // 0x9999 default rs485 interface broadcast address
#define DEF_HC_RSGRA	                35224U	// 0x8998 hotel controller default rs485 group address
#define DEF_RT_RSGRA                    30855U	// 0x7887 room thermostat default rs485 group address
#define DEF_RC_RSGRA                    26486U 	// 0x6776 room controller default rs485 group address
#define DEF_NEW_RSIFA                   65535U  // 0xFFFF new device default rs485 interface address
#define LST_DEV_RSIFA                   65534U  // room/device last possible rs485 interface addres
#define FST_DEV_RSIFA                   10U     // room/device first possible rs485 interface addres
#define LST_HC_RSIFA                    9U      // hotel controller last possible rs485 interface addres
#define FST_HC_RSIFA                    5U      // hotel controller first possible rs485 interface addres
#define LST_EX_RSIFA                    4U      // expander/bridge last possible rs485 interface address
#define FST_EX_RSIFA                    1U      // expander/bridge first possible rs485 interface address
#define DEF_TFBRA                       255     // default broadcast address
#define DEF_TFGRA                       254     // default group address
#define DEF_TFGWA                       1       // default gateway address
#define DEF_TFBPS                       115200  // default interface baudrate
/** ============================================================================*/
/**         O N E W I R E       I N T E R F A C E       D E F A U L T           */
/** ============================================================================*/
#define DEF_OWBPS                       2   	// default onewire interface baudrate
#define DEF_OWBRA                       127U    // default onewire broadcast address
#define DEF_HC_OWGRA                    51U     // hotel controller default onewire group address
#define DEF_RC_OWGRA                    41U     // room controller default onewire group address
#define DEF_CR_OWGRA                    33U     // indoor card reader default onewire group addres
#define DEF_CS_OWGRA                    32U     // room  card stacker default onewire group address
#define DEF_RT_OWGRA                    31U     // room thermostat default onewire group address
#define DEF_HC_OWIFA                    21U     // hotel controller default onewire interface address
#define DEF_RC_OWIFA                    11U     // room controller default interface address
#define DEF_IC_OWIFA                    4U     	//
#define DEF_CR_OWIFA                    3U     	// RC integrated card reader/writer default interface address
#define DEF_CS_OWIFA                    2U     	// room  card stacker default interface address
#define DEF_RT_OWIFA                    1U      // room thermostat default interface address
/** ============================================================================*/
/**                 C O U N T E R       C O N S T A N T S                       */
/** ============================================================================*/
#define TXREP_CNT                       3U      // number of repeated packet in broadcast and group link file transfer
#define MAXREP_CNT                      30U     // maximum number of resend trials to one address
#define OW_DEV_CNT                      9U      // max. number of one type device connected to single onewire bus
#define OW_UPDATE                       45U     // data refresh period
#define OW_CRWUPD                       321U    // onewire card reader writer data update period
#define OW_MAXERR                       10U     // max. trial on selected address
#define JRNUPD_CNT                      40U     // every cycles of address list create cleaning statistics journal 
#define DRV_TRIAL                       100U    // hal driver trials
/** ============================================================================*/
/**             T I M E         O U T           C O N S T A N T S               */
/** ============================================================================*/
#define RESP_TOUT                       45U     // timeout to get response from addressed device from send request should be 2 * RESP_TOUT
#define BIN_TOUT			            321U    // timeout for binary packet upload - shold be RESP_TOUT or 2 * RESP_TOUT
#define RX_TOUT                         3U      // 3 ms timeout to receive next byte
#define REC_TOUT                        5678U   // 5 s timout to receive first byte before usart reinit 
#define PAK_TOUT(BSIZE,BAUD)            ((uint32_t)(RX_TOUT + ((BSIZE * 10000U) / bps[BAUD]))) // packet transfer timeout from size and baudrate
#define DRV_TOUT                        100U    // hal driver timeout
/** ============================================================================*/
/**             T I M E         L O A D         C O N S T A N T S               */
/** ============================================================================*/
#define OW_SYNC_TIME                    2345U   // periodic device variable synchronization
#define OW_PKTIME                       456U    // timeout to receive full size packet
#define RXTIME                          5678U   // timeout to receive first byte
#define RTC_UPD_TIME                    6789U   // send time & date broadcast packet every 6,7 seconds
#define WFC_UPD_TIME                    9000U   // send weather forecast broadcast packet every 9 seconds
/** ============================================================================*/
/**             T I M E         D E L A Y       C O N S T A N T S               */
/** ============================================================================*/
#define FWR_UPLD_DEL		            2345U   // old firmware update - delay for firmware backup
#define FWR_COPY_DEL                    1567U   // time delay for room controller to copy new firmware before start bus activity
#define IMG_COPY_DEL                    4567U   // time delay for room controller to copy new received image and get ready to receive another
#define APP_START_DEL                   12345U  // time delay for room controller to run new firmware before another upload start
#define BLDR_START_DEL                  3456U   // old rc bootloader time delay for to start receiving update 
#define RX2TX_DEL                       3U      // pause in ms between receiving and transmitting
#define MUTE_DEL                        10U     // receiver mute time after wrong packet addresse
/** ============================================================================*/
/**             B U F F E R     &       D A T A     S I Z E                     */
/** ============================================================================*/
#define OW_BSIZE                        32U    // buffer size
#define RS_BSIZE                        256U
#define HC_BSIZE                        512U
#define HC_PCK_BSIZE                    128U    // update packet payload size
#define LOG_DSIZE                       16U
#define FILUPD_LIST_BSIZE               32U
#define JRNL_BSIZE                      128U
#define COLOR_BSIZE                     28U
#define QRC_BSIZE                       128U
#define QRC_DSIZE                       124U
#define DISP_XSIZE                      480U
#define DISP_YSIZE                      272U
#define WFC_BSIZE                       24U
#define WFC_DSIZE                       21U
#define RFSEN_BSIZE                     8U      // max. number of added radio sensors
#define RT_UPD_LIST_BSIZE               9U      // max. number of room thermostat to search
#define RS485_BSIZE                     1048U   // data buffer size 1kBbyte + 24 byte overhead 
/** ==========================================================================*/
/**     E R R O R     T R A C K I N G     L O G     L I S T     E V E N T S   */
/** ==========================================================================*/
#define LOG_EVN_FIRST_CODE              ((uint8_t)0x90U)  // log list first event         (marker used for search function)
#define DRV_ERR_FIRST_CODE              ((uint8_t)0x90U)  // driver error first event     (marker used for search function)
#define SPI_DRV                         ((uint8_t)0x90U)  // spi driver fail error log define
#define I2C_DRV                         ((uint8_t)0x91U)  // i2c driver fail error log define
#define USART_DRV                       ((uint8_t)0x92U)  // uart driver fail error log define
#define RTC_DRV                         ((uint8_t)0x93U)  // rtc driver fail error log define
#define TMR_DRV                         ((uint8_t)0x94U)  // timer driver fail error log define
#define ETH_DRV                         ((uint8_t)0x95U)  // ethernet interface driver error
#define CRC_DRV                         ((uint8_t)0x96U)  // crc driver error
#define ADC_DRV                         ((uint8_t)0x97U)  // analog to digital converter driver error
#define SYS_CLOCK                       ((uint8_t)0x98U)  // system clock hal driver error
#define SYS_EXEPTION                    ((uint8_t)0x99U)  // system exeption errors (divided by zero, hard fault, bus error..)
#define QSPI_DRV                        ((uint8_t)0x9AU)  // quad spi driver error
#define FLASH_DRV                       ((uint8_t)0x9BU)  // flash hal driver error
#define SW_RESET                        ((uint8_t)0x9CU)  // software reset 
#define DRV_ERR_LAST_CODE               ((uint8_t)0x9CU)  // driver error last event      (marker used for search function)

#define FUNC_ERR_FIRST_CODE             ((uint8_t)0xA0U)  // function error first event   (marker used for search function)
#define CAP_FUNC                        ((uint8_t)0xA0U)  // capacitive sensor service fail error log define
#define RC522_FUNC                      ((uint8_t)0xA1U)  // mifare card read service fail error log define
#define OW_FUNC                         ((uint8_t)0xA2U)  // onewire service fail error log define
#define RS485_FUNC                      ((uint8_t)0xA3U)  // rs485 service fail error log define
#define MAIN_FUNC                       ((uint8_t)0xA4U)  // main function fail error log define
#define DISP_FUNC                       ((uint8_t)0xA5U)  // display sevice fail error log define
#define LOGGER_FUNC                     ((uint8_t)0xA6U)  // logger sevice fail error log define
#define DIO_FUNC                        ((uint8_t)0xA7U)  // digital input-output service fail error log define
#define EEPROM_FUNC                     ((uint8_t)0xA8U)  // eeprom service fail error log define
#define ROOM_FUNC                       ((uint8_t)0xA9U)  // signal service fail error log define
#define TCPIP_FUNC                      ((uint8_t)0xAAU)  // tcp ip statck failure event  
#define HOTEL_CTRL_FUNC                 ((uint8_t)0xABU)  // hotel controller service loop failure
#define HTTPD_FUNC                      ((uint8_t)0xACU)  // httpd server service fail
#define THST_FUNC                       ((uint8_t)0xADU)  // room thermostat control loop fail
#define FUNC_OR_DRV_FAIL                ((uint8_t)0xAEU)  // general event type for driver or function failure
#define FUNC_ERR_LAST_CODE              ((uint8_t)0xAEU)  // function error last event    (marker used for search function)

#define SYS_EVENT_FIRST_CODE            ((uint8_t)0xB0U)  // system first event   (marker used for search function)
#define FS_FILE_OK                      ((uint8_t)0xB0U)  // fat file system ok
#define FS_DRIVE_ERR                    ((uint8_t)0xB1U)  // fat file system mount or drive error 
#define FS_DIRECTORY_ERROR              ((uint8_t)0xB2U)  // fat file system directory error (missing)
#define FS_FILE_ERROR                   ((uint8_t)0xB3U)  // fat filesystem file error (missing or damaged)
#define OUT_OF_MEMORY_ERROR             ((uint8_t)0xB4U)  // out of memory error (maloc function could not find free memory)
#define ADDRESS_LIST_ERR                ((uint8_t)0xB5U)  // error loading address list first from internal flash and second from usd card
#define ADDRESS_LIST_uSD_ERR            ((uint8_t)0xB6U)  // error loading address list from usd card (copy new address list commad)
#define DEV_NOT_RESP                    ((uint8_t)0xB7U)  // no response from addressed device
#define PIN_RESET					    ((uint8_t)0xB8U)  // reset source nrst pin
#define POWER_ON_RESET				    ((uint8_t)0xB9U)  // reset source new power cycle
#define SOFTWARE_RESET				    ((uint8_t)0xBAU)  // reset source call of software restart function
#define IWDG_RESET					    ((uint8_t)0xBBU)  // reset source independent watchdog timer
#define WWDG_RESET					    ((uint8_t)0xBCU)  // reset source windowed watchdog timer
#define LOW_POWER_RESET                 ((uint8_t)0xBEU)  // reset source power supply low voltage
#define SYS_EVENT_LAST_CODE             ((uint8_t)0xBEU)  // system last event    (marker used for search function)

#define APPL_EVN_FIRST_CODE             ((uint8_t)0xC8U)  // application/user first event (marker used for search function)
#define RS485_BUS_ERR                   ((uint8_t)0xC8U)  // all rs485 device stop responding after been activ
#define RT_RPM_SENS_ERR                 ((uint8_t)0xC9U)  // room thermostat rpm sensor error
#define RT_FANC_NTC_ERR                 ((uint8_t)0xCAU)  // room thermostat fancoil unit ntc sensor error
#define RT_LO_TEMP_ERR                  ((uint8_t)0xCBU)  // room thermostat low temperature of heating medium
#define RT_HI_TEMP_ERR                  ((uint8_t)0xCCU)  // room thermostat high temperature of cooling medim
#define RT_FREEZ_PROT                   ((uint8_t)0xCDU)  // room thermostat anti-freezing protection activated
#define RT_DISP_NTC_ERR                 ((uint8_t)0xCEU)  // room termostat ntc sensor error
#define FW_UPDATED			            ((uint8_t)0xCFU)  // firmware update success
#define FW_UPD_FAIL		                ((uint8_t)0xD0U)  // firmware update fail 
#define BLDR_UPDATED                    ((uint8_t)0xD1U)  // bootloader update success
#define BLDR_UPD_FAIL                   ((uint8_t)0xD2U)  // bootloader update fail
#define IMG_UPDATED				        ((uint8_t)0xD3U)  // image update success
#define IMG_UPD_FAIL			        ((uint8_t)0xD4U)  // image update fail
#define FILE_COPY_FAIL                  ((uint8_t)0xD5U)  // file copy function fail
#define FILE_BACKUP_FAIL                ((uint8_t)0xD6U)  // file backup function fail
#define UNKNOWN_CARD				    ((uint8_t)0xD7U)  // unknown card detected
#define CARD_EXPIRED				    ((uint8_t)0xD8U)  // valid card with expired time/date 
#define CARD_INVALID                    ((uint8_t)0xD9U)  // invalid card data format
#define WRONG_ROOM					    ((uint8_t)0xDAU)  // valid card with wrong room number
#define WRONG_SYSID                     ((uint8_t)0xDBU)  // card with invalid system id 
#define GUEST_CARD        	            ((uint8_t)0xDCU)  // valid guest card   
#define HANDMAID_CARD    	            ((uint8_t)0xDDU)  // valid hand maid card
#define MANAGER_CARD            	    ((uint8_t)0xDEU)  // valid manager card
#define SERVICE_CARD            	    ((uint8_t)0xDFU)  // valid servicer card
#define ENTRY_DOOR_OPENED          	    ((uint8_t)0xE0U)  // entry door opened
#define ENTRY_DOOR_CLOSED			    ((uint8_t)0xE1U)  // entry door closed
#define ENTRY_DOOR_NOT_CLOSED		    ((uint8_t)0xF2U)  // entry door opened for period of time
#define MINIBAR_USED            	    ((uint8_t)0xE3U)  // minibar door opened
#define BALCON_DOOR_OPENED			    ((uint8_t)0xE4U)  // balcony door opened
#define BALCON_DOOR_CLOSED			    ((uint8_t)0xE5U)  // balcony door closed
#define CARD_STACKER_ON				    ((uint8_t)0xE6U)  // card detected by indor card reader
#define CARD_STACKER_OFF			    ((uint8_t)0xE7U)  // card removed from indoor card reader
#define DO_NOT_DISTURB_SWITCH_ON 	    ((uint8_t)0xE8U)  // dnd switch (software or hardware) activated
#define DO_NOT_DISTURB_SWITCH_OFF	    ((uint8_t)0xE9U)  // dnd switch (software or hardware) off
#define HANDMAID_SWITCH_ON			    ((uint8_t)0xEAU)  // call handmaid button sw/hw activated
#define HANDMAID_SWITCH_OFF			    ((uint8_t)0xEBU)  // call handmaid button sw/hw off
#define HANDMAID_SERVICE_END    	    ((uint8_t)0xECU)  // handmaid service end by rfid card present 5s on room controller
#define SOS_ALARM_TRIGGER			    ((uint8_t)0xEDU)  // sos switch activated
#define SOS_ALARM_RESET				    ((uint8_t)0xEEU)  // sos activ event reset by software or hardware switch
#define FIRE_ALARM_TRIGGER			    ((uint8_t)0xEFU)  // fire switch activated
#define FIRE_ALARM_RESET          	    ((uint8_t)0xF0U)  // fire activ event off
#define FLOOD_SEN_ACTIV                 ((uint8_t)0xF1U)  // flood sensor activated
#define FLOOD_SEN_INACTIV			    ((uint8_t)0xF2U)  // flood sensor off
#define	DOOR_BELL_ACTIVE			    ((uint8_t)0xF3U)  // door bell sensor activated
#define	DOOR_LOCK_USER_OPEN			    ((uint8_t)0xF4U)  // door lock open by user activated software button
#define FIRE_EXIT_TRIGGER			    ((uint8_t)0xF5U)  // fire switch activated
#define FIRE_EXIT_RESET          	    ((uint8_t)0xF6U)  // fire activ event off
#define PASSWORD_VALID                  ((uint8_t)0xF7U)  // valid password enter in display login
#define PASSWORD_INVALID                ((uint8_t)0xF8U)  // invalid password entry in display login
#define ROOM_TIME_POWER_OFF             ((uint8_t)0xF9U)  // room power off on room time expired
#define APPL_EVN_LAST_CODE              ((uint8_t)0xF9U)  // application/user last event  (marker used for search function)
#define LOG_EVN_LAST_CODE               ((uint8_t)0xF9U)  // log list last event          (marker used for search function)    
/** ==========================================================================*/
/**     R O O M   T H E R M O S T A T      C O M M A N D		L I S T       */
/** ==========================================================================*/
#define RT_GET_BTN_STA                  ((uint8_t)0x32U)
#define RT_GET_ERR_STA                  ((uint8_t)0x33U)
#define RT_GET_DISP_STA                 ((uint8_t)0x34U)
#define RT_SET_BTN_STA                  ((uint8_t)0x42U) 
#define RT_RST_ERR_STA                  ((uint8_t)0x43U)
#define RT_SET_DISP_STA                 ((uint8_t)0x44U)
#define RT_DISP_QRC                     ((uint8_t)0x47U)
#define RT_DISP_MSG                     ((uint8_t)0x48U)
#define RT_SET_OWIF                     ((uint8_t)0x49U)
#define RT_UPD_WFC                      ((uint8_t)0x51U)
#define RT_UPD_QRC                      ((uint8_t)0x52U)
/** ==========================================================================*/
/**    	R O O M    C O N T R O L L E R	  C O M M A N D		L I S T           */
/** ==========================================================================*/
#define COPY_DISP_IMG                   ((uint8_t)0x63U)
#define DWNLD_DISP_IMG 		            ((uint8_t)0x63U)    
#define DWNLD_DISP_IMG_1 		        ((uint8_t)0x64U)
#define DWNLD_DISP_IMG_2 		        ((uint8_t)0x65U)
#define DWNLD_DISP_IMG_3 		        ((uint8_t)0x66U)
#define DWNLD_DISP_IMG_4 		        ((uint8_t)0x67U)
#define DWNLD_DISP_IMG_5 		        ((uint8_t)0x68U)
#define DWNLD_DISP_IMG_6 		        ((uint8_t)0x69U)
#define DWNLD_DISP_IMG_7 		        ((uint8_t)0x6AU)
#define DWNLD_DISP_IMG_8 		        ((uint8_t)0x6BU)
#define DWNLD_DISP_IMG_9 		        ((uint8_t)0x6CU)
#define DWNLD_DISP_IMG_10		        ((uint8_t)0x6DU)
#define DWNLD_DISP_IMG_11		        ((uint8_t)0x6EU)
#define DWNLD_DISP_IMG_12		        ((uint8_t)0x6FU)
#define DWNLD_DISP_IMG_13		        ((uint8_t)0x70U)
#define DWNLD_DISP_IMG_14		        ((uint8_t)0x71U)
#define DWNLD_DISP_IMG_15		        ((uint8_t)0x72U)
#define DWNLD_DISP_IMG_16		        ((uint8_t)0x73U)
#define DWNLD_DISP_IMG_17		        ((uint8_t)0x74U)
#define DWNLD_DISP_IMG_18		        ((uint8_t)0x75U)
#define DWNLD_DISP_IMG_19		        ((uint8_t)0x76U)
#define DWNLD_DISP_IMG_20               ((uint8_t)0x77U)
#define DWNLD_DISP_IMG_21               ((uint8_t)0x78U)
#define DWNLD_DISP_IMG_22		        ((uint8_t)0x79U)
#define DWNLD_DISP_IMG_23		        ((uint8_t)0x7AU)
#define DWNLD_DISP_IMG_24               ((uint8_t)0x7BU)
#define DWNLD_DISP_IMG_25               ((uint8_t)0x7CU)
#define DWNLD_FWR_IMG                   DWNLD_DISP_IMG_20
#define DWNLD_BLDR_IMG                  DWNLD_DISP_IMG_21
#define RT_DWNLD_FWR                    DWNLD_DISP_IMG_22
#define RT_DWNLD_BLDR                   DWNLD_DISP_IMG_23  
#define RT_DWNLD_LOGO                   DWNLD_DISP_IMG_24  
#define RT_DWNLD_LANG                   DWNLD_DISP_IMG_25

#define GET_RELAY_STATE                 ((uint8_t)0x90U)
#define DOUT_SET                        ((uint8_t)0x91U)
#define DOUT_RESET                      ((uint8_t)0x92U)
#define GET_DOUT_STATE                  ((uint8_t)0x93U)
#define GET_ROOM_STATUS                 ((uint8_t)0x94U)

#define GET_SYS_STAT 		            ((uint8_t)0xA0U)
#define GET_APPL_STAT 		            ((uint8_t)0xA1U)
#define GET_RS485_CFG 		            ((uint8_t)0xA2U)
#define GET_LOG_LIST 		            ((uint8_t)0xA3U)
#define GET_ALLOW_GROUP                 ((uint8_t)0xA4U)
#define GET_AUTH_KEYA 		            ((uint8_t)0xA5U)
#define GET_AUTH_KEYB 		            ((uint8_t)0xA6U)
#define GET_CARD_BLOCK					((uint8_t)0xA7U)
#define GET_CARD_DATA         			((uint8_t)0xA8U)
#define GET_USER                        ((uint8_t)0xA9U)
#define GET_EVENT                       ((uint8_t)0xAAU)
#define GET_COUNTERS                    ((uint8_t)0xABU)
#define GET_ROOM_TEMP                   ((uint8_t)0xACU)

#define FORMAT_EXTFLASH                 ((uint8_t)0xB9U)
#define LOAD_DEFAULT                    ((uint8_t)0xBAU)
#define APP_EXE 		                ((uint8_t)0xBBU)
#define START_BLDR 		                ((uint8_t)0xBCU)
#define FLASH_PROT_DIS                  ((uint8_t)0xBDU)
#define FLASH_PROT_ENA                  ((uint8_t)0xBEU)
#define DWNLD_FWR                       ((uint8_t)0xBFU)

#define RESTART_CTRL 		            ((uint8_t)0xC0U)
#define UPDATE_FWR 		                ((uint8_t)0xC1U)
#define UPDATE_BLDR 		            ((uint8_t)0xC2U)
#define DWNLD_JRNL                      ((uint8_t)0xC3U)
#define DWNLD_WFCST                     ((uint8_t)0xC4U)
#define UPD_FW_INFO                     ((uint8_t)0xC5U)
#define SET_DIN_CFG                     ((uint8_t)0xC6U)
#define ADD_USER                        ((uint8_t)0xC7U)
#define DELETE_USER                     ((uint8_t)0xC8U)
#define DELETE_EVENT                    ((uint8_t)0xC9U)
#define DEFRAGMENT_TABELE               ((uint8_t)0xCAU)
#define SAVE_TABELE                     ((uint8_t)0xCBU)
#define BUZZER_CTRL                     ((uint8_t)0xCDU)
#define DIN_STATE                       ((uint8_t)0xCEU)

#define SET_APPL_STAT 		            ((uint8_t)0xD0U)
#define SET_RS485_CFG 		            ((uint8_t)0xD1U)
#define SET_DOUT_STATE 		            ((uint8_t)0xD2U)
#define DEL_LOG_LIST                    ((uint8_t)0xD3U)
#define RESET_SOS_ALARM 		        ((uint8_t)0xD4U)
#define SET_RTC_DATE_TIME               ((uint8_t)0xD5U)
#define SET_ROOM_TEMP                   ((uint8_t)0xD6U)
#define SET_DISPL_BCKLGHT               ((uint8_t)0xD7U)
#define SET_SYSTEM_ID                   ((uint8_t)0xD8U)
#define PREVIEW_DISPL_IMG               ((uint8_t)0xD9U)
#define SET_BEDDING_REPL                ((uint8_t)0xDAU)
#define SET_PERMITED_GROUP              ((uint8_t)0xDBU)
#define SET_THST_HEATING                ((uint8_t)0xDCU)
#define SET_THST_COOLING                ((uint8_t)0xDDU)
#define SET_THST_OFF                    ((uint8_t)0xDEU)
/** ==========================================================================*/
/**         C O M M O N     	  C O M M A N D		    L I S T               */
/** ==========================================================================*/
#define SET_BRIDGE                      ((uint8_t)0xE0U + COM_Bridge) // set bridge macro
#define SET_BRNONE                      ((uint8_t)0xE0U) // bridge not used
#define SET_BR2RS485                    ((uint8_t)0xE1U) // bridge to rs485 interface
#define SET_BR2RADIO                    ((uint8_t)0xE2U) // bridge to radio device
#define SET_BR2WIFI                     ((uint8_t)0xE3U) // bridge to WiFi network
#define SET_BR2ETH                      ((uint8_t)0xE4U) // bridge to ethernet link
#define SET_BR2KNX                      ((uint8_t)0xE5U) // bridge to KNX network
#define SET_BR2X10                      ((uint8_t)0xE6U) // bridge to X10 power line comm  
#define SET_BR2NFC                      ((uint8_t)0xE7U) // bridge to NFC device
#define SET_BR2IR                       ((uint8_t)0xE8U) // bridge to InfraRed link
#define SET_BR2OW                       ((uint8_t)0xE9U) // bridge to onewire interface
#define SET_BR2WGND                     ((uint8_t)0xEAU) // bridge to wiegand interface
/** ==========================================================================*/
/**    	C O N T R O L L E R 	C O M M A N D		L I S T            		  */
/** ==========================================================================*/
#define FIND_FIRST                      0x0U
#define FIND_ADDR                       0x1U
#define FIND_NEXT                       0x2U
#define FIND_NEW                        0x3U
#define FIND_ALL                        0x4U

#define FW_UPDATE_INIT                  17U
#define FW_UPDATE_BLDR                  18U
#define FW_UPDATE_RUN                   19U
#define FW_UPDATE_END                   20U
#define FW_UPDATE_FAIL                  21U

#define TRANSFER_IDLE                   30U
#define TRANSFER_QUERY_LIST             31U
#define TRANSFER_DELETE_LOG             32U
#define TRANSFER_FAIL                   33U

#define FILE_UPDATE_RUN                 70U
#define FILE_UPDATE_FINISHED            71U
#define FILE_UPDATE_FAIL                72U

#define HTTP_STAT_RESP_TOUT             0x50U
#define HTTP_STAT_RESP_ERROR            0x51U
#define HTTP_STAT_RESP_BUSY             0x52U
#define HTTP_STAT_RESP_OK               0x53U
#define HTTP_LOG_TRANSFER_IDLE          0x54U
#define HTTP_GET_LOG_LIST               0x55U
#define HTTP_LOG_READY                  0x56U
#define HTTP_DEL_LOG_LIST               0x57U
#define HTTP_LOG_DELETED                0x58U
#define HTTP_FORMAT_LOG_LIST            0x59U
#define HTTP_LOG_FORMATED               0x5AU
#define HTTP_TRANSFER_FAIL              0x5BU
#define HTTP_GET_ROOM_STAT              0x5CU
#define HTTP_ROOM_STAT_READY            0x5DU
#define HTTP_SET_ROOM_STAT              0x5EU
#define HTTP_ROOM_STAT_FAIL             0x5FU

#define RS485_BUS_ERROR                 0xBFU
#define RS485_BUS_CONNECTED             0xC0U
#define RS485_BUS_DISCONNECTED          0xC1U
/** ==========================================================================*/
/**             P R O G R A M             C O N S T A N T S                   */
/** ==========================================================================*/
#define DISP_BRGHT_MAX                  80U     // max value 800
#define DISP_BRGHT_MIN                  5U      // min value 50

#define BUZZER_OFF                      0U
#define BUZZER_CLICK                    1U
#define BUZZER_SHORT                    2U
#define BUZZER_MIDDLE                   3U
#define BUZZER_LONG                     4U

#define LIGHT_MODBUS_QUERY_RESPONSE_INDEX_OUT_OF_RANGE       254U
#define CURTAIN_MODBUS_QUERY_RESPONSE_INDEX_OUT_OF_RANGE     254U

#define QR_CODE_APP_ID                  1U
#define QR_CODE_WIFI_ID                 2U
#define QR_CODE_QUERY_RESPONSE_DATA_TOO_LONG   254U

#define	BLDR_CMD_RUN                    ('W') // command to run bootloader
#define	BLDR_UPD_ERR                    ('O') // firmware update fail
#define	BLDR_UPD_OK                     ('N')
#define BLDR_LOAD_DEF                   RT_DLDR_TYPE_ADDR   // run downloader service and wait new firmware
#define BLDR_CTRL_REG                   RTC_BKP_DR10
#define BLDR_STAT_REG                   RTC_BKP_DR11
#define BLDR_CNT_REG                    RTC_BKP_DR12
#define APPL_EXEC_ERR                   ('E')
#define APP_FW_INFO_ERR                 ('A')
#define NEW_FW_INFO_ERR                 ('V')
#define BKP_FW_INFO_ERR                 ('I')
#define BKP_RECOVERED                   ('B')
#define FW_OLD_VERSION                  ('G')
#define FW_WRONG_VERSION                ('H')
#define FILE_RECEIVED                   ('R')
#define FILE_CRC_FAIL                   ('C')
/**
*--------------      card user groups predefine     ----------------------------
*/
#define USERGRP_GUEST                   ('G')
#define USERGRP_MAID                    ('H')
#define USERGRP_HANDMAID                USERGRP_MAID
#define USERGRP_MANAGER                 ('M')
#define USERGRP_SERVICE                 ('S')
#define USERGRP_RESET				    ('R')
#define USERGRP_KINDERGARDEN		    ('K')
#define USERGRP_PLAYROOM                USERGRP_KINDERGARDEN
#define USERGRP_POOL				    ('B')
#define USERGRP_PARKING				    ('P')
#define USERBRP_GARAGE                  ('A')
#define USERGRP_ENTRY                   ('E')
#define USERGRP_LIFT                    ('L')
#define PERMITED_USERS                  ("GHMSRKBPAEL")
/**
*--------------      card type predefine     -----------------------------------
*/
#define CARD_TYPE_ONE_TIME              ('O')
#define CARD_TYPE_PREPAID               ('E')
#define CARD_TYPE_PRESET                ('F')
#define CARD_TAG_TYPE_KEY_RING          ('Q')
#define CARD_TAG_TYPE_CLASIC            ('C')
#define CARD_TAG_TYPE_WRIST             ('W')
/**
*------------------  mifare card data status define    -------------------------
*/
#define CARD_PENDING                    0x0U    // card data status type
#define CARD_VALID                      0x06U	// card data status type
#define CARDID_INVALID                  ((uint8_t)0xF5U)
#define USERGRP_INVALID                 ((uint8_t)0xF6U)	
#define ROOMADDR_INVALID                ((uint8_t)0xF7U)
#define SYSTEMID_INVALID                ((uint8_t)0xF8U)
#define EXPIRYTIME_INVALID              ((uint8_t)0xF9U)
#define CARDID_DATA_INVALID             ((uint8_t)0xFAU)
#define USERGRP_DATA_INVALID            ((uint8_t)0xFBU)
#define ROOMADDR_DATA_INVALID           ((uint8_t)0xFCU)
#define SYSTEMID_DATA_INVALID           ((uint8_t)0xFDU)
#define EXPIRYTIME_DATA_INVALID         ((uint8_t)0xFEU)
/**
*--------------     Dallas  DS18B20 onewire temp. sensor define    -------------
*/
#ifdef  OW_DS18B20
#define OW_TOUT                         800U
#define OW_SRCHROM                      ((uint8_t)0xF0U)
#define OW_RDROM                        ((uint8_t)0x33U)
#define OW_MCHROM                       ((uint8_t)0x55U)
#define OW_SKIPROM                      ((uint8_t)0xCCU)
#define OW_ALSRCH                       ((uint8_t)0xECU)
#define OW_CONVERT                      ((uint8_t)0x44U)
#define OW_WRSCRPD                      ((uint8_t)0x4EU)
#define OW_RDSCRPD                      ((uint8_t)0xBEU)
#define OW_CPSCRPD                      ((uint8_t)0x48U)
#define OW_RECLE2                       ((uint8_t)0xB8U)
#define OW_RDPSU                        ((uint8_t)0xB4U)
#endif
/** ============================================================================*/
/**			        U S E F U L L           M A C R O S                         */
/** ============================================================================*/
#define ISCAPLETTER(c)    	            ((*(char*)c >= 'A') && (*(char*)c <= 'F'))
#define ISLCLETTER(c)     	            ((*(char*)c >= 'a') && (*(char*)c <= 'f'))
#define IS09(c)                         ((*(char*)c >= '0') && (*(char*)c <= '9'))
#define ISVALIDHEX(c)       	        (ISCAPLETTER(c) || ISLCLETTER(c) || IS09(c))
#define ISLETTER(c)                     (((c) >= 0x20U) && ((c) <= 0x7A))
#define ISVALIDDEC(c)     		        (((c) >= '0') && ((c) <= '9'))
#define CONVERTDEC(c)                   (*(char*)c - '0')
#define TOCHAR(c)                       ((c) + '0')
#define TODEC(c)                        ((c) - '0')
#define CONVERTALPHA(c) 	            (ISCAPLETTER(c) ? (*(char*)c - 'A' + 10U) : (*(char*)c - 'a' + 10U))
#define CONVERTHEX(c)       	        (IS09(c) ? CONVERTDEC(c) : CONVERTALPHA(c))
#define BCD2DEC(x)                      ((((x) >> 4U) & 0x0FU) * 10U + ((x) & 0x0FU))
#define ISVALIDBCD(x)                   (((((x) >> 4U) & 0x0FU) < 10U) && ((x) & 0x0FU) < 10U)
#define LEAP_YEAR(year)                 ((((year) % 4 == 0) && ((year) % 100 != 0)) || ((year) % 400 == 0))
#define DAYS_IN_YEAR(x)                 (LEAP_YEAR(x) ? 366 : 365)
#define UNIX_OFFSET_YEAR                1970
#define SECONDS_PER_DAY                 86400
#define SECONDS_PER_HOUR                3600
#define SECONDS_PER_MINUTE              60
#define BUFFERCNT(_BUFFER)              (sizeof(_BUFFER) / sizeof(*(_BUFFER)))
#define BUFFERSIZE(b)                   (COUNTOF(b) - 1U)
#define COUNTOF(a)                      (sizeof(a) / sizeof(a[0]))
#define MIN(a,b)				        (((a) < (b)) ? (a) : (b))
#define MAX(a,b)				        (((a) > (b)) ? (a) : (b))
#define ABS_RETURN(x,y)                 (((x) < (y)) ? ((y)-(x)) : ((x)-(y)))
#define ZEROFILL(p, Size)   	        (mem_set(p, 0, Size))
#define HEX2STR(s,h)                    (Hex2Str(s, h, 2U)) // convert single hex to 2 char string null teminated
#define STRINGIFY_X(x)                  #x
#define STRINGIFY(x)                    STRINGIFY_X(x)
#define STR2STR(a,b)                    STRINGIFY(a ## b)
#define POINTERIZE(x)                   ((__typeof__(x) []){ x })
/* Get the number of sectors from where the user program will be loaded */
#define FLSECT_NUM                      ((uint32_t)(ABS_RETURN(RC_APPL_ADDR,FLASH_START_BANK1))>>12)
/* Compute the mask to test if the Flash memory, where the user program will beloaded, is write protected */
#define FLPROT_SECT                     ((uint32_t) (~((0x1U<<FLSECT_NUM)-0x1U)))
/** ============================================================================*/
/**			A S C I I       C O N T R O L       C H A R A C T E R S             */
/** ============================================================================*/
#define NUL     ((char)0x0)     /* null control char                   */
#define SOH     ((char)0x1)     /* start of header control character   */
#define STX     ((char)0x2)     /* start of text control character     */
#define ETX     ((char)0x3)     /* end of text control character       */
#define EOT     ((char)0x4)     /* end of transmission control char    */
#define ENQ     ((char)0x5)     /* enquiry control character           */
#define ACK     ((char)0x6)     /* acknowledge control character       */
#define BEL     ((char)0x7)     /* bell control character              */
#define BS      ((char)0x8)     /* backspace control character         */
#define TAB     ((char)0x9)     /* horizontal tab control character    */
#define LF      ((char)0xA)     /* line feed control character         */
#define VT      ((char)0xB)     /* vertical tab control character      */
#define FF      ((char)0xC)     /* form feed new page control char     */
#define CRT     ((char)0xD)     /* carriage return control char        */
#define SO      ((char)0xE)     /* shift out control character         */
#define SI      ((char)0xF)     /* shift in control character          */
#define NAK     ((char)0x15)    /* negative acknowledge control char   */
#define ETB     ((char)0x16)    /* end of transfer block control char  */
#define CAN     ((char)0x17)    /* cancel control char                 */
#define SUB     ((char)0x1A)    /* supstitute control char             */
#define ESC     ((char)0x1B)    /* escape control char                 */
#define FS      ((char)0x1C)    /* file separator control char         */
#define GS      ((char)0x1D)    /* group separator control char        */
#define RS      ((char)0x1E)    /* record separator control char       */
#define US      ((char)0x1F)    /* unit separator control char         */
/**
*--------------           Test values and functions          -------------
*/

#define SELECT_RELAY                        0xAD
#define GET_MAIN                            0XAE
#define GET_LED                             0XAF
#define GET_OUT                             0xB0
#define PINS                                0xB1
#define READ_PINS                           0xB2
#define SET_PIN                             254  //0xB3
#define SET_THST_ON                         0xB4
#define GET_FAN_DIFFERENCE                  0xB5
#define GET_FAN_BAND                        0xB6
#define RET                                 0xB7
#define GET_SP_MAX                          0xB8
#define SET_PINS                            0xBC
#define RET_TO_PRIMARY                      0xBD
#define SET_SP_MAX                          0xBE  //0xB9
#define GET_SP_MIN                          0xC2
#define SET_SP_MIN                          0xC3
#define SET_FAN_DIFFERENCE                  0xC4
#define SET_FAN_LOBAND                      0xC5
#define SET_FAN_HIBAND                      0xC6
#define GET_FAN_SPEED_CONTROL_MODE          0xC7
#define SET_FAN_SPEED_CONTROL_MODE_ON_OFF   0xC8
#define SET_FAN_SPEED_CONTROL_MODE_3SPEED   0xC9
#define PCA9685_SetValue                    0xCA
#define THERMOSTAT_CHANGE_ALL               0xCB
#define GUEST_ENTER_KEYCARD_RS485           0xCC
#define GUEST_ENTER_PASSWORD_RS485          0xCD
#define HOTEL_READ_LOG                      0xCE
#define HOTEL_DELETE_LOG                    0xCF

#define SET_GUEST_IN_TEMP                   0xD0
#define SET_GUEST_OUT_TEMP                  0xD1
#define HOTEL_SET_USER_PASSWORD             0xD2
#define GUEST_PASSWORD_SEND                 0xD3
#define GUEST_PASSWORD_GET                  0xD4
#define GET_GUEST_IN_TEMP                   0xD7
#define GET_GUEST_OUT_TEMP                  0xD8
#define SET_PIN_V2                          0xD9
#define GET_PIN                             0xDA
#define HOTEL_SET_PIN_V2                    0xDB
#define MODBUS_SEND_WRITE_SINGLE_REGISTER   0xDF

#define MODBUS_SEND_READ_REGISTERS          0xE0
#define LIGHT_MODBUS_SET                    0xE1
#define LIGHT_MODBUS_GET                    0xE2
#define CURTAIN_MODBUS_SET                  0xE3
#define CURTAIN_MODBUS_GET                  0xE4
#define QR_CODE_GET                         0xE5
#define QR_CODE_SET                         0xE6
#define LIGHT_BRIGHTNESS_SET                0xE7
#define LIGHT_COLOR_SET                     0xE8
#define SET_LANG                            0xE9
#define GET_SYSID                           0xEA
#define SET_SYSID                           0xEB
#define GET_PASSWORD                        0xEC

#define GUEST_ENTER_KEYCARD_RS485           0xCC
#define GUEST_ENTER_PASSWORD_RS485          0xCD
#define HOTEL_READ_LOG                      0xCE
#define HOTEL_DELETE_LOG                    0xCF

#define GUEST_TEMPERATURE_IN                0xD0
#define GUEST_TEMPERATURE_OUT               0xD1
#define HOTEL_SET_USER_PASSWORD             0xD2
#define GUEST_PASSWORD_SEND                 0xD3
#define GUEST_PASSWORD_GET                  0xD4
#define SET_PIN_V2                          0xD9
#define GET_PIN                             0xDA
#define OPEN_DOOR                           0xDB

#define MAID_ENTER_KEYCARD_RS485            0x86 // Promenjeno sa 0xD5 (konflikt sa SET_RTC_DATE_TIME)
#define MAID_ENTER_PASSWORD_RS485           0x87 // Promenjeno sa 0xD6 (konflikt sa SET_ROOM_TEMP)
#define SERVICE_ENTER_KEYCARD_RS485         0x88 // Promenjeno sa 0xDD (konflikt sa SET_THST_COOLING)
#define SERVICE_ENTER_PASSWORD_RS485        0x89 // Promenjeno sa 0xDE (konflikt sa SET_THST_OFF)
#define MANAGER_ENTER_KEYCARD_RS485         0x8A // Promenjeno sa 0xDF (konflikt sa SET_PASSWORD)
#define MANAGER_ENTER_PASSWORD_RS485        0x8B // Promenjeno sa 0xE0 (konflikt sa MODBUS/BRIDGE)
#define HOTEL_GET_PIN                       0x95
#define SET_PASSWORD                        0x96

//-----------------------------------------------
#define EXP1_TIMER_RELAY                    16
#define EXP2_TIMER_RELAY                    32
#define EXP2_TIMER_RELAY2                   31
#define EXP2_TIMER_RELAY3                   30
#define EXP2_TIMER_RELAY4                   29
/* Exported type  ----------------------------------------------------- */
/* flash operation error code */
enum
{
	FLIF_OK = ((uint8_t) 0x0U), FLIF_WRCTRL_ERR = ((uint8_t) 0x1U), FLIF_WR_ERR = ((uint8_t) 0x2U)
};
/* flash protection update */
enum
{
	FLIF_PROT_NONE = ((uint8_t) 0x0U),
	FLIF_ROP_ENA   = ((uint8_t) 0x1U),
	FLIF_WRP_ENA   = ((uint8_t) 0x2U),
	FLIF_WRP_DIS   = ((uint8_t) 0x3U),
	FLIF_RDP_ENA   = ((uint8_t) 0x4U)
};
/* usart baud selection */
typedef enum
{
	BR_2400   = ((uint8_t) 0x0U),
	BR_4800   = ((uint8_t) 0x1U),
	BR_9600   = ((uint8_t) 0x2U),
	BR_19200  = ((uint8_t) 0x3U),
	BR_38400  = ((uint8_t) 0x4U),
	BR_57600  = ((uint8_t) 0x5U),
	BR_115200 = ((uint8_t) 0x6U),
	BR_230400 = ((uint8_t) 0x7U),
	BR_460800 = ((uint8_t) 0x8U),
	BR_921600 = ((uint8_t) 0x9U)

} BAUDRATE_TypeDef;
/* usart data size */
enum
{
	WL_9BIT = ((uint8_t) 0x0U), WL_8BIT = ((uint8_t) 0x1U)

};
/* communication bridge type */
typedef enum
{
	BRNONE  = ((uint8_t) 0x0U),
	BR2RS485= ((uint8_t) 0x1U),
	BR2RADIO= ((uint8_t) 0x2U),
	BR2WIFI = ((uint8_t) 0x3U),
	BR2ETH  = ((uint8_t) 0x4U),
	BR2KNX  = ((uint8_t) 0x5U),
	BR2X10  = ((uint8_t) 0x6U),
	BR2NFC  = ((uint8_t) 0x7U),
	BR2IR   = ((uint8_t) 0x8U),
	BR2OW   = ((uint8_t) 0x9U),
    BR2WGND = ((uint8_t) 0xAU)
} BRIDGE_TypeDef;
/* link type defining request & response */
typedef enum
{
	NOLINK = ((uint8_t) 0x0U),
	P2P = ((uint8_t) 0x1U),
	GROUP = ((uint8_t) 0x2U),
	BROADCAST = ((uint8_t) 0x3U),
	CTRLREQ = ((uint8_t) 0x4U)

} LinkTypeDef;
/*----------------------------------*/
/* All LUX-M product line firmware  */
/* contain at fixed flash address   */
/* with  info data for validation   */
/* and update automatization, set   */
/* structure ld_adddr with firmware */
/* source memory address somewhare  */
/* inside used memory address space */
/* and call GetFirmwareInfo function*/
/* to load after validation firmware*/
/* info: size, crc32 check, version */
/* and address where should firmware*/
/* be written and executed. struct: */
typedef struct
{
	uint32_t size;      // firmware size
	uint32_t crc32;     // firmware crc32
	uint32_t version;   // fw version, fw type
	uint32_t wr_addr;   // firmware write address
	uint32_t ld_addr;   // firmware load address

} FwInfoTypeDef;
/* receive function states during receiving  */
/* and validation of packet control block    */
typedef enum
{
	RX_INIT = ((uint8_t) 0x0U),
	RX_LOCK = ((uint8_t) 0x1U),
	RX_ERROR = ((uint8_t) 0x2U),
	RX_READY = ((uint8_t) 0x3U),
	RX_START = ((uint8_t) 0x4U),
	RX_RECADDR = ((uint8_t) 0x5U),
	RX_SNDADDR = ((uint8_t) 0x6U),
	RX_SIZE = ((uint8_t) 0x7U),
	RX_PAYLOAD = ((uint8_t) 0x8U),
	RX_CRC8 = ((uint8_t) 0x9U)

} RX_TypeDef;

typedef enum
{
	ROOM_IDLE = ((uint8_t) 0x0U),
	ROOM_READY = ((uint8_t) 0x1U),
	ROOM_BUSY = ((uint8_t) 0x2U),
	ROOM_CLEANING_REQ = ((uint8_t) 0x3U),
	ROOM_BEDDING_REQ = ((uint8_t) 0x4U),
	ROOM_GENERAL_REQ = ((uint8_t) 0x5U),
	ROOM_CLEANING_RUN = ((uint8_t) 0x6U),
	ROOM_UNUSABLE = ((uint8_t) 0x7U),
	ROOM_SOS_ALARM = ((uint8_t) 0x8U),
	ROOM_FIRE_ALARM = ((uint8_t) 0x9U),
	ROOM_FIRE_EXIT = ((uint8_t) 0xAU),
	ROOM_RESET_ALARM = ((uint8_t) 0xBU)

} ROOM_StatusTypeDef;
//
/*  created and ready to use objects from previous definitions   */
//
extern const uint32_t size;
extern const uint32_t crc_32;
extern const uint32_t version;
extern const uint32_t address;
extern const char *day[];
extern const char *month[];
extern const uint8_t rtc_months[2][12]; // days in month for leap and non leap year
extern const uint32_t bps[10];
extern LinkTypeDef COM_Link;         // current activ data link type of request and response
extern BRIDGE_TypeDef COM_Bridge;       // comunication bridge to different interface type
extern RX_TypeDef COM_State;
/* Exported function  ------------------------------------------------------- */
uint8_t Bcd2Dec(uint8_t val);
uint8_t Dec2Bcd(uint8_t val);
uint32_t GetSize(const uint8_t *pbuf);
uint32_t rtc2unix(void *tm, void *dt);
void ResetFwInfo(FwInfoTypeDef *fw_info);
uint8_t GetFwInfo(FwInfoTypeDef *fw_info);
void DelayMs(volatile uint32_t delay_time);
void substring(char [], char[], int, int);
void CharToBinStr(char *pstr, uint8_t val);
uint8_t chk_chr(const char *str, char chr);
void mem_zero(uint8_t *dest, uint32_t len);
uint8_t ValidateFwInfo(FwInfoTypeDef *fw_info);
void mem_set(void *dst, int val, uint32_t cnt);
uint32_t BaseToPower(uint16_t base, uint8_t power);
uint8_t CalcCRC(const uint8_t *pbuf, uint16_t size);
void mem_cpy(void *dst, const void *src, uint32_t cnt);
signed int Str2Int(const char *pstr, uint8_t str_size);
void Int2Str(char *pstr, signed int val, uint8_t str_size);
uint8_t mem_cmp(const void *dst, const void *src, uint32_t cnt);
void Str2Hex(const char *pstr, uint8_t *phex, uint16_t str_size);
void Hex2Str(char *pstr, const uint8_t *phex, uint16_t str_size);
void mem_copy(uint8_t *dest, const uint8_t *source, uint32_t len);
int mem_comp(const uint8_t *s1, const uint8_t *s2, uint32_t len);
uint8_t IsNewFwUpdate(FwInfoTypeDef *old_fw, FwInfoTypeDef *new_fw);
#endif  /* __COMMON_H */
/************************ (C) COPYRIGHT JUBERA D.O.O Sarajevo ************************/
