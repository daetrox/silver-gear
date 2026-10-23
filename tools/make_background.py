#!/usr/bin/env python3
"""Genera el fondo del menú (assets/ui/background_clean.jpg, 1672x941).

Por defecto parte de tools/photo/base-nevada.png, la ilustración «limpia» del usuario (base militar en
la montaña nevada, soldados con camuflaje invernal y helicópteros; sin logotipo ni botones):

  - borra el texto ilegible que la generación dejó encima del emblema del hangar;
  - borra también el emblema del zorro del hangar, que recuerda al de FOXHOUND (de Konami); con
    --con-emblema se conserva (sólo para uso personal);
  - gradúa la imagen hacia tonos más fríos y oscurece la derecha para el rótulo y los botones, que el
    launcher dibuja encima en las posiciones de la maqueta (x 1075..1588);
  - pone arriba a la derecha el rótulo propio «SILVER GEAR» (el logotipo de Metal Gear Solid es de
    Konami y no puede distribuirse).

Con --noruega usa en su lugar la foto de dominio público tools/photo/arctic-angels-norway.jpg (ver
tools/photo/CREDITOS.txt), reflejada y con una gradación de noche ártica.

Requiere: numpy, pillow, opencv-python. Uso: python3 tools/make_background.py [--con-emblema] [--noruega]
"""
import os
import sys

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
W, H = 1672, 941
PUBLIC = "--con-emblema" not in sys.argv
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
# Con la estructura del logotipo de Metal Gear Solid, pero con letra libre (Saira, OFL; tools/fonts/):
# lema en letra ancha arriba, el nombre en cursiva gruesa y condensada con las letras apretadas y, debajo,
# una franja blanca inclinada con «L A U N C H E R» en negro (donde el original pone «S O L I D»).
SS = 3                     # supermuestreo del rótulo
CX, TOP = 1331, 70         # centro horizontal (sobre los botones, x 1075..1588) y borde superior
SAIRA = os.path.join(HERE, "fonts", "Saira-Italic[wdth,wght].ttf")


def saira(size, weight, width):
    f = ImageFont.truetype(SAIRA, size * SS)
    f.set_variation_by_axes([weight, width])  # ejes: peso, anchura
    return f


def text_layer(text, font, spacing, stroke=0):
    """Texto en una capa L con espaciado propio (negativo = letras apretadas), recortada a su caja."""
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    widths = [probe.textlength(c, font=font) for c in text]
    w = int(sum(widths) + spacing * SS * (len(text) - 1) + font.size + stroke * 4)
    img = Image.new("L", (w, int(font.size * 1.6)), 0)
    d = ImageDraw.Draw(img)
    x = font.size * 0.1 + stroke
    for c, cw in zip(text, widths):
        d.text((x, 0), c, font=font, fill=255, stroke_width=stroke, stroke_fill=255)
        x += cw + spacing * SS
    return img.crop(img.getbbox())


tag = text_layer("TACTICAL ESPIONAGE ACTION", saira(29, 900, 125), 3)
name = text_layer("SILVER GEAR", saira(122, 900, 80), -5)
band_txt = text_layer("LAUNCHER", saira(31, 900, 125), 24)

# Lienzo del rótulo (a escala SS): lema, nombre y franja.
lw = max(tag.width, name.width) + 80 * SS
gap1, band_h = 8 * SS, 40 * SS
lh = tag.height + gap1 + name.height + band_h + 20 * SS
fill = Image.new("L", (lw, lh), 0)      # blanco del rótulo
ink = Image.new("L", (lw, lh), 0)       # letras negras dentro de la franja
nx = (lw - name.width) // 2
fill.paste(tag, ((lw - tag.width) // 2 + 10 * SS, 0), tag)
ny = tag.height + gap1
fill.paste(name, (nx, ny), name)
# Franja inclinada bajo el nombre, algo más corta por la izquierda (como la de SOLID).
by0 = ny + name.height + 6 * SS
slant = band_h * 0.55
band = Image.new("L", (lw, lh), 0)
ImageDraw.Draw(band).polygon([(nx + 18 * SS + slant, by0), (nx + name.width + 6 * SS, by0),
                              (nx + name.width + 6 * SS - slant, by0 + band_h), (nx + 18 * SS, by0 + band_h)], fill=255)
fill = Image.fromarray(np.maximum(np.asarray(fill), np.asarray(band)))
ink.paste(band_txt, (int(nx + name.width / 2 - band_txt.width / 2), int(by0 + (band_h - band_txt.height) / 2)), band_txt)

# A tamaño final y colocado en la imagen.
fill = fill.resize((lw // SS, lh // SS), Image.LANCZOS)
ink = ink.resize((lw // SS, lh // SS), Image.LANCZOS)
ox, oy = CX - fill.width // 2, TOP
mask = Image.new("L", (W, H), 0)
mask.paste(fill, (ox, oy))
ink_mask = Image.new("L", (W, H), 0)
ink_mask.paste(ink, (ox, oy))
# Plateado: blanco arriba que se apaga un poco hacia abajo, y la franja más clara.
grad = np.clip(1.0 - 0.3 * smoothstep(oy + 20, oy + fill.height, yy), 0, 1)
silver = (np.stack([grad * 0.93, grad * 0.95, grad], -1) * 255).astype(np.uint8)
# Sombra: contorno oscuro de las letras más una placa difusa bajo todo el rótulo, para que lo que haya
# detrás (el helicóptero) no se mezcle con ellas.
outline = mask.filter(ImageFilter.MaxFilter(5))
plate = Image.new("L", (W, H), 0)
ImageDraw.Draw(plate).rectangle([ox, oy + 6, ox + fill.width, oy + fill.height], fill=150)
shadow_layer = Image.fromarray(np.maximum.reduce([np.asarray(outline), np.asarray(mask.filter(ImageFilter.GaussianBlur(10))),
                                                  np.asarray(plate.filter(ImageFilter.GaussianBlur(26)))]))
out = Image.composite(Image.new("RGB", (W, H), (0, 0, 0)), out, shadow_layer.point(lambda p: int(p * 0.75)))
out = Image.composite(Image.fromarray(silver), out, mask)
out = Image.composite(Image.new("RGB", (W, H), (12, 14, 18)), out, ink_mask)

dst = os.path.join(ROOT, "assets", "ui", "background_clean.jpg")
out.save(dst, quality=92)
print("Fondo generado en", os.path.abspath(dst), "" if PUBLIC else "(con el emblema: sólo uso personal)")
