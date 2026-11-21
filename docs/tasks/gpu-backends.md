# GPU backends (WebGPU, WebGL2)

## Status

`Completed` — 2026-11-20.

## Objective

캔버스를 CPU가 아니라 GPU로 그립니다. ThorVG가 제공하는 WebGPU·WebGL(GL) 엔진을 그대로 쓰고, 픽셀 예산 상한을 없앱니다. GPU를 쓸 수 없는 브라우저에서는 지금까지의 software 렌더러로 자동으로 내려갑니다.

## ThorVG 1.1.4

- `subprojects/thorvg`를 `v1.0.0-347` main 스냅샷에서 **v1.1.4** 태그(`47658a8`)로 올렸습니다. 1.0의 `WgCanvas`에는 "쓰지 말라"는 경고가 있었고, 1.1.4에는 없으며 surface 재설정 크래시 수정이 들어 있습니다. [thorvg.web](https://github.com/thorvg/thorvg.web)의 webcanvas도 이 커밋을 씁니다.
- Software 엔진의 결과는 그대로라 렌더 계약(정확한 seam 바이트, ±1 밝기)이 바뀌지 않았습니다.
- WASM 빌드는 ThorVG 엔진을 `cpu,gl,wg`로, native 빌드는 `cpu`로 둡니다(`engine/meson.build`). `cross/wasm32.txt`가 컴파일·링크 모두에 `--use-port=emdawnwebgpu`(Emscripten의 Dawn WebGPU 포트)를, 링크에 `-sMAX_WEBGL_VERSION=2`를 줍니다. 1.1.4의 SIMD 기본값(켜짐)은 wasm32에서 "unsupported"로 꺼집니다.

## 렌더러 구조

- `ThorVGRenderer`(공통): 배경, 그림자, 몸체, 면과 광택, 들린 조각, 선을 ThorVG의 공통 `tvg::Canvas` 위에 만듭니다. 세 백엔드가 같은 장면 코드를 씁니다. `resize()`(검증, 같은 크기면 그대로, 실패하면 쓸 수 없음)와 `start()`(첫 target)도 여기 한 곳에 있습니다.
- `ThorVGBackend<Target>`(`ThorVGBackend.hpp`): 그리는 대상(target)과 그 종류의 ThorVG 캔버스를 함께 가집니다. 멤버를 target, 캔버스 순으로 선언해 두어 C++이 캔버스를 먼저 지웁니다 — 캔버스가 target에 그리기 때문이고, 이 순서를 사람이 기억할 필요가 없습니다. 캔버스는 target이 생긴 뒤에 만듭니다(GL 엔진은 캔버스를 만들면서 현재 컨텍스트에 버퍼를 만듦). 백엔드는 이제 target 하나씩입니다: `SoftwareTarget`, `GlTarget`, `WgTarget`.
- `create_software_renderer()` = `ThorVGBackend<SoftwareTarget>`(`ThorVGSoftwareRenderer.cpp`): `SwCanvas`와 메모리 픽셀 버퍼. Native, 테스트, GPU가 없는 페이지가 씁니다.
- `ThorVGGpuRenderer.cpp`(WASM에서만): `GlTarget`은 캔버스의 WebGL2 컨텍스트(`GlCanvas`가 기본 framebuffer를 겨눔), `WgTarget`은 WebGPU surface(`WgCanvas`)입니다. WebGPU instance·adapter·device는 하나를 모든 캔버스가 공유하고, 브라우저가 비동기로 넘겨주므로 `webgpu_device()`가 진행 상태(`GpuDeviceStatus`: 준비, 실패, 대기)를 돌려줍니다.
- **소프트웨어로 에뮬레이션한 어댑터는 엔진이 거절합니다.** 브라우저의 `isFallbackAdapter`(Chromium의 SwiftShader)를 Dawn 포트가 `WGPUAdapterType_CPU`로 알려 주고, 그 어댑터에서는 장치를 청하지 않고 실패로 답합니다. 쓰는 어댑터와 검사하는 어댑터가 같습니다.
- 잃은 것은 콘솔에 남깁니다: device의 잡히지 않은 오류, WebGPU device lost(그 뒤로 장치 상태는 실패), WebGL context lost.
- 캔버스는 한 종류의 컨텍스트만 줍니다. 그래서 실패할 수 있는 단계(크기 검증, WebGPU 장치 준비와 ThorVG 캔버스 생성)는 컨텍스트를 차지하기 전에 하고, 그 뒤의 실패만 "캔버스를 잃음"으로 따로 알립니다. ThorVG의 GL 엔진은 만들어지면서 GL 버퍼를 만들기 때문에, GL에서는 ThorVG 캔버스 생성이 컨텍스트 활성화 뒤로 갑니다(컨텍스트 활성화, 캔버스 생성, target이 "잃음"에 듭니다).
- 컨텍스트 속성은 webcanvas와 같습니다(alpha, premultiplied, depth·stencil 없음, WebGL 2.0).

## 엔진 API

- `present_surface_on_gpu(id, backend, canvas)` → `GpuPresentation`: `Refused`(캔버스는 그대로, 메모리에 계속 그림), `OnGpu`, `CanvasLost`(컨텍스트를 차지한 뒤 실패해 그 캔버스에는 이제 아무것도 그릴 수 없음). 컨텍스트를 바로 만들어 답합니다. 이미 GPU에 있는 surface는 거절합니다 — 새 렌더러가 옛 것보다 먼저 만들어져, 옛 WebGPU surface가 사라지며 새 설정을 풀어 버리기 때문입니다. GPU surface는 화면에서 빠질 때(0×0)도 컨텍스트를 놓지 않습니다. 그동안은 그릴 것도 누를 것도 복사할 것도 없는 surface입니다(`drawable()`: 렌더러와 크기가 둘 다 있어야 함).
- `webgpu_device_status()` → `GpuDeviceStatus`. 두 enum은 렌더러 생성 함수와 함께 ThorVG 타입이 없는 `render/Renderers.hpp`에 있습니다 — application이 render에서 보는 것은 이 헤더 하나이고, `render/` 밖에서 `<thorvg.h>`를 include하면 빌드가 실패합니다(`render/meson.build`).
- C ABI `thorvg_rubiks_present_surface_on_gpu(id, backend)`는 문자열을 받지 않습니다. 캔버스 키(`"!rubiks-surface-<id>"`)는 `capi.cpp`의 `gpu_canvas_key()` 한 곳에서 만들고, 페이지는 `thorvg_rubiks_gpu_canvas_key(id)`로 읽어 그 키로 `Module.specialHTMLTargets`에 캔버스를 넣습니다.
- Native 빌드는 GPU 요청을 캔버스를 건드리지 않고 거절합니다(test 있음).

## 웹

- `CubeEngine.create`가 큐브 캔버스에서 렌더러를 정합니다. 어떤 2D 컨텍스트도 만들기 전이어야 합니다 — 캔버스는 한 종류의 컨텍스트만 줍니다. auto의 순서: WebGPU(브라우저에 `navigator.gpu`가 있고, 엔진이 실제 GPU 어댑터로 device를 3초 안에 받으면) → WebGL2 → software. 다른 캔버스(넷, 링, 축)도 같은 렌더러로, 거절되면 그 캔버스만 software로 그립니다. `CanvasLost`면 그 캔버스는 어떤 것으로도 그릴 수 없으므로 분명한 오류로 끝납니다(실제로는 일어나지 않게 엔진이 순서를 잡아 둠).
- 엔진 코드값(백엔드, 장치 상태, 표시 결과)은 `CubeEngine.ts`에 이름 붙은 상수로 둡니다.
- GPU 캔버스는 `putImageData`가 없습니다. 크기는 페이지가 `canvas.width/height`로 정하고 엔진이 같은 크기로 다시 겨눕니다.
- 렌더러 이름은 `CUBE_RENDERERS`(`CubeEngine.ts`) 한 곳에 있고, `CubeRenderer` 타입과 `cubeRendererOf()`가 거기서 나옵니다.
- `?renderer=webgpu|webgl|software`로 렌더러를 지정할 수 있습니다. 읽기(`readRendererQuery`)와 쓰기(`rendererUrl`)는 `RendererQuery.ts` 한 모듈에 있습니다. 지정한 것 하나만 시도하고, 안 되면 software입니다. 정해진 렌더러는 `startApp`이 돌려주는 `AppController.renderer`로 페이지에 넘어가고, 보는 사람(e2e 포함)을 위해 `#view`의 `data-renderer`에도 남습니다.
- CubeEngine의 캔버스는 두 종류입니다: GPU가 직접 그리는 것(`kind: 'gpu'`)과 메모리에서 복사해 오는 것(`kind: 'copied'`, 2D 컨텍스트와 픽셀 버퍼 view를 가짐). 캔버스 키는 Emscripten의 `UTF8ToString`으로 읽습니다.
- 렌더러 스위치(`ui/RendererSwitch.ts`): 미니멀 페이지 헤더의 해·달 버튼 옆(평소엔 그리고 있는 렌더러 칩 하나, 마우스를 올리거나 Tab으로 들어가면 왼쪽으로 SW · WebGL · WebGPU가 다 펼쳐짐; 폰 폭에서는 SW · GL · GPU)과 전체 앱 Settings의 Appearance 아래(Renderer)에 있습니다. 누르면 `?renderer=`를 바꿔 페이지를 새로 엽니다 — 캔버스는 컨텍스트를 한 번만 주므로 같은 페이지에서는 바꿀 수 없고, 큐브 상태는 다른 새로고침처럼 이어지지 않습니다. 눌린 버튼은 요청한 것이 아니라 실제로 그린 렌더러(`AppController.renderer`)이고, 시작에 실패한 페이지에서는 아무것도 눌리지 않은 채 셋 다 고를 수 있습니다.
- **픽셀 예산(`MAX_STAGE_PIXELS`)을 없앴습니다.** 모든 화면은 기기 밀도로 그립니다(`stageDensity`). Software로 내려간 페이지는 큰 화면에서 그만큼 느릴 수 있습니다.

## ThorVG 1.1.4에서 만난 문제

- `tvgWgRenderer.cpp`의 `surfaceConfigure`가 `WGPUSurfaceCapabilities capabilities;`를 초기화하지 않고 `wgpuSurfaceGetCapabilities`에 넘겨, Emscripten Dawn 포트가 `nextInChain` assert로 abort합니다. 이 경로는 adapter를 넘기는 `target(Context, …)`일 때만 타므로, 엔진은 adapter 없는 `target(device, instance, surface, …)`를 씁니다. Upstream에 고칠 만한 버그입니다(`WGPUSurfaceCapabilities capabilities = {};`).
- SwiftShader WebGPU에서는 위 우회로도 무효 텍스처에 그립니다(원인 오류는 보고되지 않음). 엔진이 fallback adapter를 거절하는 이유입니다.

## 테스트

- Native: 30개. GPU 요청이 native에서 캔버스를 건드리지 않고 거절되는지(`SurfaceTest`).
- Unit: 411개. GPU 캔버스에 복사하지 않음, 거절되면 software, 캔버스를 잃으면 분명한 오류, WebGPU 대기와 3초 시간 초과, 지정한 WebGPU가 없으면 software, auto에서 WebGPU 실패 뒤 WebGL, `navigator.gpu`가 없으면 묻지 않음, 다른 캔버스(`CubeEngine.test.ts`); 예산 없는 밀도와 `AppController.renderer`(`AppLifecycle.test.ts`); 렌더러 이름·query 읽기·쓰기(`RendererQuery.test.ts`); 스위치의 눌림·이동·실패 뒤(`RendererSwitch.test.ts`, `GameShell.test.ts`, `bootstrap.test.ts`). 가짜 모듈이 모든 GPU export를 갖고 있어 제품 코드에 테스트용 분기가 없습니다.
- E2E는 프로젝트가 둘입니다(`playwright.config.ts`).
  - `software`: 기존 suite 전부(렌더러 스위치가 새 주소로 다시 열고, 거절되면 software가 눌리는지 포함). WebGL을 끈 브라우저(`--disable-3d-apis`, 어댑터 없음)라 페이지가 software로 내려가고, 2D 컨텍스트로 읽는 픽셀 계약이 그대로 유지됩니다.
  - `gpu`: `gpu.spec.ts`. Chromium의 new headless(`channel: 'chromium'`, 실제 GPU)에서 WebGPU·WebGL이 계약의 세 면 색(±32)을 그리고 Scramble 뒤 다시 그리는지(큐브 위 7×7 격자) 스크린샷으로 확인합니다. GPU 캔버스는 2D 컨텍스트로 읽을 수 없어서이고, 스크린샷은 브라우저가 화면 배율까지 맞춰 해석합니다. 실제 GPU WebGPU가 없는 기계(CI)에서는 WebGPU 테스트를 건너뛰고 페이지가 WebGL을 고르는지 봅니다.
- 두 프로젝트를 동시에 돌리면 부하에 민감한 기존 test(`sharing.spec.ts`의 Solve 버튼 상태 등)가 가끔 흔들립니다. 따로 돌리면 안정적입니다.
