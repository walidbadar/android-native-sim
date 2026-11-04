# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

# This file is also read for the other native_sim variants
if(CONFIG_BOARD_NATIVE_SIM_NATIVE_AARCH64)
  set(SUPPORTED_EMU_PLATFORMS custom)
  add_custom_target(run_custom
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/android_run.py
            ${APPLICATION_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.exe
    DEPENDS native_runner_executable
    USES_TERMINAL
  )
endif()
