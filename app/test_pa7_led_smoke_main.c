/**
 * @file test_pa7_led_smoke_main.c
 * @brief Isolated active-low PA7 user-LED hardware smoke test.
 * @author Bui Minh Huy
 * @date 2026-07-20
 *
 * Copy this entire source to `app/main.c` for a direct PA7/LED verification.
 * It deliberately configures no audio, serial, camera, microphone, or storage
 * peripheral, so a missing blink isolates flash, pin mux, PA7, LED, or power.
 */

#include <stdint.h>

#include "NuMicro.h"

/** LED-on dwell time; equal off time gives a clearly visible one-hertz blink. */
#define PA7_LED_SMOKE_HALF_PERIOD_US  500000UL

/**
 * @brief Configure the board user LED pin as an active-low GPIO output.
 *
 * The board routes VDD through R23 and the yellow LED to PA7. PA7 must sink
 * current, so writing zero illuminates the LED and writing one turns it off.
 */
static void pa7_led_smoke_init(void)
{
    SYS_UnlockReg();
    CLK_EnableModuleClock(GPIOA_MODULE);
    SET_GPIO_PA7();
    SYS_LockReg();

    GPIO_SetMode(PA, BIT7, GPIO_MODE_OUTPUT);
    PA7 = 1U;
}

/**
 * @brief Blink the PA7 user LED independently of every audio peripheral.
 * @return Never returns.
 *
 * Delays are safe here because this source owns no DMA, serial, or realtime
 * stream. A visible 500 ms on / 500 ms off cycle proves basic PA7 execution.
 */
int main(void)
{
    pa7_led_smoke_init();

    for (;;)
    {
        PA7 = 0U;
        CLK_SysTickDelay(500000UL);
        PA7 = 1U;
        CLK_SysTickDelay(500000UL);
    }
}
