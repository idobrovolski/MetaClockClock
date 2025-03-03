/*
 * Copyright (c) 2019, 2020, Erich Styger
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "platform.h"

#include <assert.h>
#include "matrixconfig.h"
#include "matrix.h"
#include "rs485.h"
#include "McuUtility.h"
#include "stepper.h"
#include "Shell.h"
#include "McuLog.h"
#include "McuRTOS.h"
#if PL_CONFIG_USE_WDT
  #include "watchdog.h"
#endif
#include "StepperBoard.h"
#if PL_CONFIG_USE_STEPPER
  #include "McuX12_017.h"
#endif
#if PL_CONFIG_USE_MAG_SENSOR
  #include "magnets.h"
#endif
#if PL_CONFIG_USE_NVMC
  #include "nvmc.h"
#endif
#if PL_CONFIG_USE_NEO_PIXEL_HW
  #include "NeoStepperRing.h"
  #include "application.h"
#endif
#include "NeoPixel.h"
#include "mfont.h"
#include "matrixposition.h"
#include "matrixhand.h"
#include "matrixring.h"
#include "matrixpixel.h"

#define STEPPER_HAND_ZERO_DELAY     (2)

#if PL_CONFIG_IS_ANALOG_CLOCK && (PL_CONFIG_USE_NEO_PIXEL_HW || PL_MATRIX_CONFIG_IS_RGB)
  static uint32_t MATRIX_LedHandColor = PL_CONFIG_MATRIX_DEFAULT_HAND_COLOR;
  static uint8_t MATRIX_LedHandBrightness = PL_CONFIG_MATRIX_DEFAULT_HAND_BRIGHTNESS; /* led brightness, 0-255 */
#endif

#if PL_CONFIG_USE_STEPPER
  /* list of boards */
  static STEPBOARD_Handle_t MATRIX_Boards[MATRIX_NOF_BOARDS];
  static SemaphoreHandle_t semMatrixExecuteQueue; /* semaphore use to trigger processing the matrix queues */
#else /* used in case no stepper and no LED rings are used */
  /* list of bards defined in matrixconfig.h */
#endif

#if PL_CONFIG_IS_MASTER
  MATRIX_Matrix_t matrix; /* map of current matrix */
  static MATRIX_Matrix_t prevMatrix; /* map of previous matrix, used to reduce communication traffic */
#endif /* PL_CONFIG_IS_MASTER */

static MATRIX_IgnoreCallbackFct MATRIX_IgnoredCallback = NULL; /* optional callback to ignore a coordinate */

void MATRIX_SetIgnoreCallback(MATRIX_IgnoreCallbackFct fct) {
  MATRIX_IgnoredCallback = fct;
}

#if PL_CONFIG_USE_X12_STEPPER
typedef struct X12_Stepper_t {
  McuX12_017_Motor_e x12motor; /* which motor */
  McuX12_017_Handle_t x12device; /* motor driver IC */
#if PL_CONFIG_USE_MAG_SENSOR
  MAG_MagSensor_e mag;
#endif
} X12_Stepper_t;

static X12_Stepper_t x12Steppers[MATRIX_NOF_STEPPERS]; /* all the stepper motors on a board */

static void X12_SingleStep(void *dev, int step) {
  X12_Stepper_t *device = (X12_Stepper_t*)dev;

  McuX12_017_SingleStep(device->x12device, device->x12motor, step);
}
#endif /* PL_CONFIG_USE_X12_STEPPER */

#if PL_CONFIG_USE_X12_STEPPER
void MATRIX_EnableStepper(void *dev) {
  X12_Stepper_t *device = (X12_Stepper_t*)dev;
  McuX12_017_EnableDevice(device->x12device);
}
#endif

#if PL_CONFIG_USE_X12_STEPPER
void MATRIX_DisableStepper(void *dev) {
  X12_Stepper_t *device = (X12_Stepper_t*)dev;
  McuX12_017_DisableDevice(device->x12device);
}
#endif

#if PL_CONFIG_USE_STEPPER
STEPBOARD_Handle_t MATRIX_AddrGetBoard(uint8_t addr) {
  for(int i=0; i<MATRIX_NOF_BOARDS; i++){
    if (STEPBOARD_GetAddress(MATRIX_Boards[i])==addr) {
      return MATRIX_Boards[i];
    }
  }
  return NULL;
}
#endif

#if PL_CONFIG_USE_STEPPER
STEPPER_Handle_t MATRIX_GetStepper(int32_t x, int32_t y, int32_t z) {
  STEPPER_Handle_t stepper;
  STEPBOARD_Handle_t board;

  assert(x<MATRIX_NOF_STEPPERS_X && y<MATRIX_NOF_STEPPERS_Y && z<MATRIX_NOF_STEPPERS_Z);
#if PL_CONFIG_IS_MASTER
  board = MATRIX_AddrGetBoard(clockMatrix[x][y][z].addr);
  if (board==NULL) {
    return NULL;
  }
  int boardX, boardY, boardZ;

  boardX = clockMatrix[x][y][z].board.x;
  boardY = clockMatrix[x][y][z].board.y;
  boardZ = clockMatrix[x][y][z].board.z;
  stepper = STEPBOARD_GetStepper(board, boardX, boardY, boardZ);
#else
  board = STEPBOARD_GetBoard();
  if (board==NULL) {
    return NULL;
  }
  stepper = STEPBOARD_GetStepper(board, x, y, z);
#endif
  return stepper;
}
#endif

#if PL_CONFIG_USE_LED_RING && PL_CONFIG_USE_NEO_PIXEL_HW
NEOSR_Handle_t MATRIX_GetLedRingDevice(int32_t x, int32_t y, uint8_t z) {
  assert(x<MATRIX_NOF_STEPPERS_X && y<MATRIX_NOF_STEPPERS_Y && z<MATRIX_NOF_STEPPERS_Z);
#if PL_CONFIG_USE_VIRTUAL_STEPPER /* virtual stepper */
  return STEPPER_GetDevice(MATRIX_GetStepper(x, y, z));
#elif PL_CONFIG_USE_X12_LED_STEPPER
  return STEPBOARD_GetStepperLedRing(MATRIX_Boards[0], x, y, z);
#else
  #error "wrong configuration"
  return ERR_FAILED;
#endif
}
#endif /* PL_CONFIG_USE_LED_RING */

#if PL_CONFIG_IS_ANALOG_CLOCK && (PL_CONFIG_USE_NEO_PIXEL_HW || PL_MATRIX_CONFIG_IS_RGB)
void MATRIX_GetHandColorBrightness(uint32_t *pColor, uint8_t *pBrightness) {
  *pColor = MATRIX_LedHandColor;
  *pBrightness = MATRIX_LedHandBrightness;
}
#endif

#if PL_CONFIG_IS_ANALOG_CLOCK && (PL_CONFIG_USE_NEO_PIXEL_HW || PL_MATRIX_CONFIG_IS_RGB)
NEO_PixelColor MATRIX_GetHandColorAdjusted(void) {
  return NEO_BrightnessFactorColor(MATRIX_LedHandColor, MATRIX_LedHandBrightness);
}
#endif

#if PL_CONFIG_USE_RS485 && PL_CONFIG_IS_MASTER
void MATRIX_SendCmdToBoard(uint8_t toAddr, unsigned char *cmd) {
  uint8_t res;
  uint8_t addr;

  if (toAddr==RS485_BROADCAST_ADDRESS) {
    for(int i=0; i<MATRIX_NOF_BOARDS; i++) {
      addr = MATRIX_BoardList[i].addr;
      res = RS485_SendCommand(addr, cmd, 1000, false, 1);
      if (res!=ERR_OK) {
        McuLog_error("failed sending command '%s' to %d", cmd, addr);
      }
    } /* for */
  } else {
    res = RS485_SendCommand(toAddr, cmd, 1000, false, 1);
    if (res!=ERR_OK) {
      McuLog_error("failed sending command '%s' to %d", cmd, toAddr);
    }
  }
}
#endif /* PL_CONFIG_USE_RS485 */

#if PL_CONFIG_USE_LED_DIMMING
void MATRIX_SetHandBrightness(int32_t x, int32_t y, int32_t z, uint8_t brightness) {
  assert(x<MATRIX_NOF_STEPPERS_X && y<MATRIX_NOF_STEPPERS_Y && z<MATRIX_NOF_STEPPERS_Z);
  NEOSR_SetHandBrightness(MATRIX_GetLedRingDevice(x, y, z), brightness);
}
#endif

#if PL_CONFIG_USE_LED_DIMMING
void MATRIX_StartHandDimming(int32_t x, int32_t y, int32_t z, uint8_t targetBrightness) {
  assert(x<MATRIX_NOF_STEPPERS_X && y<MATRIX_NOF_STEPPERS_Y && z<MATRIX_NOF_STEPPERS_Z);
  NEOSR_StartHandDimming(MATRIX_GetLedRingDevice(x, y, z), targetBrightness);
}
#endif

#if PL_CONFIG_USE_LED_DIMMING
void MATRIX_SetHandBrightnessAll(uint8_t brightness) {
  for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
    for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        MATRIX_SetHandBrightness(x, y, z, brightness);
      }
    }
  }
}
#endif


uint8_t MATRIX_GetAddress(int32_t x, int32_t y, int32_t z) {
#if PL_CONFIG_IS_MASTER
  return clockMatrix[x][y][z].addr;
#else
  return RS485_GetAddress();
#endif
}

#if PL_CONFIG_IS_MASTER
void MATRIX_Delay(int32_t ms) {
  while (ms>100) { /* wait in smaller pieces to keep watchdog task informed */
    vTaskDelay(pdMS_TO_TICKS(100));
#if PL_CONFIG_USE_WDT
  WDT_Report(WDT_REPORT_ID_CURR_TASK, 100);
#endif
    ms -= 100;
  }
  /* wait for the remaining time */
  vTaskDelay(pdMS_TO_TICKS(ms));
#if PL_CONFIG_USE_WDT
  WDT_Report(WDT_REPORT_ID_CURR_TASK, ms);
#endif
}
#endif

#if PL_CONFIG_IS_MASTER

#if PL_MATRIX_CONFIG_IS_RGB
void MATRIX_DrawRingColor(uint8_t x, uint8_t y, uint8_t z, uint32_t color) {
  assert(x<MATRIX_NOF_STEPPERS_X && y<MATRIX_NOF_STEPPERS_Y && z<MATRIX_NOF_STEPPERS_Z);
  matrix.colorRingMap[x][y][z] = color;
}
#endif

#if PL_MATRIX_CONFIG_IS_RGB
void MATRIX_DrawAllRingColor(uint32_t color) {
  for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
    for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        MATRIX_DrawRingColor(x, y, z, color);
      }
    }
  }
}
#endif

#if MATRIX_NOF_STEPPERS_Z==2
void MATRIX_SetMoveDelayZ0Z1Checked(uint8_t x, uint8_t y, uint8_t delay0, uint8_t delay1) {
  if (x>=MATRIX_NOF_STEPPERS_X || y>=MATRIX_NOF_STEPPERS_Y) {
    return;
  }
  matrix.delayMap[x][y][0] = delay0;
  matrix.delayMap[x][y][1] = delay1;
}
#endif

#if MATRIX_NOF_STEPPERS_Z==2
void MATRIX_SetMoveDelayZ0Z1All(uint8_t delay0, uint8_t delay1) {
  for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
    for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
      matrix.delayMap[x][y][0] = delay0;
      matrix.delayMap[x][y][1] = delay1;
    }
  }
}
#endif

void MATRIX_SetMoveDelayAll(uint8_t delay) {
  for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
    for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        matrix.delayMap[x][y][z] = delay;
      }
    }
  }
}
#endif /* PL_CONFIG_IS_MASTER */

#if PL_CONFIG_IS_MASTER
static void MATRIX_ResetBoardListCmdSent(void) {
  for(int i=0; i<MATRIX_NOF_BOARDS; i++) { /* initialize array */
    MATRIX_BoardList[i].cmdSent = false;
  }
}

static bool MATRIX_CommandHasBeenSentToBoards(void) {
  for(int i=0; i<MATRIX_NOF_BOARDS; i++) { /* initialize array */
    if (MATRIX_BoardList[i].cmdSent) {
      return true;
    }
  }
  return false;
}

static bool MATRIX_CommandHasBeenSentToBoard(unsigned int i) {
  if (i>=MATRIX_NOF_BOARDS) {
    return false; /* error case */
  }
  return MATRIX_BoardList[i].cmdSent;
}
#endif

#if PL_CONFIG_IS_MASTER
uint8_t MATRIX_WaitForIdle(int32_t timeoutMs) {
  bool boardIsIdle[MATRIX_NOF_BOARDS];
  uint8_t res;
  uint8_t addr;

  for(int i=0; i<MATRIX_NOF_BOARDS; i++) { /* initialize array */
    boardIsIdle[i] = false;
  }
  for(;;) { /* breaks or returns */
    for(int i=0; i<MATRIX_NOF_BOARDS; i++) { /* go through all boards */
      if (!boardIsIdle[i]) { /* ask board if it is still not idle */
#if PL_CONFIG_IS_MASTER
        addr = MATRIX_BoardList[i].addr;
#else
        addr = STEPBOARD_GetAddress(MATRIX_Boards[i]);
#endif
        if (MATRIX_CommandHasBeenSentToBoard(i)) {
          McuLog_trace("Waiting for idle (addr 0x%02x)", addr);
          res = RS485_SendCommand(addr, (unsigned char*)"idle", 1000, false, 1); /* ask board if it is idle */
          if (res==ERR_OK) { /* board is idle */
            boardIsIdle[i] = true;
          }
        } else { /* board is not enabled or has not received a comand, so we consider it as idle */
          boardIsIdle[i] = true;
        }
      }
    } /* for all boards */
    /* check if all boards are idle now */
    for(int i=0; /* breaks */; i++) {
      if (i==MATRIX_NOF_BOARDS) {
        McuLog_trace("Waiting for idle: all idle");
        return ERR_OK; /* all boards are idle now */
      }
      if (!boardIsIdle[i]) {
        break; /* at least one is still not idle: break this loop and check non-idle boards again */
      }
    } /* for */
    MATRIX_Delay(1000); /* give boards time to get idle */
  #if PL_CONFIG_USE_WDT
    WDT_Report(WDT_REPORT_ID_CURR_TASK, 1000);
  #endif
    timeoutMs -= 1000;
    if (timeoutMs<0) {
      McuLog_error("Timeout.");
      return ERR_BUSY;
    }
  } /* for which breaks or returns */
  return ERR_OK;
}

static const unsigned char*GetModeString(STEPPER_MoveMode_e mode, bool speedUp, bool slowDown) {
  const unsigned char *str = (unsigned char*)"SH"; /* default and error case */
  if (speedUp) {
    if (slowDown) {
      switch(mode) {
        case STEPPER_MOVE_MODE_SHORT: str = (unsigned char*)"sh";  break;
        case STEPPER_MOVE_MODE_CW:    str = (unsigned char*)"cw";  break;
        case STEPPER_MOVE_MODE_CCW:   str = (unsigned char*)"cc";  break;
      }
    } else {
      switch(mode) {
        case STEPPER_MOVE_MODE_SHORT: str = (unsigned char*)"sH";  break;
        case STEPPER_MOVE_MODE_CW:    str = (unsigned char*)"cW";  break;
        case STEPPER_MOVE_MODE_CCW:   str = (unsigned char*)"cC";  break;
      }
    }
  } else {
    if (slowDown) {
      switch(mode) {
        case STEPPER_MOVE_MODE_SHORT: str = (unsigned char*)"Sh";  break;
        case STEPPER_MOVE_MODE_CW:    str = (unsigned char*)"Cw";  break;
        case STEPPER_MOVE_MODE_CCW:   str = (unsigned char*)"Cc";  break;
      }
    } else {
      switch(mode) {
        case STEPPER_MOVE_MODE_SHORT: str = (unsigned char*)"SH";  break;
        case STEPPER_MOVE_MODE_CW:    str = (unsigned char*)"CW";  break;
        case STEPPER_MOVE_MODE_CCW:   str = (unsigned char*)"CC";  break;
      }
    }
  }
  return str;
}

#if PL_CONFIG_USE_NEO_PIXEL_HW
static void QueueMoveCommand(int x, int y, int z, int angle, int delay, STEPPER_MoveMode_e mode, bool speedUp, bool slowDown, bool absolute) {
  uint8_t buf[McuShell_CONFIG_DEFAULT_SHELL_BUFFER_SIZE];

  /*  matrix q <x> <y> <z> a <angle> <delay> <mode> */
  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"matrix q ");
  McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.x); /* <x> */
  McuUtility_chcat(buf, sizeof(buf), ' ');
  McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.y); /* <y> */
  McuUtility_chcat(buf, sizeof(buf), ' ');
  McuUtility_strcatNum8u(buf, sizeof(buf), z); /* <z> */
  McuUtility_strcat(buf, sizeof(buf), absolute?(unsigned char*)" a ":(unsigned char*)" r ");
  McuUtility_strcatNum32s(buf, sizeof(buf), angle); /* <a> */
  McuUtility_chcat(buf, sizeof(buf), ' ');
  McuUtility_strcatNum16u(buf, sizeof(buf), delay); /* <d> */
  McuUtility_chcat(buf, sizeof(buf), ' ');
  McuUtility_strcat(buf, sizeof(buf), GetModeString(mode, speedUp, slowDown));
  (void)RS485_SendCommand(clockMatrix[x][y][z].addr, buf, 1000, true, 1); /* queue the command for the remote boards */
#if PL_CONFIG_USE_NEO_PIXEL_HW
  /* build a command for the LED rings:  */
  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"matrix q ");
  McuUtility_strcatNum8u(buf, sizeof(buf), x); /* <x> */
  McuUtility_chcat(buf, sizeof(buf), ' ');
  McuUtility_strcatNum8u(buf, sizeof(buf), y); /* <y> */
  McuUtility_chcat(buf, sizeof(buf), ' ');
  McuUtility_strcatNum8u(buf, sizeof(buf), z); /* <z> */
  McuUtility_strcat(buf, sizeof(buf), absolute?(unsigned char*)" a ":(unsigned char*)" r ");
  McuUtility_strcatNum32s(buf, sizeof(buf), angle); /* <a> */
  McuUtility_chcat(buf, sizeof(buf), ' ');
  McuUtility_strcatNum16u(buf, sizeof(buf), delay); /* <d> */
  McuUtility_chcat(buf, sizeof(buf), ' ');
  McuUtility_strcat(buf, sizeof(buf), GetModeString(mode, speedUp, slowDown));
  (void)RS485_SendCommand(RS485_GetAddress(), buf, 1000, true, 1); /* queue the command for ourself (LED ring) */
#endif
}
#endif

#if PL_CONFIG_USE_RS485
static uint8_t QueueBoardMoveCommand(uint8_t addr, bool *cmdSent) {
  /* example command: "@14 03 63 cmd matrix q 0 0 0 a 90 2 cc,0 0 1 a 180 4 cw" */
  uint8_t buf[McuShell_CONFIG_DEFAULT_SHELL_BUFFER_SIZE];
  uint8_t resBoards;
#if PL_CONFIG_USE_NEO_PIXEL_HW
  uint8_t ledbuf[McuShell_CONFIG_DEFAULT_SHELL_BUFFER_SIZE];
  uint8_t resLeds;
#endif
  int nof = 0;
  bool isRelative = false;

  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"matrix q ");
#if PL_CONFIG_USE_NEO_PIXEL_HW
  McuUtility_strcpy(ledbuf, sizeof(ledbuf), (unsigned char*)"matrix q ");
#endif
  for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) { /* every clock row */
    for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) { /* every clock in column */
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        if (MATRIX_IgnoredCallback==NULL || !MATRIX_IgnoredCallback(x,y,z)) { /* callback exists and not ignored coordinate */
          if (clockMatrix[x][y][z].addr==addr) { /* check if is a matching board */
            #if PL_MATRIX_CONFIG_IS_RGB
            /* *************** hand enable command *********************** */
            if (matrix.enabledHandMap[x][y][z]!=prevMatrix.enabledHandMap[x][y][z]) { /* only send changes */
              if (nof>0) {
                McuUtility_strcat(buf, sizeof(buf), (unsigned char*)",");
              }
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.x); /* <x> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.y); /* <y> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), z); /* <z> */
              McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" he ");
              McuUtility_strcat(buf, sizeof(buf), matrix.enabledHandMap[x][y][z]?(unsigned char*)"on":(unsigned char*)"off");
              nof++;
            }
            #endif

            #if PL_CONFIG_USE_EXTENDED_HANDS
            /* *************** 2nd hand enable command *********************** */
            if (matrix.enabled2ndHandMap[x][y][z]!=prevMatrix.enabled2ndHandMap[x][y][z]) { /* only send changes */
              if (nof>0) {
                McuUtility_strcat(buf, sizeof(buf), (unsigned char*)",");
              }
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.x); /* <x> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.y); /* <y> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), z); /* <z> */
              McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" he2 ");
              McuUtility_strcat(buf, sizeof(buf), matrix.enabled2ndHandMap[x][y][z]?(unsigned char*)"on":(unsigned char*)"off");
              nof++;
            }
            #endif

            #if PL_CONFIG_USE_LED_RING
            /* *************** ring enable command *********************** */
            if (matrix.enabledRingMap[x][y][z]!=prevMatrix.enabledRingMap[x][y][z]) { /* only send changes */
              if (nof>0) {
                McuUtility_strcat(buf, sizeof(buf), (unsigned char*)",");
              }
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.x); /* <x> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.y); /* <y> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), z); /* <z> */
              McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" re ");
              McuUtility_strcat(buf, sizeof(buf), matrix.enabledRingMap[x][y][z]?(unsigned char*)"on":(unsigned char*)"off");
              nof++;
            }
            #endif

            /* *************** move command *********************** */
  #if PL_CONFIG_USE_RELATIVE_MOVES
            isRelative = matrix.relAngleMap[x][y][z]!=0;
  #else
            isRelative = false;
  #endif
            if (isRelative || matrix.angleMap[x][y][z]!=prevMatrix.angleMap[x][y][z]) { /* only send changes */
              if (nof>0) {
                McuUtility_chcat(buf, sizeof(buf), ',');
              }
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.x); /* <x> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.y); /* <y> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), z); /* <z> */
              if (isRelative) {
  #if PL_CONFIG_USE_RELATIVE_MOVES
                McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" r ");
                McuUtility_strcatNum16s(buf, sizeof(buf), matrix.relAngleMap[x][y][z]); /* <a> */
                matrix.angleMap[x][y][z] += matrix.relAngleMap[x][y][z]; /* update to expected position */
                matrix.relAngleMap[x][y][z] = 0; /* set back to zero as it gets executed */
  #endif
              } else {
                McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" a ");
                McuUtility_strcatNum16s(buf, sizeof(buf), matrix.angleMap[x][y][z]); /* <a> */
              }
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum16u(buf, sizeof(buf), matrix.delayMap[x][y][z]); /* <d> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcat(buf, sizeof(buf), GetModeString(matrix.moveMap[x][y][z], true, true));
            #if PL_CONFIG_USE_NEO_PIXEL_HW
              /* build a command for the LED rings (virtual steppers for it):  */
              if (nof>0) {
                McuUtility_chcat(ledbuf, sizeof(ledbuf), ',');
              }
              McuUtility_strcatNum8u(ledbuf, sizeof(ledbuf), x); /* <x> */
              McuUtility_chcat(ledbuf, sizeof(ledbuf), ' ');
              McuUtility_strcatNum8u(ledbuf, sizeof(ledbuf), y); /* <y> */
              McuUtility_chcat(ledbuf, sizeof(ledbuf), ' ');
              McuUtility_strcatNum8u(ledbuf, sizeof(ledbuf), z); /* <z> */
              McuUtility_strcat(ledbuf, sizeof(ledbuf), isRelative?(unsigned char*)" r ":(unsigned char*)" a ");
              McuUtility_strcatNum16s(ledbuf, sizeof(ledbuf), matrix.angleMap[x][y][z]); /* <a> */
              McuUtility_chcat(ledbuf, sizeof(ledbuf), ' ');
              McuUtility_strcatNum16u(ledbuf, sizeof(ledbuf), matrix.delayMap[x][y][z]); /* <d> */
              McuUtility_chcat(ledbuf, sizeof(ledbuf), ' ');
              McuUtility_strcat(ledbuf, sizeof(ledbuf), GetModeString(matrix.moveMap[x][y][z], true, true));
            #endif
              nof++;
            }
            /* *************** commands *********************** */
          } /* addr */
        } /* not ignored */
      } /* z */
    } /* x */
  } /* y */
  if (nof>0) {
    *cmdSent = true;
    McuLog_trace("Queue enable & move (0x%02x)", addr);
    resBoards = RS485_SendCommand(addr, buf, 1000, true, 1); /* queue the command for the remote board */
#if PL_CONFIG_USE_NEO_PIXEL_HW
    resLeds = RS485_SendCommand(RS485_GetAddress(), ledbuf, 1000, true, 1); /* queue the command for ourself (LED ring) */
    if (resBoards!=ERR_OK || resLeds!=ERR_OK) {
      return ERR_FAILED;
    }
#else
    if (resBoards!=ERR_OK) {
      return ERR_FAILED;
    }
#endif
  }
  return ERR_OK;
}
#endif /* PL_CONFIG_USE_RS485 */

#if PL_MATRIX_CONFIG_IS_RGB
static uint8_t QueueBoardHandColorCommand(uint8_t addr, bool *cmdSent) {
  /* example command: "@14 03 63 cmd matrix q 0 0 0 hc 0x100000 , ..." */
  uint8_t buf[McuShell_CONFIG_DEFAULT_SHELL_BUFFER_SIZE];
  uint8_t resBoards;
  int nof = 0;

  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"matrix q ");
  for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) { /* every clock row */
    for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) { /* every clock in column */
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        if (MATRIX_IgnoredCallback==NULL || !MATRIX_IgnoredCallback(x,y,z)) { /* callback exists and not ignored coordinate */
          if (clockMatrix[x][y][z].addr==addr) { /* check if is a matching board and clock is enabled */
            if (matrix.colorHandMap[x][y][z]!=prevMatrix.colorHandMap[x][y][z]) { /* only send changes */
              if (nof>0) {
                McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" ,");
              }
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.x); /* <x> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.y); /* <y> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), z); /* <z> */
              McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" hc 0x");
              McuUtility_strcatNum24Hex(buf, sizeof(buf), matrix.colorHandMap[x][y][z]); /* color */
              nof++;
            }
        #if PL_CONFIG_USE_EXTENDED_HANDS
            if (matrix.color2ndHandMap[x][y][z]!=prevMatrix.color2ndHandMap[x][y][z]) { /* only send changes */
              if (nof>0) {
                McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" ,");
              }
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.x); /* <x> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.y); /* <y> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), z); /* <z> */
              McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" hc2 0x");
              McuUtility_strcatNum24Hex(buf, sizeof(buf), matrix.color2ndHandMap[x][y][z]); /* color */
              nof++;
            }
        #endif
          } /* if */
        } /* not ignored */
      }  /* z */
    } /* x */
  } /* y */
  if (nof>0) {
    *cmdSent = true;
    McuLog_trace("Queue hand color (0x%02x)", addr);
    resBoards = RS485_SendCommand(addr, buf, 1000, true, 1); /* queue the command for the remote board */
    if (resBoards!=ERR_OK) {
      return ERR_FAILED;
    }
  }
  return ERR_OK;
}
#endif

#if PL_CONFIG_USE_LED_RING
static uint8_t QueueBoardRingColorCommand(uint8_t addr, bool *cmdSent) {
  /* example command: "@14 03 63 cmd matrix q 0 0 0 rc 0x100000 ,..." */
  uint8_t buf[McuShell_CONFIG_DEFAULT_SHELL_BUFFER_SIZE];
  uint8_t resBoards;
  int nof = 0;

  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"matrix q ");
  for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) { /* every clock row */
    for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) { /* every clock in column */
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        if (MATRIX_IgnoredCallback==NULL || !MATRIX_IgnoredCallback(x,y,z)) { /* callback exists and not ignored coordinate */
          if (clockMatrix[x][y][z].addr==addr) { /* check if is a matching board and clock is enabled */
            if (matrix.colorRingMap[x][y][z]!=prevMatrix.colorRingMap[x][y][z]) { /* only send changes */
              if (nof>0) {
                McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" ,");
              }
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.x); /* <x> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), clockMatrix[x][y][z].board.y); /* <y> */
              McuUtility_chcat(buf, sizeof(buf), ' ');
              McuUtility_strcatNum8u(buf, sizeof(buf), z); /* <z> */
              McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" rc 0x");
              McuUtility_strcatNum24Hex(buf, sizeof(buf), matrix.colorRingMap[x][y][z]); /* color */
              nof++;
            }
          } /* if addr */
        } /* if not ignored */
      } /* z */
    } /* x */
  } /* y */
  if (nof>0) {
    *cmdSent = true;
    McuLog_trace("Queue ring color (0x%02x)", addr);
    resBoards = RS485_SendCommand(addr, buf, 1000, true, 1); /* queue the command for the remote board */
    if (resBoards!=ERR_OK) {
      return ERR_FAILED;
    }
  }
  return ERR_OK;
}
#endif

static void MATRIX_CopyMatrix(MATRIX_Matrix_t *dst, MATRIX_Matrix_t *src) {
  memcpy(dst, src, sizeof(MATRIX_Matrix_t));
}

#if PL_CONFIG_USE_RS485
uint8_t MATRIX_SendToRemoteQueue(void) {
  uint8_t res;

  for(int i=0; i<MATRIX_NOF_BOARDS; i++) {
#if PL_MATRIX_CONFIG_IS_RGB
    /* queue the color commands first so they get executed first */
    res = QueueBoardHandColorCommand(MATRIX_BoardList[i].addr, &MATRIX_BoardList[i].cmdSent);
    if (res!=ERR_OK) {
      break;
    }
  #if PL_CONFIG_USE_LED_RING
    res = QueueBoardRingColorCommand(MATRIX_BoardList[i].addr, &MATRIX_BoardList[i].cmdSent);
    if (res!=ERR_OK) {
      break;
    }
  #endif
#endif
    res = QueueBoardMoveCommand(MATRIX_BoardList[i].addr, &MATRIX_BoardList[i].cmdSent);
    if (res!=ERR_OK) {
      break;
    }
  } /* for */
  if (res!=ERR_OK) {
    return res;
  }
  MATRIX_CopyMatrix(&prevMatrix, &matrix); /* make backup */
  return ERR_OK;
}
#endif /* PL_CONFIG_USE_RS485 */

#if PL_CONFIG_USE_RS485
static uint8_t MATRIX_CheckRemoteLastError(void) {
  bool boardHasError[MATRIX_NOF_BOARDS];
  uint8_t res;
  uint8_t addr;

  for(int i=0; i<MATRIX_NOF_BOARDS; i++) {
    boardHasError[i] = false;
  }
  for(int i=0; i<MATRIX_NOF_BOARDS; i++) {
    if (!boardHasError[i]) { /* ask board if it is still not idle */
#if PL_CONFIG_IS_MASTER
      addr = MATRIX_BoardList[i].addr;
#else
      addr = STEPBOARD_GetAddress(MATRIX_Boards[i]);
#endif
      if (MATRIX_CommandHasBeenSentToBoard(i)) {
        McuLog_trace("Checking last error (addr 0x%02x)", addr);
        res = RS485_SendCommand(addr, (unsigned char*)"lastError", 1000, false, 1); /* ask board if there was an error */
        if (res==ERR_OK) { /* no error */
          boardHasError[i] = false;
        } else { /* send command again! */
          boardHasError[i] = true;
          (void)RS485_SendCommand(addr, (unsigned char*)"matrix exq", 1000, true, 1); /* execute the queue */
        }
      } else { /* board is not enabled, so it is considered to be fine */
        boardHasError[i] = false;
      }
    }
  } /* for */
  return ERR_OK;
}
#endif /* PL_CONFIG_USE_RS485 */

#if PL_CONFIG_USE_RS485
static uint8_t SendExecuteCommand(void) {
  McuLog_trace("Sending broadcast exq");
  /* send broadcast execute queue command */
  (void)RS485_SendCommand(RS485_BROADCAST_ADDRESS, (unsigned char*)"matrix exq", 1000, true, 0); /* execute the queue */
  /* check with lastEror if all have received the message */
#if PL_CONFIG_CHECK_LAST_ERROR
  return MATRIX_CheckRemoteLastError();
#else
  return ERR_OK;
#endif
}
#endif

#if PL_CONFIG_USE_RS485
uint8_t MATRIX_ExecuteRemoteQueueAndWait(bool wait) {
  bool failed = false;

  if (!MATRIX_CommandHasBeenSentToBoards()) {
    return ERR_OK; /* nothing was sent to the boards, so don't need to do anyhing */
  }
  if (SendExecuteCommand()!=ERR_OK) {
    failed = true;
  }
  if (wait) {
    MATRIX_Delay(500); /* give the clocks some time to start executing */
    if (MATRIX_WaitForIdle(30000)!=ERR_OK) {
      failed = true;
    }
  }
  MATRIX_ResetBoardListCmdSent();
  if (failed) {
    return ERR_FAILED;
  }
  return ERR_OK;;
}
#endif

uint8_t MATRIX_SendToRemoteQueueExecuteAndWait(bool wait) {
#if PL_CONFIG_USE_RS485
  uint8_t resSend, resExecute;

  resSend = MATRIX_SendToRemoteQueue();
  resExecute = MATRIX_ExecuteRemoteQueueAndWait(wait);
  if (resSend!=ERR_OK || resExecute!=ERR_OK) {
    return ERR_FAILED;
  }
#endif /* PL_CONFIG_USE_RS485 */
  return ERR_OK;
}

#endif /* PL_CONFIG_IS_MASTER */

#if PL_CONFIG_IS_MASTER
static uint8_t MATRIX_SendMatrixCmdToAllBoards(const unsigned char *cmd) {
#if PL_CONFIG_USE_RS485
  uint8_t res;
  uint8_t addr;
  bool hasError = false;

  for(int i=0; i<MATRIX_NOF_BOARDS; i++) { /* go through all boards */
    addr = MATRIX_BoardList[i].addr;
    McuLog_trace("Sending '%s' to board 0x%02x", cmd, addr);
    res = RS485_SendCommand(addr, cmd, 1000, false, 1);
    if (res!=ERR_OK) {
      McuLog_error("failed sending command '%s' to board 0x%x", cmd, addr);
      hasError = true;
    }
  }
  if (hasError) {
    return ERR_FAILED;
  }
#endif
  return ERR_OK;
}
#endif /* PL_CONFIG_IS_MASTER */

#if PL_CONFIG_IS_ANALOG_CLOCK
static uint8_t MATRIX_MoveAlltoHour(uint8_t hour, int32_t timeoutMs, const McuShell_StdIOType *io) {
  if (hour>=12) {
    hour = 0;
  }
#if PL_CONFIG_IS_MASTER && PL_CONFIG_USE_RS485
#if PL_CONFIG_USE_EXTENDED_HANDS
  MHAND_2ndHandEnableAll(false);
#endif
  MPOS_SetAngleAll(hour*360/12);
  MATRIX_SetMoveDelayAll(2);
  MPOS_SetMoveModeAll(STEPPER_MOVE_MODE_CW);
#if PL_CONFIG_USE_LED_RING
  MHAND_HandEnableAll(true);
  MRING_EnableRingAll(false);
#elif PL_MATRIX_CONFIG_IS_RGB
  MHAND_HandEnableAll(true);
#endif
  return MATRIX_SendToRemoteQueueExecuteAndWait(true);
#elif PL_CONFIG_USE_STEPPER
  int x, y, z;

  for(x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        STEPPER_MoveClockDegreeAbs(MATRIX_GetStepper(x, y, z), hour*360/12, STEPPER_MOVE_MODE_CW, 4, true, true);
      }
    }
  }
  STEPBOARD_MoveAndWait(STEPBOARD_GetBoard(), 10);
  return ERR_OK;
#endif
}
#else
static uint8_t MATRIX_MoveAllToStartPosition(int32_t timeoutMs, const McuShell_StdIOType *io) {
#if PL_CONFIG_IS_MASTER && PL_CONFIG_USE_RS485
#warning "todo NYI"
  #if 0 /* \todo not implemented yet */
  #if PL_CONFIG_USE_EXTENDED_HANDS
  MHAND_2ndHandEnableAll(false);
  #endif
    MPOS_SetAngleZ0Z1All(hour*360/12, hour*360/12);
    MATRIX_SetMoveDelayZ0Z1All(2, 2);
    MHAND_SetMoveModeAll(STEPPER_MOVE_MODE_CW);
  #if PL_CONFIG_USE_LED_RING
    MHAND_HandEnableAll(true);
    MRING_EnableRingAll(false);
  #endif
  #endif
  return MATRIX_SendToRemoteQueueExecuteAndWait(true);
#elif PL_CONFIG_USE_STEPPER
  /* moving all stepper motors to the start position. Will move them by the max steps so they hit the end stop */
  int x, y, z;

  for(x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        STEPPER_MoveMotorStepsRel(MATRIX_GetStepper(x, y, z), STEPPER_FULL_RANGE_NOF_STEPS, 0);
      }
    }
  }
  STEPBOARD_MoveAndWait(STEPBOARD_GetBoard(), 10);
  return ERR_OK;
#endif
}
#endif

#if PL_CONFIG_IS_ANALOG_CLOCK
uint8_t MATRIX_MoveAllto12(int32_t timeoutMs, const McuShell_StdIOType *io) {
  return MATRIX_MoveAlltoHour(12, timeoutMs, io);
}
#endif

#if PL_CONFIG_USE_MAG_SENSOR  && PL_CONFIG_IS_CLIENT
static void MATRIX_MoveByOffset(STEPPER_Handle_t motor, int16_t offset, uint16_t delay) {
  /* here the hand is on the sensor: adjust with offset */
  STEPPER_MoveMotorStepsRel(motor, offset, delay);
  STEPBOARD_MoveAndWait(STEPBOARD_GetBoard(), 10);
}

static void MATRIX_SetZeroPosition(STEPPER_Handle_t motor) {
  /* set zero position */
  STEPPER_SetPos(motor, 0);
}

static uint8_t MATRIX_MoveHandOnSensor(STEPPER_Handle_t motor, bool onSensor, int32_t stepSize, int32_t timeoutms, uint32_t waitms, uint16_t delay) {
  uint8_t res = ERR_OK;
  bool done;
  X12_Stepper_t *s;
  TickType_t start, curr;

  start = xTaskGetTickCount();
  /* move hand over sensor */
  for(;;) { /* breaks */
    done = true;
    /* check if all motors are on sensor */
    s = STEPPER_GetDevice(motor);
    if (MAG_IsTriggered(s->mag)!=onSensor) {
      done = false; /* not yet */
    }
    if (done) { /* all hands on sensor */
      break;
    }
    s = STEPPER_GetDevice(motor);
    if (MAG_IsTriggered(s->mag)!=onSensor) {
      STEPPER_MoveMotorStepsRel(motor, stepSize, delay); /* make by 1 degree */
    }
    STEPBOARD_MoveAndWait(STEPBOARD_GetBoard(), waitms);
    curr = xTaskGetTickCount();
    if (pdMS_TO_TICKS(curr-start)>timeoutms) {
      McuLog_error("timeout moving hand on sensor");
      res = ERR_UNDERFLOW;
      break;
    }
  } /* for */
  return res;
}

static uint8_t MATRIX_ZeroHand(STEPPER_Handle_t motor, int16_t offset, uint16_t delay) {
  uint8_t res = ERR_OK;
  X12_Stepper_t *s;

  /* if hand is on sensor: move hand out of the sensor area */
  s = STEPPER_GetDevice(motor);
  if (MAG_IsTriggered(s->mag)) { /* hand on sensor? */
    STEPPER_MoveMotorDegreeRel(motor, 90, delay); /* move away from sensor */
  }
  STEPBOARD_MoveAndWait(STEPBOARD_GetBoard(), 10);

  /* move forward ccw in larger steps to find sensor */
  if (MATRIX_MoveHandOnSensor(motor, true, -10, 20000, 10, delay)!=ERR_OK) {
    res = ERR_FAILED;
  }

  /* step back cw in micro-steps just to leave the sensor */
  if (MATRIX_MoveHandOnSensor(motor, false, 1, 3000, 10, delay)!=ERR_OK) {
    res = ERR_FAILED;
  }

  /* step forward ccw in micro-steps just to enter the sensor again */
  if (MATRIX_MoveHandOnSensor(motor, true, -1, 3000, 2, delay)!=ERR_OK) {
    res = ERR_FAILED;
  }

  /* here all hands are on the sensor: adjust with offset */
  MATRIX_MoveByOffset(motor, offset, delay);

  /* set zero position */
  MATRIX_SetZeroPosition(motor);
  return res;
}

static uint8_t MATRIX_ZeroAllHands(void) {
  STEPPER_Handle_t motor;
  int16_t offset;

  for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        motor = MATRIX_GetStepper(x, y, z);
    #if PL_CONFIG_USE_NVMC
        offset = NVMC_GetStepperZeroOffset(x, y, z);
    #else
        offset = 0;
    #endif
        if (MATRIX_ZeroHand(motor, offset, STEPPER_HAND_ZERO_DELAY)!=ERR_OK) {
          return ERR_FAILED;
        }
      }
    }
  }
  return ERR_OK;
}

static uint8_t MATRIX_SetOffsetFrom12(void) {
  /* all hands shall be at 12-o-clock position */
  uint8_t res = ERR_OK;
  STEPPER_Handle_t motor;
  uint32_t flags;

  res = NVMC_GetFlags(&flags);
  if (res!=ERR_OK) {
    McuLog_info("failed getting flags");
    return ERR_FAILED;
  }
  if (!(flags&NVMC_FLAGS_MAGNET_ENABLED)) {
    McuLog_info("No magnets, ignoring command");
    return ERR_OK;
  }
#if PL_CONFIG_USE_MOTOR_ON_OFF
  if (!STEPBOARD_IsMotorSwitchOn(STEPBOARD_GetBoard())) {
    McuLog_trace("turning on motors");
    STEPBOARD_MotorSwitchOnOff(STEPBOARD_GetBoard(), true); /* turn on motors */
  }
#endif
  /* first zero position at current position */
  for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        if (NVMC_GetIsEnabled(x, y, z)) {
          motor = MATRIX_GetStepper(x, y, z);
          STEPPER_SetPos(motor, 0);

          McuLog_trace("Move hands CCW to find sensor");
          /* move ccw in larger steps to find sensor */
          if (MATRIX_MoveHandOnSensor(motor, true, -10, 10000, 5, STEPPER_HAND_ZERO_DELAY)!=ERR_OK) {
            return ERR_FAILED;
          }

          McuLog_trace("Move hands CW back got get off sensor");
          /* step back cw in micro-steps just to leave the sensor */
          if (MATRIX_MoveHandOnSensor(motor, false, 1, 1000, 2, STEPPER_HAND_ZERO_DELAY)!=ERR_OK) {
            return ERR_FAILED;
          }

          McuLog_trace("Move hands CW over sensor again");
          /* step ccw in micro-steps just to enter the sensor again */
          if (MATRIX_MoveHandOnSensor(motor, true, -1, 1000, 2, STEPPER_HAND_ZERO_DELAY)!=ERR_OK) {
            return ERR_FAILED;
          }

        #if PL_CONFIG_USE_NVMC
          McuLog_trace("Store offset in NVMC");
          /* store new offsets */
          NVMC_SetStepperZeroOffset(x, y, z, -STEPPER_GetPos(motor));
        #endif

          McuLog_trace("Move hands back CCW to zero position");
          MATRIX_MoveByOffset(motor, NVMC_GetStepperZeroOffset(x, y, z), STEPPER_HAND_ZERO_DELAY);
        }
      }
    }
  }
  return ERR_OK;
}
#endif /* PL_CONFIG_USE_MAG_SENSOR */

#if PL_CONFIG_USE_STEPPER
static uint8_t MATRIX_Test(void) {
  const uint8_t delay = 0;
  /* Test the clock stepper motors. Moves each hand 90 degrees cw four times, then the same thing back ccw */
#if PL_CONFIG_USE_MOTOR_ON_OFF
  STEPBOARD_MotorSwitchOnOff(STEPBOARD_GetBoard(), true); /* turn on motors */
#endif
  for (int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
    for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
      #if PL_CONFIG_IS_ANALOG_CLOCK
        STEPPER_MoveClockDegreeRel(MATRIX_GetStepper(x, y, z), 180, STEPPER_MOVE_MODE_CW, delay, true, true);
      #else
        STEPPER_MoveMotorStepsRel(MATRIX_GetStepper(x, y, z), 300, delay);
      #endif
        STEPBOARD_MoveAndWait(STEPBOARD_GetBoard(), 50);
      #if PL_CONFIG_IS_ANALOG_CLOCK
        STEPPER_MoveClockDegreeRel(MATRIX_GetStepper(x, y, z), 180, STEPPER_MOVE_MODE_CCW, delay, true, true);
      #else
        STEPPER_MoveMotorStepsRel(MATRIX_GetStepper(x, y, z), -300, delay);
      #endif
        STEPBOARD_MoveAndWait(STEPBOARD_GetBoard(), 50);
      }
    }
  }
#if PL_CONFIG_USE_MOTOR_ON_OFF
  STEPBOARD_MotorSwitchOnOff(STEPBOARD_GetBoard(), false); /* turn off motors */
#endif
  return ERR_OK;
}
#endif

#if PL_CONFIG_IS_MASTER

#if MATRIX_NOF_STEPPERS_Z==2
void MATRIX_DrawHLine(int x, int y, int w) {
  for(int xb=x; xb<x+w; xb++) {
    MPOS_SetAngleZ0Z1(xb, y, 270, 90);
    /* upper left corner */
  #if PL_MATRIX_CONFIG_IS_RGB
    MHAND_HandEnable(xb, y, 0, true);
    MHAND_HandEnable(xb, y, 1, true);
  #endif
  }
}
#endif

#if MATRIX_NOF_STEPPERS_Z==2
void MATRIX_DrawVLine(int x, int y, int h) {
  for(int yb=y; yb<y+h; yb++) {
    MPOS_SetAngleZ0Z1(x, yb, 0, 180);
    /* upper left corner */
  #if PL_MATRIX_CONFIG_IS_RGB
    MHAND_HandEnable(x, yb, 0, true);
    MHAND_HandEnable(x, yb, 1, true);
  #endif
  }
}
#endif

#if MATRIX_NOF_STEPPERS_Z==2
void MATRIX_DrawRectangle(int x, int y, int w, int h) {
  MPOS_SetAngleZ0Z1(x, y, 180, 90);
  /* upper left corner */
#if PL_MATRIX_CONFIG_IS_RGB
  MHAND_HandEnable(x, y, 0, true);
  MHAND_HandEnable(x, y, 1, true);
#endif
  /* upper right corner */
  MPOS_SetAngleZ0Z1(x+w-1, y, 270, 180);
#if PL_MATRIX_CONFIG_IS_RGB
  MHAND_HandEnable(x+w-1, y, 0, true);
  MHAND_HandEnable(x+w-1, y, 1, true);
#endif
  /* lower right corner */
  MPOS_SetAngleZ0Z1(x+w-1, y+h-1,  270, 0);
#if PL_MATRIX_CONFIG_IS_RGB
  MHAND_HandEnable(x+w-1, y+h-1, 0, true);
  MHAND_HandEnable(x+w-1, y+h-1, 1, true);
#endif
  /* lower left corner */
  MPOS_SetAngleZ0Z1(x, y+h-1,  0, 90);
#if PL_MATRIX_CONFIG_IS_RGB
  MHAND_HandEnable(x, y+h-1, 0, true);
  MHAND_HandEnable(x, y+h-1, 1, true);
#endif
  /* horizontal lines */
  MATRIX_DrawHLine(x+1, y, w-2);
  MATRIX_DrawHLine(x+1, y+h-1, w-2);
  /* vertical lines */
  MATRIX_DrawVLine(x, y+1, h-2);
  MATRIX_DrawVLine(x+w-1, y+1, h-2);
}
#endif
#endif /* PL_CONFIG_IS_MASTER */

#if PL_CONFIG_USE_SHELL && PL_CONFIG_USE_STEPPER
static uint8_t PrintStepperStatus(const McuShell_StdIOType *io) {
  uint8_t buf[128];
  uint8_t statusStr[16];

  for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        STEPPER_Handle_t stepper;
        uint8_t addr;

        stepper = MATRIX_GetStepper(x, y, z);
        addr = MATRIX_GetAddress(x, y, z);
        buf[0] = '\0';
        McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"addr:0x");
        McuUtility_strcatNum8Hex(buf, sizeof(buf), addr);
        McuUtility_strcat(buf, sizeof(buf), (unsigned char*)", Stepper ");
        STEPPER_StrCatStatus(stepper, buf, sizeof(buf));
        McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"\r\n");

        McuUtility_strcpy(statusStr, sizeof(statusStr), (unsigned char*)"  M[");
        McuUtility_strcatNum8u(statusStr, sizeof(statusStr), x);
        McuUtility_chcat(statusStr, sizeof(statusStr), ',');
        McuUtility_strcatNum8u(statusStr, sizeof(statusStr), y);
        McuUtility_chcat(statusStr, sizeof(statusStr), ',');
        McuUtility_strcatNum8u(statusStr, sizeof(statusStr), z);
        McuUtility_strcat(statusStr, sizeof(statusStr), (unsigned char*)"]");
        McuShell_SendStatusStr(statusStr, buf, io->stdOut);
      }
    }
  } /* for */
  return ERR_OK;
}
#endif

#if PL_CONFIG_USE_SHELL && PL_CONFIG_USE_LED_RING && PL_CONFIG_USE_STEPPER
static uint8_t PrintRingStatus(const McuShell_StdIOType *io) {
  uint8_t buf[128];
  uint8_t statusStr[16];

  for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {

        buf[0] = '\0';
        McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"Ring ");
        NEOSR_StrCatRingStatus(MATRIX_GetLedRingDevice(x, y, z), buf, sizeof(buf));
        McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"\r\n");

        McuUtility_strcpy(statusStr, sizeof(statusStr), (unsigned char*)"  R[");
        McuUtility_strcatNum8u(statusStr, sizeof(statusStr), x);
        McuUtility_chcat(statusStr, sizeof(statusStr), ',');
        McuUtility_strcatNum8u(statusStr, sizeof(statusStr), y);
        McuUtility_chcat(statusStr, sizeof(statusStr), ',');
        McuUtility_strcatNum8u(statusStr, sizeof(statusStr), z);
        McuUtility_strcat(statusStr, sizeof(statusStr), (unsigned char*)"]");
        McuShell_SendStatusStr(statusStr, buf, io->stdOut);
      }
    }
  } /* for */

  for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {

        buf[0] = '\0';
        McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"Hand ");
        NEOSR_StrCatHandStatus(MATRIX_GetLedRingDevice(x, y, z), buf, sizeof(buf));
        McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"\r\n");

        McuUtility_strcpy(statusStr, sizeof(statusStr), (unsigned char*)"  H[");
        McuUtility_strcatNum8u(statusStr, sizeof(statusStr), x);
        McuUtility_chcat(statusStr, sizeof(statusStr), ',');
        McuUtility_strcatNum8u(statusStr, sizeof(statusStr), y);
        McuUtility_chcat(statusStr, sizeof(statusStr), ',');
        McuUtility_strcatNum8u(statusStr, sizeof(statusStr), z);
        McuUtility_strcat(statusStr, sizeof(statusStr), (unsigned char*)"]");
        McuShell_SendStatusStr(statusStr, buf, io->stdOut);
      }
    }
  } /* for */

  return ERR_OK;
}
#endif

#if PL_CONFIG_USE_SHELL
static uint8_t PrintStatus(const McuShell_StdIOType *io) {
  uint8_t buf[32];

  McuShell_SendStatusStr((unsigned char*)"matrix", (unsigned char*)"Matrix settings\r\n", io->stdOut);
  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"x*y*z: ");
  McuUtility_strcatNum8u(buf, sizeof(buf), MATRIX_NOF_STEPPERS_X);
  McuUtility_chcat(buf, sizeof(buf), '*');
  McuUtility_strcatNum8u(buf, sizeof(buf), MATRIX_NOF_STEPPERS_Y);
  McuUtility_chcat(buf, sizeof(buf), '*');
  McuUtility_strcatNum8u(buf, sizeof(buf), MATRIX_NOF_STEPPERS_Z);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"\r\n");
  McuShell_SendStatusStr((unsigned char*)"  stepper", buf, io->stdOut);

#if PL_CONFIG_IS_ANALOG_CLOCK && (PL_CONFIG_USE_NEO_PIXEL_HW || PL_MATRIX_CONFIG_IS_RGB)
  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"hand: 0x");
  McuUtility_strcatNum24Hex(buf, sizeof(buf), MATRIX_LedHandColor);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"\r\n");
  McuShell_SendStatusStr((unsigned char*)"  color", buf, io->stdOut);

  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"hand: 0x");
  McuUtility_strcatNum8Hex(buf, sizeof(buf), MATRIX_LedHandBrightness);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"\r\n");
  McuShell_SendStatusStr((unsigned char*)"  brightness", buf, io->stdOut);
#endif
#if PL_CONFIG_USE_MOTOR_ON_OFF && !PL_CONFIG_IS_MASTER
  McuShell_SendStatusStr((unsigned char*)"  motor on",
  #if PL_CONFIG_USE_MOTOR_ON_OFF_AUTO
      STEPBOARD_IsMotorSwitchOn(STEPBOARD_GetBoard())?(unsigned char*)"yes (auto)\r\n":(unsigned char*)"no (auto)\r\n", io->stdOut);
  #else
      STEPBOARD_IsMotorSwitchOn(STEPBOARD_GetBoard())?(unsigned char*)"yes\r\n":(unsigned char*)"no\r\n", io->stdOut);
  #endif
#endif
  return ERR_OK;
}

#if PL_CONFIG_USE_STEPPER
static uint8_t ParseMatrixCommand(const unsigned char **cmd, int32_t *xp, int32_t *yp, int32_t *zp, int32_t *vp, uint8_t *dp, STEPPER_MoveMode_e *modep, bool *speedUpp, bool *slowDownp) {
  /* parse a string like <x> <y> <z> <v> <d> <md> */
  int32_t x, y, z, v, d;

  if (   McuUtility_xatoi(cmd, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
      && McuUtility_xatoi(cmd, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
      && McuUtility_xatoi(cmd, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
      && McuUtility_xatoi(cmd, &v)==ERR_OK
      && McuUtility_xatoi(cmd, &d)==ERR_OK && d>=0
     )
  {
    if (**cmd==' ') {
      (*cmd)++;
    }
    if (**cmd=='\0') { /* mode is optional, set it to defaults */
      *modep = STEPPER_MOVE_MODE_SHORT;
      *speedUpp = *slowDownp = true;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"cw", sizeof("cw")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_CW;
      *speedUpp = *slowDownp = true;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"Cw", sizeof("Cw")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_CW;
      *speedUpp = false; *slowDownp = true;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"cW", sizeof("cW")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_CW;
      *speedUpp = true; *slowDownp = false;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"CW", sizeof("CW")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_CW;
      *speedUpp = *slowDownp = false;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"cc", sizeof("cc")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_CCW;
      *speedUpp = *slowDownp = true;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"Cc", sizeof("Cc")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_CCW;
      *speedUpp = false; *slowDownp = true;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"cC", sizeof("cC")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_CCW;
      *speedUpp = true; *slowDownp = false;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"CC", sizeof("CC")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_CCW;
      *speedUpp = *slowDownp = false;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"sh", sizeof("sh")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_SHORT;
      *speedUpp = *slowDownp = true;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"Sh", sizeof("Sh")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_SHORT;
      *speedUpp = false; *slowDownp = true;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"sH", sizeof("sH")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_SHORT;
      *speedUpp = true; *slowDownp = false;
    } else if (McuUtility_strncmp((char*)*cmd, (char*)"SH", sizeof("SH")-1)==0) {
      *cmd += 2;
      *modep = STEPPER_MOVE_MODE_SHORT;
      *speedUpp = *slowDownp = false;
    } else {
      return ERR_FAILED;
    }
    *xp = x;
    *yp = y;
    *zp = z;
    *vp = v;
    *dp = d;
    return ERR_OK;
  }
  return ERR_FAILED;
}
#endif

#if PL_MATRIX_CONFIG_IS_RGB
void MATRIX_RequestRgbUpdate(void) {
  /* request LED update either locally or remote */
#if PL_CONFIG_USE_NEO_PIXEL_HW
  APP_RequestUpdateLEDs();
#elif PL_CONFIG_USE_RS485 /* send over RS-485 */
  (void)MATRIX_SendToRemoteQueueExecuteAndWait(false); /* no not need to wait */
#endif
}
#endif

#if PL_MATRIX_CONFIG_IS_RGB && PL_CONFIG_IS_ANALOG_CLOCK
void MATRIX_EnableDisableHandsAll(bool enable) {
  MHAND_HandEnableAll(enable); /* enable/disable hands */
  #if PL_CONFIG_USE_EXTENDED_HANDS
  MHAND_2ndHandEnableAll(false); /* turn off 2nd hand */
  #endif
  #if PL_CONFIG_USE_LED_RING
  MRING_EnableRingAll(false); /* turn off ring */
  #endif
  #if PL_CONFIG_USE_RS485
  MATRIX_SendToRemoteQueueExecuteAndWait(true);
  #endif
}
#endif

static uint8_t PrintHelp(const McuShell_StdIOType *io) {
  McuShell_SendHelpStr((unsigned char*)"matrix", (unsigned char*)"Group of matrix commands\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  help|status", (unsigned char*)"Print help or status information\r\n", io->stdOut);
#if PL_CONFIG_USE_STEPPER
  McuShell_SendHelpStr((unsigned char*)"  stepper status", (unsigned char*)"Print stepper status information\r\n", io->stdOut);
#endif
#if PL_CONFIG_USE_STEPPER
  McuShell_SendHelpStr((unsigned char*)"  test", (unsigned char*)"Test the stepper on the board\r\n", io->stdOut);
#endif
#if PL_CONFIG_IS_ANALOG_CLOCK
  McuShell_SendHelpStr((unsigned char*)"  hour <hour>", (unsigned char*)"Set matrix to hour (1-12) position\r\n", io->stdOut);
#endif
#if PL_CONFIG_USE_NEO_PIXEL_HW
  McuShell_SendHelpStr((unsigned char*)"  rgb pixel <xyz> <rgb>", (unsigned char*)"Set pixel color\r\n", io->stdOut);
#endif
#if PL_CONFIG_USE_LED_RING
  McuShell_SendHelpStr((unsigned char*)"  he all on|off", (unsigned char*)"Enabling all hand LEDs\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  he <xyz> on|off", (unsigned char*)"Enable single hand LED (comma separated)\r\n", io->stdOut);
  #if PL_CONFIG_USE_EXTENDED_HANDS
  McuShell_SendHelpStr((unsigned char*)"  he2 all on|off", (unsigned char*)"Enable all 2nd hand LED\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  he2 <xyz> on|off", (unsigned char*)"Enable single 2nd hand LED (comma separated)\r\n", io->stdOut);

  McuShell_SendHelpStr((unsigned char*)"  hc2 all <rgb>", (unsigned char*)"Set all 2nd hand color (comma separated)\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  hc2 <xyz> <rgb>", (unsigned char*)"Set single 2nd hand color (comma separated)\r\n", io->stdOut);
  #endif

  McuShell_SendHelpStr((unsigned char*)"  re all on|off", (unsigned char*)"Enabling ring LEDs\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  re <xyz> on|off", (unsigned char*)"Enable single ring LED (comma separated)\r\n", io->stdOut);

  McuShell_SendHelpStr((unsigned char*)"  hc all <rgb>", (unsigned char*)"Set hand color for all hands\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  hc <xyz> <rgb>", (unsigned char*)"Set single hand color (comma separated)\r\n", io->stdOut);

  McuShell_SendHelpStr((unsigned char*)"  rc all <rgb>", (unsigned char*)"Set ring color for all rings\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  rc <xyz> <rgb>", (unsigned char*)"Set single ring color (comma separated)\r\n", io->stdOut);

  #if PL_CONFIG_USE_LED_DIMMING
  McuShell_SendHelpStr((unsigned char*)"  hand brightness all <f>", (unsigned char*)"Set brightness (0-255) for all hands\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  hand brightness <xyz> <f>", (unsigned char*)"Set brightness (0-255) for hands\r\n", io->stdOut);
  #endif
#endif

#if PL_CONFIG_IS_ANALOG_CLOCK && (PL_CONFIG_USE_NEO_PIXEL_HW || PL_MATRIX_CONFIG_IS_RGB)
  McuShell_SendHelpStr((unsigned char*)"  hand color <rgb>", (unsigned char*)"Set default hand led color\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  hand brightness <val>", (unsigned char*)"Set default hand led brightness (0-255)\r\n", io->stdOut);
#endif

  McuShell_SendHelpStr((unsigned char*)"", (unsigned char*)"<xyz>: coordinate, separated by space, e.g. 0 0 1\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"", (unsigned char*)"<md>: mode (cc, cw, sh), lowercase mode letter is with acceleration control for start/stop, e.g. Cw\r\n", io->stdOut);
#if PL_CONFIG_IS_MASTER
  McuShell_SendHelpStr((unsigned char*)"", (unsigned char*)"<d>: delay, 0 is no delay\r\n", io->stdOut);
#endif
#if PL_CONFIG_IS_MASTER && PL_CONFIG_USE_NEO_PIXEL_HW
  McuShell_SendHelpStr((unsigned char*)"  R <xyz> <a> <d> <md>", (unsigned char*)"Relative angle move for LED and motor\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  A <xyz> <a> <d> <md>", (unsigned char*)"Absolute angle move for LED and motor\r\n", io->stdOut);
#endif
#if PL_CONFIG_USE_STEPPER
#if PL_CONFIG_IS_ANALOG_CLOCK
  McuShell_SendHelpStr((unsigned char*)"  r <xyz> <a> <d> <md>", (unsigned char*)"Relative angle move (comma separated)\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  a <xyz> <a> <d> <md>", (unsigned char*)"Absolute angle move (comma separated)\r\n", io->stdOut);
#else
  McuShell_SendHelpStr((unsigned char*)"  r <xyz> <s> <d> <md>", (unsigned char*)"Relative step move (comma separated)\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  a <xyz> <s> <d> <md>", (unsigned char*)"Absolute step move (comma separated)\r\n", io->stdOut);
#endif
  McuShell_SendHelpStr((unsigned char*)"  q <xyz> <cmd>", (unsigned char*)"Queue a 'r' or 'a' command, e.g. 'matrix q 0 0 0 r 90 8 cc', (comma separated)\r\n", io->stdOut);
#endif /* PL_CONFIG_USE_STEPPER */
  McuShell_SendHelpStr((unsigned char*)"  exq", (unsigned char*)"Execute commands in queues\r\n", io->stdOut);
#if PL_CONFIG_IS_MASTER
  McuShell_SendHelpStr((unsigned char*)"  lastError", (unsigned char*)"Check remotes for last error\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  waitidle", (unsigned char*)"Check remotes for idle state\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  sendcmd <cmd>", (unsigned char*)"Send a command to all boards\r\n", io->stdOut);
#endif
#if PL_CONFIG_USE_X12_STEPPER
  McuShell_SendHelpStr((unsigned char*)"  reset high|low", (unsigned char*)"Set motor driver reset line (LOW active)\r\n", io->stdOut);
#endif
#if PL_CONFIG_USE_MAG_SENSOR
  McuShell_SendHelpStr((unsigned char*)"  zero all", (unsigned char*)"Move all motors to zero position using magnet sensor\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  zero <x> <y> <z>", (unsigned char*)"Move clock to zero position using magnet sensor\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  offs <x> <y> <z> <v>", (unsigned char*)"Set offset value for clock\r\n", io->stdOut);
  McuShell_SendHelpStr((unsigned char*)"  offs 12", (unsigned char*)"Calculate offset from 12-o-clock for all clocks\r\n", io->stdOut);
#endif
#if PL_CONFIG_USE_MOTOR_ON_OFF
  McuShell_SendHelpStr((unsigned char*)"  motor on|off", (unsigned char*)"Switch motors on or off\r\n", io->stdOut);
#endif
  McuShell_SendHelpStr((unsigned char*)"  park on|off", (unsigned char*)"Park the motor(s)\r\n", io->stdOut);
  return ERR_OK;
}

uint8_t MATRIX_ParseCommand(const unsigned char *cmd, bool *handled, const McuShell_StdIOType *io) {
  const unsigned char *p;
  uint8_t res = ERR_OK;
#if PL_CONFIG_USE_STEPPER || PL_CONFIG_USE_NEO_PIXEL_HW
  bool speedUp, slowDown;
  STEPPER_MoveMode_e mode;
  uint8_t d;
  int32_t v;
#endif
#if PL_MATRIX_CONFIG_IS_RGB
  uint8_t r, g, b;
#endif

  if (McuUtility_strcmp((char*)cmd, McuShell_CMD_HELP)==0 || McuUtility_strcmp((char*)cmd, "matrix help")==0) {
    *handled = true;
    return PrintHelp(io);
  } else if ((McuUtility_strcmp((char*)cmd, McuShell_CMD_STATUS)==0) || (McuUtility_strcmp((char*)cmd, "matrix status")==0)) {
    *handled = true;
    return PrintStatus(io);
#if PL_CONFIG_USE_STEPPER
  } else if ((McuUtility_strcmp((char*)cmd, McuShell_CMD_STATUS)==0) || (McuUtility_strcmp((char*)cmd, "matrix stepper status")==0)) {
    *handled = true;
    PrintStepperStatus(io);
  #if PL_CONFIG_USE_LED_RING
    PrintRingStatus(io);
  #endif
    return ERR_OK;
#endif

#if PL_CONFIG_IS_ANALOG_CLOCK
  } else if (McuUtility_strncmp((char*)cmd, "matrix hour ", sizeof("matrix hour ")-1)==0) {
    uint8_t hour;

    *handled = TRUE;
    p = cmd + sizeof("matrix hour ")-1;
    if (McuUtility_ScanDecimal8uNumber(&p, &hour)==ERR_OK && hour<=12) {
      return MATRIX_MoveAlltoHour(hour, 10000, io);
    }
    return ERR_FAILED;
#endif
#if PL_CONFIG_IS_MASTER
  } else if (McuUtility_strncmp((char*)cmd, "matrix delay ", sizeof("matrix delay ")-1)==0) {
    uint8_t delay;

    *handled = TRUE;
    p = cmd + sizeof("matrix delay ")-1;
    if (McuUtility_ScanDecimal8uNumber(&p, &delay)==ERR_OK) {
      MATRIX_SetMoveDelayAll(delay);
      return ERR_OK;
    } else {
      return ERR_FAILED;
    }
#endif /* PL_CONFIG_IS_MASTER */
#if PL_CONFIG_USE_STEPPER
  #if PL_CONFIG_IS_MASTER && PL_CONFIG_USE_NEO_PIXEL_HW
  } else if (McuUtility_strncmp((char*)cmd, "matrix A ", sizeof("matrix A ")-1)==0   /* "matrix A <x> <y> <z> <a> <d> <md>" */
          || McuUtility_strncmp((char*)cmd, "matrix R ", sizeof("matrix R ")-1)==0   /* "matrix R <x> <y> <z> <a> <d> <md>" */
            )
  {
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd+sizeof("matrix A ")-1;
    res = ParseMatrixCommand(&p, &x, &y, &z, &v, &d, &mode, &speedUp, &slowDown);
    if (res==ERR_OK) {
      QueueMoveCommand(x, y, z, v, d, mode, speedUp, slowDown, cmd[7]=='A');
      /* send it to the ourselve as master first. RS485_SendCommand() below will wait for the OK which adds time. */
      (void)RS485_SendCommand(RS485_GetAddress(), (unsigned char*)"matrix exq", 1000, true, 0);
      /* send execute to the deviceon the bus: */
      (void)RS485_SendCommand(clockMatrix[x][y][z].addr, (unsigned char*)"matrix exq", 1000, true, 0); /* execute the queue */
      return ERR_OK;
    } else {
      return ERR_FAILED;
    }
  #endif /* PL_CONFIG_IS_MASTER && PL_CONFIG_USE_NEO_PIXEL_HW */

  } else if (McuUtility_strncmp((char*)cmd, "matrix r ", sizeof("matrix r ")-1)==0) { /* relative move, "matrix r <x> <y> <z> <v> <d> <md>" */
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd+sizeof("matrix r ")-1;
    do {
      if (*p==',') { /* skip comma for multiple commands */
        p++;
      }
      res = ParseMatrixCommand(&p, &x, &y, &z, &v, &d, &mode, &speedUp, &slowDown);
      if (res==ERR_OK) {
        #if PL_CONFIG_IS_ANALOG_CLOCK
        STEPPER_MoveClockDegreeRel(MATRIX_GetStepper(x, y, z), v, mode, d, speedUp, slowDown);
        #else
        STEPPER_MoveMotorStepsRel(MATRIX_GetStepper(x, y, z), v, d);
        #endif
        STEPPER_StartTimer();
        res = ERR_OK;
      } else {
        res = ERR_FAILED;
      }
    } while(res==ERR_OK && *p==',');
    return res;

  } else if (McuUtility_strncmp((char*)cmd, "matrix a ", sizeof("matrix a ")-1)==0) { /* absolute move, "matrix a <x> <y> <z> <a> <d> <md>" */
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd+sizeof("matrix a ")-1;
    do {
      if (*p==',') { /* skip comma (multiple commands) */
        p++;
      }
      res = ParseMatrixCommand(&p, &x, &y, &z, &v, &d, &mode, &speedUp, &slowDown);
      if (res==ERR_OK) {
        #if PL_CONFIG_IS_ANALOG_CLOCK
        STEPPER_MoveClockDegreeAbs(MATRIX_GetStepper(x, y, z), v, mode, d, speedUp, slowDown);
        #else
        STEPPER_MoveMotorStepsAbs(MATRIX_GetStepper(x, y, z), v, 0);
        #endif
        STEPPER_StartTimer();
        res = ERR_OK;
      } else {
        res = ERR_FAILED;
      }
    } while(res==ERR_OK && *p==',');
    return res;

  } else if (McuUtility_strncmp((char*)cmd, "matrix q ", sizeof("matrix q ")-1)==0) { /* queue a command: example: matrix q 0 0 0 hc 0x10 ,0 0 1 r 90 0 sh */
    unsigned char *ptr, *data;
    STEPPER_Handle_t stepper;
    size_t len;
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd + sizeof("matrix q ")-1;
    do {
      res = ERR_OK;
      if (*p==',') { /* skip comma (multiple commands) */
        p++;
      }
      if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
          && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
          && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
         )
      {
        McuUtility_SkipSpaces(&p);
        data = (unsigned char*)p;
        len = 0;
        while(*p!='\0' && *p!=',') {
          len++;
          p++;
        }
        ptr = pvPortMalloc(len+1); /* +1 for the zero byte */
        if (ptr==NULL) {
          res = ERR_FAILED;
          break;
        }
        stepper = MATRIX_GetStepper(x, y, z);
        strncpy((char*)ptr, (char*)data, len); /* copy the command string */
        ptr[len] = '\0';
        if (xQueueSendToBack(STEPPER_GetQueue(stepper), &ptr, pdMS_TO_TICKS(100))!=pdTRUE) {
          res = ERR_FAILED;
          break;
        }
      } else {
        res = ERR_FAILED;
      }
    } while(res==ERR_OK && *p==',');
    return res;
#endif /* PL_CONFIG_USE_STEPPER */

#if PL_CONFIG_USE_LED_RING
    /* ---------------------- enable/disable all ---------------------------------- */
  } else if (McuUtility_strncmp((char*)cmd, "matrix he all ", sizeof("matrix he all ")-1)==0) {
    *handled = TRUE;
    p = cmd + sizeof("matrix he all ")-1;
    if (McuUtility_strcmp((char*)p, (char*)"on")==0) {
      MHAND_HandEnableAll(true);
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else if (McuUtility_strcmp((char*)p, (char*)"off")==0) {
      MHAND_HandEnableAll(false);
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else {
      return ERR_FAILED;
    }
    return ERR_OK;
#endif

#if PL_CONFIG_USE_LED_RING
  } else if (McuUtility_strncmp((char*)cmd, "matrix re all ", sizeof("matrix re all ")-1)==0) {
    *handled = TRUE;
    p = cmd + sizeof("matrix re all ")-1;
    if (McuUtility_strcmp((char*)p, (char*)"on")==0) {
      MRING_EnableRingAll(true);
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else if (McuUtility_strcmp((char*)p, (char*)"off")==0) {
      MRING_EnableRingAll(false);
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else {
      return ERR_FAILED;
    }
    return ERR_OK;
#endif

#if PL_CONFIG_USE_LED_RING
  /* ---------------------- enabled/disable for a ring/hand ---------------------------------- */
  } else if (McuUtility_strncmp((char*)cmd, "matrix he ", sizeof("matrix he ")-1)==0) {
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd+sizeof("matrix he ")-1;
    do {
      if (*p==',') { /* skip comma for multiple commands */
        p++;
      }
      if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
          && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
          && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
         )
      {
        McuUtility_SkipSpaces(&p); /* skip space(s) between coordinate and on/off */
        if (McuUtility_strcmp((char*)p, (char*)"on")==0) {
          MHAND_HandEnable(x, y, z, true);
          MATRIX_RequestRgbUpdate();
          res = ERR_OK;
        } else if (McuUtility_strcmp((char*)p, (char*)"off")==0) {
          MHAND_HandEnable(x, y, z, false);
          MATRIX_RequestRgbUpdate();
          res = ERR_OK;
        } else {
          res = ERR_FAILED;
        }
      }
    } while(res==ERR_OK && *p==',');
    return res;
#endif

#if PL_CONFIG_USE_LED_RING
  } else if (McuUtility_strncmp((char*)cmd, "matrix re ", sizeof("matrix re ")-1)==0) {
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd + sizeof("matrix re ")-1;
    do {
      if (*p==',') { /* skip comma for multiple commands */
        p++;
      }
      if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
          && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
          && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
         )
      {
        McuUtility_SkipSpaces(&p); /* skip space(s) between coordinate and on/off */
        if (McuUtility_strcmp((char*)p, (char*)"on")==0) {
          MRING_EnableRing(x, y, z, true);
          MATRIX_RequestRgbUpdate();
          res = ERR_OK;
        } else if (McuUtility_strcmp((char*)p, (char*)"off")==0) {
          MRING_EnableRing(x, y, z, false);
          MATRIX_RequestRgbUpdate();
          res = ERR_OK;
        } else {
          res = ERR_FAILED;
        }
      } else {
        res = ERR_FAILED;
      }
    } while(res==ERR_OK && *p==',');
    return res;
#endif

#if PL_CONFIG_USE_EXTENDED_HANDS
  } else if (McuUtility_strncmp((char*)cmd, "matrix he2 all ", sizeof("matrix he2 all ")-1)==0) {
    *handled = TRUE;
    p = cmd + sizeof("matrix he2 all ")-1;
    if (McuUtility_strcmp((char*)p, (char*)"on")==0) {
      MHAND_2ndHandEnableAll(true);
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else if (McuUtility_strcmp((char*)p, (char*)"off")==0) {
      MHAND_2ndHandEnableAll(false);
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else {
      res = ERR_FAILED;
    }
    return res;
  } else if (McuUtility_strncmp((char*)cmd, "matrix he2 ", sizeof("matrix he2 ")-1)==0) {
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd + sizeof("matrix he2 ")-1;
    do {
      if (*p==',') { /* skip comma for multiple commands */
        p++;
      }
      if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
          && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
          && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
         )
      {
        McuUtility_SkipSpaces(&p); /* skip space(s) between coordinate and on/off */
        if (McuUtility_strcmp((char*)p, (char*)"on")==0) {
          MHAND_2ndHandEnable(x, y, z, true);
          MATRIX_RequestRgbUpdate();
          res = ERR_OK;
        } else if (McuUtility_strcmp((char*)p, (char*)"off")==0) {
          MHAND_2ndHandEnable(x, y, z, false);
          MATRIX_RequestRgbUpdate();
          res = ERR_OK;
        } else {
          res = ERR_FAILED;
        }
      } else {
        res = ERR_FAILED;
      }
    } while(res==ERR_OK && *p==',');
    return res;
#endif /* PL_CONFIG_USE_EXTENDED_HANDS */

#if PL_CONFIG_USE_LED_RING
  /* ---------------------- set color for all ---------------------------------- */
  } else if (McuUtility_strncmp((char*)cmd, "matrix hc all ", sizeof("matrix hc all ")-1)==0) {
    *handled = TRUE;
    p = cmd + sizeof("matrix hc all ")-1;
    if (McuUtility_ScanRGB(&p, &r, &g, &b)==ERR_OK) {
      MHAND_SetHandColorAll(NEO_COMBINE_RGB(r, g, b));
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else {
      return ERR_FAILED;
    }
  } else if (McuUtility_strncmp((char*)cmd, "matrix rc all ", sizeof("matrix rc all ")-1)==0) {
    *handled = TRUE;
    p = cmd + sizeof("matrix rc all ")-1;
    if (McuUtility_ScanRGB(&p, &r, &g, &b)==ERR_OK) {
      MRING_SetRingColorAll(r, g, b);
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else {
      return ERR_FAILED;
    }
#endif
#if PL_CONFIG_USE_LED_RING
    /* ---------------------- set color for hand or ring ---------------------------------- */
  } else if (McuUtility_strncmp((char*)cmd, "matrix hc ", sizeof("matrix hc ")-1)==0) {
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd + sizeof("matrix hc ")-1;
    do {
      if (*p==',') { /* skip comma for multiple commands */
        p++;
      }
      if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
          && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
          && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
          && McuUtility_ScanRGB(&p, &r, &g, &b)==ERR_OK
         )
      {
        MHAND_SetHandColor(x, y, z, NEO_COMBINE_RGB(r, g, b));
        res = ERR_OK;
      } else {
        res = ERR_FAILED;
      }
      McuUtility_SkipSpaces(&p); /* skip space after color, e.g. at "0x10 ,0 0 1" */
    } while(res==ERR_OK && *p==',');
    MATRIX_RequestRgbUpdate();
    return res;
#endif
#if PL_CONFIG_USE_LED_RING && PL_CONFIG_USE_EXTENDED_HANDS
  /* ---------------------- set color for all ---------------------------------- */
  } else if (McuUtility_strncmp((char*)cmd, "matrix hc2 all ", sizeof("matrix hc2 all ")-1)==0) {
    *handled = TRUE;
    p = cmd + sizeof("matrix hc2 all ")-1;
    if (McuUtility_ScanRGB(&p, &r, &g, &b)==ERR_OK) {
      MHAND_Set2ndHandColorAll(NEO_COMBINE_RGB(r, g, b));
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else {
      return ERR_FAILED;
    }
  } else if (McuUtility_strncmp((char*)cmd, "matrix hc2 ", sizeof("matrix hc2 ")-1)==0) {
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd + sizeof("matrix hc2 ")-1;
    do {
      if (*p==',') { /* skip comma for multiple commands */
        p++;
      }
      if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
          && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
          && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
          && McuUtility_ScanRGB(&p, &r, &g, &b)==ERR_OK
         )
      {
        MHAND_Set2ndHandColor(x, y, z, NEO_COMBINE_RGB(r, g, b));
        res = ERR_OK;
      } else {
        res = ERR_FAILED;
      }
      McuUtility_SkipSpaces(&p); /* skip space after color, e.g. at "0x10 ,0 0 1" */
    } while(res==ERR_OK && *p==',');
    MATRIX_RequestRgbUpdate();
    return res;
#endif /* PL_CONFIG_USE_LED_RING && PL_CONFIG_USE_EXTENDED_HANDS */
  #if PL_CONFIG_USE_LED_RING
  } else if (McuUtility_strncmp((char*)cmd, "matrix rc ", sizeof("matrix rc ")-1)==0) {
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd + sizeof("matrix rc ")-1;
    do {
      if (*p==',') { /* skip comma for multiple commands */
        p++;
      }
      if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
          && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
          && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && y<MATRIX_NOF_STEPPERS_Z
          && McuUtility_ScanRGB(&p, &r, &g, &b)==ERR_OK
         )
      {
        MRING_SetRingColor(x, y, z, r, g, b);
        res = ERR_OK;
      } else {
        res = ERR_FAILED;
      }
      McuUtility_SkipSpaces(&p); /* skip space after color, e.g. at "0x10 ,0 0 1" */
    } while(res==ERR_OK && *p==',');
    MATRIX_RequestRgbUpdate();
    return res;
  #endif
#if PL_CONFIG_IS_ANALOG_CLOCK && (PL_CONFIG_USE_NEO_PIXEL_HW || PL_MATRIX_CONFIG_IS_RGB)
  } else if (McuUtility_strncmp((char*)cmd, "matrix hand color ", sizeof("matrix hand color ")-1)==0) {
    *handled = TRUE;
    p = cmd + sizeof("matrix hand color ")-1;
    if (McuUtility_ScanRGB32(&p, &MATRIX_LedHandColor)!=ERR_OK) {
      return ERR_FAILED;
    }
    uint32_t color;

    color = MATRIX_GetHandColorAdjusted();
    MHAND_SetHandColorAll(color);
    MATRIX_RequestRgbUpdate();
    return ERR_OK;
#endif
#if PL_CONFIG_IS_ANALOG_CLOCK && (PL_CONFIG_USE_NEO_PIXEL_HW || PL_MATRIX_CONFIG_IS_RGB)
  } else if (McuUtility_strncmp((char*)cmd, "matrix hand brightness ", sizeof("matrix hand brightness ")-1)==0) {
    int32_t val;

    *handled = TRUE;
    p = cmd + sizeof("matrix hand brightness ")-1;
    if (McuUtility_xatoi(&p, &val)==ERR_OK && val>=0 && val<=0xff) {
      MATRIX_LedHandBrightness = val;
      uint32_t color;

      color = MATRIX_GetHandColorAdjusted();
      MHAND_SetHandColorAll(color);
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else {
      return ERR_FAILED;
    }
#endif
#if PL_CONFIG_USE_NEO_PIXEL_HW
  } else if (McuUtility_strncmp((char*)cmd, "matrix rgb pixel ", sizeof("matrix rgb pixel ")-1)==0) {
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd + sizeof("matrix rgb pixel ")-1;
    if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
        && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
        && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
        && McuUtility_ScanRGB(&p, &r, &g, &b)==ERR_OK
       )
    {
  #if PL_CONFIG_USE_LED_RING
      MRING_SetRingPixelColor(x, y, z, r, g, b);
  #elif PL_CONFIG_USE_LED_PIXEL
      MPIXEL_SetColor(x, y, z, r, g, b);
  #else
    #error "NYI"
  #endif
      MATRIX_RequestRgbUpdate();
      return ERR_OK;
    } else {
      return ERR_FAILED;
    }
#endif
#if PL_CONFIG_USE_STEPPER
  } else if (McuUtility_strcmp((char*)cmd, "matrix exq")==0) {
    xSemaphoreGive(semMatrixExecuteQueue); /* trigger executing queue */
    *handled = TRUE;
#endif
#if PL_CONFIG_IS_MASTER && PL_CONFIG_USE_RS485
  } else if (McuUtility_strcmp((char*)cmd, "matrix lastError")==0) {
    *handled = TRUE;
    return MATRIX_CheckRemoteLastError();
  } else if (McuUtility_strcmp((char*)cmd, "matrix waitidle")==0) {
    *handled = TRUE;
    return MATRIX_WaitForIdle(10000);
#endif
#if PL_CONFIG_USE_MAG_SENSOR
  } else if (McuUtility_strcmp((char*)cmd, "matrix zero all")==0) {
    *handled = TRUE;
  #if PL_CONFIG_IS_MASTER
      McuShell_SendStr((unsigned char*)"NYI\r\n", io->stdErr); /* \todo NYI magnet handling from master */
      return ERR_FAILED; /* NYI */
  #elif PL_CONFIG_IS_CLIENT
    return MATRIX_ZeroAllHands();
  #endif
  } else if (McuUtility_strncmp((char*)cmd, "matrix zero ", sizeof("matrix zero ")-1)==0) {
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd + sizeof("matrix zero ")-1;
    if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
        && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
        && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
       )
    {
  #if PL_CONFIG_IS_CLIENT
      STEPPER_Handle_t *motor;
      int16_t offset;

      motor = MATRIX_GetStepper(x, y, z);
    #if PL_CONFIG_USE_NVMC
      offset = NVMC_GetStepperZeroOffset(x, y, z);
    #else
      offset = 0;
    #endif
      res = MATRIX_ZeroHand(motor, offset, STEPPER_HAND_ZERO_DELAY);
      if (res!=ERR_OK) {
        McuShell_SendStr((unsigned char*)"failed to find magnet/zero position\r\n", io->stdErr);
      }
    } else {
      return ERR_FAILED;
  #endif
    }
  } else if (McuUtility_strcmp((char*)cmd, "matrix offs 12")==0) {
    *handled = TRUE;
    return MATRIX_SetOffsetFrom12();
#endif /* PL_CONFIG_USE_MAG_SENSOR */
#if PL_CONFIG_USE_LED_DIMMING
  } else if (McuUtility_strncmp((char*)cmd, "matrix hand brightness all ", sizeof("matrix hand brightness all ")-1)==0) {
    uint8_t f;

    *handled = TRUE;
    p = cmd + sizeof("matrix hand brightness all ")-1;
    res = McuUtility_ScanDecimal8uNumber(&p, &f);
    if (res!=ERR_OK) {
      return res;
    }
    MATRIX_SetHandBrightnessAll(f);
    MATRIX_RequestRgbUpdate();
    return ERR_OK;
  } else if (McuUtility_strncmp((char*)cmd, "matrix hand brightness ", sizeof("matrix hand brightness ")-1)==0) {
    uint8_t f;
    int32_t x, y, z;

    *handled = TRUE;
    p = cmd + sizeof("matrix hand brightness ")-1;
    if (   McuUtility_xatoi(&p, &x)==ERR_OK && x>=0 && x<MATRIX_NOF_STEPPERS_X
        && McuUtility_xatoi(&p, &y)==ERR_OK && y>=0 && y<MATRIX_NOF_STEPPERS_Y
        && McuUtility_xatoi(&p, &z)==ERR_OK && z>=0 && z<MATRIX_NOF_STEPPERS_Z
       )
    {
      res = McuUtility_ScanDecimal8uNumber(&p, &f);
      if (res!=ERR_OK) {
        return res;
      }
      MATRIX_SetHandBrightness(x, y, z, f);
      MATRIX_RequestRgbUpdate();
    }
    return ERR_OK;
#endif
#if PL_CONFIG_USE_STEPPER
  } else if (McuUtility_strcmp((char*)cmd, "matrix test")==0) {
    *handled = TRUE;
    return MATRIX_Test();
#endif
#if PL_CONFIG_USE_X12_STEPPER
  } else if (McuUtility_strcmp((char*)cmd, "matrix reset high")==0) {
    *handled = TRUE;
    McuX12_017_SetResetLine(x12Steppers[0].x12device, true); /* assuming shared reset line */
    return ERR_OK;
  } else if (McuUtility_strcmp((char*)cmd, "matrix reset low")==0) {
    *handled = TRUE;
    McuX12_017_SetResetLine(x12Steppers[0].x12device, false); /* assuming shared reset line */
    return ERR_OK;
#endif
#if PL_CONFIG_USE_MOTOR_ON_OFF
  } else if (McuUtility_strcmp((char*)cmd, "matrix motor on")==0) {
    *handled = TRUE;
    #if PL_CONFIG_IS_MASTER
    return MATRIX_SendMatrixCmdToAllBoards((const unsigned char *)"matrix motor on");
    #else
    STEPBOARD_MotorSwitchOnOff(STEPBOARD_GetBoard(), true);
    #endif
    return ERR_OK;
  } else if (McuUtility_strcmp((char*)cmd, "matrix motor off")==0) {
    *handled = TRUE;
    #if PL_CONFIG_IS_MASTER
    return MATRIX_SendMatrixCmdToAllBoards((const unsigned char *)"matrix motor off");
    #else
    STEPBOARD_MotorSwitchOnOff(STEPBOARD_GetBoard(), false);
    return ERR_OK;
    #endif
#endif
  } else if (McuUtility_strcmp((char*)cmd, "matrix park on")==0) {
    *handled = TRUE;
  #if PL_CONFIG_IS_ANALOG_CLOCK
    MATRIX_MoveAlltoHour(12, 10000, io); /* move hands to 12-o-clock position */
  #else
    MATRIX_MoveAllToStartPosition(10000, io);
  #endif
  #if PL_CONFIG_IS_MASTER && PL_CONFIG_USE_MOTOR_ON_OFF
  #if PL_MATRIX_CONFIG_IS_RGB
    MATRIX_EnableDisableHandsAll(false);
  #endif
    return MATRIX_SendMatrixCmdToAllBoards((const unsigned char *)"matrix motor off");
  #elif PL_CONFIG_USE_MOTOR_ON_OFF
    STEPBOARD_MotorSwitchOnOff(STEPBOARD_GetBoard(), false);
    return ERR_OK;
  #endif
  } else if (McuUtility_strcmp((char*)cmd, "matrix park off")==0) {
    *handled = TRUE;
  #if PL_CONFIG_IS_MASTER && PL_CONFIG_USE_MOTOR_ON_OFF
    return MATRIX_SendMatrixCmdToAllBoards((const unsigned char *)"matrix motor on");
  #elif PL_CONFIG_USE_MOTOR_ON_OFF
    STEPBOARD_MotorSwitchOnOff(STEPBOARD_GetBoard(), true);
    return ERR_OK;
  #endif
#if PL_CONFIG_IS_MASTER
  } else if (McuUtility_strncmp((char*)cmd, "matrix sendcmd ", sizeof("matrix sendcmd ")-1)==0) {
    *handled = TRUE;

    p = cmd + sizeof("matrix sendcmd ")-1;
    return MATRIX_SendMatrixCmdToAllBoards(p);
#endif /* PL_CONFIG_IS_MASTER */
  }
  return res;
}
#endif

#if PL_CONFIG_USE_STEPPER
void MATRIX_TimerCallback(void) {
  bool workToDo = false;
  STEPPER_Handle_t stepper;

  /* go through all boards and update steps */
  for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
       for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) { /* go through all motors */
         stepper = MATRIX_GetStepper(x, y, z);
         workToDo |= STEPPER_TimerStepperCallback(stepper);
       #if PL_CONFIG_USE_LED_DIMMING
         workToDo |= NEOSR_HandDimmingNotFinished(STEPPER_GetDevice(stepper));
       #endif
      } /* for */
    }
  }
  /* check if we can stop timer */
  if (!workToDo) {
    STEPPER_StopTimer();
#if PL_CONFIG_USE_NEO_PIXEL_HW
  } else { /* request update of the LEDs */
    (void)APP_RequestUpdateLEDsFromISR();
#endif
  }
}
#endif

#if PL_CONFIG_USE_LED_RING && PL_CONFIG_USE_NEO_PIXEL_HW
void MATRIX_IlluminateHands(void) {
  STEPPER_Handle_t stepper;
  int32_t pos;

  for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        stepper = MATRIX_GetStepper(x, y, z);
        pos = STEPPER_GetPos(stepper);
    #if PL_CONFIG_USE_VIRTUAL_STEPPER /* virtual LED stepper only */
        NEOSR_Illuminate(STEPPER_GetDevice(stepper), pos);
    #elif PL_CONFIG_USE_X12_LED_STEPPER /* stepper motor combined with LED ring */
        NEOSR_Handle_t ledRing;

        ledRing = MATRIX_GetLedRingDevice(x, y, z);
        NEOSR_Illuminate(ledRing, pos);
    #endif
      }
    }
  } /* for */
}
#endif

#if PL_CONFIG_USE_STEPPER
static bool MatrixProcessAllQueues(void) {
  /* process all queues and parse the commands. Returns true if there is still work to do */
  uint8_t *cmd;
  uint8_t command[96];
  STEPPER_Handle_t stepper;
  McuShell_ConstStdIOType *io = McuShell_GetStdio();
  QueueHandle_t queue;

  for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        stepper = MATRIX_GetStepper(x, y, z);
        if (STEPPER_IsIdle(stepper)) { /* no steps to do? */
          queue = STEPPER_GetQueue(stepper);
          if (xQueueReceive(queue, &cmd, 0)==pdPASS) { /* check queue */
            McuUtility_strcpy(command, sizeof(command), (unsigned char*)"matrix ");
            if ((cmd[0]=='a' || cmd[0]=='r') && cmd[1] == ' ') {
              McuUtility_chcat(command, sizeof(command), cmd[0]);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), x);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), y);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), z);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcat(command, sizeof(command), cmd+2);
            } else if (McuUtility_strncmp((char*)cmd, (char*)"hc ", sizeof("hc ")-1)==0) {
              McuUtility_strcat(command, sizeof(command), (unsigned char*)"hc ");
              McuUtility_strcatNum8u(command, sizeof(command), x);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), y);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), z);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcat(command, sizeof(command), cmd+sizeof("hc ")-1);
              McuUtility_strCutTail(command, (unsigned char*)" "); /* trim possible space at the end */
            } else if (McuUtility_strncmp((char*)cmd, (char*)"rc ", sizeof("rc ")-1)==0) {
              McuUtility_strcat(command, sizeof(command), (unsigned char*)"rc ");
              McuUtility_strcatNum8u(command, sizeof(command), x);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), y);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), z);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcat(command, sizeof(command), cmd+sizeof("rc ")-1);
              McuUtility_strCutTail(command, (unsigned char*)" "); /* trim possible space at the end */
            } else if (McuUtility_strncmp((char*)cmd, (char*)"he ", sizeof("he ")-1)==0) {
              McuUtility_strcat(command, sizeof(command), (unsigned char*)"he ");
              McuUtility_strcatNum8u(command, sizeof(command), x);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), y);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), z);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcat(command, sizeof(command), cmd+sizeof("he ")-1);
          #if PL_CONFIG_USE_EXTENDED_HANDS
            } else if (McuUtility_strncmp((char*)cmd, (char*)"he2 ", sizeof("he2 ")-1)==0) {
              McuUtility_strcat(command, sizeof(command), (unsigned char*)"he2 ");
              McuUtility_strcatNum8u(command, sizeof(command), x);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), y);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), z);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcat(command, sizeof(command), cmd+sizeof("he2 ")-1);
            } else if (McuUtility_strncmp((char*)cmd, (char*)"hc2 ", sizeof("hc2 ")-1)==0) {
              McuUtility_strcat(command, sizeof(command), (unsigned char*)"hc2 ");
              McuUtility_strcatNum8u(command, sizeof(command), x);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), y);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), z);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcat(command, sizeof(command), cmd+sizeof("hc2 ")-1);
              McuUtility_strCutTail(command, (unsigned char*)" "); /* trim possible space at the end */
          #endif
            } else if (McuUtility_strncmp((char*)cmd, (char*)"re ", sizeof("re ")-1)==0) {
              McuUtility_strcat(command, sizeof(command), (unsigned char*)"re ");
              McuUtility_strcatNum8u(command, sizeof(command), x);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), y);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcatNum8u(command, sizeof(command), z);
              McuUtility_chcat(command, sizeof(command), ' ');
              McuUtility_strcat(command, sizeof(command), cmd+sizeof("re ")-1);
            } else { /* not expected command? pass as-is */
              McuUtility_strcat(command, sizeof(command), cmd);
            }
            bool handled = false;
            if (MATRIX_ParseCommand(command, &handled, io) !=ERR_OK || !handled) { /* parse and execute it */
              McuLog_error("Failed executing queued command '%s'", command);
            } else {
              McuLog_trace("Executed queued command '%s'", command);
            }
            vPortFree(cmd); /* free memory for command */
          } /* item in queue */
        } /* stepper idle? */
      } /* all motors on clock */
    } /* all clocks on board */
  } /* for all boards */
  /* check the number of items still pending in the queue */
  int nofPendingInQueues = 0;

  for(int x=0; x<MATRIX_NOF_STEPPERS_X; x++) {
    for(int y=0; y<MATRIX_NOF_STEPPERS_Y; y++) {
      for(int z=0; z<MATRIX_NOF_STEPPERS_Z; z++) {
        stepper = MATRIX_GetStepper(x, y, z);
        queue = STEPPER_GetQueue(stepper);
        nofPendingInQueues += uxQueueMessagesWaiting(queue);
      }
    }
  }
  return nofPendingInQueues!=0; /* return true if there is still work to do */
}

static void MatrixQueueTask(void *pv) {
  bool workToDo;
  BaseType_t res;

  McuLog_trace("Starting MatrixQueue Task");
  for(;;) {
    res = xSemaphoreTake(semMatrixExecuteQueue, portMAX_DELAY); /* block until we get a request to update */
    if (res==pdTRUE) { /* received the signal */
      do {
        workToDo = MatrixProcessAllQueues();
        if (workToDo) {
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      } while (workToDo);
    } /* semaphore received */
  } /* for */
}
#endif

#if PL_CONFIG_USE_VIRTUAL_STEPPER
static void CreateBoardLedRings(int boardNo, uint8_t addr, bool boardEnabled, int ledLane, int ledStartPos) {
  NEOSR_Config_t stepperRingConfig;
  NEOSR_Handle_t ring[PL_CONFIG_NOF_STEPPER_ON_BOARD_X*PL_CONFIG_NOF_STEPPER_ON_BOARD_Y*PL_CONFIG_NOF_STEPPER_ON_BOARD_Z];
  STEPPER_Config_t stepperConfig;
  STEPPER_Handle_t stepper[PL_CONFIG_NOF_STEPPER_ON_BOARD_X*PL_CONFIG_NOF_STEPPER_ON_BOARD_Y*PL_CONFIG_NOF_STEPPER_ON_BOARD_Z];
  STEPBOARD_Config_t stepBoardConfig;

  /* get default configurations */
  STEPPER_GetDefaultConfig(&stepperConfig);
  STEPBOARD_GetDefaultConfig(&stepBoardConfig);
  NEOSR_GetDefaultConfig(&stepperRingConfig);

  /* setup ring: note that we assume a virtual 1x4 board layout, so it has a total of 8 rings */
  stepperRingConfig.ledLane = ledLane;
  stepperRingConfig.ledStartPos = ledStartPos;
  stepperRingConfig.ledCw = false;
  ring[0] = NEOSR_InitDevice(&stepperRingConfig);
  ring[1] = NEOSR_InitDevice(&stepperRingConfig);
  stepperRingConfig.ledStartPos = ledStartPos+40;
  ring[2] = NEOSR_InitDevice(&stepperRingConfig);
  ring[3] = NEOSR_InitDevice(&stepperRingConfig);

  stepperRingConfig.ledStartPos = ledStartPos+80;
  ring[4] = NEOSR_InitDevice(&stepperRingConfig);
  ring[5] = NEOSR_InitDevice(&stepperRingConfig);

  stepperRingConfig.ledStartPos = ledStartPos+120;
  ring[6] = NEOSR_InitDevice(&stepperRingConfig);
  ring[7] = NEOSR_InitDevice(&stepperRingConfig);

  /* setup stepper */
  stepperConfig.stepFn = NULL;

  stepperConfig.device = ring[0];
  stepper[0] = STEPPER_InitDevice(&stepperConfig);
  stepperConfig.device = ring[1];
  stepper[1] = STEPPER_InitDevice(&stepperConfig);
  stepperConfig.device = ring[2];
  stepper[2] = STEPPER_InitDevice(&stepperConfig);
  stepperConfig.device = ring[3];
  stepper[3] = STEPPER_InitDevice(&stepperConfig);
  stepperConfig.device = ring[4];
  stepper[4] = STEPPER_InitDevice(&stepperConfig);
  stepperConfig.device = ring[5];
  stepper[5] = STEPPER_InitDevice(&stepperConfig);
  stepperConfig.device = ring[6];
  stepper[6] = STEPPER_InitDevice(&stepperConfig);
  stepperConfig.device = ring[7];
  stepper[7] = STEPPER_InitDevice(&stepperConfig);

  /* setup board */
  stepBoardConfig.addr = addr;
  stepBoardConfig.enabled = boardEnabled;

  stepBoardConfig.ledRing[0][0][0] = ring[0];
  stepBoardConfig.ledRing[0][0][1] = ring[1];
  stepBoardConfig.ledRing[1][0][0] = ring[2];
  stepBoardConfig.ledRing[1][0][1] = ring[3];
  stepBoardConfig.ledRing[2][0][0] = ring[4];
  stepBoardConfig.ledRing[2][0][1] = ring[5];
  stepBoardConfig.ledRing[3][0][0] = ring[6];
  stepBoardConfig.ledRing[3][0][1] = ring[7];

  stepBoardConfig.stepper[0][0][0] = stepper[0];
  stepBoardConfig.stepper[0][0][1] = stepper[1];
  stepBoardConfig.stepper[1][0][0] = stepper[2];
  stepBoardConfig.stepper[1][0][1] = stepper[3];
  stepBoardConfig.stepper[2][0][0] = stepper[4];
  stepBoardConfig.stepper[2][0][1] = stepper[5];
  stepBoardConfig.stepper[3][0][0] = stepper[6];
  stepBoardConfig.stepper[3][0][1] = stepper[7];

  MATRIX_Boards[boardNo] = STEPBOARD_InitDevice(&stepBoardConfig);
}
#endif /* PL_CONFIG_USE_VIRTUAL_STEPPER */

#if PL_CONFIG_USE_VIRTUAL_STEPPER
static void InitLedRings(void) {
#if PL_CONFIG_IS_MASTER
#if PL_MATRIX_CONFIGURATION_ID==PL_MATRIX_ID_CLOCK_8x3
  CreateBoardLedRings(0, BOARD_ADDR_00, true, 0, 0);
  CreateBoardLedRings(1, BOARD_ADDR_01, true, 1, 0);
  CreateBoardLedRings(2, BOARD_ADDR_02, true, 2, 0);

  CreateBoardLedRings(3, BOARD_ADDR_05, true, 0, 4*40);
  CreateBoardLedRings(4, BOARD_ADDR_06, true, 1, 4*40);
  CreateBoardLedRings(5, BOARD_ADDR_07, true, 2, 4*40);
#elif PL_MATRIX_CONFIGURATION_ID==PL_MATRIX_ID_CLOCK_12x5_60B || PL_MATRIX_CONFIGURATION_ID==PL_MATRIX_ID_CLOCK_12x5_MOD
#if MATRIX_NOF_BOARDS>=1
  CreateBoardLedRings(0, BOARD_ADDR_00, true, 0, 0);
#endif
#if MATRIX_NOF_BOARDS>=2
  CreateBoardLedRings(1, BOARD_ADDR_01, true, 1, 0);
#endif
#if MATRIX_NOF_BOARDS>=3
  CreateBoardLedRings(2, BOARD_ADDR_02, true, 2, 0);
#endif
#if MATRIX_NOF_BOARDS>=4
  CreateBoardLedRings(3, BOARD_ADDR_03, true, 3, 0);
#endif
#if MATRIX_NOF_BOARDS>=5
  CreateBoardLedRings(4, BOARD_ADDR_04, true, 4, 0);
#endif

#if MATRIX_NOF_BOARDS>=6
  CreateBoardLedRings(5, BOARD_ADDR_05, true, 0, 4*40);
#endif
#if MATRIX_NOF_BOARDS>=7
  CreateBoardLedRings(6, BOARD_ADDR_06, true, 1, 4*40);
#endif
#if MATRIX_NOF_BOARDS>=8
  CreateBoardLedRings(7, BOARD_ADDR_07, true, 2, 4*40);
#endif
#if MATRIX_NOF_BOARDS>=9
  CreateBoardLedRings(8, BOARD_ADDR_08, true, 3, 4*40);
#endif
#if MATRIX_NOF_BOARDS>=10
  CreateBoardLedRings(9, BOARD_ADDR_09, true, 4, 4*40);
#endif

#if MATRIX_NOF_BOARDS>=11
  CreateBoardLedRings(10, BOARD_ADDR_10, true, 0, 8*40);
#endif
#if MATRIX_NOF_BOARDS>=12
  CreateBoardLedRings(11, BOARD_ADDR_11, true, 1, 8*40);
#endif
#if MATRIX_NOF_BOARDS>=13
  CreateBoardLedRings(12, BOARD_ADDR_12, true, 2, 8*40);
#endif
#if MATRIX_NOF_BOARDS>=14
  CreateBoardLedRings(13, BOARD_ADDR_13, true, 3, 8*40);
#endif
#if MATRIX_NOF_BOARDS>=15
  CreateBoardLedRings(14, BOARD_ADDR_14, true, 4, 8*40);
#endif
#endif
#else /* not master, normal clock board */
  CreateBoardLedRings(0, RS485_GetAddress(), true, 0, 0);
#endif /* PL_CONFIG_IS_MASTER */
}
#endif /* PL_CONFIG_USE_VIRTUAL_STEPPER */

#if PL_CONFIG_USE_X12_STEPPER
static void InitSteppers(void) {
  McuX12_017_Config_t x12config;
  McuX12_017_Handle_t x12device[PL_CONFIG_BOARD_NOF_MOTOR_DRIVER]; /* motor driver IC on the board */
  STEPPER_Config_t stepperConfig;
  STEPPER_Handle_t stepper[MATRIX_NOF_STEPPERS];
  STEPBOARD_Config_t stepBoardConfig;

  /* get default configurations */
  McuX12_017_GetDefaultConfig(&x12config);
  STEPPER_GetDefaultConfig(&stepperConfig);
  STEPBOARD_GetDefaultConfig(&stepBoardConfig);

  /* -------- X12.017 motor drivers: 1 or 2 on each board -------------- */
#if PL_CONFIG_BOARD_NOF_MOTOR_DRIVER>=1
  /* initialize first X12.017 */
  /* DRV_RESET: */
  x12config.hasReset = true;
#if PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN128 /* they have a single quad driver on board */
  x12config.hw_reset.gpio = GPIOA;
  x12config.hw_reset.port = PORTA;
  x12config.hw_reset.pin  = 19U;
  /* M0_DIR: */
  x12config.motor[X12_017_M0].hw_dir.gpio = GPIOE;
  x12config.motor[X12_017_M0].hw_dir.port = PORTE;
  x12config.motor[X12_017_M0].hw_dir.pin  = 17U;

  /* M0_STEP: */
  x12config.motor[X12_017_M0].hw_step.gpio = GPIOE;
  x12config.motor[X12_017_M0].hw_step.port = PORTE;
  x12config.motor[X12_017_M0].hw_step.pin  = 16U;

  /* M1_DIR: */
  x12config.motor[X12_017_M1].hw_dir.gpio = GPIOE;
  x12config.motor[X12_017_M1].hw_dir.port = PORTE;
  x12config.motor[X12_017_M1].hw_dir.pin  = 18U;

  /* M1_STEP:  */
  x12config.motor[X12_017_M1].hw_step.gpio = GPIOE;
  x12config.motor[X12_017_M1].hw_step.port = PORTE;
  x12config.motor[X12_017_M1].hw_step.pin  = 19U;

  /* M2_DIR: */
  x12config.motor[X12_017_M2].hw_dir.gpio = GPIOB;
  x12config.motor[X12_017_M2].hw_dir.port = PORTB;
  x12config.motor[X12_017_M2].hw_dir.pin  = 1U;

  /* M2_STEP:  */
  x12config.motor[X12_017_M2].hw_step.gpio = GPIOB;
  x12config.motor[X12_017_M2].hw_step.port = PORTB;
  x12config.motor[X12_017_M2].hw_step.pin  = 0U;

  /* M3_DIR: */
  x12config.motor[X12_017_M3].hw_dir.gpio = GPIOD;
  x12config.motor[X12_017_M3].hw_dir.port = PORTD;
  x12config.motor[X12_017_M3].hw_dir.pin  = 5U;

  /* M3_STEP: */
  x12config.motor[X12_017_M3].hw_step.gpio = GPIOD;
  x12config.motor[X12_017_M3].hw_step.port = PORTD;
  x12config.motor[X12_017_M3].hw_step.pin  = 4U;
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN128
  x12config.hw_reset.gpio = GPIOA;
  x12config.hw_reset.port = PORTA;
  x12config.hw_reset.pin  = 19U;
  /* M0_DIR: */
  x12config.motor[X12_017_M0].hw_dir.gpio = GPIOE;
  x12config.motor[X12_017_M0].hw_dir.port = PORTE;
  x12config.motor[X12_017_M0].hw_dir.pin  = 17U;

  /* M0_STEP: */
  x12config.motor[X12_017_M0].hw_step.gpio = GPIOE;
  x12config.motor[X12_017_M0].hw_step.port = PORTE;
  x12config.motor[X12_017_M0].hw_step.pin  = 16U;

  /* M1_DIR: */
  x12config.motor[X12_017_M1].hw_dir.gpio = GPIOE;
  x12config.motor[X12_017_M1].hw_dir.port = PORTE;
  x12config.motor[X12_017_M1].hw_dir.pin  = 18U;

  /* M1_STEP:  */
  x12config.motor[X12_017_M1].hw_step.gpio = GPIOE;
  x12config.motor[X12_017_M1].hw_step.port = PORTE;
  x12config.motor[X12_017_M1].hw_step.pin  = 19U;

  /* M2_DIR: */
  x12config.motor[X12_017_M2].hw_dir.gpio = GPIOD;
  x12config.motor[X12_017_M2].hw_dir.port = PORTD;
  x12config.motor[X12_017_M2].hw_dir.pin  = 4U;

  /* M2_STEP:  */
  x12config.motor[X12_017_M2].hw_step.gpio = GPIOD;
  x12config.motor[X12_017_M2].hw_step.port = PORTD;
  x12config.motor[X12_017_M2].hw_step.pin  = 5U;

  /* M3_DIR: */
  x12config.motor[X12_017_M3].hw_dir.gpio = GPIOB;
  x12config.motor[X12_017_M3].hw_dir.port = PORTB;
  x12config.motor[X12_017_M3].hw_dir.pin  = 0U;

  /* M3_STEP: */
  x12config.motor[X12_017_M3].hw_step.gpio = GPIOB;
  x12config.motor[X12_017_M3].hw_step.port = PORTB;
  x12config.motor[X12_017_M3].hw_step.pin  = 1U;
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_2X2 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_1X4/* LPC845 */
  /* Reset for both motor drivers: PIO0_14 */
  x12config.hw_reset.gpio = GPIO;
  x12config.hw_reset.port = 0U;
  x12config.hw_reset.pin  = 14U;
  /* M0_DIR: PIO1_9 */
  x12config.motor[X12_017_M0].hw_dir.gpio = GPIO;
  x12config.motor[X12_017_M0].hw_dir.port = 1U;
  x12config.motor[X12_017_M0].hw_dir.pin  = 9U;

  /* M0_STEP: PIO0_12 */
  x12config.motor[X12_017_M0].hw_step.gpio = GPIO;
  x12config.motor[X12_017_M0].hw_step.port = 0U;
  x12config.motor[X12_017_M0].hw_step.pin  = 12U;

  /* M1_DIR: PIO0_13 */
  x12config.motor[X12_017_M1].hw_dir.gpio = GPIO;
  x12config.motor[X12_017_M1].hw_dir.port = 0U;
  x12config.motor[X12_017_M1].hw_dir.pin  = 13U;

  /* M1_STEP: PIO1_8 */
  x12config.motor[X12_017_M1].hw_step.gpio = GPIO;
  x12config.motor[X12_017_M1].hw_step.port = 1U;
  x12config.motor[X12_017_M1].hw_step.pin  = 8U;

  /* M2_DIR: PIO0_4 */
  x12config.motor[X12_017_M2].hw_dir.gpio = GPIO;
  x12config.motor[X12_017_M2].hw_dir.port = 0U;
  x12config.motor[X12_017_M2].hw_dir.pin  = 4U;

  /* M2_STEP: PIO0_28 */
  x12config.motor[X12_017_M2].isInverted = true;
  x12config.motor[X12_017_M2].hw_step.gpio = GPIO;
  x12config.motor[X12_017_M2].hw_step.port = 0U;
  x12config.motor[X12_017_M2].hw_step.pin  = 28U;

#if PL_CONFIG_BOARD_VERSION==1 /* old V0.1, PIO0_11 needs external pull-up! */
  /* M3_DIR: PIO0_11 */
  x12config.motor[X12_017_M3].isInverted = true;
  x12config.motor[X12_017_M3].hw_dir.gpio = GPIO;
  x12config.motor[X12_017_M3].hw_dir.port = 0U;
  x12config.motor[X12_017_M3].hw_dir.pin  = 11U;
#else /* current V1.0 */
  /* M3_DIR: PIO0_27 */
  x12config.motor[X12_017_M3].isInverted = true;
  x12config.motor[X12_017_M3].hw_dir.gpio = GPIO;
  x12config.motor[X12_017_M3].hw_dir.port = 0U;
  x12config.motor[X12_017_M3].hw_dir.pin  = 27U;
#endif

#if PL_CONFIG_BOARD_VERSION==1 /* old V0.1 */
  /* M3_STEP: PIO1_0 */
  x12config.motor[X12_017_M3].hw_step.gpio = GPIO;
  x12config.motor[X12_017_M3].hw_step.port = 1U;
  x12config.motor[X12_017_M3].hw_step.pin  = 0U;
#else /* current V1.0 */
  /* M3_STEP: PIO0_26 */
  x12config.motor[X12_017_M3].hw_step.gpio = GPIO;
  x12config.motor[X12_017_M3].hw_step.port = 0U;
  x12config.motor[X12_017_M3].hw_step.pin  = 26U;
#endif
#else
  #error "NYI"
#endif /* LPC845 */

  x12device[0] = McuX12_017_InitDevice(&x12config);
#endif /* PL_CONFIG_BOARD_NOF_MOTOR_DRIVER>=1 */

#if PL_CONFIG_BOARD_NOF_MOTOR_DRIVER>=2 /* boards with two motor driver IC */
  /* initialize second X12.017 */
  x12config.hasReset = false; /* second device shares the reset line from the first */
#if PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_2X2 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_1X4
  /* M4_DIR: PIO0_0 */
  x12config.motor[X12_017_M0].hw_dir.gpio = GPIO;
  x12config.motor[X12_017_M0].hw_dir.port = 0U;
  x12config.motor[X12_017_M0].hw_dir.pin  = 0U;

  /* M4_STEP: PIO1_7 */
  x12config.motor[X12_017_M0].hw_step.gpio = GPIO;
  x12config.motor[X12_017_M0].hw_step.port = 1U;
  x12config.motor[X12_017_M0].hw_step.pin  = 7U;

  /* M5_DIR: PIO0_6 */
  x12config.motor[X12_017_M1].hw_dir.gpio = GPIO;
  x12config.motor[X12_017_M1].hw_dir.port = 0U;
  x12config.motor[X12_017_M1].hw_dir.pin  = 6U;

  /* M5_STEP: PIO0_7 */
  x12config.motor[X12_017_M1].hw_step.gpio = GPIO;
  x12config.motor[X12_017_M1].hw_step.port = 0U;
  x12config.motor[X12_017_M1].hw_step.pin  = 7U;

  /* M6_DIR: PIO0_8 */
  x12config.motor[X12_017_M2].isInverted = true;
  x12config.motor[X12_017_M2].hw_dir.gpio = GPIO;
  x12config.motor[X12_017_M2].hw_dir.port = 0U;
  x12config.motor[X12_017_M2].hw_dir.pin  = 8U;

  /* M6_STEP: PIO0_9 */
  x12config.motor[X12_017_M2].hw_step.gpio = GPIO;
  x12config.motor[X12_017_M2].hw_step.port = 0U;
  x12config.motor[X12_017_M2].hw_step.pin  = 9U;

  /* M7_DIR: PIO1_5 */
  x12config.motor[X12_017_M3].isInverted = true;
  x12config.motor[X12_017_M3].hw_dir.gpio = GPIO;
  x12config.motor[X12_017_M3].hw_dir.port = 1U;
  x12config.motor[X12_017_M3].hw_dir.pin  = 5U;

  /* M7_STEP: PIO1_6 */
  x12config.motor[X12_017_M3].hw_step.gpio = GPIO;
  x12config.motor[X12_017_M3].hw_step.port = 1U;
  x12config.motor[X12_017_M3].hw_step.pin  = 6U;

  x12device[1] = McuX12_017_InitDevice(&x12config);
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN128
  /* M4_DIR: */
  x12config.motor[X12_017_M0].hw_dir.gpio = GPIOE;
  x12config.motor[X12_017_M0].hw_dir.port = PORTE;
  x12config.motor[X12_017_M0].hw_dir.pin  = 24U;

  /* M4_STEP: */
  x12config.motor[X12_017_M0].hw_step.gpio = GPIOC;
  x12config.motor[X12_017_M0].hw_step.port = PORTC;
  x12config.motor[X12_017_M0].hw_step.pin  = 6U;

  /* M5_DIR: */
  x12config.motor[X12_017_M1].hw_dir.gpio = GPIOA;
  x12config.motor[X12_017_M1].hw_dir.port = PORTA;
  x12config.motor[X12_017_M1].hw_dir.pin  = 1U;

  /* M5_STEP:  */
  x12config.motor[X12_017_M1].hw_step.gpio = GPIOE;
  x12config.motor[X12_017_M1].hw_step.port = PORTE;
  x12config.motor[X12_017_M1].hw_step.pin  = 25U;

  /* M6_DIR: */
  x12config.motor[X12_017_M2].hw_dir.gpio = GPIOA;
  x12config.motor[X12_017_M2].hw_dir.port = PORTA;
  x12config.motor[X12_017_M2].hw_dir.pin  = 4U;

  /* M6_STEP:  */
  x12config.motor[X12_017_M2].hw_step.gpio = GPIOA;
  x12config.motor[X12_017_M2].hw_step.port = PORTA;
  x12config.motor[X12_017_M2].hw_step.pin  = 2U;

  /* M7_DIR: */
  x12config.motor[X12_017_M3].hw_dir.gpio = GPIOD;
  x12config.motor[X12_017_M3].hw_dir.port = PORTD;
  x12config.motor[X12_017_M3].hw_dir.pin  = 6U;

  /* M7_STEP: */
  x12config.motor[X12_017_M3].hw_step.gpio = GPIOD;
  x12config.motor[X12_017_M3].hw_step.port = PORTD;
  x12config.motor[X12_017_M3].hw_step.pin  = 7U;

  x12device[1] = McuX12_017_InitDevice(&x12config);
#else
  #error "configure 2nd driver!"
#endif

#endif /* PL_CONFIG_BOARD_NOF_MOTOR_DRIVER>=2 */

  /* setup the stepper motors */
#if PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN128
  #if MATRIX_NOF_STEPPERS>=1
  x12Steppers[0].x12device = x12device[0];
  x12Steppers[0].x12motor = X12_017_M1;
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  x12Steppers[1].x12device = x12device[0];
  x12Steppers[1].x12motor = X12_017_M0;
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  x12Steppers[2].x12device = x12device[0];
  x12Steppers[2].x12motor = X12_017_M3;
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  x12Steppers[3].x12device = x12device[0];
  x12Steppers[3].x12motor = X12_017_M2;
  #endif
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_2X2 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_1X4
  #if MATRIX_NOF_STEPPERS>=1
  x12Steppers[0].x12device = x12device[0];
  x12Steppers[0].x12motor = X12_017_M1;
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  x12Steppers[1].x12device = x12device[0];
  x12Steppers[1].x12motor = X12_017_M0;
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  x12Steppers[2].x12device = x12device[0];
  x12Steppers[2].x12motor = X12_017_M3;
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  x12Steppers[3].x12device = x12device[0];
  x12Steppers[3].x12motor = X12_017_M2;
  #endif
  #if MATRIX_NOF_STEPPERS>=5
  x12Steppers[4].x12device = x12device[1];
  x12Steppers[4].x12motor = X12_017_M3;
  #endif
  #if MATRIX_NOF_STEPPERS>=6
  x12Steppers[5].x12device = x12device[1];
  x12Steppers[5].x12motor = X12_017_M2;
  #endif
  #if MATRIX_NOF_STEPPERS>=7
  x12Steppers[6].x12device = x12device[1];
  x12Steppers[6].x12motor = X12_017_M1;
  #endif
  #if MATRIX_NOF_STEPPERS>=8
  x12Steppers[7].x12device = x12device[1];
  x12Steppers[7].x12motor = X12_017_M0;
  #endif
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN128
  #if MATRIX_NOF_STEPPERS>=1
  x12Steppers[0].x12device = x12device[0];
  x12Steppers[0].x12motor = X12_017_M0;
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  x12Steppers[1].x12device = x12device[0];
  x12Steppers[1].x12motor = X12_017_M1;
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  x12Steppers[2].x12device = x12device[0];
  x12Steppers[2].x12motor = X12_017_M2;
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  x12Steppers[3].x12device = x12device[0];
  x12Steppers[3].x12motor = X12_017_M3;
  #endif
  #if MATRIX_NOF_STEPPERS>=5
  x12Steppers[4].x12device = x12device[1];
  x12Steppers[4].x12motor = X12_017_M0;
  #endif
  #if MATRIX_NOF_STEPPERS>=6
  x12Steppers[5].x12device = x12device[1];
  x12Steppers[5].x12motor = X12_017_M1;
  #endif
  #if MATRIX_NOF_STEPPERS>=7
  x12Steppers[6].x12device = x12device[1];
  x12Steppers[6].x12motor = X12_017_M2;
  #endif
  #if MATRIX_NOF_STEPPERS>=8
  x12Steppers[7].x12device = x12device[1];
  x12Steppers[7].x12motor = X12_017_M3;
  #endif
#else
  #error "check your configuration!"
#endif

#if PL_CONFIG_USE_MAG_SENSOR
#if PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN128
  #if MATRIX_NOF_STEPPERS>=1
  x12Steppers[0].mag = MAG_MAG1;
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  x12Steppers[1].mag = MAG_MAG0;
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  x12Steppers[2].mag = MAG_MAG3;
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  x12Steppers[3].mag = MAG_MAG2;
  #endif
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_2X2 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_1X4
  x12Steppers[0].mag = MAG_MAG1;
  x12Steppers[1].mag = MAG_MAG0;
  x12Steppers[2].mag = MAG_MAG2;
  x12Steppers[3].mag = MAG_MAG3;
  x12Steppers[4].mag = MAG_MAG4;
  x12Steppers[5].mag = MAG_MAG5;
  x12Steppers[6].mag = MAG_MAG7;
  x12Steppers[7].mag = MAG_MAG6;
#else
  #error "check your configuration!"
#endif
#endif
  /* initialize stepper, 2 or 4 for each X12 driver, depending on the IC (dual or quad) */
  stepperConfig.stepFn = X12_SingleStep;

#if PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN128
  #if MATRIX_NOF_STEPPERS>=1
  stepperConfig.device = &x12Steppers[0];
  stepper[0] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  stepperConfig.device = &x12Steppers[1];
  stepper[1] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  stepperConfig.device = &x12Steppers[2];
  stepper[2] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  stepperConfig.device = &x12Steppers[3];
  stepper[3] = STEPPER_InitDevice(&stepperConfig);
  #endif
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_2X2 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_1X4
  #if MATRIX_NOF_STEPPERS>=1
  stepperConfig.device = &x12Steppers[0];
  stepper[0] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  stepperConfig.device = &x12Steppers[1];
  stepper[1] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  stepperConfig.device = &x12Steppers[2];
  stepper[2] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  stepperConfig.device = &x12Steppers[3];
  stepper[3] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=5
  stepperConfig.device = &x12Steppers[4];
  stepper[4] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=6
  stepperConfig.device = &x12Steppers[5];
  stepper[5] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=7
  stepperConfig.device = &x12Steppers[6];
  stepper[6] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=8
  stepperConfig.device = &x12Steppers[7];
  stepper[7] = STEPPER_InitDevice(&stepperConfig);
  #endif
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN128
  #if MATRIX_NOF_STEPPERS>=1
  stepperConfig.device = &x12Steppers[0];
  stepper[0] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  stepperConfig.device = &x12Steppers[1];
  stepper[1] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  stepperConfig.device = &x12Steppers[2];
  stepper[2] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  stepperConfig.device = &x12Steppers[3];
  stepper[3] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=5
  stepperConfig.device = &x12Steppers[4];
  stepper[4] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=6
  stepperConfig.device = &x12Steppers[5];
  stepper[5] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=7
  stepperConfig.device = &x12Steppers[6];
  stepper[6] = STEPPER_InitDevice(&stepperConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=8
  stepperConfig.device = &x12Steppers[7];
  stepper[7] = STEPPER_InitDevice(&stepperConfig);
  #endif
#else
  #error "check your configuration!"
#endif

  /* setup board */
  stepBoardConfig.addr = RS485_GetAddress();
  stepBoardConfig.enabled = true;
#if PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_K02FN128
  #if MATRIX_NOF_STEPPERS>=1
  stepBoardConfig.stepper[0][0][0] = stepper[0];
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  stepBoardConfig.stepper[0][0][1] = stepper[1];
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  stepBoardConfig.stepper[1][0][0] = stepper[2];
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  stepBoardConfig.stepper[1][0][1] = stepper[3];
  #endif
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_1X4
  #if MATRIX_NOF_STEPPERS>=1
  stepBoardConfig.stepper[0][0][0] = stepper[6];
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  stepBoardConfig.stepper[0][0][1] = stepper[7];
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  stepBoardConfig.stepper[1][0][0] = stepper[4];
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  stepBoardConfig.stepper[1][0][1] = stepper[5];
  #endif
  #if MATRIX_NOF_STEPPERS>=5
  stepBoardConfig.stepper[2][0][0] = stepper[2];
  #endif
  #if MATRIX_NOF_STEPPERS>=6
  stepBoardConfig.stepper[2][0][1] = stepper[3];
  #endif
  #if MATRIX_NOF_STEPPERS>=7
  stepBoardConfig.stepper[3][0][0] = stepper[0];
  #endif
  #if MATRIX_NOF_STEPPERS>=8
  stepBoardConfig.stepper[3][0][1] = stepper[1];
  #endif
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_CLOCK_LPC845_2X2
  #if MATRIX_NOF_STEPPERS>=1
  stepBoardConfig.stepper[0][0][0] = stepper[0];
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  stepBoardConfig.stepper[0][0][1] = stepper[1];
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  stepBoardConfig.stepper[1][0][0] = stepper[6];
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  stepBoardConfig.stepper[1][0][1] = stepper[7];
  #endif
  #if MATRIX_NOF_STEPPERS>=5
  stepBoardConfig.stepper[0][1][0] = stepper[2];
  #endif
  #if MATRIX_NOF_STEPPERS>=6
  stepBoardConfig.stepper[0][1][1] = stepper[3];
  #endif
  #if MATRIX_NOF_STEPPERS>=7
  stepBoardConfig.stepper[1][1][0] = stepper[4];
  #endif
  #if MATRIX_NOF_STEPPERS>=8
  stepBoardConfig.stepper[1][1][1] = stepper[5];
  #endif
#elif PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN64 || PL_CONFIG_BOARD_ID==PL_CONFIG_BOARD_ID_PIXELUNIT_K02FN128
  #if MATRIX_NOF_STEPPERS>=1
  stepBoardConfig.stepper[0][0][0] = stepper[7];
  #endif
  #if MATRIX_NOF_STEPPERS>=2
  stepBoardConfig.stepper[1][0][0] = stepper[6];
  #endif
  #if MATRIX_NOF_STEPPERS>=3
  stepBoardConfig.stepper[2][0][0] = stepper[5];
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  stepBoardConfig.stepper[3][0][0] = stepper[4];
  #endif
  #if MATRIX_NOF_STEPPERS>=5
  stepBoardConfig.stepper[4][0][0] = stepper[3];
  #endif
  #if MATRIX_NOF_STEPPERS>=6
  stepBoardConfig.stepper[5][0][0] = stepper[2];
  #endif
  #if MATRIX_NOF_STEPPERS>=7
  stepBoardConfig.stepper[6][0][0] = stepper[1];
  #endif
  #if MATRIX_NOF_STEPPERS>=8
  stepBoardConfig.stepper[7][0][0] = stepper[0];
  #endif
#else
  #error "check your configuration!"
#endif

#if PL_CONFIG_USE_LED_RING
  /* setup ring */
  NEOSR_Config_t stepperRingConfig;

  NEOSR_GetDefaultConfig(&stepperRingConfig);
  stepperRingConfig.ledLane = 5; /* PTC5 */
  stepperRingConfig.ledStartPos = 0;
  stepperRingConfig.ledCw = false;
  #if MATRIX_NOF_STEPPERS>=2
  stepBoardConfig.ledRing[0][0][0] = NEOSR_InitDevice(&stepperRingConfig);
  stepBoardConfig.ledRing[0][0][1] = NEOSR_InitDevice(&stepperRingConfig);
  #endif
  #if MATRIX_NOF_STEPPERS>=4
  stepperRingConfig.ledLane = 6; /* PTC6 */
  stepBoardConfig.ledRing[1][0][0] = NEOSR_InitDevice(&stepperRingConfig);
  stepBoardConfig.ledRing[1][0][1] = NEOSR_InitDevice(&stepperRingConfig);
  #endif
#endif

  MATRIX_Boards[0] = STEPBOARD_InitDevice(&stepBoardConfig);
#if PL_CONFIG_USE_MOTOR_ON_OFF
  McuGPIO_Handle_t gpioSwitch;
  McuGPIO_Config_t gpioSwitchConfig;

  McuGPIO_GetDefaultConfig(&gpioSwitchConfig);
  gpioSwitchConfig.hw.gpio = GPIOA;
  gpioSwitchConfig.hw.port = PORTA;
  gpioSwitchConfig.hw.pin = 18;
  gpioSwitchConfig.isHighOnInit = true; /* on/off switch is LOW active: have power disabled at the start */
  gpioSwitchConfig.isInput = false;
  gpioSwitch = McuGPIO_InitGPIO(&gpioSwitchConfig);
  STEPBOARD_SetMotorSwitch(MATRIX_Boards[0], gpioSwitch);

#if 1
  /* disable motors from the start to reduce current */
  STEPBOARD_MotorSwitchOnOff(MATRIX_Boards[0], false);
#else
  /* enable motors from the start */
  STEPBOARD_MotorSwitchOnOff(MATRIX_Boards[0], true);
#endif

#endif /* PL_CONFIG_USE_MOTOR_ON_OFF */
  McuX12_017_ResetDriver(x12device[0]); /* shared reset line for all motor drivers */
}
#endif /* PL_CONFIG_USE_X12_STEPPER */

#if PL_CONFIG_USE_STEPPER
static void InitMatrixHardware(void) {
#if PL_CONFIG_USE_VIRTUAL_STEPPER /* only using virtual LED stepper */
  InitLedRings();
#elif PL_CONFIG_USE_X12_STEPPER /* stepper motors, with and without LEDs */
  InitSteppers();
#endif
#if PL_CONFIG_USE_STEPPER
  STEPBOARD_SetBoard(MATRIX_Boards[0]); /* set default board */
#endif
}
#endif /* PL_CONFIG_USE_STEPPER */

void MATRIX_Init(void) {
#if PL_CONFIG_USE_STEPPER
  if (xTaskCreate(
      MatrixQueueTask,  /* pointer to the task */
      "MatrixQueue", /* task name for kernel awareness debugging */
      800/sizeof(StackType_t), /* task stack size */
      (void*)NULL, /* optional task startup argument */
      tskIDLE_PRIORITY+3,  /* initial priority */
      NULL /* optional task handle to create */
    ) != pdPASS)
  {
    for(;;){} /* error! probably out of memory */
  }
  semMatrixExecuteQueue = xSemaphoreCreateBinary();
  if (semMatrixExecuteQueue==NULL) {
    for(;;){}
  }
  vQueueAddToRegistry(semMatrixExecuteQueue, "semMatrixExecQueue");
  InitMatrixHardware();
#endif
#if PL_CONFIG_IS_MASTER
  MATRIX_ResetBoardListCmdSent();
  MPOS_SetAngleAll(0);
#if PL_CONFIG_USE_RELATIVE_MOVES
  MPOS_RelativeMoveAll(0);
#endif
  MPOS_SetMoveModeAll(STEPPER_MOVE_MODE_SHORT);
  MATRIX_SetMoveDelayAll(2);
#if PL_MATRIX_CONFIG_IS_RGB
  MHAND_SetHandColorAll(0); /* set to zero, will be set below after the matrix copy operation */
  MATRIX_DrawAllRingColor(0x000000); /* ring color off */
  MHAND_HandEnableAll(true); /* will be set below */
#endif
  MATRIX_CopyMatrix(&prevMatrix, &matrix); /* make backup */
#if PL_CONFIG_USE_NEO_PIXEL_HW
  MHAND_SetHandColorAll(NEO_COMBINE_RGB(0x08, 0x08, 0x08));
#endif
#if PL_MATRIX_CONFIG_IS_RGB
  MHAND_SetHandColorAll(MATRIX_GetHandColorAdjusted()); /* default hand color */
#endif
#endif
}
