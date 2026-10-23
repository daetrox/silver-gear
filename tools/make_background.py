#!/usr/bin/env python3
"""Genera el fondo del menú (assets/ui/background_clean.jpg, 1672x941).

Por defecto parte de tools/photo/base-nevada.png, la ilustración «limpia» del usuario (base militar en
la montaña nevada, soldados con camuflaje invernal y helicópteros; sin logotipo ni botones):

  - borra el texto ilegible que la generación dejó encima del emblema del hangar;
  - con --publicable, borra también el emblema del zorro (recuerda al de FOXHOUND, de Konami);
  - gradúa la imagen hacia tonos más fríos y oscurece la derecha para el rótulo y los botones, que el
    launcher dibuja encima en las posiciones de la maqueta (x 1075..1588);
  - pone arriba a la derecha el rótulo propio «SILVER GEAR» (el logotipo de Metal Gear Solid es de
    Konami y no puede distribuirse).

Con --noruega usa en su lugar la foto de dominio público tools/photo/arctic-angels-norway.jpg (ver
tools/photo/CREDITOS.txt), reflejada y con una gradación de noche ártica.

Requiere: numpy, pillow, opencv-python. Uso: python3 tools/make_background.py [--publicable] [--noruega]
"""
import os
import sys

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
W, H = 1672, 941
PUBLIC = "--publicable" in sys.argv
NORWAY = "--noruega" in sys.argv

yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
u, v = xx / W, yy / H


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def base_nevada():
    bgr = cv2.imread(os.path.join(HERE, "photo", "base-nevada.png"), cv2.IMREAD_COLOR)
    bgr = cv2.resize(bgr, (W, H), interpolation=cv2.INTER_LANCZOS4)  # 1671 → 1672 de ancho

    gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    if PUBLIC:
        # Todo el escudo: es un dibujo claro sobre la pared oscura, así que se borran sus trazos (lo
        # claro, ensanchado) y se rellenan con la pared de alrededor; la nieve que cae no se toca
        # fuera del escudo.
        x0, y0, x1, y1 = 414, 240, 508, 362
        mask = np.zeros(gray.shape, np.uint8)
        region = gray[y0:y1, x0:x1].astype(np.float32)
        wall = np.median(region)
        mask[y0:y1, x0:x1] = (region > wall + 18).astype(np.uint8) * 255
        mask = cv2.dilate(mask, np.ones((5, 5), np.uint8), iterations=2)
        bgr = cv2.inpaint(bgr, mask, 7, cv2.INPAINT_TELEA)
    else:
        # Sólo la franja superior del escudo, donde hay letras inventadas (por debajo de su borde): los
        # trazos claros se borran.
        x0, y0, x1, y1 = 430, 250, 494, 263
        mask = np.zeros(gray.shape, np.uint8)
        band = gray[y0:y1, x0:x1]
        mask[y0:y1, x0:x1] = (band > np.percentile(band, 45)).astype(np.uint8) * 255
        mask = cv2.dilate(mask, np.ones((3, 3), np.uint8))
        bgr = cv2.inpaint(bgr, mask, 4, cv2.INPAINT_TELEA)

    img = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    # Algo más fría y con más contraste; las luces de la base se conservan.
    lum = img @ np.array([0.299, 0.587, 0.114], np.float32)
    img = img * 0.8 + lum[..., None] * 0.2                       # un poco menos de saturación
    img *= np.array([0.94, 0.98, 1.06], np.float32)              # hacia el azul
    img = np.clip((img - 0.04) / 0.96, 0, 1) ** 1.12
    return img


def noruega():
    photo = Image.open(os.path.join(HERE, "photo", "arctic-angels-norway.jpg")).convert("RGB")
    photo = photo.transpose(Image.FLIP_LEFT_RIGHT).crop((640, 230, 3840, 2030)).resize((W, H), Image.LANCZOS)
    img = np.asarray(photo).astype(np.float32) / 255.0
    lum = img @ np.array([0.299, 0.587, 0.114], np.float32)
    lum = np.clip((lum - 0.08) / 0.92, 0, 1) ** 1.35
    shadow = np.array([0.012, 0.022, 0.04], np.float32)
    mid = np.array([0.22, 0.3, 0.4], np.float32)
    light = np.array([0.74, 0.82, 0.92], np.float32)
    t = lum[..., None]
    tone = np.where(t < 0.5, shadow + (mid - shadow) * (t / 0.5), mid + (light - mid) * ((t - 0.5) / 0.5))
    col = tone * 0.86 + img * np.array([0.8, 0.9, 1.05], np.float32) * 0.14 * (0.35 + 0.65 * t)
    col *= (0.42 + 0.58 * smoothstep(0.02, 0.5, v))[..., None]
    d = np.hypot((u - 0.2) * W / H, v - 0.16)
    col += (np.exp(-(d / 0.09) ** 2) * 0.55 + np.exp(-(d / 0.3) ** 2) * 0.12)[..., None] * np.array(
        [0.7, 0.8, 0.95], np.float32)
    return col


col = noruega() if NORWAY else base_nevada()

# Derecha oscura para el rótulo y los botones: un velo azul casi negro con borde en diagonal (como un
# barrido de pincel), que deja intuir las montañas detrás.
edge = 0.6 + 0.07 * (1 - v) + 0.012 * np.sin(v * 37.0) + 0.006 * np.sin(v * 113.0)
veil = smoothstep(edge - 0.02, edge + 0.13, u) * (0.9 if not NORWAY else 0.9)
col = col * (1 - veil[..., None]) + np.array([0.012, 0.017, 0.026], np.float32) * veil[..., None]
# Viñeta.
vig = 1 - 0.45 * smoothstep(0.45, 1.05, np.hypot((u - 0.42) * 1.3, v - 0.5))
col *= vig[..., None]
# Grano muy fino.
rng = np.random.default_rng(7)
col += rng.standard_normal((H, W, 1)).astype(np.float32) * 0.01

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
x0, x1 = spaced(ImageDraw.Draw(mask), "SILVER GEAR", title, cx, top, 5, 255)
grad = np.clip(1.0 - 0.38 * smoothstep(top + 15, top + 95, yy), 0, 1)
silver = (np.stack([grad * 0.93, grad * 0.95, grad], -1) * 255).astype(np.uint8)
# Sombra: la del propio texto más una placa difusa bajo todo el rótulo (subtítulo incluido), para
# que lo que haya detrás (el helicóptero) no se mezcle con las letras.
plate = Image.new("L", (W, H), 0)
ImageDraw.Draw(plate).rectangle([x0 - 10, top + 6, x1 + 10, top + 150], fill=150)
shadow_layer = Image.fromarray(np.maximum(np.asarray(mask.filter(ImageFilter.GaussianBlur(10))),
                                          np.asarray(plate.filter(ImageFilter.GaussianBlur(26)))))
out = Image.composite(Image.new("RGB", (W, H), (0, 0, 0)), out, shadow_layer.point(lambda p: int(p * 0.7)))
out = Image.composite(Image.fromarray(silver), out, mask)
d = ImageDraw.Draw(out)
ly = top + 108
d.rectangle([x0, ly, x1, ly + 2], fill=(190, 200, 212))
spaced(d, "TACTICAL ESPIONAGE LAUNCHER", small, cx, ly + 12, 6, (170, 182, 196))

dst = os.path.join(ROOT, "assets", "ui", "background_clean.jpg")
out.save(dst, quality=92)
print("Fondo generado en", os.path.abspath(dst), "(publicable)" if PUBLIC else "")
