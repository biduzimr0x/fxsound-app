# Build do FxSound para Linux (PipeWire). O build Windows continua sendo o
# Visual Studio (fxsound/Project/FxSound.sln).
project('fxsound', ['c', 'cpp'],
  version : '1.2.0',
  license : 'AGPL-3.0-or-later',
  meson_version : '>= 1.1',
  default_options : ['cpp_std=c++17', 'buildtype=release', 'warning_level=1', 'b_ndebug=if-release'])

subdir('linux')
