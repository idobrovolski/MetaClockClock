/***********************************************************************************************************************
 * This file was generated by the MCUXpresso Config Tools. Any manual edits made to this file
 * will be overwritten if the respective MCUXpresso Config Tools is used to update this file.
 **********************************************************************************************************************/

/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
!!GlobalInfo
product: Pins v11.0
processor: LPC845
package_id: LPC845M301JBD48
mcu_data: ksdk2_0
processor_version: 11.0.1
board: LPC845BREAKOUT
pin_labels:
- {pin_num: '11', pin_signal: PIO1_0/CAPT_X1}
- {pin_num: '14', pin_signal: PIO1_1/CAPT_X2}
- {pin_num: '16', pin_signal: PIO1_2/CAPT_X3}
- {pin_num: '1', pin_signal: PIO1_8/CAPT_YL}
- {pin_num: '2', pin_signal: PIO0_13/ADC_10}
- {pin_num: '3', pin_signal: PIO1_9/CAPT_YH}
- {pin_num: '4', pin_signal: PIO0_12}
- {pin_num: '5', pin_signal: RESETN/PIO0_5, label: 'K2/CN3[10]/CN1[9]/CN1[34]/RESET/PIO0_5'}
- {pin_num: '6', pin_signal: PIO0_4/ADC_11}
- {pin_num: '7', pin_signal: PIO0_28/WKTCLKIN}
- {pin_num: '9', pin_signal: PIO0_31/CAPT_X0}
- {pin_num: '12', pin_signal: PIO0_11/I2C0_SDA}
- {pin_num: '13', pin_signal: PIO0_10/I2C0_SCL}
- {pin_num: '15', pin_signal: PIO0_16, label: RS485_RTS, identifier: RS485_RTS}
- {pin_num: '17', pin_signal: PIO0_27}
- {pin_num: '18', pin_signal: PIO0_26}
- {pin_num: '23', pin_signal: PIO1_4/CAPT_X5, label: PIO1_4/CAPT_X5, identifier: RS485_TXRX_EN}
- {pin_num: '25', pin_signal: PIO0_9/XTALOUT}
- {pin_num: '26', pin_signal: PIO0_8/XTALIN}
- {pin_num: '27', pin_signal: PIO1_5/CAPT_X6}
- {pin_num: '28', pin_signal: PIO1_6/CAPT_X7}
- {pin_num: '33', pin_signal: PIO0_7/ADC_0}
- {pin_num: '34', pin_signal: PIO0_6/ADC_1/ACMPVREF}
- {pin_num: '35', pin_signal: PIO1_7/CAPT_X8}
- {pin_num: '36', pin_signal: PIO0_0/ACMP_I1}
- {pin_num: '38', pin_signal: PIO0_29/DACOUT_1}
- {pin_num: '39', pin_signal: PIO0_23/ADC_3/ACMP_I4}
- {pin_num: '47', pin_signal: PIO0_18/ADC_8, label: RS485_RX, identifier: RS485_RX}
- {pin_num: '48', pin_signal: PIO0_17/ADC_9/DACOUT_0, label: RS485-TX, identifier: RS485_TX}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */

#include "fsl_common.h"
#include "fsl_iocon.h"
#include "fsl_swm.h"
#include "pin_mux.h"

/* FUNCTION ************************************************************************************************************
 *
 * Function Name : BOARD_InitBootPins
 * Description   : Calls initialization functions.
 *
 * END ****************************************************************************************************************/
void BOARD_InitBootPins(void)
{
    BOARD_InitSWD_DEBUGPins();
    BOARD_InitRS485();
    BOARD_InitCDC();
}

/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
BOARD_InitSWD_DEBUGPins:
- options: {callFromInitBoot: 'true', coreID: core0, enableClock: 'true'}
- pin_list:
  - {pin_num: '8', peripheral: SWD, signal: SWCLK, pin_signal: SWCLK/PIO0_3, mode: pullUp, invert: disabled, hysteresis: enabled, opendrain: disabled, smode: bypass,
    clkdiv: div0}
  - {pin_num: '10', peripheral: SWD, signal: SWDIO, pin_signal: SWDIO/PIO0_2, mode: pullUp, invert: disabled, hysteresis: enabled, opendrain: disabled, smode: bypass,
    clkdiv: div0}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */

/* FUNCTION ************************************************************************************************************
 *
 * Function Name : BOARD_InitSWD_DEBUGPins
 * Description   : Configures pin routing and optionally pin electrical features.
 *
 * END ****************************************************************************************************************/
/* Function assigned for the Cortex-M0P */
void BOARD_InitSWD_DEBUGPins(void)
{
    /* Enables clock for IOCON.: enable */
    CLOCK_EnableClock(kCLOCK_Iocon);
    /* Enables clock for switch matrix.: enable */
    CLOCK_EnableClock(kCLOCK_Swm);

    const uint32_t IOCON_INDEX_PIO0_2_config = (/* Selects pull-up function */
                                                IOCON_PIO_MODE_PULLUP |
                                                /* Enable hysteresis */
                                                IOCON_PIO_HYS_EN |
                                                /* Input not invert */
                                                IOCON_PIO_INV_DI |
                                                /* Disables Open-drain function */
                                                IOCON_PIO_OD_DI |
                                                /* Bypass input filter */
                                                IOCON_PIO_SMODE_BYPASS |
                                                /* IOCONCLKDIV0 */
                                                IOCON_PIO_CLKDIV0);
    /* PIO0 PIN2 (coords: 10) is configured as SWD, SWDIO. */
    IOCON_PinMuxSet(IOCON, IOCON_INDEX_PIO0_2, IOCON_INDEX_PIO0_2_config);

    const uint32_t IOCON_INDEX_PIO0_3_config = (/* Selects pull-up function */
                                                IOCON_PIO_MODE_PULLUP |
                                                /* Enable hysteresis */
                                                IOCON_PIO_HYS_EN |
                                                /* Input not invert */
                                                IOCON_PIO_INV_DI |
                                                /* Disables Open-drain function */
                                                IOCON_PIO_OD_DI |
                                                /* Bypass input filter */
                                                IOCON_PIO_SMODE_BYPASS |
                                                /* IOCONCLKDIV0 */
                                                IOCON_PIO_CLKDIV0);
    /* PIO0 PIN3 (coords: 8) is configured as SWD, SWCLK. */
    IOCON_PinMuxSet(IOCON, IOCON_INDEX_PIO0_3, IOCON_INDEX_PIO0_3_config);

    /* SWCLK connect to P0_3 */
    SWM_SetFixedPinSelect(SWM0, kSWM_SWCLK, true);

    /* SWDIO connect to P0_2 */
    SWM_SetFixedPinSelect(SWM0, kSWM_SWDIO, true);

    /* Disable clock for switch matrix. */
    CLOCK_DisableClock(kCLOCK_Swm);
}

/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
BOARD_InitRS485:
- options: {callFromInitBoot: 'true', coreID: core0, enableClock: 'true'}
- pin_list:
  - {pin_num: '15', peripheral: USART1, signal: RTS, pin_signal: PIO0_16, direction: OUTPUT}
  - {pin_num: '48', peripheral: USART1, signal: TXD, pin_signal: PIO0_17/ADC_9/DACOUT_0}
  - {pin_num: '47', peripheral: USART1, signal: RXD, pin_signal: PIO0_18/ADC_8}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */

/* FUNCTION ************************************************************************************************************
 *
 * Function Name : BOARD_InitRS485
 * Description   : Configures pin routing and optionally pin electrical features.
 *
 * END ****************************************************************************************************************/
/* Function assigned for the Cortex-M0P */
void BOARD_InitRS485(void)
{
    /* Enables clock for switch matrix.: enable */
    CLOCK_EnableClock(kCLOCK_Swm);

    /* USART1_TXD connect to P0_17 */
    SWM_SetMovablePinSelect(SWM0, kSWM_USART1_TXD, kSWM_PortPin_P0_17);

    /* USART1_RXD connect to P0_18 */
    SWM_SetMovablePinSelect(SWM0, kSWM_USART1_RXD, kSWM_PortPin_P0_18);

    /* USART1_RTS connect to P0_16 */
    SWM_SetMovablePinSelect(SWM0, kSWM_USART1_RTS, kSWM_PortPin_P0_16);

    /* Disable clock for switch matrix. */
    CLOCK_DisableClock(kCLOCK_Swm);
}

/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
BOARD_InitCDC:
- options: {callFromInitBoot: 'true', coreID: core0, enableClock: 'true'}
- pin_list:
  - {pin_num: '19', peripheral: USART0, signal: TXD, pin_signal: PIO0_25}
  - {pin_num: '20', peripheral: USART0, signal: RXD, pin_signal: PIO0_24}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */

/* FUNCTION ************************************************************************************************************
 *
 * Function Name : BOARD_InitCDC
 * Description   : Configures pin routing and optionally pin electrical features.
 *
 * END ****************************************************************************************************************/
/* Function assigned for the Cortex-M0P */
void BOARD_InitCDC(void)
{
    /* Enables clock for switch matrix.: enable */
    CLOCK_EnableClock(kCLOCK_Swm);

    /* USART0_TXD connect to P0_25 */
    SWM_SetMovablePinSelect(SWM0, kSWM_USART0_TXD, kSWM_PortPin_P0_25);

    /* USART0_RXD connect to P0_24 */
    SWM_SetMovablePinSelect(SWM0, kSWM_USART0_RXD, kSWM_PortPin_P0_24);

    /* Disable clock for switch matrix. */
    CLOCK_DisableClock(kCLOCK_Swm);
}

/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
BOARD_InitI2cPins:
- options: {callFromInitBoot: 'false', coreID: core0, enableClock: 'true'}
- pin_list:
  - {pin_num: '13', peripheral: I2C0, signal: SCL, pin_signal: PIO0_10/I2C0_SCL}
  - {pin_num: '12', peripheral: I2C0, signal: SDA, pin_signal: PIO0_11/I2C0_SDA}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */

/* FUNCTION ************************************************************************************************************
 *
 * Function Name : BOARD_InitI2cPins
 * Description   : Configures pin routing and optionally pin electrical features.
 *
 * END ****************************************************************************************************************/
/* Function assigned for the Cortex-M0P */
void BOARD_InitI2cPins(void)
{
    /* Enables clock for switch matrix.: enable */
    CLOCK_EnableClock(kCLOCK_Swm);

    /* I2C0_SDA connect to P0_11 */
    SWM_SetFixedPinSelect(SWM0, kSWM_I2C0_SDA, true);

    /* I2C0_SCL connect to P0_10 */
    SWM_SetFixedPinSelect(SWM0, kSWM_I2C0_SCL, true);

    /* Disable clock for switch matrix. */
    CLOCK_DisableClock(kCLOCK_Swm);
}

/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
BOARD_InitI2cPinsAsGPIO:
- options: {callFromInitBoot: 'false', coreID: core0, enableClock: 'true'}
- pin_list:
  - {pin_num: '12', peripheral: GPIO, signal: 'PIO0, 11', pin_signal: PIO0_11/I2C0_SDA}
  - {pin_num: '13', peripheral: GPIO, signal: 'PIO0, 10', pin_signal: PIO0_10/I2C0_SCL}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */

/* FUNCTION ************************************************************************************************************
 *
 * Function Name : BOARD_InitI2cPinsAsGPIO
 * Description   : Configures pin routing and optionally pin electrical features.
 *
 * END ****************************************************************************************************************/
/* Function assigned for the Cortex-M0P */
void BOARD_InitI2cPinsAsGPIO(void)
{

    /* Disable clock for switch matrix. */
    CLOCK_DisableClock(kCLOCK_Swm);
}
/***********************************************************************************************************************
 * EOF
 **********************************************************************************************************************/
