import { expect, test } from '@playwright/test';

import {
  assertDrawingBufferMatchesCss,
  assertSceneContract,
  assertSeams,
  cubeRegionSide,
  probeCanvas,
  SEAM_MIN_REGION,
} from './sceneContract.ts';

test('renders the scene contract and re-verifies it after resize', async ({
  page,
}) => {
  const consoleErrors: string[] = [];
  page.on('console', (message) => {
    if (message.type() === 'error') consoleErrors.push(message.text());
  });

  const pageErrors: string[] = [];
  page.on('pageerror', (error) => pageErrors.push(String(error)));

  const failedRequests: string[] = [];
  page.on('requestfailed', (request) => failedRequests.push(request.url()));

  // Every JavaScript and WASM response must succeed; at least one of each
  // proves the engine actually travelled over the network.
  const assetResponses: { url: string; ok: boolean }[] = [];
  page.on('response', (response) => {
    if (/\.(js|wasm)(\?|$)/.test(response.url())) {
      assetResponses.push({ url: response.url(), ok: response.ok() });
    }
  });

  // Relative to baseURL so the Pages subpath is preserved; '/' would
  // resolve to the host root and dodge base path regressions.
  await page.goto('./?extended');

  await expect(page.locator('#app')).toHaveAttribute('data-state', 'ready');

  const initial = await probeCanvas(page);
  assertDrawingBufferMatchesCss(initial);
  assertSceneContract(initial);

  await page.setViewportSize({ width: 480, height: 360 });
  await expect
    .poll(async () => (await probeCanvas(page)).width)
    .not.toBe(initial.width);

  const resized = await probeCanvas(page);
  assertDrawingBufferMatchesCss(resized);
  assertSceneContract(resized);

  // And a large window, which the drawing buffer grows to as well.
  await page.setViewportSize({ width: 2000, height: 1600 });
  await expect
    .poll(async () => (await probeCanvas(page)).width)
    .toBeGreaterThan(resized.width);

  const enlarged = await probeCanvas(page);
  assertDrawingBufferMatchesCss(enlarged);
  assertSceneContract(enlarged);

  await expect(page.locator('#app')).toHaveAttribute('data-state', 'ready');

  expect(consoleErrors).toEqual([]);
  expect(pageErrors).toEqual([]);
  expect(failedRequests).toEqual([]);
  expect(assetResponses.filter((r) => !r.ok)).toEqual([]);
  expect(
    assetResponses.some((r) => r.url.endsWith('.wasm')),
    'a WASM asset response is expected',
  ).toBe(true);
  expect(
    assetResponses.some((r) => r.url.endsWith('.js')),
    'a JavaScript asset response is expected',
  ).toBe(true);
});

// The seams are the narrowest thing the contract reads. Beside the net the
// 3D region shares the canvases' pixel budget and stops a little short of the
// size they need; on its own it has the whole budget. Asserting the size
// first keeps the seam branch from silently skipping if the layout changes.
test('the seams between stickers show the cubie body', async ({ page }) => {
  await page.setViewportSize({ width: 2000, height: 1600 });
  await page.goto('./?extended');
  await expect(page.locator('#app')).toHaveAttribute('data-state', 'ready');
  await page.locator('[data-view="3d"]').click();
  await expect(page.locator('[data-view-mode]').first()).toHaveAttribute(
    'data-view-mode',
    '3d',
  );

  await expect
    .poll(async () => cubeRegionSide(await probeCanvas(page)))
    .toBeGreaterThanOrEqual(SEAM_MIN_REGION);

  // The 3D view alone: the seams, and only the seams -- the rest of the
  // contract is read beside the net, in the test above.
  const alone = await probeCanvas(page);
  assertDrawingBufferMatchesCss(alone);
  assertSeams(alone);
});
