/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define PD_PIN_DEFAULT_VALUE 0

#if DT_NODE_EXISTS(DT_ALIAS(pdgpio1))
#define PD_GPIO_PIN1_EXISTS 1
#else
#define PD_GPIO_PIN1_EXISTS 0
#endif

#if DT_NODE_EXISTS(DT_ALIAS(pdgpio2))
#define PD_GPIO_PIN2_EXISTS 1
#else
#define PD_PGIO_PIN2_EXISTS 0
#endif

static uint32_t pd_gpio_pin1 = PD_PIN_DEFAULT_VALUE;
static uint32_t pd_gpio_pin2 = PD_PIN_DEFAULT_VALUE;

//static const struct gpio_dt_spec select1 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(pdgpio1), gpios, {0});
//static const struct gpio_dt_spec select2 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(pdgpio2), gpios, {0});

#define INITIALIZE_IF_EXISTS(pin)                                              \
    if (DT_NODE_EXISTS(DT_ALIAS(pdgpio##pin))) {                               \
        pd_gpio_pin##pin = CONFIG_NORDIC_PD_GPIO_MONITOR_PD_SELECT##pin;       \
    }


#if defined(CONFIG_SOC_SERIES_NRF55)
#define NORDIC_PD_SREGS_BASE  0x5010F000UL
#define NORDIC_PD_PDSELECT_OFF 0x780UL
#elif defined(CONFIG_SOC_NRF54L15)
#define NORDIC_PD_SREGS_BASE  0x5010F000UL
#define NORDIC_PD_PDSELECT_OFF 0x780UL
#else
# error "NORDIC_PD_GPIO_MONITOR: define SREGS base/offset for this SoC"
#endif

#define NORDIC_PD_PDSELECT_ADDR (NORDIC_PD_SREGS_BASE + NORDIC_PD_PDSELECT_OFF)
#define PDSELECT_SEL0_POS  0U
#define PDSELECT_SEL1_POS  4U
#define PDSELECT_SEL_MAX   0xBUL   /* if same encoding as FM20; else per-SoC */

static inline uint32_t nordic_pdselect_encode(uint32_t sel1, uint32_t sel2)
{
	__ASSERT_NO_MSG(sel0 <= PDSELECT_SEL_MAX);
	__ASSERT_NO_MSG(sel1 <= PDSELECT_SEL_MAX);
	return FIELD_PREP(0xFUL << PDSELECT_SEL0_POS, sel1) |
	       FIELD_PREP(0xFUL << PDSELECT_SEL1_POS, sel2);
}
static inline void nrf_set_pdselect(uint32_t sel1, uint32_t sel2)
{
	sys_write32(nordic_pdselect_encode(sel1, sel2),
		    NORDIC_PD_PDSELECT_ADDR);
}

static int init_debug_pins(void)
{
    /* setup the gpio pins as output pins - if available */
    INITIALIZE_IF_EXISTS(1);
    INITIALIZE_IF_EXISTS(2);

    nrf_set_pdselect(pd_gpio_pin1, pd_gpio_pin2);

    return 0;
}

/*
 * Function that can be called during run-time to change the connected PD.
 * If GPIO pin is not available - the default value will be written to the
 * PD_SELECT register
 */
void nordic_set_pd_select(uint8_t pd_pin1, uint8_t pd_pin2)
{
#if !PD_GPIO_PIN1_EXISTS
   pd_pin1 = PD_PIN_DEFAULT_VALUE;
#endif
#if !PD_GPIO_PIN2_EXISTS
   pd_pin2 = PD_PIN_DEFAULT_VALUE;
#endif
   nrf_set_pdselect(pd_pin1, pd_pin2);
}

// Automatically runs early during system boot
SYS_INIT(init_debug_pins, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

