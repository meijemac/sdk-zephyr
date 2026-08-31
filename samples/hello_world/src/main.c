/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <hal/nrf_vpr_csr.h>

int main(void)
{
        csr_write(VPRCSR_NORDIC_VPRNORDICSLEEPCTRL,
                  VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_WAIT);

	while(1) {
	    printf("What do we say to the god of death?\n");
	    printf("Not today zephyr: %s\n", CONFIG_BOARD_TARGET);
	    k_msleep(100);
        }	    

	return 0;
}
