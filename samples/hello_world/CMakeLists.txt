# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

target_sources(app PRIVATE src/main.c)

if(CONFIG_BOARD_FLIP_SIM_NRF54L15_CPUFLPR)
  target_sources(app PRIVATE src/flip_sim_printk.c)
  zephyr_linker_sources(ROM_START SORT_KEY 0x0vectors boards/flip_sim_nrf54l15_cpuflpr.ld)
endif()

