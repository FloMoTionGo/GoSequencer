#!/usr/bin/env python
"""
Generates the mockup PNGs referenced from INSTRUCTIONS.md.

These are illustrative recreations of the plugin's dark theme (colours taken
directly from Source/BoardComponent.h's `theme` namespace) - not screenshots,
since they need to exist before/without a built plugin - drawn with Pillow.

Run:  python generate.py
"""

import json
import os

from PIL import Image, ImageDraw, ImageFont

SS = 3  # supersample factor for anti-aliasing

# ---- theme (Source/BoardComponent.h) ---------------------------------------
BG           = (0x16, 0x15, 0x1a)
PANEL        = (0x22, 0x21, 0x29)
PANEL_BRIGHT = (0x2c, 0x2b, 0x35)
TEXT         = (0xe8, 0xe4, 0xdc)
DIM          = (0x8d, 0x8a, 0x96)
ACCENT       = (0xc8, 0x5a, 0x3c)
WOOD_LIGHT   = (0xea, 0xca, 0x90)
WOOD_DARK    = (0xcf, 0x9d, 0x58)
LINES        = (0x3b, 0x2a, 0x19)
BLACK_STONE  = (0x14, 0x13, 0x17)
WHITE_STONE  = (0xf0, 0xec, 0xe4)
RED_FLASH    = (0xe0, 0x4f, 0x4f)

RING_COLOURS = [(0xc8, 0x5a, 0x3c), (0x5a, 0xa7, 0xc8), (0x8a, 0xc8, 0x5a), (0xc8, 0xa8, 0x5a)]
QUAD_COLOURS = [(0xc8, 0x5a, 0x3c), (0x5a, 0xa7, 0xc8), (0x8a, 0xc8, 0x5a), (0xb0, 0x7a, 0xd6)]

FONT_DIR = r"C:\Windows\Fonts"


def font(size, bold=False, italic=False):
    name = "segoeuib.ttf" if bold else ("segoeuii.ttf" if italic else "segoeui.ttf")
    return ImageFont.truetype(f"{FONT_DIR}\\{name}", size * SS)


def new_canvas(w, h, bg=BG):
    img = Image.new("RGB", (w * SS, h * SS), bg)
    return img, ImageDraw.Draw(img)


def save(img, name, final_w, final_h):
    img = img.resize((final_w, final_h), Image.LANCZOS)
    img.save(f"{name}.png")
    print(f"wrote {name}.png  {final_w}x{final_h}")


def sw(v):
    return max(1, int(round(v * SS)))

def s(v):
    return v * SS


def rrect(d, box, r, fill=None, outline=None, width=1):
    d.rounded_rectangle([s(v) for v in box], radius=s(r), fill=fill, outline=outline,
                         width=sw(width) if outline else None)


def text(d, pos, txt, f, fill=TEXT, anchor="la"):
    d.text((s(pos[0]), s(pos[1])), txt, font=f, fill=fill, anchor=anchor)


def alpha_over(base, w, h, draw_fn, alpha):
    """Draws with draw_fn(draw) onto a transparent layer at `alpha` opacity,
    then composites it onto base (which stays RGB)."""
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    draw_fn(ld)
    # apply alpha to every non-transparent pixel drawn
    r, g, b, a = layer.split()
    a = a.point(lambda v: int(v * alpha))
    layer = Image.merge("RGBA", (r, g, b, a))
    based = base.convert("RGBA")
    based.alpha_composite(layer)
    return based.convert("RGB")


# =============================================================================
# Go board geometry (mirrors Source/GoBoard.h exactly)
# =============================================================================

def spiral_order(size):
    out = []
    top, bottom, left, right = 0, size - 1, 0, size - 1
    while top <= bottom and left <= right:
        for c in range(left, right + 1):
            out.append((c, top))
        top += 1
        for r in range(top, bottom + 1):
            out.append((right, r))
        right -= 1
        if top <= bottom:
            for c in range(right, left - 1, -1):
                out.append((c, bottom))
            bottom -= 1
        if left <= right:
            for r in range(bottom, top - 1, -1):
                out.append((left, r))
            left += 1
    return out


def ring_count(size):
    return (size - 1) // 2


def ring_offset(size, r):
    return 4 * r * (size - r)


def ring_length(size, r):
    return 4 * (size - 1 - 2 * r)


def quad_radius(size):
    return (size - 1) // 4


def quad_side(size):
    return 2 * quad_radius(size) + 1


def quad_origin(size, q):
    r = quad_radius(size)
    col = 2 * r if q in (1, 2) else 0
    row = 2 * r if q >= 2 else 0
    return col, row


def quad_order(size, q):
    local = spiral_order(quad_side(size))
    ox, oy = quad_origin(size, q)
    return [(ox + c, oy + r) for (c, r) in local]


SIZE = 9
SPIRAL9 = spiral_order(SIZE)
STAR_POINTS = [(2, 2), (6, 2), (2, 6), (6, 6), (4, 4)]


# =============================================================================
# Shared board-drawing helper
# =============================================================================

def draw_goban(d, ox, oy, cell, size=SIZE, board_fill=PANEL_BRIGHT):
    span = cell * (size - 1)
    rrect(d, (ox - 22, oy - 22, ox + span + 22, oy + span + 22), 10, fill=board_fill)

    for i in range(size):
        d.line([s(ox), s(oy + i * cell), s(ox + span), s(oy + i * cell)], fill=LINES, width=sw(1.4))
        d.line([s(ox + i * cell), s(oy), s(ox + i * cell), s(oy + span)], fill=LINES, width=sw(1.4))

    for (c, r) in STAR_POINTS:
        cx, cy = ox + c * cell, oy + r * cell
        d.ellipse([s(cx - 3), s(cy - 3), s(cx + 3), s(cy + 3)], fill=LINES)

    return span


def pt(ox, oy, cell, c, r):
    return ox + c * cell, oy + r * cell


def draw_stone(d, cx, cy, radius, black, stroke=None):
    fill = BLACK_STONE if black else WHITE_STONE
    outline = stroke or (WOOD_DARK if black else LINES)
    d.ellipse([s(cx - radius), s(cy - radius), s(cx + radius), s(cy + radius)],
              fill=fill, outline=outline, width=sw(1.4))


def draw_playhead_ring(d, cx, cy, radius, colour=ACCENT):
    d.ellipse([s(cx - radius), s(cy - radius), s(cx + radius), s(cy + radius)],
              outline=colour, width=sw(2.4))
    d.ellipse([s(cx - 2), s(cy - 2), s(cx + 2), s(cy + 2)], fill=colour)


def dashed_path(img_w, img_h, pts_px, colour, width=2.4, dash=6, gap=5):
    """pts_px: list of (x, y) already scaled with s()."""
    layer = Image.new("RGBA", (img_w, img_h), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    for i in range(len(pts_px) - 1):
        x0, y0 = pts_px[i]
        x1, y1 = pts_px[i + 1]
        seg_len = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
        if seg_len == 0:
            continue
        ux, uy = (x1 - x0) / seg_len, (y1 - y0) / seg_len
        dist = 0.0
        draw_on = True
        while dist < seg_len:
            step = min(dash if draw_on else gap, seg_len - dist)
            xa, ya = x0 + ux * dist, y0 + uy * dist
            xb, yb = x0 + ux * (dist + step), y0 + uy * (dist + step)
            if draw_on:
                ld.line([xa, ya, xb, yb], fill=colour + (235,), width=int(width))
            dist += step
            draw_on = not draw_on
    return layer


def paste_dashed(base_rgb, w, h, pts_px, colour, width=2.4, dash=7, gap=5):
    layer = dashed_path(w, h, pts_px, colour, width, dash, gap)
    based = base_rgb.convert("RGBA")
    based.alpha_composite(layer)
    return based.convert("RGB")


def caption_bar(w, h, title, subtitle=None):
    img, d = new_canvas(w, h)
    text(d, (18, 14), title, font(15, bold=True), fill=ACCENT)
    if subtitle:
        text(d, (18, 36), subtitle, font(11.5), fill=DIM)
    return img, d


LEGEND_Y_PAD = 4


def legend_row(d, x, y, items, f):
    """items: list of (swatch_draw_fn(d, cx, cy), label)"""
    cx = x
    for draw_fn, label in items:
        draw_fn(d, cx + 6, y + 6)
        text(d, (cx + 18, y), label, f, fill=DIM)
        cx += 18 + int(d.textlength(label, font=f) / SS) + 26


# =============================================================================
# 1. Board mode mockups
# =============================================================================

def mockup_spiral():
    W, H = 460, 520
    img, d = caption_bar(W, H, "Spiral mode", "one playhead, corner to tengen")
    cell = 40
    ox, oy = 62, 84
    span = draw_goban(d, ox, oy, cell)

    pts_px = [(s(pt(ox, oy, cell, c, r)[0]), s(pt(ox, oy, cell, c, r)[1])) for (c, r) in SPIRAL9]
    img = paste_dashed(img, W * SS, H * SS, pts_px, ACCENT, width=2.6, dash=8, gap=6)
    d = ImageDraw.Draw(img)

    # a handful of stones along the path to show colour + fade behaviour
    demo = [(0, True, False), (2, False, False), (7, True, False),
            (11, False, False), (16, True, True), (24, False, False)]
    for idx, black, faded in demo:
        c, r = SPIRAL9[idx]
        cx, cy = pt(ox, oy, cell, c, r)
        if faded:
            def fn(ld, cx=cx, cy=cy, black=black):
                fill = BLACK_STONE if black else WHITE_STONE
                ld.ellipse([s(cx - 13), s(cy - 13), s(cx + 13), s(cy + 13)], fill=fill)
            img = alpha_over(img, W * SS, H * SS, fn, 0.38)
            d = ImageDraw.Draw(img)
        else:
            draw_stone(d, cx, cy, 13, black)

    # current playhead
    hc, hr = SPIRAL9[19]
    hx, hy = pt(ox, oy, cell, hc, hr)
    draw_playhead_ring(d, hx, hy, 15)

    ly = oy + span + 48
    legend_row(d, ox, ly, [
        (lambda dd, cx, cy: dd.ellipse([s(cx-8), s(cy-8), s(cx+8), s(cy+8)], fill=BLACK_STONE, outline=WOOD_DARK, width=sw(1.4)), "black"),
        (lambda dd, cx, cy: dd.ellipse([s(cx-8), s(cy-8), s(cx+8), s(cy+8)], fill=WHITE_STONE, outline=LINES, width=sw(1.4)), "white"),
        (lambda dd, cx, cy: dd.ellipse([s(cx-8), s(cy-8), s(cx+8), s(cy+8)], outline=ACCENT, width=sw(2.4)), "playhead"),
    ], font(11.5))
    text(d, (ox, ly + 26), "faded = spent (lifespan expired, still on board, silent)", font(11), fill=DIM)
    save(img, "spiral-mode", W, H)


def mockup_quads():
    W, H = 500, 560
    img, d = caption_bar(W, H, "Quads out / Quads in", "one playhead per quadrant, meeting at the centre")
    cell = 38
    ox, oy = 74, 92
    span = draw_goban(d, ox, oy, cell)

    pts_all = []
    for q in range(4):
        order = quad_order(SIZE, q)
        pts_px = [(s(pt(ox, oy, cell, c, r)[0]), s(pt(ox, oy, cell, c, r)[1])) for (c, r) in order]
        pts_all.append(pts_px)

    for q, pts_px in enumerate(pts_all):
        img = paste_dashed(img, W * SS, H * SS, pts_px, QUAD_COLOURS[q], width=2.4, dash=7, gap=5)
    d = ImageDraw.Draw(img)

    # arrowhead + stones near the star point of each quadrant (quads-in look:
    # heads run from the block's outer edge inward to the star point)
    for q in range(4):
        order = quad_order(SIZE, q)
        star_c, star_r = order[-1]
        sx, sy = pt(ox, oy, cell, star_c, star_r)
        draw_playhead_ring(d, sx, sy, 13, QUAD_COLOURS[q])
        oc, orow = order[0]
        ex, ey = pt(ox, oy, cell, oc, orow)
        draw_stone(d, ex, ey, 11, black=(q % 2 == 0))

    ly = oy + span + 56
    for q in range(4):
        cx = ox + (q % 2) * (span // 2 + 20)
        cyy = ly + (q // 2) * 22
        d.ellipse([s(cx - 7), s(cyy + 6 - 7), s(cx + 7), s(cyy + 6 + 7)], fill=QUAD_COLOURS[q])
        text(d, (cx + 14, cyy), f"head {q+1} (quadrant {q+1})", font(11.5), fill=DIM)
    text(d, (ox, ly + 50), "rings shown winding IN to the star point (\u201cQuads in\u201d) \u2014 \u201cQuads out\u201d is the same path, reversed", font(10.8), fill=DIM)
    save(img, "quads-mode", W, H)


def mockup_polyrhythm():
    W, H = 500, 560
    img, d = caption_bar(W, H, "Polyrhythm mode", "one playhead per concentric ring \u2014 tengen excluded")
    cell = 38
    ox, oy = 74, 92
    span = draw_goban(d, ox, oy, cell)

    rc = ring_count(SIZE)
    for ring in range(rc):
        off = ring_offset(SIZE, ring)
        length = ring_length(SIZE, ring)
        seg = SPIRAL9[off:off + length] + [SPIRAL9[off]]
        pts_px = [(s(pt(ox, oy, cell, c, r)[0]), s(pt(ox, oy, cell, c, r)[1])) for (c, r) in seg]
        img = paste_dashed(img, W * SS, H * SS, pts_px, RING_COLOURS[ring], width=2.4, dash=7, gap=5)
    d = ImageDraw.Draw(img)

    # tengen marker (not a ring)
    tc, tr = 4, 4
    txx, tyy = pt(ox, oy, cell, tc, tr)
    draw_stone(d, txx, tyy, 10, black=True)
    text(d, (txx + 14, tyy - 7), "tengen (silent \u2014 no ring)", font(10.5), fill=DIM)

    # a playhead per ring, plus a demo stone
    demo_positions = [3, 5, 4, 2]
    for ring in range(rc):
        off = ring_offset(SIZE, ring)
        length = ring_length(SIZE, ring)
        pos = demo_positions[ring] % length
        c, r = SPIRAL9[off + pos]
        cx, cy = pt(ox, oy, cell, c, r)
        draw_playhead_ring(d, cx, cy, 12, RING_COLOURS[ring])

    ly = oy + span + 56
    labels = ["ring 1 (outer)  \u2014 32 pts", "ring 2  \u2014 24 pts", "ring 3  \u2014 16 pts", "ring 4 (inner)  \u2014 8 pts"]
    for i, label in enumerate(labels):
        cx = ox + (i % 2) * (span // 2 + 40)
        cyy = ly + (i // 2) * 22
        d.ellipse([s(cx - 7), s(cyy + 6 - 7), s(cx + 7), s(cyy + 6 + 7)], fill=RING_COLOURS[i])
        text(d, (cx + 14, cyy), label, font(11), fill=DIM)
    text(d, (ox, ly + 50), "all rings share the step clock but drift out of phase \u2014 own channel + Spread (transpose) each, and they all sound together", font(10.4), fill=DIM)
    save(img, "polyrhythm-mode", W, H)


def mockup_interaction():
    W, H = 860, 300
    img, d = caption_bar(W, H, "Placing, lifting, and illegal moves", None)

    card_w = (W - 40 - 3 * 18) / 4
    card_h = 210
    card_y = 62

    def card(i, heading, body_fn, caption_lines):
        cx0 = 20 + i * (card_w + 18)
        rrect(d, (cx0, card_y, cx0 + card_w, card_y + card_h), 8, fill=PANEL)
        text(d, (cx0 + 16, card_y + 14), heading, font(11.5, bold=True), fill=TEXT)
        centre_x, centre_y = cx0 + card_w / 2, card_y + 78
        body_fn(centre_x, centre_y)
        ty = card_y + 128
        for line in caption_lines:
            text(d, (cx0 + 16, ty), line, font(10.2), fill=DIM)
            ty += 16
        return centre_x, centre_y, cx0

    # 1) hover an empty point
    def draw_hover(cx, cy):
        d.ellipse([s(cx - 13), s(cy - 13), s(cx + 13), s(cy + 13)], outline=DIM, width=sw(1.6))

    card(0, "Empty point", draw_hover,
         ["Hovering shows where a", "click would land."])

    # 2) place a stone
    def draw_place(cx, cy):
        draw_stone(d, cx, cy, 14, black=True)

    card(1, "Left-click \u2192 places", draw_place,
         ["Plays a stone, if it's a", "legal Go move. Colour is", "set by \u201cPlace\u201d."])

    # 3) lift a stone (eraser)
    def draw_lift(cx, cy):
        draw_stone(d, cx, cy, 14, black=False)
        d.line([s(cx - 9), s(cy - 9), s(cx + 9), s(cy + 9)], fill=RED_FLASH, width=sw(2.4))
        d.line([s(cx - 9), s(cy + 9), s(cx + 9), s(cy - 9)], fill=RED_FLASH, width=sw(2.4))

    card(2, "Click again \u2192 lifts", draw_lift,
         ["Or right-click / Shift-click /", "Alt-click. Ignores Go legality", "\u2014 it's just the eraser."])

    # 4) illegal move flash
    cx4, cy4, cx0_4 = card(3, "Illegal \u2192 flashes red", lambda cx, cy: None, [])

    def flash_fn(ld, cx=cx4, cy=cy4):
        ld.ellipse([s(cx - 15), s(cy - 15), s(cx + 15), s(cy + 15)], fill=RED_FLASH)

    img = alpha_over(img, W * SS, H * SS, flash_fn, 0.55)
    d = ImageDraw.Draw(img)
    d.ellipse([s(cx4 - 15), s(cy4 - 15), s(cx4 + 15), s(cy4 + 15)], outline=RED_FLASH, width=sw(1.8))
    ty = card_y + 128
    for line in ["Nothing is placed. Header", "explains why, e.g. \u201cko:", "that would repeat\u2026\u201d"]:
        text(d, (cx0_4 + 16, ty), line, font(10.2), fill=DIM)
        ty += 16

    save(img, "board-interaction", W, H)


# =============================================================================
# 2. UI panel mockups
# =============================================================================

def mock_slider(d, x, y, w, caption, value_text, enabled=True):
    text(d, (x, y), caption, font(10.5), fill=DIM if enabled else (0x55, 0x53, 0x5c))
    ty = y + 18
    track_col = PANEL_BRIGHT if enabled else PANEL
    rrect(d, (x, ty + 8, x + w, ty + 14), 3, fill=track_col)
    fill_w = w * 0.42
    fill_col = ACCENT if enabled else (0x5a, 0x40, 0x38)
    rrect(d, (x, ty + 8, x + fill_w, ty + 14), 3, fill=fill_col)
    thumb_x = x + fill_w
    d.ellipse([s(thumb_x - 6), s(ty + 5), s(thumb_x + 6), s(ty + 17)],
              fill=TEXT if enabled else DIM)
    box_w = 62
    rrect(d, (x + w - box_w, ty, x + w, ty + 20), 4, fill=PANEL)
    text(d, (x + w - box_w + 6, ty + 3), value_text, font(10.2), fill=TEXT if enabled else DIM, anchor="la")


def mock_combo(d, x, y, w, caption, value_text, enabled=True):
    text(d, (x, y), caption, font(10.5), fill=DIM if enabled else (0x55, 0x53, 0x5c))
    ty = y + 18
    rrect(d, (x, ty, x + w, ty + 24), 4, fill=PANEL, outline=PANEL_BRIGHT, width=1)
    text(d, (x + 10, ty + 5), value_text, font(11), fill=TEXT if enabled else DIM)
    # chevron
    cx, cy = x + w - 16, ty + 12
    d.line([s(cx - 5), s(cy - 3), s(cx), s(cy + 3), s(cx + 5), s(cy - 3)], fill=DIM, width=sw(1.6), joint="curve")


def mock_toggle(d, x, y, w, caption, on, enabled=True):
    ty = y + 18
    if on and enabled:
        rrect(d, (x, ty, x + w, ty + 24), 5, fill=ACCENT)
    else:
        rrect(d, (x, ty, x + w, ty + 24), 5, fill=PANEL_BRIGHT, outline=(0x3a, 0x38, 0x44), width=1)
    label_fill = (0xff, 0xff, 0xff) if (on and enabled) else ((0x5a, 0x58, 0x62) if not enabled else DIM)
    text(d, (x + w / 2, ty + 12), caption, font(10.8, bold=(on and enabled)), fill=label_fill, anchor="mm")


def section_header(d, x, y, text_str):
    text(d, (x, y), text_str, font(11, bold=True), fill=ACCENT)


def mockup_sequencer_panel():
    W, H = 760, 300
    img, d = new_canvas(W, H)
    x0 = 20
    rrect(d, (14, 14, W - 14, H - 14), 8, fill=PANEL)
    section_header(d, x0, 28, "SEQUENCER")

    colw = (W - 28 - 3 * 16) / 4
    y = 54
    for i, (cap, val) in enumerate([("step rate", "1/16"), ("note", "C3"),
                                     ("gate", "50%"), ("free tempo", "120.0 BPM")]):
        mock_slider(d, x0 + i * (colw + 16), y, colw, cap, val) if i else \
            mock_combo(d, x0 + i * (colw + 16), y, colw, cap, val)

    y += 62
    for i, (cap, val, kind) in enumerate([
            ("mode", "Polyrhythm", "combo"), ("spread", "+3 st", "slider"),
            ("stone life", "15 steps", "slider"), ("life counts", "Steps", "combo")]):
        fn = mock_combo if kind == "combo" else mock_slider
        fn(d, x0 + i * (colw + 16), y, colw, cap, val)

    y += 62
    for i, (cap, val, kind) in enumerate([
            ("black velocity", "100", "slider"), ("white velocity", "100", "slider"),
            ("board", "9 x 9", "combo"), ("place", "Alternate", "combo")]):
        fn = mock_combo if kind == "combo" else mock_slider
        fn(d, x0 + i * (colw + 16), y, colw, cap, val)

    y += 62
    sw_w = (W - 28 - 4 * 14) / 5
    toggles = [("Ko rule", True), ("Self capture", False), ("Free run", False),
               ("Show path", True), ("Clear board", False)]
    for i, (cap, on) in enumerate(toggles):
        mock_toggle(d, x0 + i * (sw_w + 14), y, sw_w, cap, on)

    save(img, "sequencer-panel", W, H)


def mockup_channels_foldout():
    W, H = 760, 260
    img, d = new_canvas(W, H)
    x0 = 20

    # closed state (left half)
    rrect(d, (14, 14, W / 2 - 6, 74), 8, fill=PANEL)
    text(d, (x0, 30), "\u25ba  MIDI CHANNELS", font(11, bold=True), fill=ACCENT)
    text(d, (x0, 52), "(collapsed \u2014 default)", font(10), fill=DIM)

    # open state (right half)
    ox = W / 2 + 6
    rrect(d, (ox, 14, W - 14, H - 14), 8, fill=PANEL)
    text(d, (ox + 14, 30), "\u25bc  MIDI CHANNELS", font(11, bold=True), fill=ACCENT)

    colw = (W / 2 - 14 - 28 - 3 * 16) / 4
    y = 58
    row1 = [("black channel", "1", True), ("white channel", "2", True),
            ("head 1 channel", "1", False), ("head 2 channel", "2", False)]
    for i, (cap, val, en) in enumerate(row1):
        mock_slider(d, ox + 14 + i * (colw + 16), y, colw, cap, val, enabled=en)

    y += 62
    row2 = [("head 3 channel", "3", False), ("head 4 channel", "4", False)]
    for i, (cap, val, en) in enumerate(row2):
        mock_slider(d, ox + 14 + i * (colw + 16), y, colw, cap, val, enabled=en)

    text(d, (ox + 14, y + 60), "greyed = not used by the current Mode/Board (Spiral routes by colour, multi-head modes by playhead)",
         font(9.6), fill=DIM)

    save(img, "channels-foldout", W, H)


def mockup_game_record():
    W, H = 320, 300
    W2, H2 = 760, 486
    img, d = new_canvas(W2, H2)
    x0 = 20
    rrect(d, (14, 14, W2 - 14, H2 - 14), 8, fill=PANEL)
    section_header(d, x0, 28, "GAME RECORD")

    text(d, (x0, 54), "Gruener123 vs FloMo", font(12.5), fill=TEXT)
    text(d, (W2 - 20, 54), "9x9  \u00b7  B+3.5", font(11), fill=DIM, anchor="ra")

    colw = (W2 - 28 - 3 * 16) / 4
    y = 84
    mock_toggle(d, x0, y, colw, "Load SGF\u2026", False)
    mock_combo(d, x0 + colw + 16, y, colw, "move rate", "1 bar")
    mock_toggle(d, x0 + 2 * (colw + 16), y, colw, "Run game", True)
    mock_toggle(d, x0 + 3 * (colw + 16), y, colw, "Loop", False, enabled=False)

    y += 62
    mock_toggle(d, x0, y, colw, "AI self-play", False)
    mock_slider(d, x0 + colw + 16, y, colw, "game length", "60 mv", enabled=False)
    mock_slider(d, x0 + 2 * (colw + 16), y, colw, "variation", "35%", enabled=False)
    mock_slider(d, x0 + 3 * (colw + 16), y, colw, "seed", "1", enabled=False)

    y += 62
    mock_toggle(d, x0, y, colw, "From board", False)
    mock_toggle(d, x0 + colw + 16, y, colw, "Use book", False, enabled=False)
    text(d, (x0, y - 18), "opening", font(10.5), fill=DIM)
    text(d, (x0 + 2 * (colw + 16), y + 6), "the book line", font(11), fill=DIM)

    y += 62
    half_w = (W2 - 28) / 2 - 10
    mock_toggle(d, x0, y, half_w, "Wave replay", True)
    mock_slider(d, x0 + half_w + 20, y, half_w, "wave gap", "20")

    y += 62
    slider_w = (W2 - 28) / 2 - 10
    text(d, (x0, y), "position in the record", font(10), fill=DIM)
    mock_slider(d, x0, y + 16, slider_w, "", "move 62 / 145")

    bx = x0 + slider_w + 20
    text(d, (bx, y), "", font(10))
    prev_w = 60
    rrect(d, (bx, y + 34, bx + prev_w, y + 58), 4, fill=PANEL_BRIGHT)
    text(d, (bx + prev_w / 2, y + 46), "<", font(13, bold=True), fill=TEXT, anchor="mm")
    rrect(d, (bx + prev_w + 6, y + 34, bx + 2 * prev_w + 6, y + 58), 4, fill=PANEL_BRIGHT)
    text(d, (bx + prev_w + 6 + prev_w / 2, y + 46), ">", font(13, bold=True), fill=TEXT, anchor="mm")

    ux = bx + 2 * prev_w + 30
    mock_toggle(d, ux, y - 16, slider_w / 2, "Unload", False)

    text(d, (x0, y + 76), "drag & drop an .sgf file anywhere on the window to load it", font(10), fill=DIM)

    save(img, "game-record-panel", W2, H2)


# =============================================================================
# 5. AI self-play
#
# These two are drawn from ai-games.json, which is not hand written: it is what
# tools/GoAiDump.cpp printed, so every stone below is a move the plugin's own
# players actually made. Regenerate both the data and the pictures with
#
#     cmake --build ../../build --config Release --target GoAiDump
#     ../../build/Release/GoAiDump --games 6 --seed 1 --json ai-games.json
#     python generate.py
# =============================================================================

AI_DATA_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ai-games.json")
AI_OPENING = 10


def load_ai_games():
    with open(AI_DATA_PATH, encoding="utf-8") as f:
        return json.load(f)


def draw_ai_stone(d, cx, cy, radius, black):
    """Like draw_stone, but with a thinner outline: these boards carry fifty
    stones rather than five, and the heavy ring turns them into a blob."""
    d.ellipse([s(cx - radius), s(cy - radius), s(cx + radius), s(cy + radius)],
              fill=BLACK_STONE if black else WHITE_STONE,
              outline=WOOD_DARK if black else LINES, width=sw(1.0))


def draw_ai_board(d, ox, oy, cell, position, size, last_move=None, radius=None):
    """position: one frame of the JSON - "0" empty, "1" black, "2" white."""
    draw_goban(d, ox, oy, cell, size)
    r = radius or cell * 0.41

    for i, ch in enumerate(position):
        if ch == "0":
            continue
        cx, cy = pt(ox, oy, cell, i % size, i // size)
        draw_ai_stone(d, cx, cy, r, ch == "1")

    if last_move is not None and 0 <= last_move < len(position) and position[last_move] != "0":
        cx, cy = pt(ox, oy, cell, last_move % size, last_move // size)
        d.ellipse([s(cx - r * 0.45), s(cy - r * 0.45), s(cx + r * 0.45), s(cy + r * 0.45)],
                  outline=ACCENT, width=sw(1.8))


def mockup_selfplay_games():
    """Six games from one opening: the last position of each, side by side."""
    data = load_ai_games()
    size = data["size"]
    games = data["games"][:6]

    cell = 26
    board_w = cell * (size - 1) + 44
    gap_x, gap_y = 26, 54
    cols, rows = 3, 2

    W = 40 + cols * board_w + (cols - 1) * gap_x
    H = 92 + rows * board_w + (rows - 1) * gap_y + 16

    img, d = caption_bar(W, H, "AI self-play: six games, one opening",
                         "the last position of each - the first ten moves were identical in all of them")

    for n, game in enumerate(games):
        col, row = n % cols, n // cols
        ox = 40 + col * (board_w + gap_x)
        oy = 92 + row * (board_w + gap_y)

        draw_ai_board(d, ox, oy, cell, game["frames"][-1], size,
                      last_move=game["moves"][-1])

        label_y = oy + cell * (size - 1) + 30
        text(d, (ox - 22, label_y), "game %d" % (n + 1), font(10.5, bold=True), fill=TEXT)
        text(d, (ox + board_w - 22, label_y), "%d captured" % game["captures"],
             font(10), fill=DIM, anchor="ra")

    save(img, "self-play-games", W, H)


def selfplay_frame(data, game_index, move, cell=32):
    """One frame of the animation: the board part way through one game, with the
    move counter underneath and the book opening marked out on it."""
    size = data["size"]
    game = data["games"][game_index]
    total = len(game["moves"])

    span = cell * (size - 1)
    W = span + 120
    H = span + 196

    img, d = new_canvas(W, H)

    text(d, (30, 22), "AI SELF-PLAY", font(11, bold=True), fill=ACCENT)
    text(d, (W - 30, 22), "game %d of the run" % (game_index + 1),
         font(11), fill=DIM, anchor="ra")
    text(d, (30, 42), "Kuro (territorial)  vs  Shiro (fighting)", font(11), fill=TEXT)

    ox, oy = 60, 86
    last = game["moves"][move - 1] if move > 0 else None
    draw_ai_board(d, ox, oy, cell, game["frames"][move], size, last_move=last)

    # the progress bar: one cell per move, the book in accent
    bar_y = oy + span + 44
    bar_w = span
    step = bar_w / total

    for i in range(total):
        x = ox + i * step
        played = i < move
        book = i < AI_OPENING

        if book:
            colour = ACCENT if played else tuple(int(c * 0.42) for c in ACCENT)
        else:
            colour = TEXT if played else PANEL_BRIGHT

        d.rectangle([s(x), s(bar_y), s(x + step * 0.72), s(bar_y + 7)], fill=colour)

    label = "opening" if move <= AI_OPENING else "self-play"
    text(d, (ox, bar_y + 20), label, font(10), fill=ACCENT if move <= AI_OPENING else DIM)
    text(d, (ox + bar_w, bar_y + 20), "move %d / %d" % (move, total),
         font(10), fill=DIM, anchor="ra")
    #  every stone that is not on the board was captured, so the count so far
    #  is just the moves played less what is standing
    standing = sum(1 for ch in game["frames"][move] if ch != "0")

    text(d, (ox, bar_y + 40), "seed %d" % game["seed"], font(10), fill=DIM)
    text(d, (ox + bar_w, bar_y + 40), "%d captured" % (move - standing),
         font(10), fill=DIM, anchor="ra")

    return img.resize((W, H), Image.LANCZOS)


def mockup_selfplay_animation(games=3, hold_frames=9):
    """Several games in a row, exactly as a run of them plays: the same ten book
    moves every time, then a different middlegame."""
    data = load_ai_games()

    frames, durations = [], []

    for gi in range(min(games, len(data["games"]))):
        total = len(data["games"][gi]["moves"])

        for move in range(total + 1):
            frames.append(selfplay_frame(data, gi, move))
            # the book moves are held a little longer: that is the part that
            # repeats, and it is what the ear is meant to recognise
            durations.append(150 if move <= AI_OPENING else 90)

        for _ in range(hold_frames):
            frames.append(frames[-1])
            durations.append(120)

    #  one shared palette and disposal 1 (leave the frame in place), so the
    #  encoder only has to store the stones that changed between two moves
    palette = [f.convert("P", palette=Image.ADAPTIVE, colors=32) for f in frames]

    out = "self-play.gif"
    palette[0].save(out, save_all=True, append_images=palette[1:], duration=durations,
                    loop=0, optimize=True, disposal=1)

    print("wrote %s  %d frames  %dx%d  %.1f KB"
          % (out, len(frames), frames[0].width, frames[0].height,
             os.path.getsize(out) / 1024.0))


if __name__ == "__main__":
    mockup_spiral()
    mockup_quads()
    mockup_polyrhythm()
    mockup_interaction()
    mockup_sequencer_panel()
    mockup_channels_foldout()
    mockup_game_record()
    mockup_selfplay_games()
    mockup_selfplay_animation()
