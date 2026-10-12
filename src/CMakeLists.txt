# ac04_core: code with no window/GPU dependency: guest memory model (src/common), paths, log,
#            settings, command line, disc image, ELF loader, pacing and layout logic.
#            Linked by the executable and the unit tests.
# ac04_platform: SDL3 window, input, audio and the Vulkan renderer.
# ac04_game: translated game code (src/game), the SDK layer (src/sce) and the IOP stand-ins. An
#            OBJECT library so the static AC04_GUEST_FUNCTION registrations are always linked.
# ac04port: the executable.

add_library(ac04_core STATIC
    common/debug_hooks.cpp
    common/guest_call.cpp
    common/guest_memory.cpp
    common/guest_stack.cpp
    platform/command_line.cpp
    platform/elf_loader.cpp
    platform/frame_pacer.cpp
    platform/iso9660.cpp
    platform/iso_image.cpp
    platform/log.cpp
    platform/paths.cpp
    platform/settings.cpp
    platform/sha1.cpp
    platform/render/output_layout.cpp
    platform/render/test_pattern.cpp
    platform/win32/crash_win32.cpp
    platform/win32/file_reader_win32.cpp
    platform/win32/paths_win32.cpp
    platform/win32/system_info_win32.cpp
    platform/win32/timer_win32.cpp)
target_link_libraries(ac04_core PUBLIC ac04_options ac04_json PRIVATE shell32 ole32 winmm)

add_library(ac04_platform STATIC
    platform/audio.cpp
    platform/debug/overlay.cpp
    platform/input.cpp
    platform/window.cpp
    platform/render/renderer.cpp
    platform/render/vk_context.cpp
    platform/render/vma_impl.cpp)
target_link_libraries(ac04_platform PUBLIC ac04_core SDL3::SDL3-static ac04_volk ac04_vma ac04_imgui)
ac04_add_shaders(ac04_platform
    "${CMAKE_SOURCE_DIR}/shaders/output.vert"
    "${CMAKE_SOURCE_DIR}/shaders/output.frag")

add_library(ac04_game OBJECT
    game/boot.cpp
    game/cd_file.cpp
    game/display.cpp
    game/hdd.cpp
    game/heap.cpp
    game/math.cpp
    game/movie_init.cpp
    game/options.cpp
    game/pad.cpp
    game/save_init.cpp
    game/scene.cpp
    game/sound_rpc.cpp
    game/static_init.cpp
    game/usbjoy.cpp
    game/vag_stream.cpp
    platform/iop/iop_services.cpp
    sce/hw_registers.cpp
    sce/kernel.cpp
    sce/libc.cpp
    sce/libcdvd.cpp
    sce/libgraph.cpp
    sce/libpad.cpp
    sce/runtime.cpp
    sce/sce_host.cpp)
target_link_libraries(ac04_game PUBLIC ac04_core)

add_executable(ac04port main.cpp)
target_link_libraries(ac04port PRIVATE ac04_game ac04_platform)
# Release builds are a GUI program (no console window); development builds keep the console so
# the log is also visible there.
target_link_options(ac04port PRIVATE $<IF:$<CONFIG:Release>,/SUBSYSTEM:WINDOWS,/SUBSYSTEM:CONSOLE>)
set_target_properties(ac04port PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ac04port)
