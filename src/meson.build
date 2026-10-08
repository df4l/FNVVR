subdir('wsi')
subdir('util')
subdir('spirv')
subdir('vulkan')
subdir('dxvk')

if get_option('enable_d3d9')
  subdir('d3d9')
endif

# Nothing selected
if not get_option('enable_d3d9')
  warning('Nothing selected to be built.?')
endif
