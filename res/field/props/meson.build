subdir('animations')
subdir('models')
subdir('model_sets')
subdir('texture_sets')

# The material/shape pairs each prop model is drawn with (build_model_matshp.dat).
nitrofs_files += custom_target('build_model_matshp.dat',
    output: 'build_model_matshp.dat',
    input: files('material_shapes.json', 'models/map_prop_models.order'),
    command: [ prop_material_shapes_py, '@INPUT0@', '@OUTPUT@', '--model-order', '@INPUT1@' ],
)
