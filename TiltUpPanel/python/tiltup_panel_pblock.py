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
    # pymxs exposes ParamBlock tabs as 0-based sequences.
    for i in range(count):
        reveals.append(
            {
                "axis": int(_tab_value(obj, "revealAxis", i)),
                "pos": float(_tab_value(obj, "revealPos", i)),
            }
        )
    return {
        "width": float(obj.width),
        "height": float(obj.height),
        "depth": float(obj.depth),
        "grooveWidth": float(obj.grooveWidth),
        "grooveDepth": float(obj.grooveDepth),
        "edgeSides": bool(obj.edgeSides),
        "edgeTopBot": bool(obj.edgeTopBot),
        "reveals": reveals,
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
    """Append a reveal. Returns new 0-based index."""
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
