# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BOARD_FLIP_SIM_NRF54L15_CPUFLPR)
  set(GEN_FLIP_HEX_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/gen_flip_hex.py)
  set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
    COMMAND ${PYTHON_EXECUTABLE} ${GEN_FLIP_HEX_SCRIPT}
            ${CMAKE_OBJDUMP} ${KERNEL_ELF_NAME} zephyr_flip.hex
  )
  set_property(GLOBAL APPEND PROPERTY extra_post_build_byproducts
    zephyr_flip.hex
  )
endif()
