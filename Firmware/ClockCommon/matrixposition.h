/*
 * matrixposition.h
 *
 * Copyright (c) 2020: Erich Styger
 * License: PDX-License-Identifier: BSD-3-Clause
 *
 * Interface to set motor position in the matrix
 */

#ifndef HANDS_H_
#define HANDS_H_

#include "platform.h"
#include <stdint.h>
#include "stepper.h"

#define MPOS_ANGLE_HIDE   (225)  /* special angle position to 'hide' */

/* set the angle of a single hand */
void MPOS_SetAngle(uint8_t x, uint8_t y, uint8_t z, int16_t angle);
void MPOS_SetAngleChecked(uint8_t x, uint8_t y, uint8_t z, int16_t angle);

#if MATRIX_NOF_STEPPERS_Z==2 /* the special functions below are only available for dual shaft motors */
/* set the angle of two hands: z==0 and z==1 */
void MPOS_SetAngleZ0Z1(uint8_t x, uint8_t y, int16_t z0Angle, int16_t z1Angle);
void MPOS_SetAngleZ0Z1Checked(uint8_t x, uint8_t y, int16_t z0Angle, int16_t z1Angle);

/* set the angle of all clocks for z==0 and z==1 */
void MPOS_SetAngleZ0Z1All(int16_t z0Angle, int16_t z1Angle);
#endif

/* set the angle for all motors */
void MPOS_SetAngleAll(int16_t angle);

void MPOS_SetMoveMode(uint8_t x, uint8_t y, uint8_t z, STEPPER_MoveMode_e mode);
void MPOS_SetMoveModeChecked(uint8_t x, uint8_t y, uint8_t z, STEPPER_MoveMode_e mode);
#if MATRIX_NOF_STEPPERS_Z==2 /* the special functions below are only available for dual shaft motors */
void MPOS_SetMoveModeZ0Z1(uint8_t x, uint8_t y, STEPPER_MoveMode_e mode0, STEPPER_MoveMode_e mode1);
void MPOS_SetMoveModeZ0Z1Checked(uint8_t x, uint8_t y, STEPPER_MoveMode_e mode0, STEPPER_MoveMode_e mode1);
void MPOS_SetMoveModeZ0Z1All(STEPPER_MoveMode_e mode0, STEPPER_MoveMode_e mode1);
#endif
void MPOS_SetMoveModeAll(STEPPER_MoveMode_e mode);

#if PL_CONFIG_USE_RELATIVE_MOVES
void MPOS_RelativeMove(uint8_t x, uint8_t y, uint8_t z, int16_t angle);
#if MATRIX_NOF_STEPPERS_Z==2 /* the special functions below are only available for dual shaft motors */
void MPOS_RelativeMoveZ0Z1(uint8_t x, uint8_t y, int16_t angle0, int16_t angle1);
void MPOS_SetRelativeMoveZ0Z1Checked(int x, int y, int angleZ0, int angleZ1);
#endif
void MPOS_RelativeMoveAll(int16_t angle);
#endif

#endif /* HANDS_H_ */
