project('swapchain', 'cpp', default_options: ['cpp_std=c++20'])

dxc = find_program('dxc', native: true)
shaders = []
foreach shader : [
  ['patternVertexShader', 'vertexMain', 'vs_6_0'],
  ['patternPixelShader', 'pixelMain', 'ps_6_0'],
]
  shaders += custom_target(shader[0],
    input: 'pattern.hlsl',
    output: shader[0] + '.h',
    command: [dxc, '-O3', '-T', shader[2], '-E', shader[1],
              '-Fh', '@OUTPUT@', '-Vn', shader[0], '@INPUT@'])
endforeach

executable('swapchain', 'main.cpp', shaders,
  win_subsystem: 'windows',
  link_args: ['-ld3d12', '-ldxgi'],
  install: true)
