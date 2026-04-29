add_executable(usart2test-vp main.cpp)
target_compile_definitions(usart2test-vp PUBLIC TARGET_RV32)
target_link_libraries(usart2test-vp rv32 platform-common gdb-mc ${Boost_LIBRARIES} systemc pthread)
INSTALL(TARGETS usart2test-vp RUNTIME DESTINATION bin)
