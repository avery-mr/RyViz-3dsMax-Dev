"""
Modeless PySide reveal/opening layout editor for RyViz_TiltUpPanel.

Canvas matches a Front view looking toward -Y: +Z up, +X to the left.
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
    add_opening,
    add_reveal,
    ensure_panel_colors,
    is_tiltup_panel,
    list_regions,
    read_panel,
    remove_opening,
    remove_reveal,
    write_dimensions,
    write_edge_insets,
    write_groove,
    write_opening,
    write_panel_color,
    write_reveal_pos,
)

rt = pymxs.runtime

_EDITOR_INSTANCE = None

# Selection kinds
SEL_NONE = 0
SEL_REVEAL = 1
SEL_OPENING = 2
SEL_REGION = 3


def _max_main_window():
    try:
        import qtmax

        return qtmax.GetQMaxMainWindow()
    except Exception:
        return None


def _scene_y(panel_height: float, z: float) -> float:
    return panel_height - z


def _scene_x(panel_width: float, x: float) -> float:
    return panel_width - x


def _panel_x(panel_width: float, scene_x: float) -> float:
    return panel_width - scene_x


def _clamp(value: float, lo: float, hi: float) -> float:
    if hi < lo:
        return lo
    return max(lo, min(hi, value))


class WorldUnitSpinBox(QtWidgets.QDoubleSpinBox):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setRange(-1.0e30, 1.0e30)
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

        self.kind = SEL_REVEAL
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
                view.revealMoved.emit(self.reveal_index, self.panel_pos())


class OpeningItem(QtWidgets.QGraphicsRectItem):
    """Opening rect in scene space (X-flipped). Supports move + edge resize."""

    def __init__(self, index: int, x: float, z: float, w: float, h: float, panel_w: float, panel_h: float):
        super().__init__()
        self.kind = SEL_OPENING
        self.opening_index = index
        self.panel_w = panel_w
        self.panel_h = panel_h
        self._mode = None
        self._press_panel = None
        self._orig = None  # left, right, bottom, top

        self.setFlags(QtWidgets.QGraphicsItem.ItemIsSelectable)
        self.setAcceptHoverEvents(True)
        self.setZValue(8.0)
        self._set_from_panel(x, z, w, h)
        self._apply_style(False)

    def _set_from_panel(self, x: float, z: float, w: float, h: float) -> None:
        sx0 = _scene_x(self.panel_w, x + w)
        sx1 = _scene_x(self.panel_w, x)
        sy0 = _scene_y(self.panel_h, z + h)
        sy1 = _scene_y(self.panel_h, z)
        left = min(sx0, sx1)
        top = min(sy0, sy1)
        self.setPos(left, top)
        self.setRect(0.0, 0.0, abs(sx1 - sx0), abs(sy1 - sy0))

    def _apply_style(self, selected: bool) -> None:
        if selected:
            self.setPen(QtGui.QPen(QtGui.QColor(120, 220, 255), 0.0))
            self.setBrush(QtGui.QBrush(QtGui.QColor(80, 160, 220, 50)))
        else:
            self.setPen(QtGui.QPen(QtGui.QColor(90, 180, 230, 220), 0.0, QtCore.Qt.DashLine))
            self.setBrush(QtGui.QBrush(QtGui.QColor(60, 140, 200, 35)))

    def itemChange(self, change, value):
        if change == QtWidgets.QGraphicsItem.ItemSelectedHasChanged:
            self._apply_style(bool(value))
        return super().itemChange(change, value)

    def panel_rect(self) -> tuple[float, float, float, float]:
        r = self.rect()
        p = self.pos()
        scene_left = p.x()
        scene_top = p.y()
        scene_right = scene_left + r.width()
        scene_bottom = scene_top + r.height()
        # Scene left = panel right; scene right = panel left (X flip).
        left = _panel_x(self.panel_w, scene_right)
        right = _panel_x(self.panel_w, scene_left)
        bottom = self.panel_h - scene_bottom
        top = self.panel_h - scene_top
        x = min(left, right)
        z = min(bottom, top)
        return (x, z, abs(right - left), abs(top - bottom))

    def _hit_thresh(self) -> float:
        view = self.scene().views()[0] if self.scene() and self.scene().views() else None
        if view is not None:
            # ~8px in scene units.
            return abs(view.mapToScene(8, 0).x() - view.mapToScene(0, 0).x())
        return max(self.panel_w, self.panel_h) * 0.02

    def _hit_mode(self, local_pt: QtCore.QPointF) -> str:
        r = self.rect()
        t = self._hit_thresh()
        on_left = abs(local_pt.x() - 0.0) <= t
        on_right = abs(local_pt.x() - r.width()) <= t
        on_top = abs(local_pt.y() - 0.0) <= t
        on_bot = abs(local_pt.y() - r.height()) <= t
        # Scene edges map to flipped panel edges.
        if on_left and not on_top and not on_bot:
            return "right"  # panel right
        if on_right and not on_top and not on_bot:
            return "left"  # panel left
        if on_top and not on_left and not on_right:
            return "top"
        if on_bot and not on_left and not on_right:
            return "bottom"
        return "move"

    def hoverMoveEvent(self, event) -> None:
        mode = self._hit_mode(event.pos())
        cursors = {
            "left": QtCore.Qt.SizeHorCursor,
            "right": QtCore.Qt.SizeHorCursor,
            "top": QtCore.Qt.SizeVerCursor,
            "bottom": QtCore.Qt.SizeVerCursor,
            "move": QtCore.Qt.SizeAllCursor,
        }
        self.setCursor(cursors.get(mode, QtCore.Qt.ArrowCursor))
        super().hoverMoveEvent(event)

    def mousePressEvent(self, event) -> None:
        if event.button() != QtCore.Qt.LeftButton:
            super().mousePressEvent(event)
            return
        self.setSelected(True)
        self._mode = self._hit_mode(event.pos())
        scene_pt = self.mapToScene(event.pos())
        px = _panel_x(self.panel_w, scene_pt.x())
        pz = self.panel_h - scene_pt.y()
        self._press_panel = (px, pz)
        x, z, w, h = self.panel_rect()
        self._orig = (x, x + w, z, z + h)  # left, right, bottom, top
        event.accept()

    def mouseMoveEvent(self, event) -> None:
        if self._mode is None or self._orig is None or self._press_panel is None:
            super().mouseMoveEvent(event)
            return
        scene_pt = self.mapToScene(event.pos())
        px = _panel_x(self.panel_w, scene_pt.x())
        pz = self.panel_h - scene_pt.y()
        dx = px - self._press_panel[0]
        dz = pz - self._press_panel[1]
        left, right, bottom, top = self._orig
        min_size = max(self.panel_w, self.panel_h) * 1.0e-4

        if self._mode == "move":
            left += dx
            right += dx
            bottom += dz
            top += dz
        elif self._mode == "left":
            left = min(self._orig[0] + dx, right - min_size)
        elif self._mode == "right":
            right = max(self._orig[1] + dx, left + min_size)
        elif self._mode == "bottom":
            bottom = min(self._orig[2] + dz, top - min_size)
        elif self._mode == "top":
            top = max(self._orig[3] + dz, bottom + min_size)

        self._set_from_panel(left, bottom, right - left, top - bottom)
        event.accept()

    def mouseReleaseEvent(self, event) -> None:
        if self._mode is not None:
            view = self.scene().views()[0] if self.scene() and self.scene().views() else None
            if isinstance(view, PanelCanvas):
                x, z, w, h = self.panel_rect()
                view.openingMoved.emit(self.opening_index, x, z, w, h)
            self._mode = None
            self._press_panel = None
            self._orig = None
            event.accept()
            return
        super().mouseReleaseEvent(event)


class RegionItem(QtWidgets.QGraphicsRectItem):
    """Paintable subpanel between reveal dividers (scene X-flipped)."""

    def __init__(self, region_id: int, x: float, z: float, w: float, h: float, panel_w: float, panel_h: float, rgb, show_color: bool):
        sx0 = _scene_x(panel_w, x + w)
        sx1 = _scene_x(panel_w, x)
        sy0 = _scene_y(panel_h, z + h)
        sy1 = _scene_y(panel_h, z)
        left = min(sx0, sx1)
        top = min(sy0, sy1)
        super().__init__(0.0, 0.0, abs(sx1 - sx0), abs(sy1 - sy0))
        self.setPos(left, top)
        self.kind = SEL_REGION
        self.region_id = region_id
        self.setZValue(2.0)
        self.setFlag(QtWidgets.QGraphicsItem.ItemIsSelectable, False)
        self.setAcceptedMouseButtons(QtCore.Qt.LeftButton)
        self._apply_color(rgb, show_color)

    def _apply_color(self, rgb, show_color: bool) -> None:
        if show_color and rgb is not None:
            r, g, b = [int(max(0.0, min(1.0, c)) * 255) for c in rgb]
            self.setPen(QtGui.QPen(QtGui.QColor(r, g, b, 200), 0.0))
            self.setBrush(QtGui.QBrush(QtGui.QColor(r, g, b, 120)))
        else:
            self.setPen(QtCore.Qt.NoPen)
            self.setBrush(QtCore.Qt.NoBrush)

    def mousePressEvent(self, event) -> None:
        view = self.scene().views()[0] if self.scene() and self.scene().views() else None
        if isinstance(view, PanelCanvas) and view._paint_mode:
            view.regionPainted.emit(self.region_id)
            event.accept()
            return
        event.ignore()


class PanelCanvas(QtWidgets.QGraphicsView):
    selectionChanged = QtCore.Signal(int, int)  # kind, index
    revealMoved = QtCore.Signal(int, float)
    openingMoved = QtCore.Signal(int, float, float, float, float)
    openingDrawn = QtCore.Signal(float, float, float, float)  # x,z,w,h panel
    placeRequested = QtCore.Signal(float, float)  # panel x, z
    regionPainted = QtCore.Signal(int)

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
        self._draw_opening = False
        self._paint_mode = False
        self._show_colors = True
        self._marquee_origin = None
        self._marquee_item = None
        self._bands = []
        self._openings = []
        self._regions = []
        self._panning = False

        self._scene.selectionChanged.connect(self._on_scene_selection_changed)

    def set_place_axis(self, axis) -> None:
        self._place_axis = axis
        self._draw_opening = False
        self._paint_mode = False
        self._update_cursor()

    def set_draw_opening(self, enabled: bool) -> None:
        self._draw_opening = enabled
        self._place_axis = None
        if enabled:
            self._paint_mode = False
        self._update_cursor()

    def set_paint_mode(self, enabled: bool) -> None:
        self._paint_mode = enabled
        if enabled:
            self._place_axis = None
            self._draw_opening = False
        self._update_cursor()

    def set_show_colors(self, enabled: bool) -> None:
        self._show_colors = enabled

    def _update_cursor(self) -> None:
        if self._draw_opening:
            self.viewport().setCursor(QtCore.Qt.CrossCursor)
        elif self._paint_mode:
            self.viewport().setCursor(QtCore.Qt.PointingHandCursor)
        elif self._place_axis == AXIS_HORIZONTAL:
            self.viewport().setCursor(QtCore.Qt.SizeVerCursor)
        elif self._place_axis == AXIS_VERTICAL:
            self.viewport().setCursor(QtCore.Qt.SizeHorCursor)
        else:
            self.viewport().unsetCursor()

    def draw_panel(self, data: dict, sel_kind: int = SEL_NONE, sel_index: int = -1, fit: bool = False) -> None:
        self._scene.blockSignals(True)
        self._scene.clear()
        self._bands = []
        self._openings = []
        self._regions = []
        self._marquee_item = None

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

        colors = data.get("panelColors") or []
        for reg in data.get("regions", []):
            rid = reg["id"]
            rgb = colors[rid] if rid < len(colors) else (0.55, 0.55, 0.55)
            item = RegionItem(
                rid, reg["x"], reg["z"], reg["w"], reg["h"], width, height, rgb, self._show_colors
            )
            self._scene.addItem(item)
            self._regions.append(item)

        edge_pen = QtGui.QPen(QtGui.QColor(120, 180, 255, 180), 0.0, QtCore.Qt.DashLine)
        edge_brush = QtGui.QBrush(QtGui.QColor(120, 180, 255, 40))

        if data["edgeSides"]:
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

        for i, op in enumerate(data.get("openings", [])):
            item = OpeningItem(i, op["x"], op["z"], op["w"], op["h"], width, height)
            self._scene.addItem(item)
            self._openings.append(item)
            if sel_kind == SEL_OPENING and i == sel_index:
                item.setSelected(True)

        for i, reveal in enumerate(data["reveals"]):
            item = RevealBandItem(i, reveal["axis"], reveal["pos"], width, height, gw)
            self._scene.addItem(item)
            self._bands.append(item)
            if sel_kind == SEL_REVEAL and i == sel_index:
                item.setSelected(True)

        self.setSceneRect(-gw, -gw, width + 2.0 * gw, height + 2.0 * gw)
        self._scene.blockSignals(False)

        if fit or not self._fitted:
            self.fitInView(0.0, 0.0, width, height, QtCore.Qt.KeepAspectRatio)
            self._fitted = True

        self._on_scene_selection_changed()

    def _on_scene_selection_changed(self) -> None:
        selected = [i for i in self._scene.selectedItems()]
        if len(selected) == 1:
            item = selected[0]
            if isinstance(item, RevealBandItem):
                self.selectionChanged.emit(SEL_REVEAL, item.reveal_index)
                return
            if isinstance(item, OpeningItem):
                self.selectionChanged.emit(SEL_OPENING, item.opening_index)
                return
        self.selectionChanged.emit(SEL_NONE, -1)

    def _scene_to_panel(self, scene_pt: QtCore.QPointF) -> tuple[float, float]:
        x = _panel_x(self._panel_w, scene_pt.x())
        z = self._panel_h - scene_pt.y()
        return (x, z)

    def mousePressEvent(self, event) -> None:
        if event.button() == QtCore.Qt.LeftButton and self._draw_opening:
            self._marquee_origin = self.mapToScene(event.pos())
            self._marquee_item = QtWidgets.QGraphicsRectItem()
            self._marquee_item.setPen(QtGui.QPen(QtGui.QColor(120, 220, 255), 0.0, QtCore.Qt.DashLine))
            self._marquee_item.setBrush(QtGui.QBrush(QtGui.QColor(80, 160, 220, 40)))
            self._marquee_item.setZValue(20.0)
            self._scene.addItem(self._marquee_item)
            event.accept()
            return
        if event.button() == QtCore.Qt.LeftButton and self._place_axis is not None:
            scene_pt = self.mapToScene(event.pos())
            x, z = self._scene_to_panel(scene_pt)
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
        if self._draw_opening and self._marquee_origin is not None and self._marquee_item is not None:
            cur = self.mapToScene(event.pos())
            rect = QtCore.QRectF(self._marquee_origin, cur).normalized()
            self._marquee_item.setRect(rect)
            event.accept()
            return
        if self._panning:
            delta = event.pos() - self._pan_last
            self._pan_last = event.pos()
            self.horizontalScrollBar().setValue(self.horizontalScrollBar().value() - delta.x())
            self.verticalScrollBar().setValue(self.verticalScrollBar().value() - delta.y())
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event) -> None:
        if event.button() == QtCore.Qt.LeftButton and self._draw_opening and self._marquee_origin is not None:
            cur = self.mapToScene(event.pos())
            rect = QtCore.QRectF(self._marquee_origin, cur).normalized()
            if self._marquee_item is not None:
                self._scene.removeItem(self._marquee_item)
                self._marquee_item = None
            self._marquee_origin = None
            if rect.width() > 1.0e-3 and rect.height() > 1.0e-3:
                x0 = _panel_x(self._panel_w, rect.right())
                x1 = _panel_x(self._panel_w, rect.left())
                z0 = self._panel_h - rect.bottom()
                z1 = self._panel_h - rect.top()
                x = min(x0, x1)
                z = min(z0, z1)
                w = abs(x1 - x0)
                h = abs(z1 - z0)
                self.openingDrawn.emit(x, z, w, h)
            event.accept()
            return
        if event.button() == QtCore.Qt.MiddleButton and self._panning:
            self._panning = False
            self._update_cursor()
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
        self._sel_kind = SEL_NONE
        self._sel_index = -1
        self._data = None

        self.setWindowTitle(f"TiltUp Reveal Layout — {node.name}")
        self.resize(860, 640)

        self._canvas = PanelCanvas()
        self._status = QtWidgets.QLabel()
        self._status.setWordWrap(True)

        self._width = WorldUnitSpinBox()
        self._height = WorldUnitSpinBox()
        self._depth = WorldUnitSpinBox()
        self._width.setRange(0.0, 1.0e30)
        self._height.setRange(0.0, 1.0e30)
        self._depth.setRange(0.0, 1.0e30)
        self._groove_w = WorldUnitSpinBox()
        self._groove_d = WorldUnitSpinBox()
        self._groove_w.setRange(0.0, 1.0e30)
        self._groove_d.setRange(0.0, 1.0e30)
        self._edge_sides = QtWidgets.QCheckBox("Inset L/R")
        self._edge_topbot = QtWidgets.QCheckBox("Inset T/B")

        self._pos_spin = WorldUnitSpinBox()
        self._pos_spin.setRange(0.0, 1.0e30)
        self._pos_spin.setEnabled(False)
        self._pct_spin = QtWidgets.QDoubleSpinBox()
        self._pct_spin.setRange(0.0, 100.0)
        self._pct_spin.setDecimals(2)
        self._pct_spin.setSingleStep(1.0)
        self._pct_spin.setSuffix(" %")
        self._pct_spin.setKeyboardTracking(False)
        self._pct_spin.setEnabled(False)

        self._open_left = WorldUnitSpinBox()
        self._open_right = WorldUnitSpinBox()
        self._open_bottom = WorldUnitSpinBox()
        self._open_top = WorldUnitSpinBox()
        self._open_left_pct = self._make_pct_spin()
        self._open_right_pct = self._make_pct_spin()
        self._open_bottom_pct = self._make_pct_spin()
        self._open_top_pct = self._make_pct_spin()
        self._open_edge_spins = (
            self._open_left,
            self._open_right,
            self._open_bottom,
            self._open_top,
            self._open_left_pct,
            self._open_right_pct,
            self._open_bottom_pct,
            self._open_top_pct,
        )
        for s in self._open_edge_spins:
            s.setEnabled(False)

        self._btn_add_h = QtWidgets.QPushButton("Add Horiz")
        self._btn_add_v = QtWidgets.QPushButton("Add Vert")
        self._btn_place_h = QtWidgets.QPushButton("Place Horiz…")
        self._btn_place_v = QtWidgets.QPushButton("Place Vert…")
        self._btn_draw_open = QtWidgets.QPushButton("Draw Opening…")
        self._btn_paint = QtWidgets.QPushButton("Paint Fill…")
        self._btn_paint.setCheckable(True)
        self._show_colors = QtWidgets.QCheckBox("Show colors in editor")
        self._show_colors.setChecked(True)
        self._btn_pick_color = QtWidgets.QPushButton("Pick Color…")
        self._active_color = QtGui.QColor(180, 180, 180)
        self._swatches = []
        default_swatches = [
            QtGui.QColor(220, 220, 220),
            QtGui.QColor(180, 180, 180),
            QtGui.QColor(140, 140, 140),
            QtGui.QColor(200, 170, 140),
            QtGui.QColor(160, 180, 200),
            QtGui.QColor(170, 190, 150),
            QtGui.QColor(210, 160, 160),
            QtGui.QColor(120, 120, 130),
        ]
        swatch_row = QtWidgets.QHBoxLayout()
        for i, col in enumerate(default_swatches):
            btn = QtWidgets.QToolButton()
            btn.setFixedSize(22, 22)
            btn.setStyleSheet(
                f"background-color: {col.name()}; border: 1px solid #888; border-radius: 2px;"
            )
            btn.clicked.connect(lambda _=False, c=col: self._set_active_color(c))
            self._swatches.append(btn)
            swatch_row.addWidget(btn)
        swatch_row.addStretch(1)

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
        self._open_left.valueChanged.connect(lambda _v: self._on_opening_edge_pos("left"))
        self._open_right.valueChanged.connect(lambda _v: self._on_opening_edge_pos("right"))
        self._open_bottom.valueChanged.connect(lambda _v: self._on_opening_edge_pos("bottom"))
        self._open_top.valueChanged.connect(lambda _v: self._on_opening_edge_pos("top"))
        self._open_left_pct.valueChanged.connect(lambda _v: self._on_opening_edge_pct("left"))
        self._open_right_pct.valueChanged.connect(lambda _v: self._on_opening_edge_pct("right"))
        self._open_bottom_pct.valueChanged.connect(lambda _v: self._on_opening_edge_pct("bottom"))
        self._open_top_pct.valueChanged.connect(lambda _v: self._on_opening_edge_pct("top"))
        self._btn_add_h.clicked.connect(lambda: self._add_reveal(AXIS_HORIZONTAL))
        self._btn_add_v.clicked.connect(lambda: self._add_reveal(AXIS_VERTICAL))
        self._btn_place_h.clicked.connect(lambda: self._begin_place(AXIS_HORIZONTAL))
        self._btn_place_v.clicked.connect(lambda: self._begin_place(AXIS_VERTICAL))
        self._btn_draw_open.clicked.connect(self._begin_draw_opening)
        self._btn_paint.toggled.connect(self._on_paint_toggled)
        self._show_colors.toggled.connect(self._on_show_colors_toggled)
        self._btn_pick_color.clicked.connect(self._pick_color)
        self._btn_remove.clicked.connect(self._remove_selected)
        self._btn_refresh.clicked.connect(lambda: self.reload_from_node(fit=False))
        self._btn_pct_25.clicked.connect(lambda: self._set_pct(25.0))
        self._btn_pct_50.clicked.connect(lambda: self._set_pct(50.0))
        self._btn_pct_75.clicked.connect(lambda: self._set_pct(75.0))

        self._canvas.selectionChanged.connect(self._on_canvas_selection)
        self._canvas.revealMoved.connect(self._on_reveal_moved)
        self._canvas.openingMoved.connect(self._on_opening_moved)
        self._canvas.openingDrawn.connect(self._on_opening_drawn)
        self._canvas.placeRequested.connect(self._on_place_at)
        self._canvas.regionPainted.connect(self._on_region_painted)

        form = QtWidgets.QFormLayout()
        form.addRow("Width", self._width)
        form.addRow("Height", self._height)
        form.addRow("Depth", self._depth)
        form.addRow("Groove W", self._groove_w)
        form.addRow("Groove D", self._groove_d)
        form.addRow(self._edge_sides)
        form.addRow(self._edge_topbot)
        form.addRow("Reveal Pos", self._pos_spin)
        form.addRow("Reveal %", self._pct_spin)
        form.addRow("X Left", self._edge_row(self._open_left, self._open_left_pct))
        form.addRow("X Right", self._edge_row(self._open_right, self._open_right_pct))
        form.addRow("Z Bottom", self._edge_row(self._open_bottom, self._open_bottom_pct))
        form.addRow("Z Top", self._edge_row(self._open_top, self._open_top_pct))

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
        side.addWidget(self._btn_draw_open)
        side.addWidget(self._btn_paint)
        side.addWidget(self._show_colors)
        side.addWidget(self._btn_pick_color)
        side.addLayout(swatch_row)
        side.addWidget(self._btn_remove)
        side.addWidget(self._btn_refresh)
        side.addStretch(1)
        side.addWidget(self._status)

        self._update_color_button()
        layout = QtWidgets.QHBoxLayout(self)
        layout.addWidget(self._canvas, stretch=1)
        layout.addLayout(side)

        self.reload_from_node(fit=True)

    @staticmethod
    def _make_pct_spin() -> QtWidgets.QDoubleSpinBox:
        spin = QtWidgets.QDoubleSpinBox()
        spin.setRange(0.0, 100.0)
        spin.setDecimals(2)
        spin.setSingleStep(1.0)
        spin.setSuffix(" %")
        spin.setKeyboardTracking(False)
        spin.setMinimumWidth(88)
        spin.setMaximumWidth(100)
        return spin

    @staticmethod
    def _edge_row(pos_spin: QtWidgets.QWidget, pct_spin: QtWidgets.QWidget) -> QtWidgets.QWidget:
        pos_spin.setMaximumWidth(110)
        pos_spin.setMinimumWidth(80)
        row = QtWidgets.QWidget()
        layout = QtWidgets.QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(pos_spin, stretch=1)
        layout.addWidget(pct_spin)
        return row

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
            data["panelColors"] = ensure_panel_colors(self._node)
            data["regions"] = list_regions(data)
            self._data = data
            self._width.setValue(data["width"])
            self._height.setValue(data["height"])
            self._depth.setValue(data["depth"])
            self._groove_w.setValue(data["grooveWidth"])
            self._groove_d.setValue(data["grooveDepth"])
            self._edge_sides.setChecked(data["edgeSides"])
            self._edge_topbot.setChecked(data["edgeTopBot"])

            if self._sel_kind == SEL_REVEAL and self._sel_index >= len(data["reveals"]):
                self._sel_index = len(data["reveals"]) - 1
                if self._sel_index < 0:
                    self._sel_kind = SEL_NONE
            if self._sel_kind == SEL_OPENING and self._sel_index >= len(data["openings"]):
                self._sel_index = len(data["openings"]) - 1
                if self._sel_index < 0:
                    self._sel_kind = SEL_NONE

            self._canvas.set_show_colors(self._show_colors.isChecked())
            self._canvas.draw_panel(
                data,
                sel_kind=self._sel_kind,
                sel_index=self._sel_index,
                fit=fit,
            )
            self._sync_selection_controls()
            mode = ""
            if self._canvas._paint_mode:
                mode = "  |  click a subpanel to fill"
            elif self._canvas._draw_opening:
                mode = "  |  drag marquee for opening"
            elif self._canvas._place_axis == AXIS_HORIZONTAL:
                mode = "  |  click to place H"
            elif self._canvas._place_axis == AXIS_VERTICAL:
                mode = "  |  click to place V"
            self._status.setText(
                f"{rt.units.formatValue(data['width'])} × {rt.units.formatValue(data['height'])}  |  "
                f"{len(data['reveals'])} reveal(s), {len(data['openings'])} opening(s), "
                f"{len(data['regions'])} panel(s){mode}"
            )
        finally:
            self._loading = False

    def _sync_selection_controls(self) -> None:
        is_rev = self._sel_kind == SEL_REVEAL and self._data and 0 <= self._sel_index < len(self._data["reveals"])
        is_op = self._sel_kind == SEL_OPENING and self._data and 0 <= self._sel_index < len(self._data["openings"])
        self._pos_spin.setEnabled(bool(is_rev))
        self._pct_spin.setEnabled(bool(is_rev))
        for b in (self._btn_pct_25, self._btn_pct_50, self._btn_pct_75):
            b.setEnabled(bool(is_rev))
        for s in self._open_edge_spins:
            s.setEnabled(bool(is_op))
        self._btn_remove.setEnabled(bool(is_rev or is_op))

        if is_rev:
            reveal = self._data["reveals"][self._sel_index]
            limit = self._data["height"] if reveal["axis"] == AXIS_HORIZONTAL else self._data["width"]
            limit = max(limit, 1.0e-6)
            self._pos_spin.setRange(0.0, limit)
            self._pos_spin.setValue(reveal["pos"])
            self._pct_spin.setValue(100.0 * reveal["pos"] / limit)
        if is_op:
            op = self._data["openings"][self._sel_index]
            width = max(self._data["width"], 1.0e-6)
            height = max(self._data["height"], 1.0e-6)
            left = op["x"]
            right = op["x"] + op["w"]
            bottom = op["z"]
            top = op["z"] + op["h"]
            self._open_left.setValue(left)
            self._open_right.setValue(right)
            self._open_bottom.setValue(bottom)
            self._open_top.setValue(top)
            self._open_left_pct.setValue(100.0 * left / width)
            self._open_right_pct.setValue(100.0 * right / width)
            self._open_bottom_pct.setValue(100.0 * bottom / height)
            self._open_top_pct.setValue(100.0 * top / height)

    def _on_canvas_selection(self, kind: int, index: int) -> None:
        if self._loading:
            return
        self._sel_kind = kind
        self._sel_index = index
        self._loading = True
        try:
            self._sync_selection_controls()
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

    def _write_selected_reveal_pos(self, pos: float) -> None:
        if self._sel_kind != SEL_REVEAL or self._sel_index < 0 or not self._data:
            return
        reveal = self._data["reveals"][self._sel_index]
        limit = self._data["height"] if reveal["axis"] == AXIS_HORIZONTAL else self._data["width"]
        pos = _clamp(float(pos), 0.0, max(limit, 0.0))
        write_reveal_pos(self._node, self._sel_index, pos)
        self.reload_from_node(fit=False)

    def _on_pos_changed(self, value: float) -> None:
        if self._loading:
            return
        self._write_selected_reveal_pos(value)

    def _on_pct_changed(self, value: float) -> None:
        if self._loading or self._sel_kind != SEL_REVEAL or not self._data:
            return
        reveal = self._data["reveals"][self._sel_index]
        limit = self._data["height"] if reveal["axis"] == AXIS_HORIZONTAL else self._data["width"]
        self._write_selected_reveal_pos(limit * float(value) / 100.0)

    def _set_pct(self, pct: float) -> None:
        if self._sel_kind != SEL_REVEAL:
            return
        self._pct_spin.setValue(pct)

    def _on_reveal_moved(self, index: int, pos: float) -> None:
        if self._loading:
            return
        self._sel_kind = SEL_REVEAL
        self._sel_index = index
        write_reveal_pos(self._node, index, float(pos))
        self.reload_from_node(fit=False)

    def _opening_edges(self) -> tuple[float, float, float, float]:
        """Return left, right, bottom, top from current opening selection."""
        op = self._data["openings"][self._sel_index]
        return (op["x"], op["x"] + op["w"], op["z"], op["z"] + op["h"])

    def _commit_opening_edges(self, left: float, right: float, bottom: float, top: float) -> None:
        min_size = 1.0e-4
        if right < left + min_size:
            right = left + min_size
        if top < bottom + min_size:
            top = bottom + min_size
        write_opening(
            self._node,
            self._sel_index,
            left,
            bottom,
            right - left,
            top - bottom,
        )
        self.reload_from_node(fit=False)

    def _on_opening_edge_pos(self, edge: str) -> None:
        if self._loading or self._sel_kind != SEL_OPENING or self._sel_index < 0 or not self._data:
            return
        left, right, bottom, top = self._opening_edges()
        if edge == "left":
            left = self._open_left.value()
        elif edge == "right":
            right = self._open_right.value()
        elif edge == "bottom":
            bottom = self._open_bottom.value()
        elif edge == "top":
            top = self._open_top.value()
        self._commit_opening_edges(left, right, bottom, top)

    def _on_opening_edge_pct(self, edge: str) -> None:
        if self._loading or self._sel_kind != SEL_OPENING or self._sel_index < 0 or not self._data:
            return
        width = max(self._data["width"], 1.0e-6)
        height = max(self._data["height"], 1.0e-6)
        left, right, bottom, top = self._opening_edges()
        if edge == "left":
            left = width * self._open_left_pct.value() / 100.0
        elif edge == "right":
            right = width * self._open_right_pct.value() / 100.0
        elif edge == "bottom":
            bottom = height * self._open_bottom_pct.value() / 100.0
        elif edge == "top":
            top = height * self._open_top_pct.value() / 100.0
        self._commit_opening_edges(left, right, bottom, top)

    def _on_opening_moved(self, index: int, x: float, z: float, w: float, h: float) -> None:
        if self._loading:
            return
        self._sel_kind = SEL_OPENING
        self._sel_index = index
        write_opening(self._node, index, x, z, w, h)
        self.reload_from_node(fit=False)

    def _on_opening_drawn(self, x: float, z: float, w: float, h: float) -> None:
        self._canvas.set_draw_opening(False)
        self._sel_kind = SEL_OPENING
        self._sel_index = add_opening(self._node, x, z, w, h)
        self.reload_from_node(fit=False)

    def _add_reveal(self, axis: int) -> None:
        self._canvas.set_place_axis(None)
        self._canvas.set_draw_opening(False)
        self._sel_kind = SEL_REVEAL
        self._sel_index = add_reveal(self._node, axis)
        self.reload_from_node(fit=False)

    def _begin_place(self, axis: int) -> None:
        self._canvas.set_draw_opening(False)
        self._canvas.set_place_axis(axis)
        label = "H" if axis == AXIS_HORIZONTAL else "V"
        self._status.setText(f"Click canvas to place {label} reveal (Esc cancels)")

    def _begin_draw_opening(self) -> None:
        self._btn_paint.setChecked(False)
        self._canvas.set_place_axis(None)
        self._canvas.set_draw_opening(True)
        self._status.setText("Drag a rectangle for the opening (Esc cancels)")

    def _on_paint_toggled(self, checked: bool) -> None:
        self._canvas.set_paint_mode(checked)
        if checked:
            self._status.setText("Paint mode: click a subpanel to fill with the active color")
        else:
            self.reload_from_node(fit=False)

    def _on_show_colors_toggled(self, checked: bool) -> None:
        self._canvas.set_show_colors(checked)
        self.reload_from_node(fit=False)

    def _set_active_color(self, color: QtGui.QColor) -> None:
        self._active_color = QtGui.QColor(color)
        self._update_color_button()

    def _update_color_button(self) -> None:
        c = self._active_color
        self._btn_pick_color.setStyleSheet(
            f"background-color: {c.name()}; color: {'#000' if c.lightness() > 140 else '#fff'};"
        )

    def _pick_color(self) -> None:
        color = QtWidgets.QColorDialog.getColor(self._active_color, self, "Panel Color")
        if color.isValid():
            self._set_active_color(color)

    def _on_region_painted(self, region_id: int) -> None:
        c = self._active_color
        rgb = (c.redF(), c.greenF(), c.blueF())
        write_panel_color(self._node, region_id, rgb)
        self.reload_from_node(fit=False)

    def _on_place_at(self, x: float, z: float) -> None:
        axis = self._canvas._place_axis
        if axis is None:
            return
        pos = z if axis == AXIS_HORIZONTAL else x
        self._canvas.set_place_axis(None)
        self._sel_kind = SEL_REVEAL
        self._sel_index = add_reveal(self._node, axis, pos)
        self.reload_from_node(fit=False)

    def _remove_selected(self) -> None:
        if self._sel_kind == SEL_REVEAL and self._sel_index >= 0:
            remove_reveal(self._node, self._sel_index)
        elif self._sel_kind == SEL_OPENING and self._sel_index >= 0:
            remove_opening(self._node, self._sel_index)
        else:
            return
        self._sel_kind = SEL_NONE
        self._sel_index = -1
        self._canvas.set_place_axis(None)
        self._canvas.set_draw_opening(False)
        self.reload_from_node(fit=False)

    def keyPressEvent(self, event) -> None:
        if event.key() == QtCore.Qt.Key_Escape:
            self._canvas.set_place_axis(None)
            self._canvas.set_draw_opening(False)
            self._btn_paint.setChecked(False)
            self._canvas.set_paint_mode(False)
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
