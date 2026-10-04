project('d3d12caps', 'cpp', default_options: ['cpp_std=c++20'])

executable('d3d12caps', 'main.cpp',
  dependencies: dependency('sdl3'),
  link_args: ['-ld3d12', '-ldxgi'],
  win_subsystem: 'windows',
  install: true)
