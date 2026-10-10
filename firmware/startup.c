#include <stdint.h>

extern uint32_t _etext, _sdata, _edata, _sbss, _ebss, _stack_top;
int main(void);
void fault_blink(int pattern); /* main.c — visible diagnostics, never returns */

#define REG32(a) (*(volatile uint32_t *)(a))
#define NVIC_ICER0  REG32(0xE000E180)
#define NVIC_ICER1  REG32(0xE000E184)
#define NVIC_ICPR0  REG32(0xE000E280)
#define NVIC_ICPR1  REG32(0xE000E284)
#define SYST_CSR    REG32(0xE000E010)

void Reset_Handler(void)
{
    /* the PineTime bootloader (mynewt/MCUBoot) jumps here with interrupts
       enabled and peripherals (UART console, timers, SPI flash) still armed.
       Quiesce everything before touching anything. */
    __asm volatile("cpsid i");
    SYST_CSR = 0;
    NVIC_ICER0 = 0xFFFFFFFF;
    NVIC_ICER1 = 0xFFFFFFFF;
    NVIC_ICPR0 = 0xFFFFFFFF;
    NVIC_ICPR1 = 0xFFFFFFFF;

    /* enable the FPU (CP10/CP11 full access) before any float code runs */
    REG32(0xE000ED88) |= (0xFu << 20);
    __asm volatile("dsb; isb");

    uint32_t *src = &_etext, *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;

    /* interrupts stay globally enabled from here; individual lines are
       still all masked in NVIC above and drivers arm what they need.
       BLE (RADIO/RTC0/RNG) is the first consumer of this. */
    __asm volatile("cpsie i");
    main();
    for (;;)
        ;
}

void HardFault_Handler(void) { fault_blink(1); }
void Default_Handler(void)   { fault_blink(2); }

/* full Cortex-M4 + nRF52832 table: 16 system + 38 peripheral vectors.
   The bootloader relocates 0x100 bytes of this to 0x7F00 and sets VTOR,
   so every slot must be a real handler. */
__attribute__((section(".vectors"), used))
const void *const vectors[16 + 38] = {
    &_stack_top,
    Reset_Handler,
    Default_Handler,   /* NMI */
    HardFault_Handler,
    HardFault_Handler, /* MemManage */
    HardFault_Handler, /* BusFault */
    HardFault_Handler, /* UsageFault */
    0, 0, 0, 0,
    Default_Handler,   /* SVC */
    Default_Handler,   /* DebugMon */
    0,
    Default_Handler,   /* PendSV */
    Default_Handler,   /* SysTick */
    /* peripheral IRQs 0..37 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler,
};
