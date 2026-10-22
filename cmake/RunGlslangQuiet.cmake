# Runs glslang (GLSLANG; ARGS: its arguments separated by "|") and prints its output without the "names containing
# consecutive underscores are reserved" warnings, which NVIDIA's FXAA3_11.h (third party) triggers on
# every macro and which glslang's --suppress-warnings does not silence. Every other line, and the
# exit code, is passed through. Used by cmake/Shaders.cmake for fxaa.frag only.
string(REPLACE "|" ";" args "${ARGS}")
execute_process(COMMAND "${GLSLANG}" ${args} OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
string(REPLACE "\r" "" out "${out}${err}")
string(REPLACE "\n" ";" lines "${out}")
foreach(line IN LISTS lines)
    if(line MATCHES "consecutive underscores are reserved" OR line STREQUAL "")
        continue()
    endif()
    message("${line}")
endforeach()
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "glslang failed (${rc})")
endif()
