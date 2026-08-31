/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLIP _sim_printk trap at startpc + 0xA0 (padding before IRQ vector table).
 */

. = __rom_start_address + 0xA0;
KEEP(*(.flip_sim_hook))
