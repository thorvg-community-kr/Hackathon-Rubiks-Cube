import { defineConfig } from '@playwright/test';

/**
 * Browser smoke tests run against the production build served by Vite
 * preview under the Pages base path, so the e2e pass also validates the
 * deployable artifacts. The WASM artifacts must be synchronized by
 * build_wasm.sh beforehand.
 *
 * With SMOKE_BASE_URL set, the same suite runs against an already
 * deployed site instead: no local server is started and the URL (the
 * page_url reported by the Pages deployment) becomes the base.
 */
const smokeBaseUrl = process.env.SMOKE_BASE_URL;

const previewUrl = 'http://127.0.0.1:4173/Hackathon-Rubiks-Cube/';

export default defineConfig({
  testDir: './tests/e2e',
  use: {
    // Trailing slash matters: specs navigate with relative paths like
    // './' so the subpath is preserved.
    baseURL: smokeBaseUrl ?? previewUrl,
    // The rendered-scene contract is written against the dark ground, and
    // with no saved preference the page follows the machine. Saying which
    // machine this is here keeps every pixel assertion in the suite reading
    // the same background; theme.spec.ts overrides it to check the other one.
    colorScheme: 'dark',
  },
  // Two browsers. The suite's pixel assertions read the cube's canvas through
  // a 2D context, which only the software renderer leaves it with, and they
  // are written against the software rasterizer's exact output: that browser
  // has no WebGL and no WebGPU adapter, so the page falls back to software.
  // gpu.spec.ts checks the GPU renderers instead, in a browser that has
  // both, from screenshots rather than from the canvas. `--project=software`
  // runs the suite alone on a machine with no GPU.
  projects: [
    {
      name: 'software',
      testIgnore: /gpu\.spec\.ts/,
      use: { launchOptions: { args: ['--disable-3d-apis'] } },
    },
    {
      // Chromium's new headless mode, on the machine's own GPU: the old one
      // emulates WebGPU on SwiftShader, where ThorVG 1.1.4's WebGPU engine
      // ends up drawing into an invalid texture.
      name: 'gpu',
      testMatch: /gpu\.spec\.ts/,
      use: { channel: 'chromium', launchOptions: { args: ['--enable-unsafe-webgpu'] } },
    },
  ],
  webServer: smokeBaseUrl
    ? undefined
    : {
        command:
          'npm run build && npm run preview -- --host 127.0.0.1 --port 4173 --strictPort',
        url: previewUrl,
        reuseExistingServer: false,
        timeout: 120_000,
      },
});
