subdir('encounters')
subdir('events')
subdir('frontier_scripts')
subdir('lighting')
subdir('map_object_preloads')
subdir('matrices')
subdir('props')
subdir('maps') # after props: map data refers to map prop models by name
subdir('area_data')
subdir('scripts')

# Game Freak's internal name for each map header (fielddata/maptable/mapname.bin).
nitrofs_files += custom_target('mapname.bin',
    output: 'mapname.bin',
    input: files('map_header_names.json'),
    command: [ map_header_names_py, '@INPUT@', '@OUTPUT@' ],
    env: jsoncnv_env,
    depends: py_consts_generators,
)
