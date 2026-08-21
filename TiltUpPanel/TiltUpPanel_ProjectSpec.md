# RyViz Tilt-Up Concrete Wall Panel — Project Spec

## Overview

A custom 3ds Max primitive (`RyViz_TiltUpPanel`) that models a tilt-up precast
concrete wall panel with parametric horizontal/vertical reveals. Layout of
reveals is authored through a floating 2D panel editor built in Python
(PySide), which reads and writes the object's `ParamBlock2` data. Geometry is
generated procedurally in C++ from that same data — the editor is a *view*
onto the object's parameters, not a separate authoring step.

**Target environment:** 3ds Max 2027 SDK, Visual Studio 2022, RyViz-MaxSDK
solution/props layout (sibling-project structure, shared `RyViz-MaxSDK.props`).

**Prior art to reuse:** `InsetMod` — miter-bisector inset math, BFS adjacent-face
grouping, deferred face deletion. The reveal-carving logic is a generalized,
grid-driven version of this same technique.

---

## Architecture

Two components, split cleanly at the data boundary:

1. **C++ plugin (`GeomObject` / `SimpleObject2`-style primitive)**
   - Owns the `ParamBlock2`: width, height, depth, and Tabs for reveal
     definitions (axis, position, groove width, groove depth).
   - `BuildMesh()` regenerates the full mesh from the pblock every time —
     no cached/manual mesh state, ever. This is what makes the object stay
     parametric and re-editable.
   - Standard Create panel entry (next to Box/Cylinder, custom "RyViz"
     category), created via click-drag like Box (base rectangle, then
     height click).
   - Modify panel rollout: width/height/depth spinners + an
     "Edit Reveal Layout..." button that launches the floating Python editor.
   - No mesh caching of edits made via boolean/opening carving — openings are
     represented as hole loops cut into the grid at generation time, not as
     true CSG booleans (avoid a boolean solver entirely).

2. **Python (PySide2/PySide6) floating tool window**
   - `QGraphicsScene` / `QGraphicsView`-based 2D editor showing the panel's
     current width × height as the drawable canvas.
   - Draggable line items for horizontal/vertical reveals, snap-to-grid,
     numeric spin-box entry for precise position/width/depth.
   - Reads current reveal list from the object's pblock via `pymxs` on open;
     writes back to the pblock on edit/apply (no custom IPC — plain
     `pymxs.runtime` parameter access, same as any MAXScript interop).
   - Setting pblock values triggers the normal `REFMSG_CHANGE` /
     `BuildMesh()` pipeline automatically — the editor never touches geometry.
   - v1 target: modeless floating window (stays open while orbiting/tweaking
     viewport). Docking into Modify panel is a possible later enhancement.

---

## Data Model

```cpp
struct RevealDef {
    enum Axis { Horizontal, Vertical } axis;
    float position;      // distance along width (vertical) or height (horizontal), from origin
    float grooveWidth;
    float grooveDepth;
    // Phase 2+: int panelColorID;
};

struct OpeningDef {      // Phase 2 — windows/doors
    float x, y;           // lower-left corner, panel-local coords
    float width, height;
};
```

Stored in the `ParamBlock2` as parallel `TYPE_FLOAT_TAB` / `TYPE_INT_TAB`
entries (or a single serialized blob you parse — Tabs are simpler for
per-element pblock UI binding and animation, if that's ever wanted).

**Design decision needed before Phase 3 (editor UI):** should reveal positions
stay fixed in world units when panel width/height changes (recommended —
tilt-up reveals are dimensioned to real layout), or scale proportionally? Plan
to clamp/warn if a reveal falls outside new bounds under the fixed-unit model.

---

## Geometry Generation Pipeline

1. **Build a cut grid** on the front (and back) face using the panel bounds
   plus sorted horizontal/vertical reveal positions — same concept as a Box
   primitive's segment grid, but with irregular, reveal-position-driven cut
   lines instead of even spacing.
2. **Identify reveal strips** — each horizontal reveal maps to a row of grid
   faces between two cut lines; each vertical reveal maps to a column.
3. **Inset + carve per strip**, reusing `InsetMod`'s miter-bisector inset and
   BFS face-grouping logic, generalized from "inset per selection" to "inset
   per strip," with a controlled depth (groove carve, not full extrude-out).
   Deferred face deletion after the carve, same as `InsetMod`.
4. **Crossing reveals** (horizontal × vertical overlap) — treat the grid as
   the single source of truth and let reveal regions be unions of grid cells,
   rather than doing independent insets on overlapping rectangles. This is
   the trickiest topology edge case; solve it after single-axis reveals work.
5. **Openings (Phase 2+)** — cut a hole loop directly into the grid at
   generation time (split the affected quad(s) into a ring around a
   rectangular hole), the same representation Editable Poly uses for holed
   polygons. No boolean solver.
6. **Panel color / material ID (later)** — effectively free once per-strip
   face groups exist; assign material IDs per group.

---

## Workflow (User-Facing)

1. **Create:** Click "Tilt-Up Panel" in Create panel → click-drag base
   rectangle in viewport → click for height, same rhythm as Box. Object is
   created flat/default, no reveals yet.
2. **Configure:** Object lands in Modify panel like any primitive. Adjust
   width/height/depth via spinners, or click "Edit Reveal Layout..." to open
   the floating 2D editor.
3. **Edit reveals:** In the floating editor, roughly place a reveal by
   drawing/dragging, then type an exact position (e.g. `15'`) in the numeric
   field. Add horizontal/vertical reveals, set groove size/depth.
4. **Live parametric link:** Editing in the 2D window writes to the pblock →
   `BuildMesh()` regenerates → viewport updates. Reopening the editor later
   shows the actual current design, not a stale snapshot. Width/height
   spinner changes and the 2D editor stay in sync since both are views onto
   the same pblock.
5. **Stack behavior:** Modifiers applied above the primitive behave normally —
   editing base params invalidates upstream edits exactly like any other
   procedural primitive (e.g., Box + Edit Poly).

---

## Prototype Roadmap

1. **Grid pipeline validation.** Cube with width/height/depth params, no
   reveals — confirm the cut-grid generation approach works and topology is
   clean, before any reveal logic.
2. **Hardcoded reveal list → geometry.** Add horizontal/vertical reveal
   Tabs via plain spinners (no 2D UI yet). Prove inset/carve-per-strip
   produces correct, clean topology, including at least one crossing case.
3. **Python/PySide 2D editor.** Build the floating `QGraphicsView` editor
   against the same pblock data — read on open, write on edit, verify the
   round-trip with the C++ side.
4. **Openings via hole-in-grid.** Rectangular hole loops cut into the grid,
   no boolean solver.
5. **Panel color assignment.** Per-face-group material ID painting in the
   editor, using the strip groups already established in step 2.

Recommended build order matches this list — steps 1–2 validate the hardest
technical risk (clean procedural topology) before any UI work begins.

---

## Open Decisions to Confirm Before/During Build

- Fixed-unit vs. proportional reveal positions on panel resize (see Data
  Model section).
- Modeless floating editor vs. docked-in-Modify-panel (v1 = floating).
- Whether the editor should re-target automatically on Max selection change
  (nice-to-have, not v1).
- Exact groove profile: is a reveal a simple rectangular inset-carve, or does
  it need a chamfered/beveled edge option later?
- Custom Create-panel category/subcategory name ("RyViz" vs. something more
  specific like "RyViz Precast").

---

## Repo / Solution Notes

- Add as a new sibling project in the existing RyViz-MaxDev solution,
  referencing the shared `RyViz-MaxSDK.props` property sheet, consistent with
  `InsetMod` and other existing plugins.
- Python editor tool ships as a standalone `.py` (PySide) launched via
  `pymxs`, invoked from the C++ rollout button — keep it in its own
  `scripts/` or `python/` folder within the project rather than embedded as a
  resource, to keep iteration fast during development.
