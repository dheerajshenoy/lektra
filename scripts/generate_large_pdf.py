#!/usr/bin/env python3
"""
Generate a huge PDF (e.g., 10k pages) to stress-test PDF viewers.
Includes optional heavy vector figures and raster images for benchmarking.

Requires:
  pip install reportlab
  pip install pillow  # only if images are enabled (default)

Examples:
  python gen_big_pdf.py out.pdf
  python gen_big_pdf.py out.pdf --pages 10000 --font-size 10 --with-rects --vary-sizes
  python gen_big_pdf.py out.pdf --pages 2000 --image-px 2200 --images-per-page 3
  python gen_big_pdf.py out.pdf --pages 2000 --fast
"""

import argparse
import math
import random
from dataclasses import dataclass
from functools import lru_cache
from io import BytesIO

from reportlab.lib.pagesizes import A4, LETTER, landscape
from reportlab.lib.units import mm
from reportlab.lib.utils import ImageReader
from reportlab.pdfgen import canvas

LOREM = (
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
    "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
    "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi "
    "ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit "
    "in voluptate velit esse cillum dolore eu fugiat nulla pariatur. "
    "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia "
    "deserunt mollit anim id est laborum."
)


def wrap_lines(text: str, max_chars: int):
    words = text.split()
    line = []
    n = 0
    for w in words:
        add = len(w) + (1 if line else 0)
        if n + add > max_chars:
            yield " ".join(line)
            line = [w]
            n = len(w)
        else:
            if line:
                line.append(w)
                n += add
            else:
                line = [w]
                n = len(w)
    if line:
        yield " ".join(line)


def page_size_for(i: int, vary: bool):
    if not vary:
        return A4
    # Cycle through a few sizes/orientations to stress dimension handling.
    mod = i % 4
    if mod == 0:
        return A4
    if mod == 1:
        return LETTER
    if mod == 2:
        return landscape(A4)
    return landscape(LETTER)


@dataclass(frozen=True)
class ImageConfig:
    enabled: bool
    per_page: int
    px: int
    variants: int
    seed: int
    fast: bool


def _require_pillow():
    try:
        from PIL import Image  # noqa: F401
    except Exception as exc:
        raise SystemExit(
            "Pillow is required for --no-images to be false. "
            "Install it with: pip install pillow"
        ) from exc


@lru_cache(maxsize=8)
def _make_image_reader(px: int, variant: int, seed: int) -> ImageReader:
    # Generate a heavy raster image (noise + shapes) to stress decoding.
    from PIL import Image, ImageDraw, ImageFilter

    rng = random.Random(seed + variant * 10007)
    img = Image.effect_noise((px, px), rng.uniform(30.0, 90.0)).convert("RGB")

    # Add colorful shapes to avoid trivial compression.
    draw = ImageDraw.Draw(img)
    for _ in range(120):
        x0 = rng.randint(0, px - 1)
        y0 = rng.randint(0, px - 1)
        x1 = rng.randint(x0, min(px, x0 + rng.randint(20, px // 2)))
        y1 = rng.randint(y0, min(px, y0 + rng.randint(20, px // 2)))
        color = (rng.randrange(256), rng.randrange(256), rng.randrange(256))
        if rng.random() < 0.5:
            draw.rectangle([x0, y0, x1, y1], outline=color, fill=None)
        else:
            draw.ellipse([x0, y0, x1, y1], outline=color, fill=None)

    img = img.filter(ImageFilter.GaussianBlur(radius=rng.uniform(0.5, 1.5)))

    bio = BytesIO()
    img.save(bio, format="PNG", optimize=False)
    bio.seek(0)
    return ImageReader(bio)


@lru_cache(maxsize=8)
def _figure_variants(w: float, h: float, complexity: int, variants: int, seed: int):
    # Precompute heavy figure points for speed; reused across pages.
    data = []
    cx, cy = w * 0.5, h * 0.55
    radius = min(w, h) * 0.38
    step = max(6.0, min(w, h) / 80.0)
    grid = []
    y = h * 0.15
    while y < h * 0.45:
        x = w * 0.1
        while x < w * 0.9:
            grid.append((x, y))
            x += step
        y += step
    for v in range(variants):
        rng = random.Random(seed + v * 137)
        colors = [(rng.random(), rng.random(), rng.random()) for _ in range(4)]
        paths = []
        for k in range(4):
            pts = []
            for t in range(complexity):
                a = (2 * math.pi * t) / complexity
                r = radius * (0.35 + 0.65 * math.sin(a * (k + 2)))
                x = cx + r * math.cos(a * (k + 1))
                y = cy + r * math.sin(a * (k + 3))
                pts.append((x, y))
            paths.append(pts)
        data.append((colors, paths, step * 0.35, grid))
    return data


def draw_heavy_figures(
    c: canvas.Canvas,
    w: float,
    h: float,
    complexity: int,
    variant: int,
    seed: int,
):
    # Dense vector shapes to stress vector rendering (cached per size).
    c.saveState()
    c.setLineWidth(0.4)
    variants = _figure_variants(w, h, complexity, 4, seed)
    colors, paths, grid_r, grid = variants[variant % len(variants)]
    for color, pts in zip(colors, paths):
        c.setStrokeColorRGB(*color)
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            c.line(x1, y1, x2, y2)

    c.setStrokeColorRGB(0.2, 0.2, 0.2)
    for x, y in grid:
        c.circle(x, y, grid_r, stroke=1, fill=0)
    c.restoreState()


@lru_cache(maxsize=8)
def _rects_for_size(w: float, h: float, seed: int):
    rng = random.Random(seed)
    rects = []
    margin = 15 * mm
    n_rects = 80
    for _ in range(n_rects):
        rw = rng.uniform(5, 40) * mm
        rh = rng.uniform(3, 25) * mm
        rx = rng.uniform(margin, max(margin, w - margin - rw))
        ry = rng.uniform(margin, max(margin, h - margin - rh))
        rects.append((rx, ry, rw, rh))
    return rects


@lru_cache(maxsize=8)
def _image_positions(w: float, h: float, per_page: int, seed: int):
    rng = random.Random(seed)
    margin = 15 * mm
    positions = []
    max_w = w - 2 * margin
    max_h = h - 2 * margin
    for _ in range(per_page):
        scale = rng.uniform(0.35, 0.7)
        draw_w = max_w * scale
        draw_h = draw_w
        if draw_h > max_h * 0.8:
            draw_h = max_h * 0.8
            draw_w = draw_h
        x = rng.uniform(margin, max(margin, w - margin - draw_w))
        y = rng.uniform(margin + 20, max(margin + 20, h - margin - draw_h))
        positions.append((x, y, draw_w, draw_h))
    return positions


def draw_page(
    c: canvas.Canvas,
    i: int,
    w: float,
    h: float,
    font_name: str,
    font_size: int,
    with_rects: bool,
    figure_complexity: int,
    img_cfg: ImageConfig,
):
    margin = 15 * mm
    x0, y0 = margin, h - margin

    c.setFont(font_name, font_size + 4)
    c.drawString(x0, y0, f"Stress Test PDF — Page {i}")

    c.setFont(font_name, font_size)
    c.drawString(
        x0,
        y0 - 18,
        f"Page size: {w:.1f} x {h:.1f} pts    (font {font_name} {font_size}pt)",
    )

    # Add a “body” of wrapped text lines
    body_top = y0 - 45
    max_chars = max(
        40, int((w - 2 * margin) / (font_size * 0.55))
    )  # crude but good enough
    lines = list(wrap_lines(LOREM * 6, max_chars))

    y = body_top
    line_h = font_size * 1.25
    for ln in lines:
        if y < margin + 60:
            break
        c.drawString(x0, y, ln)
        y -= line_h

    # Add a simple vector “chart” so rendering isn’t just text
    chart_x = x0
    chart_y = margin + 20
    chart_w = min(140 * mm, w - 2 * margin)
    chart_h = 35 * mm

    c.rect(chart_x, chart_y, chart_w, chart_h, stroke=1, fill=0)
    # Sine-ish polyline
    pts = []
    steps = 120
    for k in range(steps + 1):
        t = (k / steps) * 2 * math.pi
        yy = math.sin(t + i * 0.01) * 0.45 + 0.5  # [~0.05, ~0.95]
        px = chart_x + (k / steps) * chart_w
        py = chart_y + yy * chart_h
        pts.append((px, py))
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        c.line(x1, y1, x2, y2)

    c.setFont(font_name, max(6, font_size - 2))
    c.drawString(
        chart_x, chart_y + chart_h + 6, "Vector polyline (varies slightly per page)"
    )

    if figure_complexity > 0:
        draw_heavy_figures(
            c,
            w,
            h,
            figure_complexity,
            variant=i,
            seed=img_cfg.seed,
        )

    # Optional: sprinkle rectangles to increase object count (can slow generation/viewing)
    if with_rects:
        if img_cfg.fast:
            rects = _rects_for_size(w, h, img_cfg.seed)
            for rx, ry, rw, rh in rects:
                c.rect(rx, ry, rw, rh, stroke=1, fill=0)
        else:
            rng = random.Random(i)  # deterministic per page
            n_rects = 80
            for _ in range(n_rects):
                rw = rng.uniform(5, 40) * mm
                rh = rng.uniform(3, 25) * mm
                rx = rng.uniform(margin, max(margin, w - margin - rw))
                ry = rng.uniform(margin, max(margin, h - margin - rh))
                c.rect(rx, ry, rw, rh, stroke=1, fill=0)

    # Heavy raster images
    if img_cfg.enabled and img_cfg.per_page > 0:
        rng = random.Random(img_cfg.seed + i * 7919)
        positions = (
            _image_positions(w, h, img_cfg.per_page, img_cfg.seed)
            if img_cfg.fast
            else None
        )
        for idx in range(img_cfg.per_page):
            variant = rng.randrange(max(1, img_cfg.variants))
            img = _make_image_reader(img_cfg.px, variant, img_cfg.seed)
            iw, ih = img.getSize()
            if positions is not None:
                x, y, draw_w, draw_h = positions[idx]
                draw_h = draw_w * (ih / iw)
            else:
                max_w = w - 2 * margin
                max_h = h - 2 * margin
                scale = rng.uniform(0.35, 0.7)
                draw_w = min(max_w, max_w * scale)
                draw_h = draw_w * (ih / iw)
                if draw_h > max_h * 0.8:
                    draw_h = max_h * 0.8
                    draw_w = draw_h * (iw / ih)
                x = rng.uniform(margin, max(margin, w - margin - draw_w))
                y = rng.uniform(margin + 20, max(margin + 20, h - margin - draw_h))
            c.drawImage(img, x, y, draw_w, draw_h, mask=None, preserveAspectRatio=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("output", help="Output PDF path, e.g. big.pdf")
    ap.add_argument(
        "--pages", type=int, default=10000, help="Number of pages (default: 10000)"
    )
    ap.add_argument(
        "--font", default="Helvetica", help="ReportLab font name (default: Helvetica)"
    )
    ap.add_argument(
        "--font-size", type=int, default=11, help="Body font size (default: 11)"
    )
    ap.add_argument(
        "--with-rects", action="store_true", help="Add many vector rectangles per page"
    )
    ap.add_argument(
        "--no-images",
        action="store_true",
        help="Disable heavy raster images (enabled by default)",
    )
    ap.add_argument(
        "--images-per-page",
        type=int,
        default=2,
        help="Number of images to draw per page (default: 2)",
    )
    ap.add_argument(
        "--image-px",
        type=int,
        default=1600,
        help="Image side length in pixels (default: 1600)",
    )
    ap.add_argument(
        "--image-variants",
        type=int,
        default=3,
        help="Number of distinct image variants reused across pages (default: 3)",
    )
    ap.add_argument(
        "--figure-complexity",
        type=int,
        default=1200,
        help="Complexity of heavy vector figures (0 to disable, default: 1200)",
    )
    ap.add_argument(
        "--fast",
        action="store_true",
        help="Favor faster generation by reusing cached figures/placements",
    )
    ap.add_argument(
        "--vary-sizes",
        action="store_true",
        help="Vary page sizes/orientations across pages",
    )
    ap.add_argument(
        "--seed", type=int, default=0, help="Seed for any randomness (default: 0)"
    )
    args = ap.parse_args()

    random.seed(args.seed)

    img_cfg = ImageConfig(
        enabled=not args.no_images,
        per_page=max(0, args.images_per_page),
        px=max(64, args.image_px),
        variants=max(1, args.image_variants),
        seed=args.seed,
        fast=args.fast,
    )

    if img_cfg.enabled:
        _require_pillow()

    # Start with an initial page size; we'll change it per page if requested.
    w, h = page_size_for(1, args.vary_sizes)
    c = canvas.Canvas(
        args.output, pagesize=(w, h), pageCompression=0 if args.fast else 1
    )

    for i in range(1, args.pages + 1):
        w, h = page_size_for(i, args.vary_sizes)
        c.setPageSize((w, h))
        draw_page(
            c,
            i,
            w,
            h,
            args.font,
            args.font_size,
            args.with_rects,
            args.figure_complexity,
            img_cfg,
        )
        c.showPage()

        # Progress every 500 pages
        if i % 500 == 0:
            print(f"Generated {i}/{args.pages} pages...")

    c.save()
    print(f"Done: wrote {args.pages} pages to {args.output}")


if __name__ == "__main__":
    main()
