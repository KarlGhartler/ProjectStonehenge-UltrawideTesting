# GLSL -> SPIR-V at build time with the fetched glslang compiler (no Vulkan SDK needed).
# Each shader becomes a generated header "<name>_spv.h" holding `const uint32_t <name>_spv[]`.
# The FSR 1 shader headers (AC04_FSR1_INCLUDE_DIR, cmake/Dependencies.cmake) are on the include path.

function(ac04_add_shaders target)
    set(out_dir "${CMAKE_BINARY_DIR}/generated/shaders")
    file(MAKE_DIRECTORY "${out_dir}")
    set(headers)
    foreach(src IN LISTS ARGN)
        get_filename_component(abs "${src}" ABSOLUTE)
        get_filename_component(name "${src}" NAME)
        string(REPLACE "." "_" var "${name}")
        set(header "${out_dir}/${var}_spv.h")
        add_custom_command(
            OUTPUT "${header}"
            COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.2 --vn ${var}_spv
                    "-I${AC04_FSR1_INCLUDE_DIR}" -o "${header}" "${abs}"
            DEPENDS "${abs}" glslang-standalone "${AC04_FSR1_INCLUDE_DIR}/ffx_a.h" "${AC04_FSR1_INCLUDE_DIR}/ffx_fsr1.h"
            COMMENT "Compiling shader ${name}"
            VERBATIM)
        list(APPEND headers "${header}")
    endforeach()
    add_custom_target(${target}_shaders DEPENDS ${headers})
    add_dependencies(${target} ${target}_shaders)
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()
