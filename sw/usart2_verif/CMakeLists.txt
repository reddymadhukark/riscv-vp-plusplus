cmake_minimum_required(VERSION 3.16)
project(usart2_verif LANGUAGES C ASM)

# ── Toolchain ──────────────────────────────────────────────────────────────
# Expects riscv32-unknown-elf-gcc on PATH (or symlinks in ~/.local/bin)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

if(NOT DEFINED RISCV_PREFIX)
    set(RISCV_PREFIX riscv32-unknown-elf-)
endif()
set(CMAKE_C_COMPILER   ${RISCV_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${RISCV_PREFIX}gcc)

# ── Common RISC-V compile flags ────────────────────────────────────────────
set(ARCH_FLAGS "-march=rv32imac_zicsr -mabi=ilp32")
set(CMAKE_C_FLAGS   "${ARCH_FLAGS} -O1 -Wall -nostdlib")
set(CMAKE_ASM_FLAGS "${ARCH_FLAGS}")
set(LDFLAGS "${ARCH_FLAGS} -nostartfiles -static -no-pie -Wl,--no-relax")

set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/bench/common/link.ld")
set(STARTUP       "${CMAKE_CURRENT_SOURCE_DIR}/bench/common/bootstrap.S")

# ── Helper macro: add one test firmware target ─────────────────────────────
# Usage: add_usart_test(TARGET_NAME TEST_C_FILE)
macro(add_usart_test TARGET C_SRC)
    add_executable(${TARGET} ${STARTUP} ${C_SRC})
    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/bench/common
        ${CMAKE_CURRENT_SOURCE_DIR}/bench/env
        ${CMAKE_CURRENT_SOURCE_DIR}/bench/driver
        ${CMAKE_CURRENT_SOURCE_DIR}/bench/monitor
        ${CMAKE_CURRENT_SOURCE_DIR}/bench/scoreboard
    )
    set_target_properties(${TARGET} PROPERTIES
        SUFFIX ".elf"
        LINK_FLAGS "${LDFLAGS} -T ${LINKER_SCRIPT}"
    )
endmacro()

# ── Test firmware targets ──────────────────────────────────────────────────
add_usart_test(test_reset       tests/reset/test_reset.c)
add_usart_test(test_rw          tests/rw/test_rw.c)
add_usart_test(test_functional  tests/functional/test_functional.c)
add_usart_test(test_clock       tests/clock/test_clock.c)
add_usart_test(test_loopback    tests/loopback/test_loopback.c)
add_usart_test(test_advanced    tests/advanced/test_advanced.c)
add_usart_test(test_performance tests/performance/test_performance.c)
add_usart_test(test_stress      tests/stress/test_stress.c)
add_usart_test(test_negative    tests/negative/test_negative.c)
add_usart_test(test_datatype    tests/datatype/test_datatype.c)
add_usart_test(test_memory_leak tests/memory_leak/test_memory_leak.c)

# ── Install ELFs to build/bin ──────────────────────────────────────────────
install(TARGETS
    test_reset test_rw test_functional test_clock test_loopback
    test_advanced test_performance test_stress test_negative
    test_datatype test_memory_leak
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/build/bin
)
