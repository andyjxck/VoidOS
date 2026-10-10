#ifndef _MCU_CMSIS_NVIC_H
#define _MCU_CMSIS_NVIC_H

#include <stdint.h>
#include "nrf52.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Vector table lives in RAM (MCUBoot relocates it and sets VTOR),
   so SetVector is a plain store into it. */
void NVIC_SetVector(IRQn_Type irqn, uint32_t vector);
uint32_t NVIC_GetVector(IRQn_Type irqn);

#ifdef __cplusplus
}
#endif

#endif
