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


def read_panel(node) -> dict:
    obj = node.baseObject
    reveals = []
    count = _tab_count(obj, "revealAxis")
    for i in range(1, count + 1):
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
    one_based = reveal_index + 1
    with pymxs.undo(True, "TiltUpPanel reveal position"):
        _set_tab_value(obj, "revealPos", one_based, float(pos))
    rt.redrawViews()
