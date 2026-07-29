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

static struct ethosu_driver g_ethosu_driver;
static bool g_ethosu_ready;

bool ethosu_u55_init(void)
{
    if (g_ethosu_ready)
    {
        return true;
    }
    if (!m55m1_ethosu_u55_board_init())
    {
        return false;
    }
    if (ethosu_init(&g_ethosu_driver, (void *)NPU_BASE, NULL, 0U, 1, 1) != 0)
    {
        return false;
    }
    m55m1_ethosu_u55_board_enable_irq();
    g_ethosu_ready = true;
    return true;
}

uint32_t ethosu_u55_cycles_now(void)
{
    return m55m1_ethosu_u55_board_cycles_now();
}

uint32_t ethosu_u55_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle)
{
    return m55m1_ethosu_u55_board_cycles_elapsed(start_cycle, end_cycle);
}

void ethosu_u55_fault_delay_ms(uint32_t milliseconds)
{
    const uint32_t start_cycle = ethosu_u55_cycles_now();
    const uint32_t delay_cycles = (SystemCoreClock / 1000U) * milliseconds;

    while (m55m1_ethosu_u55_board_cycles_elapsed(
               start_cycle, ethosu_u55_cycles_now()) < delay_cycles)
    {
        __NOP();
    }
}

bool ethosu_u55_deadline_exceeded(uint32_t start_cycle, uint32_t milliseconds)
{
    return m55m1_ethosu_u55_board_cycles_elapsed(
               start_cycle, ethosu_u55_cycles_now()) >=
           ((SystemCoreClock / 1000U) * milliseconds);
}

NVT_ITCM void NPU_IRQHandler(void)
{
    ethosu_irq_handler(&g_ethosu_driver);
    __DSB();
    __ISB();
}

void ethosu_flush_dcache(uint32_t *address, size_t bytes)
{
    uint32_t aligned_address;
    uint32_t aligned_bytes;

    if ((address == NULL) || (bytes == 0U))
    {
        SCB_CleanDCache();
        return;
    }
    aligned_address = (uint32_t)address & ~(DCACHE_LINE_SIZE - 1UL);
    aligned_bytes = DCACHE_ALIGN_LINE_SIZE((uint32_t)bytes + ((uint32_t)address - aligned_address));
    SCB_CleanDCache_by_Addr((void *)aligned_address, (int32_t)aligned_bytes);
}

void ethosu_invalidate_dcache(uint32_t *address, size_t bytes)
{
    uint32_t aligned_address;
    uint32_t aligned_bytes;

    if ((address == NULL) || (bytes == 0U))
    {
        SCB_InvalidateDCache();
        return;
    }
    aligned_address = (uint32_t)address & ~(DCACHE_LINE_SIZE - 1UL);
    aligned_bytes = DCACHE_ALIGN_LINE_SIZE((uint32_t)bytes + ((uint32_t)address - aligned_address));
    SCB_InvalidateDCache_by_Addr((void *)aligned_address, (int32_t)aligned_bytes);
}
