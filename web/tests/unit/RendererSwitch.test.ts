import { describe, expect, it, vi } from 'vitest';

import { attachRendererSwitch } from '../../src/ui/RendererSwitch.ts';
import { CUBE_RENDERERS } from '../../src/wasm/CubeEngine.ts';

/** One button per renderer, off, as the shell writes them. */
function buttons(): HTMLButtonElement[] {
  return CUBE_RENDERERS.map((renderer) => {
    const button = document.createElement('button');
    button.dataset.rendererChoice = renderer;
    button.disabled = true;
    return button;
  });
}

function pressed(all: readonly HTMLButtonElement[]): string[] {
  return all
    .filter((button) => button.getAttribute('aria-pressed') === 'true')
    .map((button) => button.dataset.rendererChoice ?? '');
}

describe('attachRendererSwitch', () => {
  it('presses the renderer drawing the page and turns every button on', () => {
    const all = buttons();
    attachRendererSwitch({ buttons: all, current: 'webgl', switchTo: vi.fn() });

    expect(pressed(all)).toEqual(['webgl']);
    expect(all.every((button) => !button.disabled)).toBe(true);
  });

  it('opens the page again with another renderer, and not with the same', () => {
    const all = buttons();
    const switchTo = vi.fn();
    attachRendererSwitch({ buttons: all, current: 'webgpu', switchTo });

    all[2]!.click();
    expect(switchTo).not.toHaveBeenCalled();

    all[0]!.click();
    expect(switchTo).toHaveBeenCalledExactlyOnceWith('software');
  });

  it('presses none and offers all three when the page has no renderer', () => {
    const all = buttons();
    const switchTo = vi.fn();
    attachRendererSwitch({ buttons: all, current: null, switchTo });

    expect(pressed(all)).toEqual([]);
    all[2]!.click();
    expect(switchTo).toHaveBeenCalledExactlyOnceWith('webgpu');
  });

  it('stops listening on teardown', () => {
    const all = buttons();
    const switchTo = vi.fn();
    attachRendererSwitch({ buttons: all, current: 'software', switchTo }).teardown();

    all[1]!.click();
    expect(switchTo).not.toHaveBeenCalled();
  });
});
