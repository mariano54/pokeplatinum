# Map object graphics loaded together with the field map. A map header's
# preloadedMapObjectsArchiveID picks one of the lists in map_object_preloads.json.
nitrofs_files += custom_target('move_model_list.narc',
    output: 'move_model_list.narc',
    input: files('map_object_preloads.json'),
    command: [ map_object_preloads_py, '@INPUT@', '@OUTPUT@' ],
    env: jsoncnv_env,
    depends: py_consts_generators,
)

# Per-area lists in the same format; nothing in the game reads them.
nitrofs_files += custom_target('area_move_model_list.narc',
    output: 'area_move_model_list.narc',
    input: files('area_map_object_preloads.json'),
    command: [ map_object_preloads_py, '@INPUT@', '@OUTPUT@' ],
    env: jsoncnv_env,
    depends: py_consts_generators,
)
