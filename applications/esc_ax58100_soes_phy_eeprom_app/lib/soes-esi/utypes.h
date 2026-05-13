#ifndef __UTYPES_H__
#define __UTYPES_H__

#include "cc.h"

/* Object dictionary storage */

typedef struct
{
   /* Identity */

   uint32_t serial;

   /* Inputs */

   int32_t Enc_Pos[2];
   int32_t Enc_Vel[2];
   uint8_t Enc_Status[2];
   uint8_t Pwm_Status[2];
   int32_t Ctrl_Vel_Fb[2];
   uint16_t Inputs;

   /* Outputs */

   int32_t Pwm_Cmd[2];
   uint8_t Pwm_En[2];
   uint8_t Enc_En[2];
   int32_t Ctrl_Vel_Cmd[2];
   uint16_t Outputs;

   /* Parameters */

   int32_t Ctrl_Kp[2];
   int32_t Ctrl_Ki[2];
   int32_t Ctrl_FF0[2];
   int32_t Ctrl_FF1[2];
   int32_t Ctrl_Integrator_Limit[2];
   int32_t Ctrl_Output_Limit[2];
   uint8_t Mode[2];
} _Objects;

extern _Objects Obj;

#endif /* __UTYPES_H__ */
