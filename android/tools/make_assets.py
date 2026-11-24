"""Builds the app icon, logo and Play Store graphics from the two provided images."""
import sys, os
import numpy as np
import scipy.ndimage as nd
from PIL import Image, ImageDraw, ImageFont

ICON_SRC, WORDMARK_SRC, ROOT = sys.argv[1], sys.argv[2], sys.argv[3]
RES = f"{ROOT}/app/src/main/res"
STORE = f"{ROOT}/fastlane/metadata/android/en-US/images"
os.makedirs(STORE, exist_ok=True)

icon = Image.open(ICON_SRC).convert("RGB")          # 2000x2000, cloth + ghost + wordmark
wordmark = Image.open(WORDMARK_SRC).convert("RGBA")  # 1714x885, transparent
a = np.asarray(icon).astype(int)

# The wordmark has a transparent gap inside its black outline (left of the
# "P"); fill enclosed transparent pixels with the outline black.
w = np.asarray(wordmark).copy()
opaque = w[:, :, 3] > 128
gaps = nd.binary_fill_holes(opaque) & ~opaque
gaps = nd.binary_dilation(gaps, iterations=1) & (w[:, :, 3] < 255)
w[gaps] = [0, 0, 0, 255]
print("wordmark gap pixels filled:", int(gaps.sum()))
wordmark = Image.fromarray(w, "RGBA")
N = icon.width

# ---- logo for the screen header ----------------------------------------
os.makedirs(f"{RES}/drawable-nodpi", exist_ok=True)
h = 360
wordmark.resize((round(wordmark.width * h / wordmark.height), h), Image.LANCZOS).save(f"{RES}/drawable-nodpi/logo.png", optimize=True)

# ---- adaptive icon -------------------------------------------------------
# Foreground: the sticker (ghost + wordmark) cut out along its black/purple
# outline, with a soft shadow like on the cloth. Background: a patch of the
# cloth above the sticker. The sticker is scaled to fit the 66 dp safe zone.
dark = nd.binary_opening(a.sum(2) < 90, iterations=2)
lab, n = nd.label(dark)
outline = lab == (np.argmax(nd.sum(dark, lab, range(1, n + 1))) + 1)
purple = (a[:, :, 2] > 170) & (a[:, :, 1] < 90) & (a[:, :, 0] > 90)
sticker = nd.binary_fill_holes(nd.binary_closing(outline | purple, iterations=3))
ys, xs = np.where(sticker)
cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
half_diag = np.hypot((xs.max() - xs.min()) / 2, (ys.max() - ys.min()) / 2)
canvas = int(round(108 * half_diag / 33))
ox, oy = int(round(canvas / 2 - cx)), int(round(canvas / 2 - cy))

def place(arr):
    """Puts an N x N array onto the canvas at the sticker offset."""
    out = np.zeros((canvas, canvas) + arr.shape[2:], arr.dtype)
    out[oy:oy + N, ox:ox + N] = arr
    return out

alpha = nd.gaussian_filter(sticker.astype(float), 1.0)
shadow = nd.shift(nd.gaussian_filter(sticker.astype(float), 18), (22, 6), order=1) * 0.45
fg_rgba = np.dstack([a, alpha * 255]).astype(np.uint8)
sh_rgba = np.dstack([np.full_like(a, 0)[:, :, :1] + 70, np.full_like(a, 0)[:, :, :1] + 40,
                     np.full_like(a, 0)[:, :, :1] + 15, shadow * 255]).astype(np.uint8)
fg_img = Image.fromarray(place(sh_rgba), "RGBA")
fg_img.alpha_composite(Image.fromarray(place(fg_rgba), "RGBA"))

patch = icon.crop((835, 0, 1165, 330))   # plain cloth, top middle
bg_img = patch.resize((canvas, canvas), Image.BICUBIC)

# Monochrome layer (themed icons): letters, ghost and speed lines as one tint.
fg = (a[:, :, 1] > 85) & (a.max(2) > 120) & nd.binary_erosion(sticker, iterations=3)
fg = nd.binary_opening(fg, iterations=2)
lab, n = nd.label(fg)
fg = np.isin(lab, np.where(nd.sum(fg, lab, range(1, n + 1)) > 400)[0] + 1)
fg = nd.binary_closing(fg, iterations=2)
holes = nd.binary_fill_holes(fg) & ~fg                    # fill the speckles, keep letter counters
lab, n = nd.label(holes)
fg = fg | np.isin(lab, np.where(nd.sum(holes, lab, range(1, n + 1)) < 600)[0] + 1)
mono = place((fg * 255).astype(np.uint8))
mono_img = Image.merge("RGBA", [Image.new("L", (canvas, canvas), 255)] * 3 + [Image.fromarray(mono)])

for dpi, size in {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}.items():
    d = f"{RES}/mipmap-{dpi}"
    os.makedirs(d, exist_ok=True)
    bg_img.resize((size, size), Image.LANCZOS).save(f"{d}/ic_launcher_background.png", optimize=True)
    fg_img.resize((size, size), Image.LANCZOS).save(f"{d}/ic_launcher_foreground.png", optimize=True)
    mono_img.resize((size, size), Image.LANCZOS).save(f"{d}/ic_launcher_monochrome.png", optimize=True)

# Legacy icons for Android 7 (minSdk 24; adaptive icons start at 8.0):
# the two layers flattened, the visible 72 dp middle, rounded square and circle.
from PIL import ImageDraw
flat = bg_img.convert("RGBA")
flat.alpha_composite(fg_img)
inner = round(canvas * 72 / 108)
off = (canvas - inner) // 2
flat = flat.crop((off, off, off + inner, off + inner))
for dpi, size in {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}.items():
    big = size * 4
    img = flat.resize((big, big), Image.LANCZOS)
    for name, shape in (("ic_launcher", "square"), ("ic_launcher_round", "circle")):
        m = Image.new("L", (big, big), 0)
        d = ImageDraw.Draw(m)
        pad_px = round(big * 2 / 48)   # the usual 1 dp margin each side at 48 dp, scaled
        box = (pad_px, pad_px, big - pad_px - 1, big - pad_px - 1)
        if shape == "circle":
            d.ellipse(box, fill=255)
        else:
            d.rounded_rectangle(box, radius=round(big * 0.18), fill=255)
        out = Image.new("RGBA", (big, big), (0, 0, 0, 0))
        out.paste(img, (0, 0), m)
        out.resize((size, size), Image.LANCZOS).save(f"{RES}/mipmap-{dpi}/{name}.png", optimize=True)

# ---- Play Store icon (512, full square; Play rounds the corners) ------
store_icon = icon.resize((512, 512), Image.LANCZOS).convert("RGBA")
store_icon.save(f"{STORE}/icon.png", optimize=True)
store_icon.save(f"{ROOT}/app/src/main/ic_launcher-playstore.png", optimize=True)

# ---- feature graphic 1024x500, same layout as PhantomLink's ------------
BG, INK, PURPLE = (245, 238, 255), (46, 0, 80), (138, 18, 214)
fgx = Image.new("RGB", (1024, 500), BG)
wh = 250
wm = wordmark.resize((round(wordmark.width * wh / wordmark.height), wh), Image.LANCZOS)
fgx.paste(wm, ((1024 - wm.width) // 2, 70), wm)
font = ImageFont.truetype("/usr/share/fonts/opentype/inter/Inter-Medium.otf", 34)
parts = [("Your kingdom, ", INK), ("online", PURPLE), (" again.", INK)]
draw = ImageDraw.Draw(fgx)
total = sum(draw.textlength(t, font=font) for t, _ in parts)
x = (1024 - total) / 2
for t, c in parts:
    draw.text((x, 362), t, font=font, fill=c)
    x += draw.textlength(t, font=font)
fgx.save(f"{STORE}/featureGraphic.png", optimize=True)
print("assets written")
