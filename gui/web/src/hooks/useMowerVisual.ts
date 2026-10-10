import {useEffect, useSyncExternalStore} from "react";
import {MOWER_MODELS} from "../constants/mowerModels";
import {DOCK_APPEARANCES, type DockAppearanceId} from "../constants/mowerAppearances";

export const MOWER_STYLES = ["rounded", "sculpted", "utility", "yardforce", "rm1000"] as const;
export type MowerStyle = typeof MOWER_STYLES[number];
/** Display-only pair that hardware presets can supply independently of physical geometry. */
export type MowerVisualPreset = {style: MowerStyle; dockAppearance: DockAppearanceId};
export const DEFAULT_MOWER_VISUAL: MowerVisualPreset = {style: "sculpted", dockAppearance: "styled"};
type Preference = MowerVisualPreset & {transparent: boolean};
const KEY = "mowgli.robot-visual.v1";
const EVENT = "mowgli-robot-visual";
const fallback: Preference = {...DEFAULT_MOWER_VISUAL, transparent: false};
let cachedRaw: string | null | undefined;
let cached = fallback;
function snapshot(): Preference {
    let raw: string | null = null;
    try { raw = localStorage.getItem(KEY); } catch { /* private browsing */ }
    if (raw !== cachedRaw) {
        cachedRaw = raw;
        try {
            const parsed: unknown = JSON.parse(raw ?? "null");
            const data = parsed && typeof parsed === "object" ? parsed as Record<string, unknown> : {};
            const style = MOWER_STYLES.find(s => s === data.style) ?? fallback.style;
            const dockAppearance = Object.values(DOCK_APPEARANCES).find(d => d.id === data.dockAppearance)?.id
                ?? DEFAULT_MOWER_VISUAL.dockAppearance;
            cached = {style, dockAppearance, transparent: data.transparent === true};
        } catch { cached = fallback; }
    }
    return cached;
}
function subscribe(listener: () => void) {
    window.addEventListener(EVENT, listener);
    window.addEventListener("storage", listener);
    return () => {window.removeEventListener(EVENT, listener); window.removeEventListener("storage", listener);};
}
export function useMowerVisual(model?: unknown, legacyMapAppearance?: unknown) {
    // Seed old installations only once. Existing explicit shell choices win.
    useEffect(() => {
        if (typeof model !== "string" && legacyMapAppearance !== "biltema-rm1000") return;
        try {
            const raw: unknown = JSON.parse(localStorage.getItem(KEY) ?? "null");
            const data = raw && typeof raw === "object" ? raw as Record<string, unknown> : {};
            if (MOWER_STYLES.some(style => style === data.style)) return;
            const pair = legacyMapAppearance === "biltema-rm1000"
                ? {style: "rm1000", dockAppearance: "styled"}
                : MOWER_MODELS.find(preset => preset.value === model)?.appearance ?? DEFAULT_MOWER_VISUAL;
            localStorage.setItem(KEY, JSON.stringify({...pair, transparent: data.transparent === true}));
            window.dispatchEvent(new Event(EVENT));
        } catch { /* Keep the default preview when storage is unavailable. */ }
    }, [model, legacyMapAppearance]);
    const preference = useSyncExternalStore(subscribe, snapshot, () => fallback);
    return [preference, (patch: Partial<Preference>) => {
        const pair = patch.style
            ? MOWER_MODELS.find(model => model.appearance?.style === patch.style)?.appearance
                ?? {style: patch.style, dockAppearance: DEFAULT_MOWER_VISUAL.dockAppearance}
            : {};
        const next = {...snapshot(), ...pair, ...patch};
        try {localStorage.setItem(KEY, JSON.stringify(next));} catch {cached = next;}
        window.dispatchEvent(new Event(EVENT));
    }] as const;
}
