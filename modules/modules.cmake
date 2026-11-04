# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

# Zephyr picks the toolchain before it reads any board.cmake, and for native
# boards it picks the host one unless told otherwise. This file is read earlier,
# so native_sim/native/aarch64 sets its cross-compiler here.
set(aarch64_board native_sim/native/aarch64)
if("${BOARD}" STREQUAL "${aarch64_board}" OR "${CACHED_BOARD}" STREQUAL "${aarch64_board}")
  set(NATIVE_TARGET_HOST aarch64)
  set(ZEPHYR_TOOLCHAIN_VARIANT cross-compile)
  set(CROSS_COMPILE /usr/bin/aarch64-linux-gnu-)

  # Applications name their board files after native_sim/native/64, which this
  # board behaves like, and Zephyr only looks for this board's own name
  foreach(name native_sim_native_64 native_sim_64)
    set(base ${APPLICATION_SOURCE_DIR}/boards/${name})
    if(EXISTS ${base}.conf)
      list(APPEND EXTRA_CONF_FILE ${base}.conf)
    endif()
    if(EXISTS ${base}.overlay)
      list(APPEND EXTRA_DTC_OVERLAY_FILE ${base}.overlay)
    endif()
  endforeach()

  # A compiler built without libc, like Fedora's, builds this board but its
  # unwinder traps at random when native_sim threads exit
  execute_process(COMMAND ${CROSS_COMPILE}gcc -print-file-name=libgcc_eh.a
                  OUTPUT_VARIABLE libgcc_eh OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT IS_ABSOLUTE "${libgcc_eh}")
    message(FATAL_ERROR "${CROSS_COMPILE}gcc has no libgcc_eh.a, it was built without "
                        "libc. Build this board through the cross image: "
                        "west config alias.build android-build")
  endif()
endif()
