# Production-path test programs (ported from codex/ofdma-simulation tests/fullstack).
# Each is a standalone program that prints PASSED or FAILED and exits non-zero on
# failure. Build with `make fullstack-tests`; run with `ctest -L fullstack`.
set(FULLSTACK_TESTS
  test_arq_syn
  test_logger_chunks
  test_native_fec
  test_native_ofdm
  test_native_ofdma
  test_native_perf
  test_native_qam
  test_net_queue
  test_packet_align
  test_sim_device
  test_timer_wake
)
add_custom_target(fullstack-tests)
foreach(t ${FULLSTACK_TESTS})
  add_executable(${t} EXCLUDE_FROM_ALL ${t}.cc)
  target_link_libraries(${t} PRIVATE DragonRadio pybind11::embed)
  target_compile_options(${t} PRIVATE -fno-finite-math-only)
  add_dependencies(fullstack-tests ${t})
  add_test(NAME fullstack.${t} COMMAND ${t})
  set_tests_properties(fullstack.${t} PROPERTIES LABELS fullstack TIMEOUT 600)
endforeach()
