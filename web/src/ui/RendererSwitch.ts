import { cubeRendererOf, type CubeRenderer } from '../wasm/CubeEngine.ts';

/**
 * The buttons that choose which of ThorVG's engines draws the canvases.
 *
 * A canvas gives a page one kind of context, ever, and the engine settles the
 * renderer on every canvas before the first frame. So a switch is a fresh
 * page asked for the other renderer (RendererQuery.ts), rather than something
 * this page could change in place.
 */

export type RendererSwitchOptions = {
  /** One button per renderer, each carrying `data-renderer-choice`. */
  readonly buttons: readonly HTMLButtonElement[];
  /**
   * The renderer the page draws with, pressed; null when it has none, as
   * when the engine failed to start, so any of them can be tried.
   */
  readonly current: CubeRenderer | null;
  /** Opens the page again drawing with `renderer`. */
  readonly switchTo: (renderer: CubeRenderer) => void;
};

export type RendererSwitch = {
  teardown(): void;
};

/**
 * Shows which renderer draws the page and opens it again with another.
 *
 * Pressing the one already drawing does nothing. A renderer this browser
 * cannot draw with is still offered: the page that opens falls back to
 * software, and its switch says so.
 */
export function attachRendererSwitch(options: RendererSwitchOptions): RendererSwitch {
  const { buttons, current, switchTo } = options;

  const onClick = (event: Event): void => {
    const chosen = cubeRendererOf((event.currentTarget as HTMLElement).dataset.rendererChoice);
    if (chosen === null || chosen === current) return;
    switchTo(chosen);
  };

  for (const button of buttons) {
    const renderer = cubeRendererOf(button.dataset.rendererChoice);
    button.setAttribute('aria-pressed', String(renderer !== null && renderer === current));
    button.disabled = false;
    button.addEventListener('click', onClick);
  }

  return {
    teardown: (): void => {
      for (const button of buttons) button.removeEventListener('click', onClick);
    },
  };
}
