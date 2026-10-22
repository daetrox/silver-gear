#!/usr/bin/env python3
"""Dibuja los iconos que no están en la maqueta, con el mismo formato que los recortados por
make_assets.py: blanco con alfa (el shader los tiñe), unos 50 px de alto.

  icon_disc.png   disco compacto (CAMBIAR DISCO)
  icon_home.png   casa (MENÚ PRINCIPAL)

Requiere: pillow. Uso: python3 tools/make_icons.py
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "ui")
SIZE, SS = 50, 8  # tamaño final y supermuestreo (bordes suaves)
N = SIZE * SS


def save(name, draw_fn):
    mask = Image.new("L", (N, N), 0)
    draw_fn(ImageDraw.Draw(mask), lambda v: v * N)
    mask = mask.resize((SIZE, SIZE), Image.LANCZOS)
    img = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 0))
    img.putalpha(mask)
    img.save(os.path.join(OUT, name))


def disc(d, u):
    c = u(0.5)
    def circle(r, fill):
        d.ellipse([c - u(r), c - u(r), c + u(r), c + u(r)], fill=fill)
    circle(0.47, 255)        # disco
    circle(0.17, 0)          # anillo central
    circle(0.11, 255)
    circle(0.055, 0)         # agujero
    # reflejo: arco transparente, como la luz sobre la cara grabada
    d.arc([c - u(0.34), c - u(0.34), c + u(0.34), c + u(0.34)], 200, 250, fill=0, width=int(u(0.05)))


def home(d, u):
    # tejado (triángulo con alero) y cuerpo con puerta
    d.polygon([(u(0.5), u(0.06)), (u(0.97), u(0.5)), (u(0.83), u(0.5)), (u(0.83), u(0.94)), (u(0.17), u(0.94)),
               (u(0.17), u(0.5)), (u(0.03), u(0.5))], fill=255)
    d.rectangle([u(0.41), u(0.6), u(0.59), u(0.94)], fill=0)


save("icon_disc.png", disc)
save("icon_home.png", home)
print("Iconos generados en", os.path.abspath(OUT))
