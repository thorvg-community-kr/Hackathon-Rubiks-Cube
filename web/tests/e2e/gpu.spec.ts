import { expect, test, type Page } from '@playwright/test';

import {
  CUBE_REGION_SIDE,
  FRONT_LIT,
  LEFT_SAMPLE,
  RIGHT_LIT,
  RIGHT_SAMPLE,
  TOP_SAMPLE,
  UP_LIT,
} from './sceneContract.ts';

/**
 * The GPU renderers, in a browser that has WebGL2 and a WebGPU adapter.
 *
 * A canvas the GPU draws on has no 2D context to read, so the picture is read
 * from a screenshot of the page instead. The GPU's rasterizer is not the
 * software one, so the colours are held to the contract loosely: a face is
 * its colour within a margin that still tells every face from the others.
 */

/**
 * The colours of a screenshot of the page at points given in page pixels.
 *
 * Decoded by the browser itself, and scaled by however many screenshot
 * pixels a page pixel came out as.
 */
async function screenshotColours(
  page: Page,
  points: readonly { x: number; y: number }[],
): Promise<number[][]> {
  const png = await page.screenshot();
  return page.evaluate(
    async ({ data, at }) => {
      const bytes = Uint8Array.from(atob(data), (c) => c.charCodeAt(0));
      const bitmap = await createImageBitmap(new Blob([bytes], { type: 'image/png' }));
      const scale = bitmap.width / window.innerWidth;
      const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
      const context = canvas.getContext('2d')!;
      context.drawImage(bitmap, 0, 0);
      return at.map((point) =>
        Array.from(
          context
            .getImageData(Math.round(point.x * scale), Math.round(point.y * scale), 1, 1)
            .data.slice(0, 3),
        ),
      );
    },
    { data: png.toString('base64'), at: points },
  );
}

/** Points of the cube's region in page pixels, from its room on the page. */
async function regionPoints(
  page: Page,
  fractions: readonly (readonly [number, number])[],
): Promise<{ x: number; y: number }[]> {
  const room = await page.evaluate(() => {
    const box = document.getElementById('view')!.parentElement!.getBoundingClientRect();
    return { left: box.left, top: box.top, width: box.width, height: box.height };
  });
  const side = CUBE_REGION_SIDE * Math.min(room.width, room.height);
  const x0 = room.left + (room.width - side) / 2;
  const y0 = room.top + (room.height - side) / 2;
  return fractions.map(([fx, fy]) => ({ x: x0 + fx * side, y: y0 + fy * side }));
}

/** A coarse grid over the cube's region: what a scramble has to change. */
const GRID = Array.from({ length: 49 }, (_, i) => [0.2 + (i % 7) * 0.1, 0.2 + Math.floor(i / 7) * 0.1] as const);

function near(actual: number[], expected: readonly number[], margin: number): boolean {
  return actual.every((value, i) => Math.abs(value - expected[i]!) <= margin);
}

async function settled(page: Page): Promise<void> {
  await page.evaluate(
    () =>
      new Promise<void>((resolve) =>
        requestAnimationFrame(() => requestAnimationFrame(() => resolve())),
      ),
  );
}

/**
 * Whether this browser has WebGPU on a real GPU. Where it has only a GPU
 * emulated in software (SwiftShader, on a machine with no GPU such as CI),
 * the page passes WebGPU over for WebGL, and so do these tests.
 *
 * Asked of the browser itself rather than of the page on purpose: this is
 * what the page's choice is checked against, so it cannot come from that
 * choice.
 */
async function hardwareWebgpu(page: Page): Promise<boolean> {
  return page.evaluate(async () => {
    const adapter = 'gpu' in navigator ? await navigator.gpu.requestAdapter() : null;
    return adapter !== null && !adapter.info.isFallbackAdapter;
  });
}

test.use({ colorScheme: 'dark', viewport: { width: 900, height: 900 } });

test('the page draws with the GPU when it can', async ({ page }) => {
  await page.goto('./');
  await expect(page.locator('[data-state]').first()).toHaveAttribute('data-state', 'ready');
  const expected = (await hardwareWebgpu(page)) ? 'webgpu' : 'webgl';
  await expect(page.locator('#view')).toHaveAttribute('data-renderer', expected);
});

for (const renderer of ['webgpu', 'webgl'] as const) {
  test(`${renderer} draws the cube the contract describes, and redraws it`, async ({ page }) => {
    await page.goto('./?renderer=software');
    test.skip(
      renderer === 'webgpu' && !(await hardwareWebgpu(page)),
      'no WebGPU on a real GPU here: the page uses WebGL instead',
    );

    const errors: string[] = [];
    page.on('console', (message) => {
      if (message.type() === 'error') errors.push(message.text());
    });
    page.on('pageerror', (error) => errors.push(String(error)));

    await page.goto(`./?renderer=${renderer}`);
    await expect(page.locator('[data-state]').first()).toHaveAttribute('data-state', 'ready');
    await expect(page.locator('#view')).toHaveAttribute('data-renderer', renderer);
    await settled(page);

    const faces = await regionPoints(page, [TOP_SAMPLE, LEFT_SAMPLE, RIGHT_SAMPLE]);
    const grid = await regionPoints(page, GRID);
    const [top, left, right] = await screenshotColours(page, faces);
    expect(near(top!, UP_LIT, 32), `top ${top}`).toBe(true);
    expect(near(left!, FRONT_LIT, 32), `left ${left}`).toBe(true);
    expect(near(right!, RIGHT_LIT, 32), `right ${right}`).toBe(true);

    // Scrambling turns layers on the GPU's canvas, and once it is done the
    // stickers across the cube are not the ones they were. (Not the three
    // above: each is a face's centre, which no scramble moves.)
    const unscrambled = await screenshotColours(page, grid);
    await page.getByRole('button', { name: /scramble/i }).click();
    await expect(page.locator('#status')).toContainText(/scramble ready/i, { timeout: 15_000 });
    await settled(page);
    const scrambled = await screenshotColours(page, grid);
    const changed = scrambled.filter((colour, i) => !near(colour, unscrambled[i]!, 24)).length;
    expect(changed, `grid points changed by the scramble`).toBeGreaterThan(5);

    expect(errors).toEqual([]);
  });
}
