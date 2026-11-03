install_data('fxsound.desktop', install_dir : get_option('datadir') / 'applications')
install_data('fxsound.svg', install_dir : get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')
# presets de fábrica do app Windows (bin/x64/Factsoft): 1.fac .. 12.fac
install_subdir(meson.project_source_root() / 'bin' / 'x64' / 'Factsoft',
  install_dir : get_option('datadir') / 'fxsound' / 'presets', strip_directory : true)
