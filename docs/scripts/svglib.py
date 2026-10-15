# -*- coding: utf-8 -*-
"""Minimal SVG drawing helpers (stdlib only) for generating study diagrams."""

FONT = "font-family:&quot;Microsoft YaHei&quot;,&quot;PingFang SC&quot;,&quot;Noto Sans CJK SC&quot;,sans-serif;"


class SVG:
    def __init__(self, w, h, bg="#ffffff"):
        self.w, self.h = w, h
        self.parts = []
        if bg:
            self.rect(0, 0, w, h, fill=bg, stroke="none")

    def rect(self, x, y, w, h, fill="#eef4fb", stroke="#3b6ea5", sw=1.5, rx=8, opacity=1.0, dash=None):
        d = f" stroke-dasharray='{dash}'" if dash else ""
        self.parts.append(
            f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='{rx}' "
            f"fill='{fill}' fill-opacity='{opacity}' stroke='{stroke}' stroke-width='{sw}'{d}/>")

    def text(self, x, y, s, size=14, fill="#222", anchor="middle", bold=False, rotate=None):
        s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        b = " font-weight='bold'" if bold else ""
        tr = f" transform='rotate({rotate} {x} {y})'" if rotate else ""
        self.parts.append(
            f"<text x='{x}' y='{y}' font-size='{size}' fill='{fill}' text-anchor='{anchor}' "
            f"style='{FONT}'{b}{tr}>{s}</text>")

    def multiline(self, x, y, lines, size=13, fill="#222", anchor="middle", bold=False, lh=None):
        lh = lh or size + 4
        for i, ln in enumerate(lines):
            self.text(x, y + i * lh, ln, size, fill, anchor, bold)

    def line(self, x1, y1, x2, y2, stroke="#3b6ea5", sw=1.5, dash=None):
        d = f" stroke-dasharray='{dash}'" if dash else ""
        self.parts.append(f"<line x1='{x1}' y1='{y1}' x2='{x2}' y2='{y2}' stroke='{stroke}' stroke-width='{sw}'{d}/>")

    def arrow(self, x1, y1, x2, y2, stroke="#3b6ea5", sw=1.5, dash=None):
        import math
        self.line(x1, y1, x2, y2, stroke, sw, dash)
        ang = math.atan2(y2 - y1, x2 - x1)
        L = 9
        a1, a2 = ang + 2.6, ang - 2.6
        p1 = (x2 + L * math.cos(a1), y2 + L * math.sin(a1))
        p2 = (x2 + L * math.cos(a2), y2 + L * math.sin(a2))
        self.parts.append(
            f"<polygon points='{x2},{y2} {p1[0]:.1f},{p1[1]:.1f} {p2[0]:.1f},{p2[1]:.1f}' fill='{stroke}'/>")

    def polyline(self, pts, stroke="#c0392b", sw=2.0, dash=None, fill="none"):
        d = f" stroke-dasharray='{dash}'" if dash else ""
        p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        self.parts.append(f"<polyline points='{p}' fill='{fill}' stroke='{stroke}' stroke-width='{sw}'{d}/>")

    def circle(self, cx, cy, r, fill="#c0392b", stroke="none", sw=0):
        self.parts.append(f"<circle cx='{cx}' cy='{cy}' r='{r}' fill='{fill}' stroke='{stroke}' stroke-width='{sw}'/>")

    def save(self, path):
        body = "".join(self.parts)
        svg = (f"<svg xmlns='http://www.w3.org/2000/svg' width='{self.w}' height='{self.h}' "
               f"viewBox='0 0 {self.w} {self.h}'>{body}</svg>")
        with open(path, "w", encoding="utf-8") as f:
            f.write(svg)
