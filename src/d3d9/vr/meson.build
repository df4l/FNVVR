vr_src = files([
  'vr_math.cpp',
  'vr_emulator_rig.cpp',
  'vr_emulator_backend.cpp',
  'vr_game_space.cpp',
  'vr_openvr_convert.cpp',
  'vr_virtual_gamepad.cpp',
  'vr_gamepad_head.cpp',
  'vr_controller_pad.cpp',
  'vr_turn_input.cpp',
  'vr_action_filter.cpp',
  'vr_menu_pointer.cpp',
])

# Parts that depend on DXVK and Vulkan, only built into d3d9.dll
vr_dxvk_src = files([
  'vr_system.cpp',
  'vr_backend_factory.cpp',
  'vr_openvr_backend.cpp',
  'vr_eye_transition.cpp',
  'vr_extension_provider.cpp',
  'vr_gamepad_hook.cpp',
  'vr_controls.cpp',
  'vr_menu_cursor.cpp',
  'vr_head_look.cpp',
  'vr_d3d9_bridge.cpp',
  'vr_emulator_keyboard.cpp',
  'vr_game_camera.cpp',
  'vr_game_memory.cpp',
  'vr_game_nodes.cpp',
  'vr_game_state.cpp',
  'vr_hud_layers.cpp',
  'vr_menu_scenes.cpp',
  'vr_game_resolution.cpp',
  'vr_preview_window.cpp',
  'vr_stereo_renderer.cpp',
])

# Unit tests for the VR layer. They are built with the host compiler so they
# run without Wine and without a GPU.
if add_languages('cpp', native: true, required: false)
  vr_test_sources = files([
    'tests/test_main.cpp',
    'tests/test_vr_math.cpp',
    'tests/test_vr_emulator.cpp',
    'tests/test_vr_game_space.cpp',
    'tests/test_vr_virtual_gamepad.cpp',
    'tests/test_vr_gamepad_head.cpp',
    'tests/test_vr_openvr_convert.cpp',
    'tests/test_vr_controller_pad.cpp',
    'tests/test_vr_turn_input.cpp',
    'tests/test_vr_action_filter.cpp',
    'tests/test_vr_menu_pointer.cpp',
  ])

  vr_test = executable('vr_tests', vr_test_sources, vr_src,
    files('../../util/util_matrix.cpp'),
    native              : true,
    include_directories : include_directories('../../../include/vulkan/include'),
    cpp_args            : [ '-DVR_OPENVR_DIR="@0@"'.format(meson.current_source_dir() / 'openvr') ],
    override_options    : [ 'cpp_std=c++17' ])

  test('vr', vr_test)
endif
