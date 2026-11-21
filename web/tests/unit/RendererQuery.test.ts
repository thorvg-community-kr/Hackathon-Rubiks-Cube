import { describe, expect, it } from 'vitest';

import { readRendererQuery, rendererUrl } from '../../src/RendererQuery.ts';
import { cubeRendererOf } from '../../src/wasm/CubeEngine.ts';

describe('cubeRendererOf', () => {
  it('names the three renderers and nothing else', () => {
    expect(cubeRendererOf('software')).toBe('software');
    expect(cubeRendererOf('webgl')).toBe('webgl');
    expect(cubeRendererOf('webgpu')).toBe('webgpu');
    expect(cubeRendererOf('auto')).toBeNull();
    expect(cubeRendererOf('')).toBeNull();
    expect(cubeRendererOf(null)).toBeNull();
    expect(cubeRendererOf(undefined)).toBeNull();
  });
});

describe('readRendererQuery', () => {
  it('reads the renderer a page asks for, and auto otherwise', () => {
    expect(readRendererQuery('https://x.test/?renderer=webgpu')).toBe('webgpu');
    expect(readRendererQuery('https://x.test/?renderer=webgl')).toBe('webgl');
    expect(readRendererQuery('https://x.test/?extended&renderer=software')).toBe('software');
    expect(readRendererQuery('https://x.test/')).toBe('auto');
    expect(readRendererQuery('https://x.test/?renderer=vulkan')).toBe('auto');
    expect(readRendererQuery('not a url')).toBe('auto');
  });
});

describe('rendererUrl', () => {
  it('asks for the renderer and keeps the rest of the query', () => {
    expect(rendererUrl('https://x.test/cube/?extended', 'webgl')).toBe(
      'https://x.test/cube/?extended=&renderer=webgl',
    );
    expect(rendererUrl('https://x.test/cube/', 'software')).toBe(
      'https://x.test/cube/?renderer=software',
    );
  });

  it('replaces a renderer already asked for and drops the fragment', () => {
    expect(rendererUrl('https://x.test/cube/?renderer=webgpu#s=abc', 'webgl')).toBe(
      'https://x.test/cube/?renderer=webgl',
    );
  });

  it('is read back as the renderer it asks for', () => {
    for (const renderer of ['software', 'webgl', 'webgpu'] as const) {
      expect(readRendererQuery(rendererUrl('https://x.test/?extended', renderer))).toBe(renderer);
    }
  });
});
