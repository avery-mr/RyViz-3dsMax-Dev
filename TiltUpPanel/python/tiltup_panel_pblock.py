"""pymxs read/write helpers for RyViz_TiltUpPanel ParamBlock2."""

from __future__ import annotations

import pymxs

rt = pymxs.runtime

AXIS_HORIZONTAL = 0
AXIS_VERTICAL = 1


def is_tiltup_panel(node) -> bool:
    try:
        return rt.classOf(node.baseObject) == rt.RyViz_TiltUpPanel
    except Exception:
        return False


def _tab_count(obj, tab_name: str) -> int:
    tab = getattr(obj, tab_name)
    return int(tab.count)


def _tab_value(obj, tab_name: str, index: int):
    tab = getattr(obj, tab_name)
    return tab[index]


def _set_tab_value(obj, tab_name: str, index: int, value) -> None:
    tab = getattr(obj, tab_name)
    tab[index] = value


def _set_tab_count(obj, tab_name: str, count: int) -> None:
    tab = getattr(obj, tab_name)
    tab.count = int(count)


def read_panel(node) -> dict:
    obj = node.baseObject
    reveals = []
    count = _tab_count(obj, "revealAxis")
    for i in range(count):
        reveals.append(
            {
                "axis": int(_tab_value(obj, "revealAxis", i)),
                "pos": float(_tab_value(obj, "revealPos", i)),
            }
        )

    openings = []
    n_open = min(
        _tab_count(obj, "openingX"),
        _tab_count(obj, "openingZ"),
        _tab_count(obj, "openingW"),
        _tab_count(obj, "openingH"),
    )
    for i in range(n_open):
        openings.append(
            {
                "x": float(_tab_value(obj, "openingX", i)),
                "z": float(_tab_value(obj, "openingZ", i)),
                "w": float(_tab_value(obj, "openingW", i)),
                "h": float(_tab_value(obj, "openingH", i)),
            }
        )

    colors = []
    try:
        n_col = _tab_count(obj, "panelColors")
        for i in range(n_col):
            c = _tab_value(obj, "panelColors", i)
            colors.append((float(c.x), float(c.y), float(c.z)))
    except Exception:
        colors = []

    return {
        "width": float(obj.width),
        "height": float(obj.height),
        "depth": float(obj.depth),
        "grooveWidth": float(obj.grooveWidth),
        "grooveDepth": float(obj.grooveDepth),
        "edgeSides": bool(obj.edgeSides),
        "edgeTopBot": bool(obj.edgeTopBot),
        "reveals": reveals,
        "openings": openings,
        "panelColors": colors,
    }


def write_dimensions(node, width=None, height=None, depth=None) -> None:
    obj = node.baseObject
    with pymxs.undo(True, "TiltUpPanel dimensions"):
        if width is not None:
            obj.width = float(width)
        if height is not None:
            obj.height = float(height)
        if depth is not None:
            obj.depth = float(depth)
    rt.redrawViews()


def write_groove(node, groove_width=None, groove_depth=None) -> None:
    obj = node.baseObject
    with pymxs.undo(True, "TiltUpPanel groove"):
        if groove_width is not None:
            obj.grooveWidth = float(groove_width)
        if groove_depth is not None:
            obj.grooveDepth = float(groove_depth)
    rt.redrawViews()


def write_edge_insets(node, edge_sides=None, edge_topbot=None) -> None:
    obj = node.baseObject
    with pymxs.undo(True, "TiltUpPanel edge insets"):
        if edge_sides is not None:
            obj.edgeSides = bool(edge_sides)
        if edge_topbot is not None:
            obj.edgeTopBot = bool(edge_topbot)
    rt.redrawViews()


def write_reveal_pos(node, reveal_index: int, pos: float) -> None:
    obj = node.baseObject
    count = _tab_count(obj, "revealPos")
    if reveal_index < 0 or reveal_index >= count:
        return
    with pymxs.undo(True, "TiltUpPanel reveal position"):
        _set_tab_value(obj, "revealPos", reveal_index, float(pos))
    rt.redrawViews()


def add_reveal(node, axis: int, pos: float | None = None) -> int:
    obj = node.baseObject
    data = read_panel(node)
    if pos is None:
        pos = data["height"] * 0.5 if axis == AXIS_HORIZONTAL else data["width"] * 0.5
    n = len(data["reveals"])
    with pymxs.undo(True, "TiltUpPanel add reveal"):
        _set_tab_count(obj, "revealAxis", n + 1)
        _set_tab_count(obj, "revealPos", n + 1)
        _set_tab_value(obj, "revealAxis", n, int(axis))
        _set_tab_value(obj, "revealPos", n, float(pos))
    rt.redrawViews()
    return n


def remove_reveal(node, reveal_index: int) -> None:
    obj = node.baseObject
    data = read_panel(node)
    n = len(data["reveals"])
    if reveal_index < 0 or reveal_index >= n:
        return
    axes = [r["axis"] for i, r in enumerate(data["reveals"]) if i != reveal_index]
    poses = [r["pos"] for i, r in enumerate(data["reveals"]) if i != reveal_index]
    with pymxs.undo(True, "TiltUpPanel remove reveal"):
        _set_tab_count(obj, "revealAxis", len(axes))
        _set_tab_count(obj, "revealPos", len(poses))
        for i, (axis, pos) in enumerate(zip(axes, poses)):
            _set_tab_value(obj, "revealAxis", i, int(axis))
            _set_tab_value(obj, "revealPos", i, float(pos))
    rt.redrawViews()


def _write_opening_tabs(obj, openings: list[dict]) -> None:
    n = len(openings)
    _set_tab_count(obj, "openingX", n)
    _set_tab_count(obj, "openingZ", n)
    _set_tab_count(obj, "openingW", n)
    _set_tab_count(obj, "openingH", n)
    for i, op in enumerate(openings):
        _set_tab_value(obj, "openingX", i, float(op["x"]))
        _set_tab_value(obj, "openingZ", i, float(op["z"]))
        _set_tab_value(obj, "openingW", i, float(op["w"]))
        _set_tab_value(obj, "openingH", i, float(op["h"]))


def add_opening(node, x: float, z: float, w: float, h: float) -> int:
    obj = node.baseObject
    data = read_panel(node)
    openings = list(data["openings"])
    openings.append({"x": float(x), "z": float(z), "w": float(w), "h": float(h)})
    with pymxs.undo(True, "TiltUpPanel add opening"):
        _write_opening_tabs(obj, openings)
    rt.redrawViews()
    return len(openings) - 1


def write_opening(node, index: int, x: float, z: float, w: float, h: float) -> None:
    obj = node.baseObject
    data = read_panel(node)
    if index < 0 or index >= len(data["openings"]):
        return
    openings = list(data["openings"])
    openings[index] = {"x": float(x), "z": float(z), "w": float(w), "h": float(h)}
    with pymxs.undo(True, "TiltUpPanel edit opening"):
        _write_opening_tabs(obj, openings)
    rt.redrawViews()


def remove_opening(node, index: int) -> None:
    obj = node.baseObject
    data = read_panel(node)
    if index < 0 or index >= len(data["openings"]):
        return
    openings = [op for i, op in enumerate(data["openings"]) if i != index]
    with pymxs.undo(True, "TiltUpPanel remove opening"):
        _write_opening_tabs(obj, openings)
    rt.redrawViews()


def _unique_sorted(values: list[float], lo: float, hi: float) -> list[float]:
    pts = [lo, hi]
    pts.extend(values)
    pts = sorted(pts)
    out = []
    for v in pts:
        if v < lo - 1.0e-6 or v > hi + 1.0e-6:
            continue
        v = max(lo, min(hi, v))
        if not out or abs(v - out[-1]) > 1.0e-4:
            out.append(v)
    if not out or abs(out[0] - lo) > 1.0e-4:
        out.insert(0, lo)
    if abs(out[-1] - hi) > 1.0e-4:
        out.append(hi)
    return out


def region_dividers(data: dict) -> tuple[list[float], list[float]]:
    """Match C++ BuildRegionDividers (includes edge-inset reveals)."""
    width = float(data["width"])
    height = float(data["height"])
    vx_src = []
    hz_src = []
    for r in data["reveals"]:
        if r["axis"] == AXIS_HORIZONTAL:
            hz_src.append(float(r["pos"]))
        else:
            vx_src.append(float(r["pos"]))
    if data.get("edgeSides"):
        vx_src.extend([0.0, width])
    if data.get("edgeTopBot"):
        hz_src.extend([0.0, height])
    return _unique_sorted(vx_src, 0.0, width), _unique_sorted(hz_src, 0.0, height)


def region_count(data: dict) -> int:
    vx, hz = region_dividers(data)
    return max(len(vx) - 1, 1) * max(len(hz) - 1, 1)


def list_regions(data: dict) -> list[dict]:
    """Return paint regions: id, x, z, w, h in panel space."""
    vx, hz = region_dividers(data)
    nx = max(len(vx) - 1, 1)
    regions = []
    for iz in range(max(len(hz) - 1, 0)):
        for ix in range(nx):
            rid = iz * nx + ix
            x0, x1 = vx[ix], vx[ix + 1]
            z0, z1 = hz[iz], hz[iz + 1]
            regions.append(
                {
                    "id": rid,
                    "x": x0,
                    "z": z0,
                    "w": x1 - x0,
                    "h": z1 - z0,
                }
            )
    return regions


def ensure_panel_colors(node) -> list[tuple[float, float, float]]:
    """Resize panelColors to current region count; return RGB tuples 0-1."""
    obj = node.baseObject
    data = read_panel(node)
    n = region_count(data)
    colors = list(data.get("panelColors", []))
    gray = (0.55, 0.55, 0.55)
    while len(colors) < n:
        colors.append(gray)
    if len(colors) > n:
        colors = colors[:n]
    if _tab_count(obj, "panelColors") != n:
        with pymxs.undo(True, "TiltUpPanel panel colors resize"):
            _set_tab_count(obj, "panelColors", n)
            for i, c in enumerate(colors):
                _set_tab_value(obj, "panelColors", i, rt.Point3(c[0], c[1], c[2]))
        rt.redrawViews()
    return colors


def write_panel_color(node, region_id: int, rgb: tuple[float, float, float]) -> None:
    obj = node.baseObject
    colors = ensure_panel_colors(node)
    if region_id < 0 or region_id >= len(colors):
        return
    with pymxs.undo(True, "TiltUpPanel paint panel"):
        _set_tab_value(
            obj,
            "panelColors",
            region_id,
            rt.Point3(float(rgb[0]), float(rgb[1]), float(rgb[2])),
        )
    rt.redrawViews()
