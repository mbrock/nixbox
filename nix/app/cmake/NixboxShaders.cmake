# nixbox_shader(TARGET SOURCE NAME ENTRY PROFILE)
#
# Compile one HLSL entry point with DXC into <build>/shaders/NAME.h, a byte
# array called NAME, and put it on TARGET's include path. DXC on Linux signs
# its DXIL, so the console accepts it. Edits to SOURCE rebuild the header.
find_program(DXC dxc REQUIRED)

function(nixbox_shader target source name entry profile)
  set(dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  set(header ${dir}/${name}.h)
  get_filename_component(source ${source} ABSOLUTE)
  add_custom_command(
    OUTPUT ${header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
    COMMAND ${DXC} -O3 -T ${profile} -E ${entry} -Fh ${header} -Vn ${name} ${source}
    DEPENDS ${source}
    VERBATIM)
  target_sources(${target} PRIVATE ${header})
  target_include_directories(${target} PRIVATE ${dir})
endfunction()
