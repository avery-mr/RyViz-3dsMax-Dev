"""Quick listener test: read/write groove params on the current selection."""

from __future__ import annotations

import os
import sys

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

import pymxs

from tiltup_panel_pblock import is_tiltup_panel, read_panel, write_groove

rt = pymxs.runtime


def run() -> None:
    if rt.selection.count < 1:
        rt.messageBox("Select a RyViz_TiltUpPanel node first.")
        return
    node = rt.selection[1]
    if not is_tiltup_panel(node):
        rt.messageBox("Selection is not a RyViz_TiltUpPanel.")
        return

    before = read_panel(node)
    print("Before:", before)
    write_groove(node, groove_width=before["grooveWidth"], groove_depth=before["grooveDepth"])
    after = read_panel(node)
    print("After:", after)
    rt.messageBox("pblock round-trip OK. See MAXScript Listener for dump.")


if __name__ == "__main__":
    run()
