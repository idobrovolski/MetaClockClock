/*
 * Copyright (c) 2020, Erich Styger
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* header file is included with -include compiler option
Instructions:
 - Remove the 'Template_' from the name and place this file into your 'src' folder.
 - Include it with the -include compiler option with:  "${ProjDirPath}/source/IncludeMcuLibConfig.h"
 - Add the following to the -I compiler option:
../McuLib
../McuLib/config
../McuLib/config/fonts
../McuLib/fonts
../McuLib/src
../McuLib/FreeRTOS/Source/include
../McuLib/FreeRTOS/Source/portable/GCC/ARM_CM4F
../McuLib/SEGGER_RTT
../McuLib/SEGGER_Sysview
../McuLib/TraceRecorder/config
../McuLib/TraceRecorder/include
../McuLib/TraceRecorder/streamports/Jlink_RTT/include
../McuLib/HD44780
 */

#ifndef INCLUDEMCULIBCONFIG_H_
#define INCLUDEMCULIBCONFIG_H_

#define SIM_UIDH       /* FIX: SDK does not define this one, but should be! See https://mcuoneclipse.com/2020/11/30/getting-a-96bit-unique-id-for-each-kinetis-device-using-mcuxpresso-sdk/ */

/* ------------------- SDK/Library ---------------------------*/
#define McuLib_CONFIG_SDK_VERSION_USED   McuLib_CONFIG_SDK_MCUXPRESSO_2_0
#define McuLib_CONFIG_CPU_IS_LPC         (0)  /* LPC845 */
#define McuLib_CONFIG_CORTEX_M           (4)  /* K22 is a Cortex-M4 */
#define McuLib_CONFIG_CPU_IS_KINETIS     (1)  /* K22 is a Kinetis */
#define McuLib_CONFIG_CPU_VARIANT        McuLib_CONFIG_CPU_VARIANT_NXP_K22FN
/* ------------------- RTOS ---------------------------*/
#define McuLib_CONFIG_SDK_USE_FREERTOS       (1)
#define configUSE_HEAP_SCHEME                (5) /* either 1 (only alloc), 2 (alloc/free), 3 (malloc), 4 (coalesc blocks), 5 (multiple blocks), 6 (newlib) */
#define configTOTAL_HEAP_SIZE                (63*1024)
#define configUSE_HEAP_SECTION_NAME          (1)
#define configHEAP_SECTION_NAME_STRING       ".bss.$SRAM_LOWER.FreeRTOS"
#define configQUEUE_REGISTRY_SIZE            (70)
#define configMINIMAL_STACK_SIZE             (200/sizeof(StackType_t))
#define configTIMER_TASK_STACK_DEPTH         (400/sizeof(StackType_t))
#define configUSE_TIMERS                     (0)
#define INCLUDE_xTimerPendFunctionCall       (0)
#define configSYSTICK_USE_LOW_POWER_TIMER       (0)
#define configSYSTICK_LOW_POWER_TIMER_CLOCK_HZ  (CLOCK_GetFreq(kCLOCK_LpoClk))

/* performance counter: */
#define configGENERATE_RUN_TIME_STATS_USE_TICKS     (1)
#define configGET_RUNTIMER_COUNTER_VALUE_FROM_ISR   AppGetRuntimeCounterValueFromISR
#define configCONFIGURE_TIMER_FOR_RUNTIME_STATS     AppConfigureTimerForRuntimeStats
/* -------------------------------------------------*/
/* Segger SystemViewer: */
#define configUSE_SEGGER_SYSTEM_VIEWER_HOOKS (0)
#define SYSVIEW_APP_NAME                     "tinyK22 Master"
#define SYSVIEW_DEVICE_NAME                  "NXP K22FN512"
#define McuSystemView_CONFIG_RTT_BUFFER_SIZE (512)
#define McuSystemView_CONFIG_RTT_CHANNEL     (1)
/* -------------------------------------------------*/
/* Percepio Tracealyzer */
#define configUSE_PERCEPIO_TRACE_HOOKS                  (0)
#define McuPercepio_CONFIG_START_TRACE_IN_STARTUP_MODE  TRC_START
/* -------------------------------------------------*/
/* Shell */
#define McuShell_CONFIG_PROJECT_NAME_STRING             "tinyK22 Master"
#define McuShell_CONFIG_MULTI_CMD_ENABLED               (1)
#define McuShell_CONFIG_DEFAULT_SHELL_BUFFER_SIZE       (256)
#define McuShell_CONFIG_MULTI_CMD_SIZE                  (200) /* max size of each command */
#define McuShellUart_CONFIG_UART                        McuShellUart_CONFIG_UART_K22FN512_LPUART0
#define McuShellUart_CONFIG_UART_GET_CLOCK_FREQ_SELECT  /*kCLOCK_PllFllSelClk*/ kCLOCK_Osc0ErClkUndiv
/* -------------------------------------------------*/
/* RTT */
#define McuRTT_CONFIG_RTT_BUFFER_SIZE_DOWN            (150)
#define McuRTT_CONFIG_BLOCKING_SEND                   (0)
#define McuRTT_CONFIG_BLOCKING_SEND_TIMEOUT_MS        (10)
#define McuRTT_CONFIG_BLOCKING_SEND_WAIT_MS           (5)
#define McuRTT_CONFIG_RTT_BUFFER_SIZE_UP              (256)
#if configUSE_SEGGER_SYSTEM_VIEWER_HOOKS
  #define McuRTT_CONFIG_RTT_MAX_NUM_UP_BUFFERS          (2)
  #define McuRTT_CONFIG_RTT_MAX_NUM_DOWN_BUFFERS        (2)
#else
  #define McuRTT_CONFIG_RTT_MAX_NUM_UP_BUFFERS          (1)
  #define McuRTT_CONFIG_RTT_MAX_NUM_DOWN_BUFFERS        (1)
#endif
/* ------------------- I2C ---------------------------*/
#define CONFIG_USE_HW_I2C                             (1) /* if using HW I2C, otherwise use software bit banging */
#define McuGenericI2C_CONFIG_USE_ON_ERROR_EVENT       (0)
#define McuGenericI2C_CONFIG_USE_ON_REQUEST_BUS_EVENT (0)
#define McuGenericI2C_CONFIG_USE_MUTEX                (1 && McuLib_CONFIG_SDK_USE_FREERTOS)
#define I2C_RETRY_TIMES   (5000) /* set retry count for SDK */

#if CONFIG_USE_HW_I2C /* implementation in i2clib.c */
  #define McuGenericI2C_CONFIG_INTERFACE_HEADER_FILE            "i2clib.h"
  #define McuGenericI2C_CONFIG_RECV_BLOCK                       I2CLIB_RecvBlock
  #define McuGenericI2C_CONFIG_SEND_BLOCK                       I2CLIB_SendBlock
  #if McuGenericI2C_CONFIG_SUPPORT_STOP_NO_START
  #define McuGenericI2C_CONFIG_SEND_BLOCK_CONTINUE              I2CLIB_SendBlockContinue
  #endif
  #define McuGenericI2C_CONFIG_SEND_STOP                        I2CLIB_SendStop
  #define McuGenericI2C_CONFIG_SELECT_SLAVE                     I2CLIB_SelectSlave
  #define McuGenericI2C_CONFIG_RECV_BLOCK_CUSTOM_AVAILABLE      (0)
  #define McuGenericI2C_CONFIG_RECV_BLOCK_CUSTOM                I2CLIB_RecvBlockCustom
#else
  /* settings for GenericSWI2C */
  #define SCL1_CONFIG_GPIO_NAME     GPIOE // I2CLIB_SCL_GPIO
  #define SCL1_CONFIG_PORT_NAME     PORTE //I2CLIB_SCL_GPIO_PORT
  #define SCL1_CONFIG_PIN_NUMBER    1 //I2CLIB_SCL_GPIO_PIN

  #define SDA1_CONFIG_GPIO_NAME     GPIOE //I2CLIB_SDA_GPIO
  #define SDA1_CONFIG_PORT_NAME     PORTE //I2CLIB_SDA_GPIO_PORT
  #define SDA1_CONFIG_PIN_NUMBER    0 //I2CLIB_SDA_GPIO_PIN

  /* I2C Pin Muxing */
  #define SDA1_CONFIG_DO_PIN_MUXING (1)
  #define SCL1_CONFIG_DO_PIN_MUXING (1)

  #define McuGenericSWI2C_CONFIG_DO_YIELD (0 && McuLib_CONFIG_SDK_USE_FREERTOS) /* because of Yield in GenericSWI2C */
  #define McuGenericSWI2C_CONFIG_DELAY_NS (0)
#endif
/* -------------------------------------------------*/
/* Time/Date */
#define McuTimeDate_CONFIG_USE_SOFTWARE_RTC                        (1) /* enable software RTC */
#define McuTimeDate_CONFIG_USE_EXTERNAL_HW_RTC                     (1) /* enable external I2C RTC */
#define McuTimeDate_CONFIG_USE_INTERNAL_HW_RTC                     (0) /* no internal RTC */

#define McuTimeDate_CONFIG_INIT_SOFTWARE_RTC_METHOD                (McuTimeDate_INIT_SOFTWARE_RTC_FROM_EXTERNAL_RTC)
#define McuTimeDate_CONFIG_USE_GET_TIME_DATE_METHOD                (McuTimeDate_GET_TIME_DATE_METHOD_SOFTWARE_RTC)
#define McuTimeDate_CONFIG_SET_TIME_DATE_METHOD_USES_SOFTWARE_RTC  (1) /* if using software RTC */
#define McuTimeDate_CONFIG_SET_TIME_DATE_METHOD_USES_EXTERNAL_RTC  (1) /* if using external I2C RTC */
#define McuTimeDate_CONFIG_SET_TIME_DATE_METHOD_USES_INTERNAL_RTC  (0) /* if using internal HW RTC */
/* -------------------------------------------------*/
/* stepper motor driver */
#define McuX12_017_CONFIG_QUAD_DRIVER    (1)  /* using quad driver */
/* -------------------------------------------------*/
/* McuLog */
#define McuLog_CONFIG_IS_ENABLED                      (0)
#define McuLog_CONFIG_DEFAULT_LEVEL                   (McuLog_DEBUG)
#define McuLog_CONFIG_USE_COLOR                       (0)
#define McuLog_CONFIG_NOF_CONSOLE_LOGGER              (3) /* RTT, UART and USB CDC */
#define McuLog_CONFIG_LOG_TIMESTAMP_DATE              (0)
#define McuLog_CONFIG_USE_RTT_DATA_LOGGER             (0)
#define McuLog_CONFIG_RTT_DATA_LOGGER_BUFFER_SIZE     (128)
/* -------------------------------------------------*/
/* RTC */
#define McuExtRTC_CONFIG_DEVICE                       3232  /* RTC device used */
/* -------------------------------------------------*/
/* EEPROM */
#define McuEE24_CONFIG_DEVICE_ID                32    /* AT24C32 */
#define McuEE24_CONFIG_DEVICE_I2C_ADDRESS_BITS  (7)   /* device is on address 0x57 */
#define McuEE24_CONFIG_HAS_WP_PIN               (0)   /* no WP Pin */
#define McuEE24_CONFIG_DO_ACKNOWLEDGE_POLLING   (0)   /* no acknowledge polling */
#define McuEE24_CONFIG_USE_TIMEOUT              (0)   /* no timeout */
/* ---------------------------------------------------------------------------------------*/
/* McuFlash for MinIni */
#define McuFlash_CONFIG_IS_ENABLED              (1)
/* ---------------------------------------------------------------------------------------*/
/* MinINI */
#define McuMinINI_CONFIG_FS                         (McuMinINI_CONFIG_FS_TYPE_FLASH_FS)
#define McuMinINI_CONFIG_FLASH_NVM_ADDR_START       ((0+512*1024)-McuMinINI_CONFIG_FLASH_NVM_BLOCK_SIZE)
#define McuMinINI_CONFIG_FLASH_NVM_BLOCK_SIZE       (0x800)
#define McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE    (256) /* must be 64, 128, 256, ... */
#define McuMinINI_CONFIG_READ_ONLY                  (0)
#define McuMinINI_CONFIG_USE_REAL                   (0)
/* -------------------------------------------------*/
/* McuUart485 */
#define McuUart485_CONFIG_USE_RS_485      (1)


#define McuWait_CONFIG_USE_CYCLE_COUNTER  (0)

#endif /* INCLUDEMCULIBCONFIG_H_ */
