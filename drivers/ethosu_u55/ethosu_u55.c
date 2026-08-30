/**
 * @file ethosu_u55.c
 * @brief Ethos-U55 driver ownership, IRQ dispatch, and cache maintenance.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include <stddef.h>
#include <stdint.h>

#include "board/m55m1_ethosu_u55.h"
#include "drivers/ethosu_u55/ethosu_u55.h"
#include "NuMicro.h"
#include "ethosu_driver.h"
#include "pmu_ethosu.h"

static struct ethosu_driver g_ethosu_driver;
static bool g_ethosu_ready;
static volatile uint32_t g_npu_irq_count;
static volatile uint32_t g_inference_begin_count;
static volatile uint32_t g_inference_end_count;
static volatile uint64_t g_cycle_count;
static volatile uint32_t g_npu_active_cycles;
static volatile uint32_t g_npu_idle_cycles;
static volatile uint32_t g_mac_active_cycles;

static void ethosu_u55_pmu_reset(void) {
    ETHOSU_PMU_CYCCNT_Reset(&g_ethosu_driver);
    ETHOSU_PMU_EVCNTR_ALL_Reset(&g_ethosu_driver);
}

bool ethosu_u55_init(void) {
    if (g_ethosu_ready) {
        return true;
    }
    if (!m55m1_ethosu_u55_board_init()) {
        return false;
    }
    if (ethosu_init(&g_ethosu_driver, (void *)NPU_BASE, NULL, 0U, 1, 1) != 0) {
        return false;
    }
    ETHOSU_PMU_Set_EVTYPER(&g_ethosu_driver, 0U, ETHOSU_PMU_NPU_ACTIVE);
    ETHOSU_PMU_Set_EVTYPER(&g_ethosu_driver, 1U, ETHOSU_PMU_NPU_IDLE);
    ETHOSU_PMU_Set_EVTYPER(&g_ethosu_driver, 2U, ETHOSU_PMU_MAC_ACTIVE);
    ETHOSU_PMU_Enable(&g_ethosu_driver);
    ETHOSU_PMU_CNTR_Enable(&g_ethosu_driver,
                            ETHOSU_PMU_CCNT_Msk |
                            ETHOSU_PMU_CNT1_Msk |
                            ETHOSU_PMU_CNT2_Msk |
                            ETHOSU_PMU_CNT3_Msk);
    ethosu_u55_pmu_reset();
    m55m1_ethosu_u55_board_enable_irq();
    g_ethosu_ready = true;
    return true;
}

void ethosu_u55_get_diagnostics(ethosu_u55_diagnostics_t *diagnostics) {
    if (diagnostics == NULL) {
        return;
    }
    *diagnostics = (ethosu_u55_diagnostics_t){
        .npu_irq_count = g_npu_irq_count,
        .inference_begin_count = g_inference_begin_count,
        .inference_end_count = g_inference_end_count,
        .cycle_count = g_cycle_count,
        .npu_active_cycles = g_npu_active_cycles,
        .npu_idle_cycles = g_npu_idle_cycles,
        .mac_active_cycles = g_mac_active_cycles,
    };
}

void ethosu_inference_begin(struct ethosu_driver *driver, void *user_arg) {
    (void)driver;
    (void)user_arg;
    ++g_inference_begin_count;
    ethosu_u55_pmu_reset();
}

void ethosu_inference_end(struct ethosu_driver *driver, void *user_arg) {
    (void)driver;
    (void)user_arg;
    g_cycle_count += ETHOSU_PMU_Get_CCNTR(&g_ethosu_driver);
    g_npu_active_cycles += ETHOSU_PMU_Get_EVCNTR(&g_ethosu_driver, 0U);
    g_npu_idle_cycles += ETHOSU_PMU_Get_EVCNTR(&g_ethosu_driver, 1U);
    g_mac_active_cycles += ETHOSU_PMU_Get_EVCNTR(&g_ethosu_driver, 2U);
    ++g_inference_end_count;
}

uint32_t ethosu_u55_cycles_now(void) {
    return m55m1_ethosu_u55_board_cycles_now();
}

uint32_t ethosu_u55_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle) {
    return m55m1_ethosu_u55_board_cycles_elapsed(start_cycle, end_cycle);
}

void ethosu_u55_fault_delay_ms(uint32_t milliseconds) {
    const uint32_t start_cycle = ethosu_u55_cycles_now();
    const uint32_t delay_cycles = (SystemCoreClock / 1000U) * milliseconds;

    while (m55m1_ethosu_u55_board_cycles_elapsed(
               start_cycle, ethosu_u55_cycles_now()) < delay_cycles) {
        __NOP();
    }
}

bool ethosu_u55_deadline_exceeded(uint32_t start_cycle, uint32_t milliseconds) {
    return m55m1_ethosu_u55_board_cycles_elapsed(
               start_cycle, ethosu_u55_cycles_now()) >=
           ((SystemCoreClock / 1000U) * milliseconds);
}

NVT_ITCM void NPU_IRQHandler(void) {
    ++g_npu_irq_count;
    ethosu_irq_handler(&g_ethosu_driver);
    __DSB();
    __ISB();
}

void ethosu_flush_dcache(uint32_t *address, size_t bytes) {
    uint32_t aligned_address;
    uint32_t aligned_bytes;

    if ((address == NULL) || (bytes == 0U)) {
        SCB_CleanDCache();
        return;
    }
    aligned_address = (uint32_t)address & ~(DCACHE_LINE_SIZE - 1UL);
    aligned_bytes = DCACHE_ALIGN_LINE_SIZE((uint32_t)bytes + ((uint32_t)address - aligned_address));
    SCB_CleanDCache_by_Addr((void *)aligned_address, (int32_t)aligned_bytes);
}

void ethosu_invalidate_dcache(uint32_t *address, size_t bytes) {
    uint32_t aligned_address;
    uint32_t aligned_bytes;

    if ((address == NULL) || (bytes == 0U)) {
        SCB_InvalidateDCache();
        return;
    }
    aligned_address = (uint32_t)address & ~(DCACHE_LINE_SIZE - 1UL);
    aligned_bytes = DCACHE_ALIGN_LINE_SIZE((uint32_t)bytes + ((uint32_t)address - aligned_address));
    SCB_InvalidateDCache_by_Addr((void *)aligned_address, (int32_t)aligned_bytes);
}
