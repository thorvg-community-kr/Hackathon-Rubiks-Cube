# ThorVG enters the engine here and nowhere else. render/ draws a RenderScene
# and knows graphics alone. What the application may see of it is
# Renderers.hpp, which names no ThorVG type: render_dep hands ThorVG's code
# on to the link but not its headers, so an #include <thorvg.h> anywhere
# outside this folder fails to build rather than passing review.
render_lib = static_library(
  'rubiks-render',
  files(
    'ThorVGGpuRenderer.cpp',
    'ThorVGRenderer.cpp',
    'ThorVGSoftwareRenderer.cpp',
  ),
  dependencies: [graphics_dep, thorvg_dep],
)

render_dep = declare_dependency(
  dependencies: [graphics_dep],
  link_with: render_lib,
)
