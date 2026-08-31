/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLIP simulator console: arch_printk_char_out jumps to a trap at startpc+0xA0
 * (padding before the IRQ vector table). The simulator prints the character
 * from register a0 (x10) when PC hits that address (see flip/src/simulator.cpp).
 */

#include <stdint.h>
#include <zephyr/init.h>
#include <zephyr/sys/libc-hooks.h>
#include <zephyr/sys/printk-hooks.h>

#define FLIP_SIM_PRINTK_TRAP (0x30008000U + 0xA0U)

__attribute__((section(".flip_sim_hook"), used, naked))
void flip_sim_printk_trap(void)
{
	__asm__ volatile(
		".option push\n"
		".option norvc\n"
		"nop\n"
		"nop\n"
		"ret\n"
		".option pop\n");
}

int arch_printk_char_out(int c)
{
	__asm__ volatile(
		".option push\n"
		".option norvc\n"
		"mv a0, %0\n"
		"j %1\n"
		".option pop\n"
		:
		: "r"((unsigned char)c), "i"(FLIP_SIM_PRINTK_TRAP)
		: "a0", "memory");

	return c;
}

static int flip_sim_console_init(void)
{
	__stdout_hook_install(arch_printk_char_out);
	__printk_hook_install(arch_printk_char_out);

	return 0;
}

SYS_INIT(flip_sim_console_init, PRE_KERNEL_1, 0);
