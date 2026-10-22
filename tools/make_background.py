#!/usr/bin/env python3
"""Genera el fondo del menú (assets/ui/background_clean.jpg, 1672x941) a partir de una foto libre.

Foto: tools/photo/arctic-angels-norway.jpg, soldados de la 11.ª División Aerotransportada de EE. UU.
con camuflaje invernal en las montañas de Noruega (dominio público; ver tools/photo/CREDITOS.txt).

Composición: la foto se refleja para dejar a los soldados a la izquierda, se gradúa en tonos fríos de
noche ártica (luna tras la montaña) y la derecha se oscurece para los botones del menú, que el
launcher dibuja encima en las posiciones de la maqueta. Arriba a la derecha va el rótulo propio
«SILVER GEAR» (el logotipo de Metal Gear Solid es de Konami y no puede distribuirse).

Requiere: numpy, pillow. Uso: python3 tools/make_background.py
"""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
W, H = 1672, 941

photo = Image.open(os.path.join(HERE, "photo", "arctic-angels-norway.jpg")).convert("RGB")
photo = photo.transpose(Image.FLIP_LEFT_RIGHT)
# Recorte 16:9: fuera el soldado suelto del borde; la pareja queda entre el 31 % y el 61 % del ancho.
photo = photo.crop((640, 230, 3840, 2030)).resize((W, H), Image.LANCZOS)
img = np.asarray(photo).astype(np.float32) / 255.0

yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
u, v = xx / W, yy / H


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


# --- Gradación fría: luminancia → tres tonos (sombra, medio, luz) ---------------------------------
lum = img @ np.array([0.299, 0.587, 0.114], np.float32)
lum = np.clip((lum - 0.08) / 0.92, 0, 1) ** 1.35  # más contraste, de noche
shadow = np.array([0.012, 0.022, 0.04], np.float32)
mid = np.array([0.22, 0.3, 0.4], np.float32)
light = np.array([0.74, 0.82, 0.92], np.float32)
t = lum[..., None]
tone = np.where(t < 0.5, shadow + (mid - shadow) * (t / 0.5), mid + (light - mid) * ((t - 0.5) / 0.5))
# Un poco del color original (enfriado) para que el camuflaje no quede plano.
cool = img * np.array([0.8, 0.9, 1.05], np.float32)
col = tone * 0.86 + cool * 0.14 * (0.35 + 0.65 * t)

# Noche: el cielo nublado se oscurece de arriba abajo; la nieve conserva algo de luz.
col *= (0.42 + 0.58 * smoothstep(0.02, 0.5, v))[..., None]
# Luna tras la cumbre (arriba a la izquierda): halo frío.
d = np.hypot((u - 0.2) * W / H, v - 0.16)
col += (np.exp(-(d / 0.09) ** 2) * 0.55 + np.exp(-(d / 0.3) ** 2) * 0.12)[..., None] * np.array(
    [0.7, 0.8, 0.95], np.float32)

# Derecha oscura para el rótulo y los botones (empiezan en x≈1075), con transición suave.
dark = smoothstep(0.5, 0.7, u) * 0.9
col = col * (1 - dark[..., None]) + np.array([0.01, 0.014, 0.022], np.float32) * dark[..., None]
# Viñeta.
vig = 1 - 0.5 * smoothstep(0.45, 1.05, np.hypot((u - 0.42) * 1.3, v - 0.5))
col *= vig[..., None]
# Grano muy fino.
rng = np.random.default_rng(7)
col += (rng.standard_normal((H, W, 1)).astype(np.float32)) * 0.012

out = Image.fromarray((np.clip(col, 0, 1) * 255).astype(np.uint8))

# --- Rótulo «SILVER GEAR» donde la maqueta tenía el logotipo ----------------------------------------
fonts = os.path.join(ROOT, "assets", "fonts")
title = ImageFont.truetype(os.path.join(fonts, "RussoOne-Regular.ttf"), 84)
small = ImageFont.truetype(os.path.join(fonts, "Oswald[wght].ttf"), 22)
cx, top = 1331, 96  # centrado sobre los botones (x 1075..1588)


def spaced(draw, text, font, cx, y, spacing, fill):
    widths = [draw.textlength(c, font=font) for c in text]
    total = sum(widths) + spacing * (len(text) - 1)
    x = cx - total / 2
    for c, w in zip(text, widths):
        draw.text((x, y), c, font=font, fill=fill)
        x += w + spacing
    return cx - total / 2, cx + total / 2


# Texto en una capa aparte para darle un degradado plateado y una sombra suave.
mask = Image.new("L", (W, H), 0)
md = ImageDraw.Draw(mask)
x0, x1 = spaced(md, "SILVER GEAR", title, cx, top, 5, 255)
grad = np.clip(1.0 - 0.38 * smoothstep(top + 15, top + 95, yy), 0, 1)
silver = (np.stack([grad * 0.93, grad * 0.95, grad], -1) * 255).astype(np.uint8)
shadow_layer = mask.filter(ImageFilter.GaussianBlur(10))
out = Image.composite(Image.new("RGB", (W, H), (0, 0, 0)), out, shadow_layer.point(lambda p: int(p * 0.7)))
out = Image.composite(Image.fromarray(silver), out, mask)
d = ImageDraw.Draw(out)
# Filetes y subtítulo.
ly = top + 108
d.rectangle([x0, ly, x1, ly + 2], fill=(190, 200, 212))
spaced(d, "TACTICAL ESPIONAGE LAUNCHER", small, cx, ly + 12, 6, (170, 182, 196))

dst = os.path.join(ROOT, "assets", "ui", "background_clean.jpg")
out.save(dst, quality=92)
print("Fondo generado en", os.path.abspath(dst))
