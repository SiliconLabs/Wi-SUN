/***************************************************************************//**
 * @file sl_wisun_crash_handler.c
 * @brief Crash handler for Wi-SUN applications
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include <stdint.h>
#include <string.h>
#include "printf.h"
#include "sl_common.h"
#include "em_rmu.h"
#include "rail.h"
#include "sl_component_catalog.h"
#include "sl_wisun_crash_handler.h"

//#define HALT_IF_DEBUGGER_ENABLED

#if defined(SL_COMPONENT_CATALOG_PRESENT)
#include "sl_component_catalog.h"
#endif

#if defined(SL_CATALOG_MICRIUMOS_KERNEL_PRESENT)
#include "os.h"
#endif

#if defined(SL_CATALOG_FREERTOS_KERNEL_PRESENT)
#include "FreeRTOS.h"
#include "task.h"
#endif

// Trigger a breakpoint if debugger is attached
#if defined(HALT_IF_DEBUGGER_ENABLED)
#define HALT_IF_DEBUGGER()                                            \
  do {                                                                \
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0x0) {  \
      __BKPT(1);                                                      \
    }                                                                 \
  } while (0)
#else
#define HALT_IF_DEBUGGER()
#endif

// Processor context state
typedef struct {
  // R0
  uint32_t r0;
  // R1
  uint32_t r1;
  // R2
  uint32_t r2;
  // R3
  uint32_t r3;
  // R12
  uint32_t r12;
  // Link Register (R14)
  uint32_t lr;
  // Exception Return Address
  uint32_t return_address;
  // Program Status Register
  uint32_t xpsr;
} context_state_t;

// Crash data, stored in the uninitialized section to avoid initialization
// by the C library on boot.
SL_ALIGN(4) sl_wisun_crash_t sl_wisun_crash_data SL_ATTRIBUTE_ALIGN(4) SL_ATTRIBUTE_SECTION(".noinit");

// Crash info string, filled by sl_wisun_check_previous_crash()
#define SL_CRASH_STR_MAX_LEN            300
char crash_info_string[SL_CRASH_STR_MAX_LEN];

#if defined(SL_CATALOG_MICRIUMOS_KERNEL_PRESENT) \
    && (OS_CFG_TASK_STK_REDZONE_EN == DEF_ENABLED) \
    && (OS_CFG_APP_HOOKS_EN == DEF_ENABLED)
/******************************************************************************
 * This function is called when Micrium red zone check fails.
 *****************************************************************************/
void sl_wisun_crash_handler_redzone(OS_TCB *p_tcb)
{
  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_STACK_OVERFLOW;
#if (OS_CFG_DBG_EN == DEF_ENABLED)
  strncpy(sl_wisun_crash_data.u.stack_overflow.task, p_tcb->NamePtr, SL_WISUN_CRASH_TASKNAME_SIZE - 1);
#endif

  NVIC_SystemReset();
}
#endif

#if defined(SL_CATALOG_FREERTOS_KERNEL_PRESENT)
#if (configCHECK_FOR_STACK_OVERFLOW > 1)
/******************************************************************************
 * This function is called when FreeRTOS stack overflow check fails.
 *****************************************************************************/
void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                   char *pcTaskName)
{
  (void)xTask;

  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_STACK_OVERFLOW;
  strncpy(sl_wisun_crash_data.u.stack_overflow.task, pcTaskName, SL_WISUN_CRASH_TASKNAME_SIZE - 1);

  NVIC_SystemReset();
}
#endif
#endif

#if defined(__GNUC__)
/******************************************************************************
 * Wrapped version of GCC __stack_chk_fail.
 *
 * This function is called when the stack protector inserted by the
 * compiler is compromised.
 *****************************************************************************/
void __wrap___stack_chk_fail(void)
{
  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_STACK_PROTECTOR;
  sl_wisun_crash_data.u.stack_protector.lr = (uint32_t)__builtin_return_address(0);

  NVIC_SystemReset();
}
#endif

#if defined(__GNUC__)
/******************************************************************************
 * Wrapped version of GCC __assert_func.
 *
 * This function is called when assert() is triggered.
 *****************************************************************************/
void __wrap___assert_func(const char *file,
                          int line,
                          const char *func,
                          const char *failedexpr)
{
  (void)func;
  (void)failedexpr;

  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_ASSERT;
  sl_wisun_crash_data.u.assert.line = line;
  strncpy(sl_wisun_crash_data.u.assert.file, file, SL_WISUN_CRASH_FILENAME_SIZE - 1);

  NVIC_SystemReset();
}
#endif

#if defined(__ICCARM__)
/******************************************************************************
 * This function is called when IAR assert() is triggered.
 *****************************************************************************/
void __aeabi_assert(const char *mess,
                    const char *file,
                    int line)
{
  (void)mess;

  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_ASSERT;
  sl_wisun_crash_data.u.assert.line = line;
  strncpy(sl_wisun_crash_data.u.assert.file, file, SL_WISUN_CRASH_FILENAME_SIZE - 1);

  NVIC_SystemReset();
}
#endif

#if defined(DEBUG_EFM_USER)
/******************************************************************************
 * This function is called when EFM_ASSERT() is triggered.
 *****************************************************************************/
void assertEFM(const char *file,
               int line)
{
  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_ASSERT;
  sl_wisun_crash_data.u.assert.line = line;
  strncpy(sl_wisun_crash_data.u.assert.file, file, SL_WISUN_CRASH_FILENAME_SIZE - 1);

  NVIC_SystemReset();
}
#endif

/******************************************************************************
 * This function is called when a RAIL assert is triggered.
 *****************************************************************************/
SL_WEAK void RAILCb_AssertFailed(RAIL_Handle_t railHandle,
                                 RAIL_AssertErrorCodes_t errorCode)
{
  (void)railHandle;

  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_RAIL_ASSERT;
  sl_wisun_crash_data.u.rail_assert.error_code = errorCode;

  NVIC_SystemReset();
}

#if defined(__ICCARM__)
/******************************************************************************
 * This function is called when a IAR C-RUN run-time check fails.
 *****************************************************************************/
void __iar_ReportCheckFailed(void *d)
{
  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_CRUN_ERROR;
  sl_wisun_crash_data.u.crun_error.error_code = *((uint32_t *)d);

  NVIC_SystemReset();
}
#endif

#if defined(__ICCARM__)
/******************************************************************************
 * This function is called on IAR program termination.
 *****************************************************************************/
void __exit(int x)
{
  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_EXIT;
  sl_wisun_crash_data.u.exit.status = x;

  NVIC_SystemReset();
}
#endif

/******************************************************************************
 * This function handles a HardFault exception.
 *****************************************************************************/
void hard_fault_handler(const uint32_t *sp)
{
  // Context state was pushed to stack
  const context_state_t* state = (const context_state_t*)sp;

  HALT_IF_DEBUGGER();

  memset(&sl_wisun_crash_data, 0, sizeof(sl_wisun_crash_data));
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_FAULT;
  sl_wisun_crash_data.u.fault.r0 = state->r0;
  sl_wisun_crash_data.u.fault.r1 = state->r1;
  sl_wisun_crash_data.u.fault.r2 = state->r2;
  sl_wisun_crash_data.u.fault.r3 = state->r3;
  sl_wisun_crash_data.u.fault.r12 = state->r12;
  sl_wisun_crash_data.u.fault.lr = state->lr;
  sl_wisun_crash_data.u.fault.return_address = state->return_address;
  sl_wisun_crash_data.u.fault.xpsr = state->xpsr;
  sl_wisun_crash_data.u.fault.cfsr = SCB->CFSR;
  sl_wisun_crash_data.u.fault.hfsr = SCB->HFSR;
  sl_wisun_crash_data.u.fault.mmfar = SCB->MMFAR;
  sl_wisun_crash_data.u.fault.bfar = SCB->BFAR;
  sl_wisun_crash_data.u.fault.afsr = SCB->AFSR;

  NVIC_SystemReset();
}

/******************************************************************************
 * This function is called when HardFault exception is triggered.
 *
 * On exception entry, Cortex-M saves context state onto either PSP or MSP,
 * depending on the mode of the processor. This function calls the actual
 * HardFault handler, supplying a pointer to the stack frame as the argument.
 *****************************************************************************/
void HardFault_Handler(void)
{
  __asm volatile(              \
    "tst lr, #4 \n"            \
    "ite eq \n"                \
    "mrseq r0, msp \n"         \
    "mrsne r0, psp \n"         \
    "b hard_fault_handler\n");
}

void sl_wisun_crash_handler_clear()
{
  sl_wisun_crash_data.type = SL_WISUN_CRASH_TYPE_NONE;
}

void sl_wisun_crash_handler_init()
{
  uint32_t reset_cause;

#if defined(_RMU_RSTCAUSE_MASK)
#define RSTCAUSE_SYSREQ RMU_RSTCAUSE_SYSREQRST
#elif defined(_EMU_RSTCAUSE_MASK)
#define RSTCAUSE_SYSREQ EMU_RSTCAUSE_SYSREQ
#else
#warning "Unknown Reset Cause register"
#define RSTCAUSE_SYSREQ 0
#endif

  reset_cause = RMU_ResetCauseGet();

  // Clear Reset Cause since it's cumulative and only initialized
  // on Power-on Reset.
  RMU_ResetCauseClear();

#if defined(SL_CATALOG_MICRIUMOS_KERNEL_PRESENT) \
    && (OS_CFG_TASK_STK_REDZONE_EN == DEF_ENABLED) \
    && (OS_CFG_APP_HOOKS_EN == DEF_ENABLED)
  // Callback function for Micrium red zone
  OS_AppRedzoneHitHookPtr = sl_wisun_crash_handler_redzone;
#endif

  if (reset_cause & RSTCAUSE_SYSREQ) {
    // Device reset was software triggered
    if (sl_wisun_crash_handler_read()) {
      // Crash data is available
      return;
    }
  }

  // Device reset was't due to crash, clear crash data
  sl_wisun_crash_handler_clear();
}

const sl_wisun_crash_t *sl_wisun_crash_handler_read()
{
  if ((sl_wisun_crash_data.type > SL_WISUN_CRASH_TYPE_NONE) &&
      (sl_wisun_crash_data.type < SL_WISUN_CRASH_TYPE_MAX)) {
    // Crash data looks sane
    return &sl_wisun_crash_data;
  }

  return NULL;
}

int sl_wisun_check_previous_crash(void) {
  int ret = 0;
  const sl_wisun_crash_t *crash;
  crash = sl_wisun_crash_handler_read();
  if (crash) {
    switch (crash->type) {
      case SL_WISUN_CRASH_TYPE_ASSERT:
        snprintf(crash_info_string, SL_CRASH_STR_MAX_LEN,
          "[ASSERT in %s on line %u]\r\n",
          crash->u.assert.file,
          crash->u.assert.line);
        break;
      case SL_WISUN_CRASH_TYPE_RAIL_ASSERT:
        snprintf(crash_info_string, SL_CRASH_STR_MAX_LEN,
          "[RAIL ASSERT %lu]\r\n",
          crash->u.rail_assert.error_code);
        break;
      case SL_WISUN_CRASH_TYPE_STACK_OVERFLOW:
        snprintf(crash_info_string, SL_CRASH_STR_MAX_LEN,
          "[STACK OVERFLOW failure in task \"%s\"]\r\n",
          crash->u.stack_overflow.task);
        break;
      case SL_WISUN_CRASH_TYPE_STACK_PROTECTOR:
        snprintf(crash_info_string, SL_CRASH_STR_MAX_LEN,
          "[STACK PROTECTOR failure in 0x%08lx]\r\n",
          crash->u.stack_protector.lr);
        break;
      case SL_WISUN_CRASH_TYPE_FAULT:
        // In case of SL_WISUN_CRASH_TYPE_FAULT, look for LR in the .map file as the source of the fault
        #define CRASH_TYPE_FAULT_FORMAT \
          "[FAULT CFSR: 0x%08lx\r\n"                                        \
          "R0:   0x%08lx, R1:    0x%08lx, R2:   0x%08lx, R3:   0x%08lx\r\n" \
          "R12:  0x%08lx, LR:    0x%08lx, RET:  0x%08lx, XPSR: 0x%08lx\r\n" \
          "HFSR: 0x%08lx, MMFAR: 0x%08lx, BFAR: 0x%08lx, AFSR: 0x%08lx]\r\n"
        snprintf(crash_info_string, SL_CRASH_STR_MAX_LEN,
           CRASH_TYPE_FAULT_FORMAT,
           crash->u.fault.cfsr,
           crash->u.fault.r0, crash->u.fault.r1,
           crash->u.fault.r2, crash->u.fault.r3,
           crash->u.fault.r12, crash->u.fault.lr,
           crash->u.fault.return_address, crash->u.fault.xpsr,
           crash->u.fault.hfsr, crash->u.fault.mmfar,
           crash->u.fault.bfar, crash->u.fault.afsr
        );
        break;
      case SL_WISUN_CRASH_TYPE_CRUN_ERROR:
        snprintf(crash_info_string, SL_CRASH_STR_MAX_LEN,
          "[C-RUN error 0x%08lx]\r\n",
          crash->u.crun_error.error_code);
        break;
      case SL_WISUN_CRASH_TYPE_EXIT:
        snprintf(crash_info_string, SL_CRASH_STR_MAX_LEN,
          "[EXIT status %d]\r\n",
          crash->u.exit.status);
        break;
      default:
        break;
    }
    ret = crash->type;
    sl_wisun_crash_handler_clear();
  }
  return ret;
}
