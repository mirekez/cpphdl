if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME analog.pdk.ihp_prepare
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_prepare.py")
  set_tests_properties(analog.pdk.ihp_prepare PROPERTIES LABELS "analog;pdk" TIMEOUT 60)
endif()
add_subdirectory(1G_repeater_IHPSG13G2)
