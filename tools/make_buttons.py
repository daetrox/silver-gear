#!/usr/bin/env python3
"""Dibuja por código las bandas y los iconos de los botones (assets/ui/), sin arte de terceros.

  btn_normal.png     banda oscura semitransparente con borde de pincel seco y separador claro
  btn_selected.png   banda blanca con grano, separador oscuro y el extremo derecho deshilachado
  row_selected.png   la banda blanca sin separador (filas de listas y avisos)
  icon_*.png         iconos blancos con alfa (el shader los tiñe): play, achievements, options,
                     credits, quit, disc, home

Las bandas miden 513x88, como las de la maqueta, para que encajen en las mismas posiciones del menú.
Requiere: numpy, pillow. Uso: python3 tools/make_buttons.py
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "ui")
BW, BH = 513, 88
SS = 4  # supermuestreo
rng = np.random.default_rng(1998)

# Paralelogramo de la banda (en píxeles de la banda): inclinado como un trazo hacia la derecha.
TL, TR, BR, BLp = (42, 7), (508, 7), (472, 81), (6, 81)
SEP_X = 156  # separador entre icono y texto (como en la maqueta)


def noise1d(n, scale, octaves=3):
    """Ruido suave en 1D (0..1) de longitud n."""
    out = np.zeros(n)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        k = max(2, int(n / scale * 2 ** o))
        pts = rng.random(k + 1)
        out += amp * np.interp(np.linspace(0, k, n), np.arange(k + 1), pts)
        total += amp
        amp *= 0.5
    return out / total


def poly_mask(points, w, h, ss=SS, outline=0):
    img = Image.new("L", (w * ss, h * ss), 0)
    d = ImageDraw.Draw(img)
    pts = [(x * ss, y * ss) for x, y in points]
    if outline:
        d.line(pts + [pts[0]], fill=255, width=outline * ss, joint="curve")
    else:
        d.polygon(pts, fill=255)
    return np.asarray(img.resize((w, h), Image.LANCZOS)).astype(np.float32) / 255.0


def band(selected, separator=True):
    W, H = BW, BH
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    body = poly_mask([TL, TR, BR, BLp], W, H)

    # Extremo derecho deshilachado: cada fila acaba en un sitio distinto y deja cerdas sueltas.
    right_edge = TR[0] + (BR[0] - TR[0]) * (ys - TR[1]) / (BR[1] - TR[1])
    fray_len = noise1d(H, 10) * 55 + noise1d(H, 2) * 35 + 8
    start = right_edge - fray_len[:, None] * 0.8
    fray = np.clip(1 - (xs - start) / (fray_len[:, None] + 1), 0, 1) ** 0.7
    # Cerdas: cada fila con su intensidad, que además se apaga a trozos a lo largo de la fila.
    along = Image.fromarray((rng.random((H, W // 14)) * 255).astype(np.uint8)).resize((W, H), Image.BILINEAR)
    bristles = noise1d(H, 1.2, 2)[:, None] * (0.55 + 0.45 * np.asarray(along, np.float32) / 255.0)
    fray = np.where(xs > start, fray * np.clip(bristles * 1.6 - 0.15, 0, 1), 1)
    body *= np.clip(fray, 0, 1)

    # Textura de pincel: vetas horizontales.
    streak = np.repeat(noise1d(H, 2.5, 4)[:, None], W, axis=1)
    grain = rng.random((H, W)).astype(np.float32)
    grain = np.asarray(Image.fromarray((grain * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6))) / 255.0

    # Borde: trazo claro arriba y abajo, interrumpido como pincel seco, y doble trazo en la izquierda.
    edge = poly_mask([TL, TR, BR, BLp], W, H, outline=2)
    # Pincel seco distinto en el borde de arriba y en el de abajo: tramos más finos y algún hueco.
    top = np.clip((noise1d(W, 22, 4) - 0.28) * 4, 0.1, 1)[None, :]
    bottom = np.clip((noise1d(W, 22, 4) - 0.28) * 4, 0.1, 1)[None, :]
    dry = np.where(ys < H / 2, top, bottom)
    edge *= dry * (xs < right_edge - fray_len[:, None] * 0.4)
    left = poly_mask([(BLp[0] - 6, BLp[1] + 1), (TL[0] - 6, TL[1] - 1), (TL[0] - 3, TL[1] - 1), (BLp[0] - 3, BLp[1] + 1)],
                     W, H)
    left *= (noise1d(H, 4)[:, None] > 0.3)
    edge = np.clip(edge + left * 0.9, 0, 1)
    # Arañazos claros cerca del extremo derecho.
    scratches = (grain > 0.9) * (xs > right_edge - 110) * (xs < right_edge - 5) * body * 0.6

    if selected:
        v = 0.9 + 0.07 * streak - 0.05 * grain
        holes = (grain < 0.04) * (xs > right_edge - 140)  # huecos de tinta seca hacia el final
        rgb = np.stack([v, v, v * 1.01], -1)
        alpha = np.clip(body * (1 - holes * 0.8) + edge, 0, 1)
        color = rgb * (1 - edge[..., None]) + 0.97 * edge[..., None]
    else:
        # Algo más claro hacia la izquierda, donde empieza el trazo.
        v = 0.025 + 0.035 * streak + 0.02 * grain + 0.04 * np.clip(1 - xs / 200, 0, 1) * streak
        rgb = np.stack([v * 0.95, v, v * 1.15], -1)
        alpha = np.clip(body * 0.8 + edge + scratches, 0, 1)
        lightness = np.clip(edge + scratches, 0, 1)[..., None]
        color = rgb * (1 - lightness) + 0.88 * lightness

    if separator:
        sep = ((xs >= SEP_X - 1) & (xs <= SEP_X + 1) & (ys >= 25) & (ys <= 63)).astype(np.float32)
        sep = np.asarray(Image.fromarray((sep * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.5))) / 255.0
        tone = 0.06 if selected else 0.92
        color = color * (1 - sep[..., None]) + tone * sep[..., None]
        alpha = np.maximum(alpha, sep)

    out = np.dstack([np.clip(color, 0, 1) * 255, np.clip(alpha, 0, 1) * 255]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


# --- Iconos --------------------------------------------------------------------------------------
IS = 50
N = IS * 8


def icon(name, draw_fn, w=IS, h=IS):
    mask = Image.new("L", (w * 8, h * 8), 0)
    draw_fn(ImageDraw.Draw(mask), lambda v: v * 8 * IS, w * 8, h * 8)
    mask = mask.resize((w, h), Image.LANCZOS)
    img = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    img.putalpha(mask)
    img.save(os.path.join(OUT, name))


def play(d, u, W, H):
    d.polygon([(u(0.14), u(0.06)), (u(0.92), u(0.5)), (u(0.14), u(0.94))], fill=255)


def trophy(d, u, W, H):
    d.pieslice([u(0.2), u(-0.3), u(0.8), u(0.62)], 0, 180, fill=255)       # copa
    d.rectangle([u(0.2), u(0.05), u(0.8), u(0.17)], fill=255)
    for cx in (0.16, 0.84):                                                  # asas
        d.arc([u(cx - 0.14), u(0.1), u(cx + 0.14), u(0.42)], 0, 360, fill=255, width=int(u(0.06)))
    d.rectangle([u(0.44), u(0.55), u(0.56), u(0.78)], fill=255)             # pie
    d.rectangle([u(0.26), u(0.8), u(0.74), u(0.93)], fill=255)


def gear(d, u, W, H):
    c = u(0.5)
    pts = []
    teeth = 8
    for i in range(teeth * 4):
        a = i / (teeth * 4) * 2 * math.pi
        r = 0.47 if (i % 4) in (1, 2) else 0.35
        pts.append((c + u(r) * math.cos(a), c + u(r) * math.sin(a)))
    d.polygon(pts, fill=255)
    d.ellipse([c - u(0.15), c - u(0.15), c + u(0.15), c + u(0.15)], fill=0)


def document(d, u, W, H):
    d.polygon([(u(0.18), u(0.04)), (u(0.62), u(0.04)), (u(0.84), u(0.26)), (u(0.84), u(0.96)), (u(0.18), u(0.96))],
              fill=255)
    d.polygon([(u(0.6), u(0.04)), (u(0.84), u(0.28)), (u(0.6), u(0.28))], fill=0)
    for y in (0.42, 0.56, 0.7, 0.84):
        d.rectangle([u(0.3), u(y), u(0.72), u(y + 0.06)], fill=0)


def power(d, u, W, H):
    c = u(0.5)
    d.arc([c - u(0.4), c - u(0.36), c + u(0.4), c + u(0.44)], -60, 240, fill=255, width=int(u(0.11)))
    d.rounded_rectangle([c - u(0.055), u(0.02), c + u(0.055), u(0.5)], radius=u(0.05), fill=255)


def disc(d, u, W, H):
    c = u(0.5)

    def circle(r, fill):
        d.ellipse([c - u(r), c - u(r), c + u(r), c + u(r)], fill=fill)
    circle(0.47, 255)
    circle(0.17, 0)
    circle(0.11, 255)
    circle(0.055, 0)
    d.arc([c - u(0.34), c - u(0.34), c + u(0.34), c + u(0.34)], 200, 250, fill=0, width=int(u(0.05)))


def home(d, u, W, H):
    d.polygon([(u(0.5), u(0.06)), (u(0.97), u(0.5)), (u(0.83), u(0.5)), (u(0.83), u(0.94)), (u(0.17), u(0.94)),
               (u(0.17), u(0.5)), (u(0.03), u(0.5))], fill=255)
    d.rectangle([u(0.41), u(0.6), u(0.59), u(0.94)], fill=0)


band(False).save(os.path.join(OUT, "btn_normal.png"))
band(True).save(os.path.join(OUT, "btn_selected.png"))
band(True, separator=False).save(os.path.join(OUT, "row_selected.png"))
icon("icon_play.png", play, 42, 46)
icon("icon_achievements.png", trophy)
icon("icon_options.png", gear)
icon("icon_credits.png", document, 40, 50)
icon("icon_quit.png", power)
icon("icon_disc.png", disc)
icon("icon_home.png", home)
print("Bandas e iconos generados en", os.path.abspath(OUT))
