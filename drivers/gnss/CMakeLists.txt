# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_GNSS_NATIVE_SIM)
  zephyr_library()
  zephyr_library_sources(gnss_native_sim.c)
  # Host side, built into the native simulator runner
  target_sources(native_simulator INTERFACE gnss_native_sim_bottom.c)
endif()
