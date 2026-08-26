"""
Modeless PySide reveal-layout editor for RyViz_TiltUpPanel.

Launched from the Modify rollout button or via MAXScript:
  global RyViz_TiltUpPanel_EditNodeHandle = <node handle>
  python.ExecuteFile @"path/to/tilt_up_panel_editor.py"

For development, set RYVIZ_TILTUP_EDITOR to this file's full path, or copy
this folder to  %USERPROFILE%\\Documents\\3ds Max 2027\\scripts\\RyViz\\
"""

from __future__ import annotations

import os
import sys

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

try:
    from PySide2 import QtCore, QtGui, QtWidgets
except ImportError:
    from PySide6 import QtCore, QtGui, QtWidgets

import pymxs

from tiltup_panel_pblock import (
    AXIS_HORIZONTAL,
    AXIS_VERTICAL,
    is_tiltup_panel,
    read_panel,
    write_edge_insets,
    write_groove,
)

rt = pymxs.runtime

_EDITOR_INSTANCE = None


def _scene_y(panel_height: float, z: float) -> float:
    return panel_height - z


class PanelCanvas(QtWidgets.QGraphicsView):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._scene = QtWidgets.QGraphicsScene(self)
        self.setScene(self._scene)
        self.setRenderHint(QtGui.QPainter.Antialiasing, True)
        self.setDragMode(QtWidgets.QGraphicsView.ScrollHandDrag)
        self.setTransformationAnchor(QtWidgets.QGraphicsView.AnchorUnderMouse)
        self.setResizeAnchor(QtWidgets.QGraphicsView.AnchorUnderMouse)

    def draw_panel(self, data: dict) -> None:
        self._scene.clear()
        width = max(data["width"], 1.0e-3)
        height = max(data["height"], 1.0e-3)
        gw = max(data["grooveWidth"], 1.0e-3)
        half_gw = gw * 0.5

        outline = QtWidgets.QGraphicsRectItem(0.0, 0.0, width, height)
        outline.setPen(QtGui.QPen(QtGui.QColor(220, 220, 220), 0.0))
        outline.setBrush(QtGui.QBrush(QtGui.QColor(48, 48, 52)))
        self._scene.addItem(outline)

        edge_pen = QtGui.QPen(QtGui.QColor(120, 180, 255, 180), 0.0, QtCore.Qt.DashLine)
        edge_brush = QtGui.QBrush(QtGui.QColor(120, 180, 255, 40))

        if data["edgeSides"]:
            for x0 in (0.0, width - half_gw):
                band = QtWidgets.QGraphicsRectItem(x0, 0.0, half_gw, height)
                band.setPen(edge_pen)
                band.setBrush(edge_brush)
                self._scene.addItem(band)

        if data["edgeTopBot"]:
            for z_edge in (0.0, height - half_gw):
                y0 = _scene_y(height, z_edge + half_gw)
                band = QtWidgets.QGraphicsRectItem(0.0, y0, width, half_gw)
                band.setPen(edge_pen)
                band.setBrush(edge_brush)
                self._scene.addItem(band)

        reveal_pen = QtGui.QPen(QtGui.QColor(255, 190, 80, 220), 0.0)
        reveal_brush = QtGui.QBrush(QtGui.QColor(255, 190, 80, 70))

        for reveal in data["reveals"]:
            pos = reveal["pos"]
            if reveal["axis"] == AXIS_HORIZONTAL:
                y0 = _scene_y(height, pos + half_gw)
                band = QtWidgets.QGraphicsRectItem(0.0, y0, width, gw)
            else:
                band = QtWidgets.QGraphicsRectItem(pos - half_gw, 0.0, gw, height)
            band.setPen(reveal_pen)
            band.setBrush(reveal_brush)
            self._scene.addItem(band)

        self.setSceneRect(0.0, 0.0, width, height)
        self.fitInView(self._scene.sceneRect(), QtCore.Qt.KeepAspectRatio)


class TiltUpPanelEditorWindow(QtWidgets.QWidget):
    def __init__(self, node):
        super().__init__(None, QtCore.Qt.Window)
        self._node = node
        self._loading = False

        self.setWindowTitle(f"TiltUp Reveal Layout — {node.name}")
        self.resize(720, 520)

        self._canvas = PanelCanvas()
        self._status = QtWidgets.QLabel()
        self._groove_w = self._make_spin(0.0, 1.0e6, 2)
        self._groove_d = self._make_spin(0.0, 1.0e6, 3)
        self._edge_sides = QtWidgets.QCheckBox("Inset L/R")
        self._edge_topbot = QtWidgets.QCheckBox("Inset T/B")
        refresh_btn = QtWidgets.QPushButton("Refresh")
        refresh_btn.clicked.connect(self.reload_from_node)

        self._groove_w.valueChanged.connect(self._on_groove_changed)
        self._groove_d.valueChanged.connect(self._on_groove_changed)
        self._edge_sides.toggled.connect(self._on_edge_changed)
        self._edge_topbot.toggled.connect(self._on_edge_changed)

        form = QtWidgets.QFormLayout()
        form.addRow("Groove W", self._groove_w)
        form.addRow("Groove D", self._groove_d)
        form.addRow(self._edge_sides)
        form.addRow(self._edge_topbot)

        side = QtWidgets.QVBoxLayout()
        side.addLayout(form)
        side.addWidget(refresh_btn)
        side.addStretch(1)
        side.addWidget(self._status)

        layout = QtWidgets.QHBoxLayout(self)
        layout.addWidget(self._canvas, stretch=1)
        layout.addLayout(side)

        self.reload_from_node()

    @staticmethod
    def _make_spin(minimum: float, maximum: float, decimals: int) -> QtWidgets.QDoubleSpinBox:
        spin = QtWidgets.QDoubleSpinBox()
        spin.setRange(minimum, maximum)
        spin.setDecimals(decimals)
        spin.setSingleStep(0.1)
        spin.setKeyboardTracking(False)
        return spin

    def reload_from_node(self) -> None:
        if not self._node or not rt.isValidNode(self._node):
            self._status.setText("Node is no longer valid.")
            return
        if not is_tiltup_panel(self._node):
            self._status.setText("Node is not a RyViz_TiltUpPanel.")
            return

        self._loading = True
        try:
            data = read_panel(self._node)
            self._groove_w.setValue(data["grooveWidth"])
            self._groove_d.setValue(data["grooveDepth"])
            self._edge_sides.setChecked(data["edgeSides"])
            self._edge_topbot.setChecked(data["edgeTopBot"])
            self._canvas.draw_panel(data)
            self._status.setText(
                f"{data['width']:.3g} x {data['height']:.3g}  |  "
                f"{len(data['reveals'])} reveal(s)  |  read-only bands"
            )
        finally:
            self._loading = False

    def _on_groove_changed(self, _value=0.0) -> None:
        if self._loading:
            return
        write_groove(
            self._node,
            groove_width=self._groove_w.value(),
            groove_depth=self._groove_d.value(),
        )
        self.reload_from_node()

    def _on_edge_changed(self, _checked=False) -> None:
        if self._loading:
            return
        write_edge_insets(
            self._node,
            edge_sides=self._edge_sides.isChecked(),
            edge_topbot=self._edge_topbot.isChecked(),
        )
        self.reload_from_node()

    def closeEvent(self, event) -> None:
        global _EDITOR_INSTANCE
        if _EDITOR_INSTANCE is self:
            _EDITOR_INSTANCE = None
        super().closeEvent(event)


def open_editor(node_handle: int) -> None:
    global _EDITOR_INSTANCE

    node = rt.getNodeByHandle(int(node_handle))
    if node is None:
        rt.messageBox("TiltUp panel node not found.", title="RyViz TiltUpPanel")
        return
    if not is_tiltup_panel(node):
        rt.messageBox("Selected node is not a RyViz_TiltUpPanel.", title="RyViz TiltUpPanel")
        return

    if _EDITOR_INSTANCE is not None:
        try:
            _EDITOR_INSTANCE.close()
        except Exception:
            pass
        _EDITOR_INSTANCE = None

    _EDITOR_INSTANCE = TiltUpPanelEditorWindow(node)
    _EDITOR_INSTANCE.show()
    _EDITOR_INSTANCE.raise_()
    _EDITOR_INSTANCE.activateWindow()


def main() -> None:
    handle = getattr(rt, "RyViz_TiltUpPanel_EditNodeHandle", None)
    if handle is not None:
        rt.RyViz_TiltUpPanel_EditNodeHandle = None
        open_editor(int(handle))


if __name__ == "__main__":
    main()
