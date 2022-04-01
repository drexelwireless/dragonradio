add_executable(tests EXCLUDE_FROM_ALL test.cc)
target_link_libraries(tests PUBLIC
  Catch2::Catch2
  rapidcheck
  DragonRadio
  INTERFACE rapidcheck_catch
)
target_include_directories(tests PUBLIC
  ../extern/rapidcheck/extras/catch/include
)

include(CTest)
include(Catch)

catch_discover_tests(tests)
