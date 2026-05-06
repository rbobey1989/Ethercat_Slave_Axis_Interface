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
   uint16_t Inputs;
   uint8_t Enc_Status[4];
   uint8_t Pwm_Status[4];

   /* Outputs */

   int32_t Pwm_Cmd[4];
   uint8_t Pwm_En[4];
   uint8_t Enc_En[4];
   uint16_t Outputs;

} _Objects;

extern _Objects Obj;

#endif /* __UTYPES_H__ */
