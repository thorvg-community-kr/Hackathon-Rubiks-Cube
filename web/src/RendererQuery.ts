import {
  cubeRendererOf,
  type CubeRenderer,
  type CubeRendererChoice,
} from './wasm/CubeEngine.ts';

/**
 * Where the renderer a page asks for sits in its address, and how it gets in
 * and out of one: read by the lifecycle as the page starts, written by the
 * renderer switch to open it again. Pure string work, like the share link's.
 */

/** The query parameter naming the renderer to ask for. */
export const RENDERER_QUERY_KEY = 'renderer';

/**
 * The renderer a page's address asks for, which software backs if it cannot
 * draw; auto without one or with one the page does not know.
 */
export function readRendererQuery(href: string): CubeRendererChoice {
  let raw: string | null;
  try {
    raw = new URL(href).searchParams.get(RENDERER_QUERY_KEY);
  } catch {
    return 'auto';
  }
  return cubeRendererOf(raw) ?? 'auto';
}

/**
 * This page's address asking for `renderer`.
 *
 * Everything else in the query stays, so the full app reopens as the full
 * app. The fragment goes: it is a shared cube the page already read and took
 * off, and a reload is a fresh page everywhere else here.
 */
export function rendererUrl(href: string, renderer: CubeRenderer): string {
  const url = new URL(href);
  url.searchParams.set(RENDERER_QUERY_KEY, renderer);
  url.hash = '';
  return url.toString();
}
