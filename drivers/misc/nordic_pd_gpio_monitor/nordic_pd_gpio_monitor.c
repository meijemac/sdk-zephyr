/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>

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

static uint8_t pd_gpio_pin1 = PD_PIN_DEFAULT_VALUE;
static uint8_t pd_gpio_pin2 = PD_PIN_DEFAULT_VALUE;

static const struct gpio_dt_spec select1 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(pdgpio1), gpios, {0});
static const struct gpio_dt_spec select2 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(pdgpio2), gpios, {0});

#define INITIALIZE_IF_EXISTS(pin)                                              \
    if (DT_NODE_EXISTS(DT_ALIAS(pdgpio##pin))) {                               \
        pd_gpio_pin##pin = CONFIG_NORDIC_PD_GPIO_MONITOR_PD_SELECT##pin;       \
	printf("1b\n");                                                        \
        if (gpio_is_ready_dt(&select##pin)) {                                  \
            gpio_pin_configure_dt(&select##pin, GPIO_OUTPUT_INACTIVE);         \
	    printf("2a\n");                                                    \
        }                                                                      \
    }

/* Take SREGS30 Instance - which contains the PD_SELECT registers */
#define NRF_APPLICATION_SREGS_NS	((NRF_SREGS_Type*) 0x4010F000UL)
#define NRF_APPLICATION_SREGS_S		((NRF_SREGS_Type*) 0x5010F000UL)

#define NRF_SREGS NRF_APPLICATION_SREGS_S

static inline void nrf_set_pdselect(NRF_SREGS_Type * p_reg, uint32_t pd_pin0, uint32_t pd_pin1)
{
    p_reg->PDSELECT = (uint32_t) ((pd_pin0 & SREGS_PDSELECT_PIN0_Msk) | \
                                  ((pd_pin1 << SREGS_PDSELECT_PIN1_Pos) & SREGS_PDSELECT_PIN1_Msk));
}

static int init_debug_pins(void)
{
    /* setup the gpio pins as output pins - if available */
    INITIALIZE_IF_EXISTS(1);
    INITIALIZE_IF_EXISTS(2);
    printf("Snippet: Init_debug_pins: 0x%x & 0x%x\n", pd_gpio_pin1, pd_gpio_pin2);

    nrf_set_pdselect(NRF_SREGS, pd_gpio_pin1, pd_gpio_pin2);

    printf("3a");
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
   nrf_set_pdselect(NRF_SREGS, pd_pin1, pd_pin2);
}

// Automatically runs early during system boot
SYS_INIT(init_debug_pins, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

