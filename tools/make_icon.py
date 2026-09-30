#!/usr/bin/env python3
"""Draws the MCP DAW application icon (assets/icon_big.png, assets/icon_small.png).

Big (1024 px, used for the large Windows icon sizes): waveform bars + "MCP·DAW" in Audiowide.
Small (256 px, used for 16-48 px): the bars alone, which stay readable when tiny.
Colours are the app theme (window #1c2129, Make Tech teal #3fd0c9). Each bar gets a VU-meter cap
that runs hotter on louder bars: yellow on short bars, orange + yellow on medium ones, red + orange
+ yellow on the tallest - part waveform, part level meter. Re-run after tweaking:
    python tools/make_icon.py
Needs Pillow (pip install pillow).
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
BG, EDGE, TEAL, WHITE = (28, 33, 41, 255), (70, 81, 97, 255), (63, 208, 201, 255), (237, 240, 244, 255)
YELLOW, ORANGE, RED = (245, 211, 59, 255), (255, 138, 42, 255), (255, 59, 59, 255)
CAP_SEGMENT = 0.055                                       # height of one cap colour band, as a fraction of the tallest bar
BARS = [0.30, 0.55, 0.85, 1.00, 0.70, 0.45, 0.62, 0.35]   # a little waveform / level meter
BARS_SMALL = [0.45, 0.80, 1.00, 0.65, 0.40]              # fewer, fatter bars stay crisp at 16-32 px


def meter_cap(height: float) -> list:
    """Cap colours from the top down: louder (taller) bars reach further into the red."""
    if height >= 0.8:
        return [RED, ORANGE, YELLOW]
    if height >= 0.5:
        return [ORANGE, YELLOW]
    return [YELLOW]


def draw(size: int, with_text: bool) -> Image.Image:
    s = 4                                   # supersample for smooth edges
    W = size * s
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad, radius = int(W * 0.04), int(W * 0.20)
    d.rounded_rectangle([pad, pad, W - pad, W - pad], radius=radius, fill=BG, outline=EDGE, width=max(1, W // 128))

    # Waveform bars, centred; a bit higher when the wordmark sits underneath.
    area_top, area_bot = (0.18, 0.62) if with_text else (0.20, 0.80)
    mid = W * (area_top + area_bot) / 2
    max_h = W * (area_bot - area_top)
    bars = BARS if with_text else BARS_SMALL
    n = len(bars)
    bar_w = W * (0.56 if with_text else 0.64) / (n * 1.6)
    gap = bar_w * 0.6
    x = (W - (n * bar_w + (n - 1) * gap)) / 2
    for h in bars:
        bh = max_h * h
        box = [x, mid - bh / 2, x + bar_w, mid + bh / 2]
        # Paint the bar teal with its meter cap, then stamp it through a mask of the rounded bar, so the
        # cap follows the rounded top.
        paint = Image.new("RGBA", (W, W), TEAL)
        pd = ImageDraw.Draw(paint)
        top = box[1]
        for colour in meter_cap(h):
            pd.rectangle([x, top, x + bar_w, top + max_h * CAP_SEGMENT], fill=colour)
            top += max_h * CAP_SEGMENT
        mask = Image.new("L", (W, W), 0)
        ImageDraw.Draw(mask).rounded_rectangle(box, radius=bar_w / 2, fill=255)
        img.paste(paint, (0, 0), mask)
        x += bar_w + gap

    if with_text:
        font = ImageFont.truetype(str(ROOT / "assets" / "Audiowide-Regular.ttf"), int(W * 0.13))
        parts = [("MCP", WHITE), ("·", TEAL), ("DAW", TEAL)]
        widths = [d.textlength(t, font=font) for t, _ in parts]
        x = (W - sum(widths)) / 2
        y = W * 0.70
        for (t, c), w in zip(parts, widths):
            d.text((x, y), t, font=font, fill=c)
            x += w
    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    out = ROOT / "assets"
    draw(1024, True).save(out / "icon_big.png")
    draw(256, False).save(out / "icon_small.png")
    print("wrote", out / "icon_big.png", "and", out / "icon_small.png")
