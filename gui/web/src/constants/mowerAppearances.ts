/** Display-only dock registry. Hardware appearance pairs reference these IDs;
 * image dimensions are visual metadata, never ROS/collision calibration.
 */

export interface MapImageAppearance {
    src: string;
    altKey: string;
    /** Physical length represented by the source image's long axis. */
    visibleLengthM: number;
    /** Fraction of the source image occupied along its long axis. */
    visibleLengthFraction: number;
    /** Optional calibrated width; omitted for square-scaled mower art. */
    visibleWidthM?: number;
    /** Fraction of the source image occupied along its short axis. */
    visibleWidthFraction?: number;
    /** Pose location in normalized source-image coordinates (0..1). */
    poseAnchor: {x: number; y: number};
    /** Visual correction from the tracked object's heading to this image's forward axis. */
    headingOffsetRad?: number;
    /** Small display-only shift in meters along the tracked heading. */
    forwardOffsetM?: number;
}

export type DockAppearanceId = "marker" | "styled" | "generic" | "biltema-rm1000";

export interface DockAppearance {
    id: DockAppearanceId;
    labelKey: string;
    image?: MapImageAppearance;
    /** Optional foreground cutout for the dock's center tongue over the mower. */
    foregroundClipPath?: string;
}

export const DOCK_FOREGROUND_CLIP_PATH = "polygon(43% 78%, 57% 78%, 68% 83%, 68% 92%, 59% 96%, 41% 96%, 32% 92%, 32% 83%)";

export const DOCK_APPEARANCES: Record<DockAppearanceId, DockAppearance> = {
    marker: {id: "marker", labelKey: "mapToolbar.dockAppearanceMarker"},
    styled: {id: "styled", labelKey: "mowerVisual.dockStyle"},
    generic: {
        id: "generic",
        labelKey: "mapToolbar.dockAppearanceGeneric",
        foregroundClipPath: DOCK_FOREGROUND_CLIP_PATH,
        image: {
            src: "/assets/robots/generic/dock.webp",
            altKey: "mapToolbar.dockAppearanceGenericAlt",
            // Nominal visual footprint inherited from the RM1000 source photo;
            // it is only a sizing approximation for other charging stations.
            visibleLengthM: 0.63,
            visibleLengthFraction: 0.951,
            visibleWidthM: 0.46,
            visibleWidthFraction: 0.678,
            // Visually calibrated against the docked RM1000 photo; the docking
            // tongue is layered over the mower while the outer station stays behind it.
            poseAnchor: {x: 0.5, y: 0.16},
            headingOffsetRad: Math.PI,
        },
    },
    "biltema-rm1000": {
        id: "biltema-rm1000",
        labelKey: "mapToolbar.dockAppearanceBiltemaRm1000",
        foregroundClipPath: DOCK_FOREGROUND_CLIP_PATH,
        image: {
            src: "/assets/robots/biltema-rm1000/dock.webp",
            altKey: "mapToolbar.dockAppearanceBiltemaRm1000Alt",
            visibleLengthM: 0.63,
            visibleLengthFraction: 0.944,
            visibleWidthM: 0.46,
            visibleWidthFraction: 0.667,
            // Visually calibrated against the docked RM1000 photo; the docking
            // tongue is layered over the mower while the outer station stays behind it.
            poseAnchor: {x: 0.5, y: 0.16},
            headingOffsetRad: Math.PI,
        },
    },
};

/** Docks are paired with the hardware appearance; there is no separate map override. */
export function resolveDockAppearance(value: unknown): DockAppearance {
    return Object.values(DOCK_APPEARANCES).find(({id}) => id === value) ?? DOCK_APPEARANCES.styled;
}

export function shouldDisplayMapImage(
    image: MapImageAppearance | undefined,
    loadedSrc: string | undefined,
    hasPose: boolean,
    hasHeading: boolean,
): boolean {
    return Boolean(image && loadedSrc === image.src && hasPose && hasHeading);
}
