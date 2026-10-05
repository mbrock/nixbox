project('d3d12caps', 'cpp', default_options: ['cpp_std=c++20'])

# Each entry point becomes shader_<entry>.h, a byte array of signed DXIL.
dxc = find_program('dxc', native: true)
shaders = []
foreach shader : [
  ['root_address', 'cs_6_0'], ['bindless', 'cs_6_0'], ['waves', 'cs_6_0'],
  ['packed_dot', 'cs_6_4'], ['wide', 'cs_6_0'], ['indirect_mark', 'cs_6_0'],
  ['model_6_5', 'cs_6_5'], ['heap_indexing', 'cs_6_6'],
  ['fma_chain', 'cs_6_0'], ['copy_stream', 'cs_6_0'],
  ['layered_vertex', 'vs_6_0'], ['layered_pixel', 'ps_6_0'],
  ['fill_vertex', 'vs_6_0'], ['fill_pixel', 'ps_6_0'],
  ['tube_vertex', 'vs_6_0'], ['tube_pixel', 'ps_6_0'],
]
  shaders += custom_target('shader_' + shader[0],
    input: 'probe.hlsl',
    output: 'shader_' + shader[0] + '.h',
    command: [dxc, '-O3', '-T', shader[1], '-E', shader[0],
              '-Fh', '@OUTPUT@', '-Vn', 'shader_' + shader[0], '@INPUT@'])
endforeach

executable('d3d12caps', 'main.cpp', 'formats.cpp', 'gpu.cpp', 'display.cpp', shaders,
  dependencies: dependency('sdl3'),
  cpp_args: ['-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN'],
  link_args: ['-ld3d12', '-ldxgi'],
  win_subsystem: 'windows',
  install: true)
