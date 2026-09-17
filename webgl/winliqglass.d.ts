export declare class WinLiqGlass {
  constructor(canvas: HTMLCanvasElement, opts?: Record<string, unknown>);
  resize(width: number, height: number): void;
  setBackground(image: CanvasImageSource & { width: number; height: number }): void;
  setLayers(content: unknown[], ui: unknown[], top?: unknown[]): void;
  setMaterial(partial: { frost?: number; bend?: number; merge?: number; glass?: number }): void;
  setIntensity(value: number): void;
  setOverrides(partial: {
    opacity?: number;
    bend?: number;
    edge?: number;
    spec?: number;
    shine?: number;
    rim?: number;
    disp?: number;
  }): void;
  setTouch(x: number, y: number, amount?: number): void;
  render(): void;
}
