# The software engine everywhere; the WebAssembly build adds ThorVG's GL
# (WebGL2) and WebGPU engines, for a page to draw its canvases with the GPU.
thorvg_engines = host_machine.system() == 'emscripten' ? 'cpu,gl,wg' : 'cpu'

thorvg_options = [
  'default_library=static',
  'engines=' + thorvg_engines,
  'extra=',
  'file=false',
  'loaders=',
  'savers=',
  'static=true',
  'threads=' + (host_machine.system() == 'emscripten' ? 'false' : 'true'),
  'tools=',
]

thorvg_dep = subproject(
  'thorvg',
  default_options: thorvg_options,
).get_variable('thorvg_dep')

subdir('src')
