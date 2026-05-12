#ifndef __UTYPES_H__
#define __UTYPES_H__

#include "cc.h"

/* Object dictionary storage */

typedef struct
{
   /* Identity */

   uint32_t serial;

   /* Inputs */

   int32_t Enc_Pos[4];
   int32_t Enc_Vel[4];
   uint8_t Enc_Status[4];
   uint8_t Pwm_Status[4];
   int32_t Ctrl_Vel_Fb[4];
   uint16_t Inputs;

   /* Outputs */

   int32_t Pwm_Cmd[4];
   uint8_t Pwm_En[4];
   uint8_t Enc_En[4];
   int32_t Ctrl_Vel_Cmd[4];
   uint16_t Outputs;

   /* Parameters */

   int32_t Ctrl_Kp[4];
   int32_t Ctrl_Ki[4];
   int32_t Ctrl_FF0[4];
   int32_t Ctrl_FF1[4];
   int32_t Ctrl_Integrator_Limit[4];
   int32_t Ctrl_Output_Limit[4];
   uint8_t Mode[4];
   uint32_t Enc_Fast_Threshold_Cps;
} _Objects;

extern _Objects Obj;

#endif /* __UTYPES_H__ */
