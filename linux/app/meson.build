pipewire_dep = dependency('libpipewire-0.3', version : '>= 1.0')
qt6 = import('qt6')
qt_dep = dependency('qt6', modules : ['Core', 'Gui', 'Widgets', 'Network'])

fxengine_lib = static_library('fxengine', 'FxEngine.cpp',
  dependencies : [fxdsp_dep, pipewire_dep])
fxengine_dep = declare_dependency(link_with : fxengine_lib,
  dependencies : [fxdsp_dep, pipewire_dep],
  include_directories : include_directories('.'))

executable('fxsound-engine-test', 'engine_test.cpp',
  dependencies : fxengine_dep, install : false)

preset_dir = get_option('prefix') / get_option('datadir') / 'fxsound' / 'presets'
app_args = [
  '-DFXSOUND_VERSION="@0@"'.format(meson.project_version()),
  '-DFXSOUND_PRESET_DIR="@0@"'.format(preset_dir),
  '-DFXSOUND_SOURCE_PRESET_DIR="@0@"'.format(meson.project_source_root() / 'bin' / 'x64' / 'Factsoft'),
]

moc = qt6.compile_moc(headers : ['FxController.h', 'MainWindow.h'])
rcc = qt6.compile_resources(sources : '../data/fxsound.qrc')

executable('fxsound', 'main.cpp', 'FxController.cpp', 'MainWindow.cpp', moc, rcc,
  cpp_args : app_args,
  dependencies : [fxengine_dep, qt_dep],
  install : true)
