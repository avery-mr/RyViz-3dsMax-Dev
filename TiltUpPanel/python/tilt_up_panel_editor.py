"""
Modeless PySide reveal-layout editor for RyViz_TiltUpPanel.

Launched from the Modify rollout button or via MAXScript:
  global RyViz_TiltUpPanel_EditNodeHandle = <node handle>
  python.ExecuteFile @"path/to/tilt_up_panel_editor.py"

Canvas matches a Front view looking toward -Y: +Z up, +X to the left
(so left/right match the front face as seen from the reveal side).
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
    add_reveal,
    is_tiltup_panel,
    read_panel,
    remove_reveal,
    write_dimensions,
    write_edge_insets,
    write_groove,
    write_reveal_pos,
)

rt = pymxs.runtime

_EDITOR_INSTANCE = None


def _max_main_window():
    try:
        import qtmax

        return qtmax.GetQMaxMainWindow()
    except Exception:
        return None


def _scene_y(panel_height: float, z: float) -> float:
    """Panel Z -> scene Y (Z up)."""
    return panel_height - z


def _scene_x(panel_width: float, x: float) -> float:
    """Panel X -> scene X. Flipped so front (+Y) left/right match the viewport."""
    return panel_width - x


def _panel_x(panel_width: float, scene_x: float) -> float:
    return panel_width - scene_x


def _clamp(value: float, lo: float, hi: float) -> float:
    if hi < lo:
        return lo
    return max(lo, min(hi, value))


class WorldUnitSpinBox(QtWidgets.QDoubleSpinBox):
    """Stores system-unit floats; displays/parses current Max display units."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setRange(0.0, 1.0e30)
        self.setDecimals(6)
        self.setSingleStep(1.0)
        self.setKeyboardTracking(False)

    def textFromValue(self, value: float) -> str:
        try:
            return str(rt.units.formatValue(float(value)))
        except Exception:
            return f"{float(value):.3f}"

    def valueFromText(self, text: str) -> float:
        try:
            return float(rt.units.decodeValue(str(text).strip()))
        except Exception:
            return float(self.value())

    def validate(self, text, pos):
        # Allow unit strings like 10'3", 3.5m while typing.
        cleaned = str(text).strip()
        if not cleaned:
            return (QtGui.QValidator.Intermediate, text, pos)
        try:
            rt.units.decodeValue(cleaned)
            return (QtGui.QValidator.Acceptable, text, pos)
        except Exception:
            return (QtGui.QValidator.Intermediate, text, pos)


class RevealBandItem(QtWidgets.QGraphicsRectItem):
    def __init__(self, index: int, axis: int, pos: float, panel_w: float, panel_h: float, groove_w: float):
        half = max(groove_w, 1.0e-3) * 0.5
        if axis == AXIS_HORIZONTAL:
            super().__init__(0.0, -half, panel_w, max(groove_w, 1.0e-3))
            self.setPos(0.0, _scene_y(panel_h, pos))
            self.setCursor(QtCore.Qt.SizeVerCursor)
        else:
            super().__init__(-half, 0.0, max(groove_w, 1.0e-3), panel_h)
            self.setPos(_scene_x(panel_w, pos), 0.0)
            self.setCursor(QtCore.Qt.SizeHorCursor)

        self.reveal_index = index
        self.axis = axis
        self.panel_w = panel_w
        self.panel_h = panel_h
        self._dragging = False

        self.setFlags(
            QtWidgets.QGraphicsItem.ItemIsSelectable
            | QtWidgets.QGraphicsItem.ItemIsMovable
            | QtWidgets.QGraphicsItem.ItemSendsGeometryChanges
        )
        self.setAcceptHoverEvents(True)
        self.setZValue(10.0)
        self._apply_style(False)

    def _apply_style(self, selected: bool) -> None:
        if selected:
            self.setPen(QtGui.QPen(QtGui.QColor(255, 230, 120), 0.0))
            self.setBrush(QtGui.QBrush(QtGui.QColor(255, 210, 80, 140)))
        else:
            self.setPen(QtGui.QPen(QtGui.QColor(255, 190, 80, 220), 0.0))
            self.setBrush(QtGui.QBrush(QtGui.QColor(255, 190, 80, 70)))

    def itemChange(self, change, value):
        if change == QtWidgets.QGraphicsItem.ItemSelectedHasChanged:
            self._apply_style(bool(value))
        if change == QtWidgets.QGraphicsItem.ItemPositionChange and self.scene():
            new_pos = QtCore.QPointF(value)
            if self.axis == AXIS_HORIZONTAL:
                new_pos.setX(0.0)
                new_pos.setY(_clamp(new_pos.y(), 0.0, self.panel_h))
            else:
                new_pos.setY(0.0)
                new_pos.setX(_clamp(new_pos.x(), 0.0, self.panel_w))
            return new_pos
        return super().itemChange(change, value)

    def panel_pos(self) -> float:
        if self.axis == AXIS_HORIZONTAL:
            return float(self.panel_h - self.pos().y())
        return float(_panel_x(self.panel_w, self.pos().x()))

    def mousePressEvent(self, event) -> None:
        self._dragging = True
        super().mousePressEvent(event)

    def mouseReleaseEvent(self, event) -> None:
        was_dragging = self._dragging
        self._dragging = False
        super().mouseReleaseEvent(event)
        if was_dragging:
            view = self.scene().views()[0] if self.scene() and self.scene().views() else None
            if isinstance(view, PanelCanvas):
                view.bandMoved.emit(self.reveal_index, self.panel_pos())


class PanelCanvas(QtWidgets.QGraphicsView):
    selectionChanged = QtCore.Signal(int)  # -1 if none
    bandMoved = QtCore.Signal(int, float)
    placeRequested = QtCore.Signal(float, float)  # panel x, panel z

    def __init__(self, parent=None):
        super().__init__(parent)
        self._scene = QtWidgets.QGraphicsScene(self)
        self.setScene(self._scene)
        self.setRenderHint(QtGui.QPainter.Antialiasing, True)
        self.setDragMode(QtWidgets.QGraphicsView.NoDrag)
        self.setTransformationAnchor(QtWidgets.QGraphicsView.AnchorUnderMouse)
        self.setResizeAnchor(QtWidgets.QGraphicsView.AnchorUnderMouse)
        self.setBackgroundBrush(QtGui.QBrush(QtGui.QColor(36, 36, 40)))

        self._panel_w = 1.0
        self._panel_h = 1.0
        self._fitted = False
        self._place_axis = None
        self._bands = []
        self._panning = False

        self._scene.selectionChanged.connect(self._on_scene_selection_changed)

    def set_place_axis(self, axis) -> None:
        self._place_axis = axis
        if axis is None:
            self.viewport().unsetCursor()
        elif axis == AXIS_HORIZONTAL:
            self.viewport().setCursor(QtCore.Qt.SizeVerCursor)
        else:
            self.viewport().setCursor(QtCore.Qt.SizeHorCursor)

    def draw_panel(self, data: dict, selected_index: int = -1, fit: bool = False) -> None:
        self._scene.blockSignals(True)
        self._scene.clear()
        self._bands = []

        width = max(data["width"], 1.0e-3)
        height = max(data["height"], 1.0e-3)
        gw = max(data["grooveWidth"], 1.0e-3)
        half_gw = gw * 0.5
        self._panel_w = width
        self._panel_h = height

        outline = QtWidgets.QGraphicsRectItem(0.0, 0.0, width, height)
        outline.setPen(QtGui.QPen(QtGui.QColor(220, 220, 220), 0.0))
        outline.setBrush(QtGui.QBrush(QtGui.QColor(48, 48, 52)))
        outline.setZValue(0.0)
        outline.setFlag(QtWidgets.QGraphicsItem.ItemIsSelectable, False)
        self._scene.addItem(outline)

        edge_pen = QtGui.QPen(QtGui.QColor(120, 180, 255, 180), 0.0, QtCore.Qt.DashLine)
        edge_brush = QtGui.QBrush(QtGui.QColor(120, 180, 255, 40))

        if data["edgeSides"]:
            # Edge strips at panel X=0 and X=width (half-width), mapped through flipped X.
            for panel_left, panel_right in ((0.0, half_gw), (width - half_gw, width)):
                sx0 = _scene_x(width, panel_right)
                sx1 = _scene_x(width, panel_left)
                x0 = min(sx0, sx1)
                band = QtWidgets.QGraphicsRectItem(x0, 0.0, abs(sx1 - sx0), height)
                band.setPen(edge_pen)
                band.setBrush(edge_brush)
                band.setZValue(1.0)
                band.setFlag(QtWidgets.QGraphicsItem.ItemIsSelectable, False)
                self._scene.addItem(band)

        if data["edgeTopBot"]:
            for z_edge in (0.0, height - half_gw):
                y0 = _scene_y(height, z_edge + half_gw)
                band = QtWidgets.QGraphicsRectItem(0.0, y0, width, half_gw)
                band.setPen(edge_pen)
                band.setBrush(edge_brush)
                band.setZValue(1.0)
                band.setFlag(QtWidgets.QGraphicsItem.ItemIsSelectable, False)
                self._scene.addItem(band)

        for i, reveal in enumerate(data["reveals"]):
            item = RevealBandItem(i, reveal["axis"], reveal["pos"], width, height, gw)
            self._scene.addItem(item)
            self._bands.append(item)
            if i == selected_index:
                item.setSelected(True)

        self.setSceneRect(-gw, -gw, width + 2.0 * gw, height + 2.0 * gw)
        self._scene.blockSignals(False)

        if fit or not self._fitted:
            self.fitInView(0.0, 0.0, width, height, QtCore.Qt.KeepAspectRatio)
            self._fitted = True

        self._on_scene_selection_changed()

    def _on_scene_selection_changed(self) -> None:
        selected = [b for b in self._bands if b.isSelected()]
        if len(selected) == 1:
            self.selectionChanged.emit(selected[0].reveal_index)
        else:
            self.selectionChanged.emit(-1)

    def mousePressEvent(self, event) -> None:
        if event.button() == QtCore.Qt.LeftButton and self._place_axis is not None:
            scene_pt = self.mapToScene(event.pos())
            z = _clamp(self._panel_h - scene_pt.y(), 0.0, self._panel_h)
            x = _clamp(_panel_x(self._panel_w, scene_pt.x()), 0.0, self._panel_w)
            self.placeRequested.emit(x, z)
            event.accept()
            return
        if event.button() == QtCore.Qt.MiddleButton:
            self._panning = True
            self._pan_last = event.pos()
            self.viewport().setCursor(QtCore.Qt.ClosedHandCursor)
            event.accept()
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event) -> None:
        if self._panning:
            delta = event.pos() - self._pan_last
            self._pan_last = event.pos()
            self.horizontalScrollBar().setValue(self.horizontalScrollBar().value() - delta.x())
            self.verticalScrollBar().setValue(self.verticalScrollBar().value() - delta.y())
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event) -> None:
        if event.button() == QtCore.Qt.MiddleButton and self._panning:
            self._panning = False
            self.viewport().unsetCursor()
            event.accept()
            return
        super().mouseReleaseEvent(event)

    def wheelEvent(self, event) -> None:
        delta = event.angleDelta().y()
        if delta == 0:
            return
        factor = 1.15 if delta > 0 else 1.0 / 1.15
        self.scale(factor, factor)


class TiltUpPanelEditorWindow(QtWidgets.QWidget):
    def __init__(self, node):
        parent = _max_main_window()
        super().__init__(parent, QtCore.Qt.Tool)
        self.setWindowFlag(QtCore.Qt.WindowStaysOnTopHint, True)

        self._node = node
        self._loading = False
        self._selected = -1
        self._data = None

        self.setWindowTitle(f"TiltUp Reveal Layout — {node.name}")
        self.resize(820, 600)

        self._canvas = PanelCanvas()
        self._status = QtWidgets.QLabel()
        self._status.setWordWrap(True)

        self._width = WorldUnitSpinBox()
        self._height = WorldUnitSpinBox()
        self._depth = WorldUnitSpinBox()
        self._groove_w = WorldUnitSpinBox()
        self._groove_d = WorldUnitSpinBox()
        self._edge_sides = QtWidgets.QCheckBox("Inset L/R")
        self._edge_topbot = QtWidgets.QCheckBox("Inset T/B")
        self._pos_spin = WorldUnitSpinBox()
        self._pos_spin.setEnabled(False)
        self._pct_spin = QtWidgets.QDoubleSpinBox()
        self._pct_spin.setRange(0.0, 100.0)
        self._pct_spin.setDecimals(2)
        self._pct_spin.setSingleStep(1.0)
        self._pct_spin.setSuffix(" %")
        self._pct_spin.setKeyboardTracking(False)
        self._pct_spin.setEnabled(False)

        self._btn_add_h = QtWidgets.QPushButton("Add Horiz")
        self._btn_add_v = QtWidgets.QPushButton("Add Vert")
        self._btn_place_h = QtWidgets.QPushButton("Place Horiz…")
        self._btn_place_v = QtWidgets.QPushButton("Place Vert…")
        self._btn_remove = QtWidgets.QPushButton("Remove")
        self._btn_refresh = QtWidgets.QPushButton("Refresh")
        self._btn_remove.setEnabled(False)

        self._btn_pct_25 = QtWidgets.QPushButton("25%")
        self._btn_pct_50 = QtWidgets.QPushButton("50%")
        self._btn_pct_75 = QtWidgets.QPushButton("75%")
        for b in (self._btn_pct_25, self._btn_pct_50, self._btn_pct_75):
            b.setEnabled(False)

        self._width.valueChanged.connect(self._on_dims_changed)
        self._height.valueChanged.connect(self._on_dims_changed)
        self._depth.valueChanged.connect(self._on_dims_changed)
        self._groove_w.valueChanged.connect(self._on_groove_changed)
        self._groove_d.valueChanged.connect(self._on_groove_changed)
        self._edge_sides.toggled.connect(self._on_edge_changed)
        self._edge_topbot.toggled.connect(self._on_edge_changed)
        self._pos_spin.valueChanged.connect(self._on_pos_changed)
        self._pct_spin.valueChanged.connect(self._on_pct_changed)
        self._btn_add_h.clicked.connect(lambda: self._add_reveal(AXIS_HORIZONTAL))
        self._btn_add_v.clicked.connect(lambda: self._add_reveal(AXIS_VERTICAL))
        self._btn_place_h.clicked.connect(lambda: self._begin_place(AXIS_HORIZONTAL))
        self._btn_place_v.clicked.connect(lambda: self._begin_place(AXIS_VERTICAL))
        self._btn_remove.clicked.connect(self._remove_selected)
        self._btn_refresh.clicked.connect(lambda: self.reload_from_node(fit=False))
        self._btn_pct_25.clicked.connect(lambda: self._set_pct(25.0))
        self._btn_pct_50.clicked.connect(lambda: self._set_pct(50.0))
        self._btn_pct_75.clicked.connect(lambda: self._set_pct(75.0))

        self._canvas.selectionChanged.connect(self._on_canvas_selection)
        self._canvas.bandMoved.connect(self._on_band_moved)
        self._canvas.placeRequested.connect(self._on_place_at)

        form = QtWidgets.QFormLayout()
        form.addRow("Width", self._width)
        form.addRow("Height", self._height)
        form.addRow("Depth", self._depth)
        form.addRow("Groove W", self._groove_w)
        form.addRow("Groove D", self._groove_d)
        form.addRow(self._edge_sides)
        form.addRow(self._edge_topbot)
        form.addRow("Position", self._pos_spin)
        form.addRow("Percent", self._pct_spin)

        pct_row = QtWidgets.QHBoxLayout()
        pct_row.addWidget(self._btn_pct_25)
        pct_row.addWidget(self._btn_pct_50)
        pct_row.addWidget(self._btn_pct_75)

        add_row = QtWidgets.QHBoxLayout()
        add_row.addWidget(self._btn_add_h)
        add_row.addWidget(self._btn_add_v)

        place_row = QtWidgets.QHBoxLayout()
        place_row.addWidget(self._btn_place_h)
        place_row.addWidget(self._btn_place_v)

        side = QtWidgets.QVBoxLayout()
        side.addLayout(form)
        side.addLayout(pct_row)
        side.addLayout(add_row)
        side.addLayout(place_row)
        side.addWidget(self._btn_remove)
        side.addWidget(self._btn_refresh)
        side.addStretch(1)
        side.addWidget(self._status)

        layout = QtWidgets.QHBoxLayout(self)
        layout.addWidget(self._canvas, stretch=1)
        layout.addLayout(side)

        self.reload_from_node(fit=True)

    def reload_from_node(self, fit: bool = False) -> None:
        if not self._node or not rt.isValidNode(self._node):
            self._status.setText("Node is no longer valid.")
            return
        if not is_tiltup_panel(self._node):
            self._status.setText("Node is not a RyViz_TiltUpPanel.")
            return

        self._loading = True
        try:
            data = read_panel(self._node)
            self._data = data
            self._width.setValue(data["width"])
            self._height.setValue(data["height"])
            self._depth.setValue(data["depth"])
            self._groove_w.setValue(data["grooveWidth"])
            self._groove_d.setValue(data["grooveDepth"])
            self._edge_sides.setChecked(data["edgeSides"])
            self._edge_topbot.setChecked(data["edgeTopBot"])

            if self._selected >= len(data["reveals"]):
                self._selected = len(data["reveals"]) - 1

            self._canvas.draw_panel(data, selected_index=self._selected, fit=fit)
            self._sync_pos_controls()
            place = self._canvas._place_axis
            place_txt = ""
            if place == AXIS_HORIZONTAL:
                place_txt = "  |  click to place H"
            elif place == AXIS_VERTICAL:
                place_txt = "  |  click to place V"
            self._status.setText(
                f"{rt.units.formatValue(data['width'])} × {rt.units.formatValue(data['height'])}  |  "
                f"{len(data['reveals'])} reveal(s)  |  front view (+X left)  |  "
                f"drag · MMB pan · wheel zoom{place_txt}"
            )
        finally:
            self._loading = False

    def _selected_limit(self) -> float:
        reveal = self._data["reveals"][self._selected]
        return self._data["height"] if reveal["axis"] == AXIS_HORIZONTAL else self._data["width"]

    def _sync_pos_controls(self) -> None:
        has = self._data is not None and 0 <= self._selected < len(self._data["reveals"])
        self._pos_spin.setEnabled(has)
        self._pct_spin.setEnabled(has)
        self._btn_remove.setEnabled(has)
        for b in (self._btn_pct_25, self._btn_pct_50, self._btn_pct_75):
            b.setEnabled(has)
        if not has:
            return
        reveal = self._data["reveals"][self._selected]
        limit = max(self._selected_limit(), 1.0e-6)
        self._pos_spin.setRange(0.0, limit)
        self._pos_spin.setValue(reveal["pos"])
        self._pct_spin.setValue(100.0 * reveal["pos"] / limit)

    def _on_canvas_selection(self, index: int) -> None:
        if self._loading:
            return
        self._selected = index
        self._loading = True
        try:
            self._sync_pos_controls()
        finally:
            self._loading = False

    def _on_dims_changed(self, _value=0.0) -> None:
        if self._loading:
            return
        write_dimensions(
            self._node,
            width=self._width.value(),
            height=self._height.value(),
            depth=self._depth.value(),
        )
        self.reload_from_node(fit=False)

    def _on_groove_changed(self, _value=0.0) -> None:
        if self._loading:
            return
        write_groove(
            self._node,
            groove_width=self._groove_w.value(),
            groove_depth=self._groove_d.value(),
        )
        self.reload_from_node(fit=False)

    def _on_edge_changed(self, _checked=False) -> None:
        if self._loading:
            return
        write_edge_insets(
            self._node,
            edge_sides=self._edge_sides.isChecked(),
            edge_topbot=self._edge_topbot.isChecked(),
        )
        self.reload_from_node(fit=False)

    def _write_selected_pos(self, pos: float) -> None:
        if self._selected < 0:
            return
        limit = self._selected_limit() if self._data else pos
        pos = _clamp(float(pos), 0.0, max(limit, 0.0))
        write_reveal_pos(self._node, self._selected, pos)
        self.reload_from_node(fit=False)

    def _on_pos_changed(self, value: float) -> None:
        if self._loading or self._selected < 0:
            return
        self._write_selected_pos(value)

    def _on_pct_changed(self, value: float) -> None:
        if self._loading or self._selected < 0 or not self._data:
            return
        limit = self._selected_limit()
        self._write_selected_pos(limit * float(value) / 100.0)

    def _set_pct(self, pct: float) -> None:
        if self._selected < 0:
            return
        self._pct_spin.setValue(pct)

    def _on_band_moved(self, index: int, pos: float) -> None:
        if self._loading:
            return
        self._selected = index
        write_reveal_pos(self._node, index, float(pos))
        self.reload_from_node(fit=False)

    def _add_reveal(self, axis: int) -> None:
        self._canvas.set_place_axis(None)
        self._selected = add_reveal(self._node, axis)
        self.reload_from_node(fit=False)

    def _begin_place(self, axis: int) -> None:
        self._canvas.set_place_axis(axis)
        label = "H" if axis == AXIS_HORIZONTAL else "V"
        self._status.setText(f"Click canvas to place {label} reveal (Esc cancels)")

    def _on_place_at(self, x: float, z: float) -> None:
        axis = self._canvas._place_axis
        if axis is None:
            return
        pos = z if axis == AXIS_HORIZONTAL else x
        self._canvas.set_place_axis(None)
        self._selected = add_reveal(self._node, axis, pos)
        self.reload_from_node(fit=False)

    def _remove_selected(self) -> None:
        if self._selected < 0:
            return
        remove_reveal(self._node, self._selected)
        self._selected = -1
        self._canvas.set_place_axis(None)
        self.reload_from_node(fit=False)

    def keyPressEvent(self, event) -> None:
        if event.key() == QtCore.Qt.Key_Escape:
            self._canvas.set_place_axis(None)
            self.reload_from_node(fit=False)
            return
        if event.key() in (QtCore.Qt.Key_Delete, QtCore.Qt.Key_Backspace):
            self._remove_selected()
            return
        super().keyPressEvent(event)

    def closeEvent(self, event) -> None:
        global _EDITOR_INSTANCE
        if _EDITOR_INSTANCE is self:
            _EDITOR_INSTANCE = None
        super().closeEvent(event)


def open_editor(node_handle: int) -> None:
    global _EDITOR_INSTANCE

    node = rt.maxOps.getNodeByHandle(int(node_handle))
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
        return

    if rt.selection.count >= 1:
        node = rt.selection[1]
        if is_tiltup_panel(node):
            open_editor(int(node.handle))
            return
    rt.messageBox(
        "No TiltUp panel target. Select a panel or launch from Edit Reveal Layout.",
        title="RyViz TiltUpPanel",
    )


try:
    main()
except Exception as ex:
    import traceback

    traceback.print_exc()
    try:
        rt.messageBox(str(ex), title="RyViz TiltUpPanel")
    except Exception:
        pass
