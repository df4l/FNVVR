vr_src = files([
  'vr_math.cpp',
  'vr_emulator_rig.cpp',
  'vr_emulator_backend.cpp',
])

# Unit tests for the VR layer. They are built with the host compiler so they
# run without Wine and without a GPU.
if add_languages('cpp', native: true, required: false)
  vr_test_sources = files([
    'tests/test_main.cpp',
    'tests/test_vr_math.cpp',
    'tests/test_vr_emulator.cpp',
  ])

  vr_test = executable('vr_tests', vr_test_sources, vr_src,
    files('../../util/util_matrix.cpp'),
    native              : true,
    include_directories : include_directories('../../../include/vulkan/include'),
    override_options    : [ 'cpp_std=c++17' ])

  test('vr', vr_test)
endif
