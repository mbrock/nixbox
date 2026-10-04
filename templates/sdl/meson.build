project('sdlgame', 'cpp', default_options: ['cpp_std=c++20'])

# pkgsXbox.SDL3 is SDL3 with its UWP backend, as a static library.
executable('sdlgame', 'main.cpp',
  dependencies: dependency('sdl3'),
  win_subsystem: 'windows',
  install: true)
