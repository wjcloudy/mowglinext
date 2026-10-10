import type {MowerStyle} from "../../hooks/useMowerVisual";

type Shell = {
    /** Cosmetic console anchor within visible chassis bounds: front=0, rear=1.
     * roofFromTop is the side silhouette height at that same longitudinal point.
     * These move only the drawn button, never URDF geometry or safety controls. */
    stop: {fromFront: number; roofFromTop: number};
    files?: Partial<Record<"top" | "side", string>>;
    size: [number, number];
    top: [number, number, number, number];
    side: [number, number, number, number];
};
/** Visible shell bounds in source pixels. Padding is deliberately excluded from
 * physical scaling. Bounds are the tight alpha >220 envelope of each isolated
 * source projection, verified against decoded PNG pixels in Playwright.
 * Both opacity modes use these exact same atlas regions. */
export const SHELLS: Record<MowerStyle, Shell> = {
    // Separate side source preserves the approved top atlas byte-for-byte.
    // Per-view sources use the same canvas dimensions as the default atlas.
    rm1000: {stop: {fromFront: 0.8, roofFromTop: 0.092}, files: {side: "rm1000-side.png"}, size: [1774,887], top: [73,48,624,804], side: [44,162,1702,622]},
    rounded: {stop: {fromFront: 0.82, roofFromTop: 0.092}, size: [1774,887], top: [75,63,620,788], side: [911,339,825,284]},
    sculpted: {stop: {fromFront: 0.8, roofFromTop: 0.012}, size: [1774,887], top: [106,32,630,829], side: [795,302,931,338]},
    utility: {stop: {fromFront: 0.84, roofFromTop: 0.069}, size: [1774,887], top: [117,48,607,816], side: [831,356,874,261]},
    yardforce: {stop: {fromFront: 0.89, roofFromTop: 0.065}, size: [1774,887], top: [113,29,629,818], side: [852,274,857,371]},
};

