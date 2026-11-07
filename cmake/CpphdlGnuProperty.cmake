include_guard(GLOBAL)

find_program(CPPHDL_OBJCOPY
  NAMES llvm-objcopy llvm-objcopy-21 objcopy
  HINTS "${CMAKE_SOURCE_DIR}/.conda/bin"
  NO_DEFAULT_PATH)
if(NOT CPPHDL_OBJCOPY)
  find_program(CPPHDL_OBJCOPY NAMES llvm-objcopy llvm-objcopy-21 objcopy)
endif()

function(cpphdl_strip_gnu_property_file file)
  if(CPPHDL_OBJCOPY)
    execute_process(
      COMMAND "${CPPHDL_OBJCOPY}" --remove-section=.note.gnu.property "${file}"
      RESULT_VARIABLE strip_result
      OUTPUT_VARIABLE strip_output
      ERROR_VARIABLE strip_error)
    if(NOT strip_result EQUAL 0)
      string(STRIP "${strip_output}\n${strip_error}" strip_message)
      message(STATUS "Could not remove .note.gnu.property from ${file}: ${strip_message}")
    endif()
  endif()
endfunction()

function(cpphdl_strip_gnu_property_after_build target)
  if(CPPHDL_OBJCOPY)
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND "${CPPHDL_OBJCOPY}" --remove-section=.note.gnu.property "$<TARGET_FILE:${target}>"
      VERBATIM)
  endif()
endfunction()
