project('game', 'cpp', default_options: ['cpp_std=c++20'])

# Each shader entry point becomes <name>.h, a byte array called <name>.
dxc = find_program('dxc', native: true)
shaders = []
foreach shader : [
  ['cubeVertexShader', 'vertexMain', 'vs_6_0'],
  ['cubePixelShader', 'pixelMain', 'ps_6_0'],
]
  shaders += custom_target(shader[0],
    input: 'Cube.hlsl',
    output: shader[0] + '.h',
    command: [dxc, '-O3', '-T', shader[2], '-E', shader[1],
              '-Fh', '@OUTPUT@', '-Vn', shader[0], '@INPUT@'])
endforeach

# SDK import libraries are linked by name; pkgsXbox libraries in buildInputs
# are found with dependency() through pkg-config, or cxx.find_library().
executable('game', 'main.cpp', shaders,
  win_subsystem: 'windows',
  link_args: ['-ld3d12', '-ldxgi'],
  install: true)
