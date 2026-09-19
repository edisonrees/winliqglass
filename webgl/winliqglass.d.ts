export type GlassTint = [number, number, number, number];

/** A shape record, in CSS pixels, with the origin at the top left. */
export interface GlassShape {
  /** Left edge for setLayers input; the renderer centres it internally. */
  x: number;
  y: number;
  w: number;
  h: number;
  /** Corner radius. Defaults to 22% of the short side. */
  rad?: number;
  /** Rotation in radians. */
  rot?: number;
  /** 0 circle, 1 rounded rect, 2 triangle, 3 ring, 4 pentagon. */
  kind?: number;
  /** 1 joins this shape to its neighbours through the smooth minimum. */
  merge?: number | boolean;
  /** Hairline override: above 0 lights the edge, below 0 darkens it. */
  rim?: number;
  tint?: GlassTint;
}

export interface GlassHighlight {
  /** Degrees, 0 from the top, running clockwise. */
  angle?: number;
  /** 0 to 1: how brightly the edge facing away from the light answers. */
  bounce?: number;
  /** Exponent on each lobe. Above 1 shortens them. */
  sharpness?: number;
  /** Floor under both lobes. */
  base?: number;
  /** Multiplies the hairline. */
  strength?: number;
  /** Radians of slow drift around the angle. 0 holds it still. */
  sway?: number;
}

export interface GlassMaterial {
  /** Blur radius, 0 to 1. Stays at 0 for glass; above that it is frost. */
  frost?: number;
  /** Refraction depth, 0 to 1. */
  bend?: number;
  /** Smooth minimum radius between merged shapes, 0 to 1. */
  merge?: number;
  /** Body opacity, 0 to 1. */
  glass?: number;
}

export interface GlassOverrides {
  opacity?: number;
  bend?: number;
  edge?: number;
  spec?: number;
  shine?: number;
  rim?: number;
  disp?: number;
}

export type GlassImage =
  | HTMLImageElement
  | HTMLVideoElement
  | HTMLCanvasElement
  | ImageBitmap
  | OffscreenCanvas;

export declare class WinLiqGlass {
  constructor(canvas: HTMLCanvasElement, opts?: { intensity?: number });
  readonly gl: WebGL2RenderingContext;
  readonly canvas: HTMLCanvasElement;
  readonly build: string;
  /** Set true to publish shape counts onto the canvas dataset. */
  debug?: boolean;
  /** Width and height are CSS pixels; the backing store is that times dpr. */
  resize(width: number, height: number, dpr?: number): void;
  setBackground(image: GlassImage): void;
  setLayers(content: GlassShape[], ui: GlassShape[], top?: GlassShape[]): void;
  setMaterial(partial: GlassMaterial): void;
  setIntensity(value: number): void;
  setHighlight(partial: GlassHighlight): void;
  /** 1 refracts outward along the normal, -1 inward. */
  setDirection(value: number): void;
  /** Multiplies the ported constants; 1.0 on every dial is the default look. */
  setOverrides(partial: GlassOverrides): void;
  setTouch(x: number, y: number, amount?: number): void;
  render(): void;
  dispose(): void;
}

export interface CreateGlassOptions {
  /** Image URL, or any drawable source. A video sets animated for you. */
  background?: string | GlassImage;
  /** Re-upload the background every frame. Implied by a video source. */
  animated?: boolean;
  /** Use this canvas instead of creating and inserting one. */
  canvas?: HTMLCanvasElement;
  /** Host element for a created canvas, and the size it follows. */
  mount?: HTMLElement;
  /** Content layer selector. Defaults to "[data-glass]". */
  select?: string;
  /** UI layer selector. Defaults to "[data-glass-ui]". */
  selectUi?: string;
  /** Optional third layer, drawn over the UI layer. */
  selectTop?: string;
  intensity?: number;
  material?: GlassMaterial;
  highlight?: GlassHighlight;
  overrides?: GlassOverrides;
  direction?: number;
  /** Device pixel ratio cap. Defaults to min(devicePixelRatio, 2.5). */
  dpr?: number;
  /** Set false to stop tracking the pointer for the touch highlight. */
  pointer?: boolean;
  crossOrigin?: string;
  rim?: number;
  tint?: GlassTint;
  onFrame?(dt: number, glass: WinLiqGlass): void;
}

export interface GlassScene {
  glass: WinLiqGlass;
  canvas: HTMLCanvasElement;
  start(): void;
  stop(): void;
  setBackground(value: string | GlassImage): void;
  /** Re-measure after the host element changes size. */
  refresh(): void;
  destroy(): void;
}

/** Canvas, DPR, resize, frame loop, pointer and DOM driven shapes in one call. */
export declare function createGlass(options?: CreateGlassOptions): GlassScene;

/** Reads a laid out element as a shape record, radius included. */
export declare function shapeFromElement(
  el: Element,
  options?: { rim?: number; tint?: GlassTint }
): GlassShape;

/** True when this browser can create a WebGL2 context. */
export declare function isSupported(): boolean;

export declare const WINLIQGLASS: {
  build: string;
  passes: number;
  maxShapesPerPass: number;
  source: string;
};
