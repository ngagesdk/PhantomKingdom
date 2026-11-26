#!/usr/bin/env python3
"""PhantomKingdom achievement icon generator.

Renders 512x512 achievement icons for Google Play Games in the PhantomKingdom
look: canvas background, a sticker medallion with black and purple rims, and
the achievement title in the fire-gradient logo lettering. Every icon uses the
same design, so a whole set stays consistent.

Works fully offline. Needs Python 3.8+ with Pillow and NumPy.

    python pk_achievements.py                     # every title in achievements.txt
    python pk_achievements.py "Desert Oasis"      # one or more titles
    python pk_achievements.py --sheet             # also write a preview sheet
"""

import argparse
import math
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = 512                 # final icon size
SS = 3                    # supersampling factor
G = OUT * SS              # working resolution
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
GAME_FONT = os.path.join(REPO, "assets", "fonts", "pk_bold12.fnt")

# Brand palette (PhantomKingdom and PhantomLink logos)
BLACK = (18, 12, 16)
PURPLE = (168, 26, 242)
CREAM = (253, 244, 230)
WHITE = (255, 255, 255)
RED_LINE = (214, 34, 12)
DEFAULT_COLOR = (92, 30, 150)      # medallion center color

# Fire gradient of the logo lettering: (position, color), restarts per line
FIRE = [(0.00, (240, 82, 30)), (0.24, (250, 142, 38)),
        (0.48, (255, 226, 72)), (0.76, (255, 238, 100)),
        (1.00, (238, 186, 38))]

# Layout in design pixels (512 space)
MEDAL_R = 240             # outer edge of the purple rim
SAFE_R = 222              # text (including outlines) stays inside this circle
CAP_MAX = 104             # largest cap height of the title
MAX_LINES = 4


# ---------------------------------------------------------------- helpers

def shade(c, f):
    """f > 0 lightens toward white, f < 0 darkens toward black."""
    if f >= 0:
        return tuple(v + (255 - v) * f for v in c)
    return tuple(v * (1 + f) for v in c)


def parse_color(s):
    m = re.fullmatch(r"#?([0-9a-fA-F]{6})", s.strip())
    if not m:
        raise SystemExit("color must look like #5c1e96: " + s)
    h = m.group(1)
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def slug(title):
    s = re.sub(r"[^a-z0-9]+", "_", title.lower().replace("\\n", " "))
    return s.strip("_") or "achievement"


def dilate(m, r):
    """Grow a mask by r pixels (octagonal structuring element)."""
    out = m
    for i in range(int(round(r))):
        a = out
        b = a.copy()
        np.maximum(b[1:], a[:-1], out=b[1:])
        np.maximum(b[:-1], a[1:], out=b[:-1])
        np.maximum(b[:, 1:], a[:, :-1], out=b[:, 1:])
        np.maximum(b[:, :-1], a[:, 1:], out=b[:, :-1])
        if i % 2:
            c = b.copy()
            np.maximum(c[1:, 1:], b[:-1, :-1], out=c[1:, 1:])
            np.maximum(c[:-1, :-1], b[1:, 1:], out=c[:-1, :-1])
            np.maximum(c[1:, :-1], b[:-1, 1:], out=c[1:, :-1])
            np.maximum(c[:-1, 1:], b[1:, :-1], out=c[:-1, 1:])
            b = c
        out = b
    return out


def blur(m, r):
    img = Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8), "L")
    return np.asarray(img.filter(ImageFilter.GaussianBlur(r)), np.float32) / 255.0


def shift_down(m, dy):
    out = np.zeros_like(m)
    out[dy:] = m[:-dy]
    return out


def over(px, color, alpha):
    a = np.clip(alpha, 0, 1)[..., None]
    return px * (1 - a) + np.asarray(color, np.float32) * a


def disc(r, cy=OUT / 2):
    y, x = np.mgrid[0:G, 0:G].astype(np.float32) + 0.5
    d = np.sqrt((x - G / 2) ** 2 + (y - cy * SS) ** 2)
    return np.clip(r * SS - d + 0.5, 0, 1)


# ---------------------------------------------------------------- fonts

class BitmapFont:
    """AngelCode BMFont text file, e.g. the game's bundled pk_bold12."""
    integer = True

    def __init__(self, path):
        self.chars = {}
        page = None
        with open(path, encoding="utf-8") as f:
            for line in f:
                kv = dict(re.findall(r'(\w+)=("[^"]*"|\S+)', line))
                if line.startswith("common"):
                    self.line_h, self.base = int(kv["lineHeight"]), int(kv["base"])
                elif line.startswith("page"):
                    page = kv["file"].strip('"')
                elif line.startswith("char "):
                    self.chars[int(kv["id"])] = {k: int(v) for k, v in kv.items()
                                                 if re.fullmatch(r"-?\d+", v)}
        atlas = Image.open(os.path.join(os.path.dirname(path), page)).convert("LA")
        a = np.asarray(atlas.getchannel("A"), np.float32) / 255.0
        lum = np.asarray(atlas.getchannel("L"), np.float32) / 255.0
        self.atlas = a * lum if a.min() < 1 else lum
        self.cap = self.base

    def glyph(self, ch):
        g = self.chars.get(ord(ch))
        if g is None:
            print("  warning: no glyph for %r, using '?'" % ch, file=sys.stderr)
            g = self.chars[ord("?")]
        return g

    def width(self, text, k):
        x = right = 0
        for ch in text:
            g = self.glyph(ch)
            right = max(right, x + g["xoffset"] + g["width"])
            x += g["xadvance"]
        return right * k

    def render(self, text, k):
        m = np.zeros((self.line_h, max(1, int(self.width(text, 1)))), np.float32)
        x = 0
        for ch in text:
            g = self.glyph(ch)
            src = self.atlas[g["y"]:g["y"] + g["height"], g["x"]:g["x"] + g["width"]]
            y0, x0 = g["yoffset"], x + g["xoffset"]
            h = min(src.shape[0], self.line_h - y0)
            dst = m[y0:y0 + h, x0:x0 + src.shape[1]]
            np.maximum(dst, src[:h], out=dst)
            x += g["xadvance"]
        return np.kron(m, np.ones((int(k), int(k)), np.float32))


class TTFont:
    """Any TrueType/OpenType font, scaled so its cap height is 10 units."""
    integer = False
    line_h, base, cap = 12, 10, 10

    def __init__(self, path):
        self.path = path
        b = ImageFont.truetype(path, 200).getbbox("H", anchor="ls")
        self.cap_ratio = -b[1] / 200.0
        self.cache = {}

    def font(self, k):
        size = max(4, int(round(10 * k / self.cap_ratio)))
        if size not in self.cache:
            self.cache[size] = ImageFont.truetype(self.path, size)
        return self.cache[size]

    def width(self, text, k):
        b = self.font(k).getbbox(text, anchor="ls")
        return b[2] - min(b[0], 0)

    def render(self, text, k):
        f = self.font(k)
        b = f.getbbox(text, anchor="ls")
        img = Image.new("L", (int(math.ceil(b[2] - min(b[0], 0))) + 2,
                              int(round(self.line_h * k))), 0)
        ImageDraw.Draw(img).text((-min(b[0], 0), self.base * k), text,
                                 font=f, fill=255, anchor="ls")
        return np.asarray(img, np.float32) / 255.0


def load_font(spec):
    if spec == "game":
        if not os.path.exists(GAME_FONT):
            raise SystemExit("game font not found: " + GAME_FONT)
        return BitmapFont(GAME_FONT)
    if not os.path.exists(spec):
        raise SystemExit("font not found: " + spec)
    return BitmapFont(spec) if spec.lower().endswith(".fnt") else TTFont(spec)


# ---------------------------------------------------------------- layout

def outlines(cap):
    """Cumulative outline radii (white, red, black) in working pixels."""
    w = max(1.0, cap * 0.03)
    r = w + cap * 0.045
    return w, r, r + max(3 * SS, cap * 0.12)


def splits(words):
    """All ways to break the words into 1..MAX_LINES lines, in order."""
    res = []

    def rec(i, acc):
        if i == len(words):
            res.append(acc)
            return
        rest = len(words) - i
        if len(acc) == MAX_LINES - 1:
            res.append(acc + [" ".join(words[i:])])
            return
        for j in range(i + 1, i + rest + 1):
            rec(j, acc + [" ".join(words[i:j])])
    rec(0, [])
    return res


def fit(title, font):
    """Largest scale (and best line breaks) at which the title fits the circle."""
    if "\\n" in title:
        cands = [[s.strip() for s in title.split("\\n") if s.strip()]]
    else:
        cands = splits(title.split())
    cmax = CAP_MAX * SS / font.cap
    ks = (list(range(int(cmax), 0, -1)) if font.integer
          else [cmax * 0.985 ** i for i in range(200)])
    r2 = (SAFE_R * SS) ** 2
    c = G / 2
    for k in ks:
        pad = outlines(font.cap * k)[2]
        pitch = font.line_h * k
        best = None
        for lines in cands:
            n = len(lines)
            # visual block: cap top of line 1 to baseline of the last line
            top = c - ((n - 1) * pitch + font.cap * k) / 2
            ok = True
            for i, line in enumerate(lines):
                y0 = top + i * pitch - pad
                y1 = top + i * pitch + font.line_h * k + pad
                dy = max(abs(y0 - c), abs(y1 - c))
                half = font.width(line, k) / 2 + pad
                if dy * dy >= r2 or half > math.sqrt(r2 - dy * dy):
                    ok = False
                    break
            if ok:
                # prefer fewer lines, then the most even line lengths
                ws = [font.width(l, 1) for l in lines]
                score = (n, max(ws) - min(ws))
                if best is None or score < best[0]:
                    best = (score, lines, top)
        if best:
            return best[1], k, best[2], pitch
    raise SystemExit("title does not fit: " + title)


# ---------------------------------------------------------------- render

def background(seed):
    y, x = np.mgrid[0:G, 0:G].astype(np.float32) / G
    rng = np.random.default_rng(seed)
    light = np.array((240, 214, 172), np.float32)
    dark = np.array((212, 172, 122), np.float32)
    px = light + (dark - light) * (y[..., None] * 0.6 + 0.2)
    fold = np.sin(y * 2 * math.pi * 4.2 + np.sin(x * math.pi * 2.0 + 1.7) * 1.3)
    fold = np.sign(fold) * np.abs(fold) ** 0.6
    f = 1 + 0.055 * fold + rng.normal(0, 0.012, (G, G)).astype(np.float32)
    px = px * f[..., None]
    d = np.sqrt((x - 0.5) ** 2 + (y - 0.5) ** 2)
    vig = np.clip((d - 0.30) / 0.45, 0, 1) ** 1.6
    return px + (np.array((112, 70, 36), np.float32) - px) * (vig[..., None] * 0.75)


def medallion(px, color):
    px = over(px, (60, 32, 14), blur(disc(MEDAL_R - 2, cy=OUT / 2 + 8), 10 * SS) * 0.55)
    px = over(px, PURPLE, disc(MEDAL_R))
    px = over(px, BLACK, disc(MEDAL_R - 8))
    y, x = np.mgrid[0:G, 0:G].astype(np.float32) + 0.5
    dx, dy = x - G / 2, y - (G / 2 - 40 * SS)
    t = np.clip(np.sqrt(dx ** 2 + dy ** 2) / (MEDAL_R * SS), 0, 1)[..., None]
    c0 = np.array(shade(color, 0.30), np.float32)
    c1 = np.array(shade(color, -0.65), np.float32)
    fill = c0 + (c1 - c0) * t
    rays = (np.sin(np.arctan2(dy, dx) * 12) > 0).astype(np.float32)[..., None]
    fill = fill * (1 + 0.06 * rays * (1 - t))
    px = over(px, fill, disc(MEDAL_R - 22))
    ring = disc(MEDAL_R - 28) - disc(MEDAL_R - 34)
    return over(px, WHITE, ring * 0.10)


def title(px, font, lines, k, top, pitch):
    mask = np.zeros((G, G), np.float32)
    tops = []
    for i, line in enumerate(lines):
        lm = font.render(line, k)
        h, w = lm.shape
        x, y = int(round((G - w) / 2)), int(round(top + i * pitch))
        mask[y:y + h, x:x + w] = np.maximum(mask[y:y + h, x:x + w], lm)
        tops.append(y)
    cap = font.cap * k
    w_r, r_r, b_r = outlines(cap)
    hard = (mask > 0.5).astype(np.float32)
    black = dilate(hard, b_r)
    px = over(px, (10, 4, 18), blur(shift_down(black, 8 * SS), 5 * SS) * 0.55)
    px = over(px, BLACK, blur(black, 0.6))
    px = over(px, RED_LINE, blur(dilate(hard, r_r), 0.6))
    px = over(px, CREAM, blur(dilate(hard, w_r), 0.6))
    # fire gradient, restarted for every line
    yy = np.arange(G, dtype=np.float32)
    t = np.zeros(G, np.float32)
    for i, y0 in enumerate(tops):
        lo = y0 if i else 0
        hi = tops[i + 1] if i + 1 < len(tops) else G
        sel = (yy >= lo) & (yy < hi)
        t[sel] = np.clip((yy[sel] - y0) / cap, 0, 1)
    pos = np.array([p for p, _ in FIRE], np.float32)
    grad = np.stack([np.interp(t, pos, [c[ch] for _, c in FIRE]) for ch in range(3)], 1)
    fill = np.broadcast_to(grad[:, None, :].astype(np.float32), (G, G, 3))
    noise = np.random.default_rng(7).normal(0, 0.035, (G, G, 1)).astype(np.float32)
    return over(px, fill * (1 + noise), mask)


def render(text, font, color):
    lines, k, top, pitch = fit(text, font)
    px = background(seed=1)
    px = medallion(px, color)
    px = title(px, font, lines, k, top, pitch)
    img = Image.fromarray(np.clip(px, 0, 255).astype(np.uint8), "RGB")
    return img.resize((OUT, OUT), Image.LANCZOS)


# ---------------------------------------------------------------- cli

def read_list(path):
    with open(path, encoding="utf-8") as f:
        return [l.strip() for l in f if l.strip() and not l.strip().startswith("#")]


def contact_sheet(paths, out_path, cols=4):
    cell, gap = 256, 16
    rows = (len(paths) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (cell + gap) + gap, rows * (cell + gap) + gap),
                      (40, 30, 44))
    for i, p in enumerate(paths):
        im = Image.open(p).resize((cell, cell), Image.LANCZOS)
        sheet.paste(im, (gap + (i % cols) * (cell + gap), gap + (i // cols) * (cell + gap)))
    sheet.save(out_path)


def main():
    ap = argparse.ArgumentParser(description="PhantomKingdom achievement icon generator")
    ap.add_argument("titles", nargs="*", help="titles to render (default: the list file)")
    ap.add_argument("--list", default=os.path.join(HERE, "achievements.txt"),
                    help="file with one title per line (default: achievements.txt)")
    ap.add_argument("--out", default=os.path.join(HERE, "out"),
                    help="output folder (default: out)")
    ap.add_argument("--font", default="game",
                    help="'game' (bundled pk_bold12), a .fnt file or a .ttf/.otf file")
    ap.add_argument("--color", default=None,
                    help="medallion color as #rrggbb (default #5c1e96)")
    ap.add_argument("--sheet", action="store_true",
                    help="also write _sheet.png with all icons for a quick review")
    args = ap.parse_args()

    titles = args.titles or read_list(args.list)
    if not titles:
        raise SystemExit("nothing to render")
    font = load_font(args.font)
    color = parse_color(args.color) if args.color else DEFAULT_COLOR
    os.makedirs(args.out, exist_ok=True)

    written = []
    for t in titles:
        img = render(t, font, color)
        assert img.size == (OUT, OUT) and img.mode == "RGB"
        path = os.path.join(args.out, slug(t) + ".png")
        img.save(path, optimize=True)
        written.append(path)
        print("%-30s -> %s" % (t.replace("\\n", " "), path))
    if args.sheet:
        sp = os.path.join(args.out, "_sheet.png")
        contact_sheet(written, sp)
        print("preview sheet -> " + sp)


if __name__ == "__main__":
    main()
