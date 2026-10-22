#!/usr/bin/env python3
"""Genera los recursos de la interfaz de Silver Gear a partir de tools/source.png.

La ilustración (1672×941) trae pintados el logotipo de Metal Gear Solid y los cinco botones del menú
(JUGAR, LOGROS, OPCIONES, CRÉDITOS, SALIR) sobre negro. De aquí salen:

  background_clean.jpg   la ilustración sin los botones (el menú los dibuja encima)
  btn_selected.png       la banda blanca (JUGAR), sin icono ni texto
  btn_normal.png         una banda oscura, sin icono ni texto
  icon_*.png             los cinco iconos, en blanco y con alfa (el shader los tiñe)

Criterio: el alfa es la *silueta* de la banda (no su luminancia), y el color es el recorte tal cual.
Así el resultado es idéntico al de la ilustración, sin aclarar ni oscurecer la pincelada.

Requiere: opencv-python, numpy.
Uso: python3 tools/make_assets.py
"""
import os
import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "ui")
os.makedirs(OUT, exist_ok=True)

src = cv2.imread(os.path.join(HERE, "source.png"), cv2.IMREAD_COLOR)
H, W = src.shape[:2]
assert (W, H) == (1672, 941), (W, H)
gray = cv2.cvtColor(src, cv2.COLOR_BGR2GRAY).astype(np.float32)

# --- Geometría del menú (en píxeles de la ilustración) -------------------------------------------
SLOT_X, SLOT_W, SLOT_H = 1075, 513, 88
SLOT_Y = [314, 422, 527, 632, 737]  # JUGAR, LOGROS, OPCIONES, CRÉDITOS, SALIR
ICON_X = (1126, 1226)               # franja del icono (la barra separadora está en ~1231)
TEXT_X = (1262, 1592)               # franja del texto
MASK_PAD_Y = 13                     # no tocar los bordes superior/inferior (son el contorno)


def slot_box(i):
    y = SLOT_Y[i]
    return (SLOT_X, y, SLOT_X + SLOT_W, y + SLOT_H)


def icon_box(i):
    """Caja ajustada del icono dentro de la banda i (sólo el glifo, no el trazo del borde)."""
    x0, y0, x1, y1 = slot_box(i)
    reg = gray[y0 + 12:y1 - 12, ICON_X[0]:ICON_X[1]]
    on_white = (i == 0)
    ys, xs = np.where((reg < 60) if on_white else (reg > 200))
    if len(xs) == 0:
        return (ICON_X[0], y0 + 20, ICON_X[1], y1 - 20)
    return (ICON_X[0] + int(xs.min()), y0 + 12 + int(ys.min()),
            ICON_X[0] + int(xs.max()) + 1, y0 + 12 + int(ys.max()) + 1)


def clean_banner(img, on_white):
    """Borra icono y texto para poder escribir encima con la fuente del launcher.

    El umbral es exigente a propósito: en la banda oscura el texto es blanco puro (~230) mientras
    que el contorno y la pincelada son grises (~60-150), así que sólo desaparece el texto.
    """
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    mask = np.zeros(g.shape, np.uint8)
    for a, b in ((ICON_X[0] - SLOT_X, ICON_X[1] - SLOT_X), (TEXT_X[0] - SLOT_X, TEXT_X[1] - SLOT_X)):
        m = ((g[:, a:b] < 200) if on_white else (g[:, a:b] > 175)).astype(np.uint8) * 255
        m[:MASK_PAD_Y, :] = 0
        m[-MASK_PAD_Y:, :] = 0
        mask[:, a:b] = m
    mask = cv2.dilate(mask, np.ones((7, 7), np.uint8), iterations=2)
    return cv2.inpaint(img, mask, 6, cv2.INPAINT_TELEA)


def shape_alpha(img, thr=14, blur=1.5):
    """Silueta de la banda: píxeles claros + relleno de su contorno exterior, con bordes suaves."""
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    m = (g > thr).astype(np.uint8) * 255
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8))
    cnts, _ = cv2.findContours(m, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    filled = np.zeros_like(m)
    for c in cnts:
        if cv2.contourArea(c) > 1500:
            cv2.drawContours(filled, [c], -1, 255, -1)
    alpha = np.maximum(filled, m).astype(np.float32) / 255.0
    return cv2.GaussianBlur(alpha, (0, 0), blur)


# --- 1) Banda seleccionada (blanca, JUGAR) --------------------------------------------------------
x0, y0, x1, y1 = slot_box(0)
sel = clean_banner(src[y0:y1, x0:x1].copy(), on_white=True)
cv2.imwrite(os.path.join(OUT, "btn_selected.png"),
            np.dstack([sel, (shape_alpha(sel) * 255).astype(np.uint8)]))

# --- 1b) Banda seleccionada sin el separador vertical (filas de listas y avisos) -------------------
sep = (g := cv2.cvtColor(sel, cv2.COLOR_BGR2GRAY))[MASK_PAD_Y:-MASK_PAD_Y, ICON_X[1] - SLOT_X - 10:TEXT_X[0] - SLOT_X]
mask = np.zeros(g.shape, np.uint8)
mask[MASK_PAD_Y:-MASK_PAD_Y, ICON_X[1] - SLOT_X - 10:TEXT_X[0] - SLOT_X] = (sep < 200).astype(np.uint8) * 255
mask = cv2.dilate(mask, np.ones((5, 5), np.uint8), iterations=2)
row = cv2.inpaint(sel, mask, 6, cv2.INPAINT_TELEA)
cv2.imwrite(os.path.join(OUT, "row_selected.png"), np.dstack([row, (shape_alpha(row) * 255).astype(np.uint8)]))

# --- 2) Banda normal (oscura, OPCIONES) -----------------------------------------------------------
x0, y0, x1, y1 = slot_box(2)
norm = clean_banner(src[y0:y1, x0:x1].copy(), on_white=False)
cv2.imwrite(os.path.join(OUT, "btn_normal.png"),
            np.dstack([norm, (shape_alpha(norm) * 255).astype(np.uint8)]))

# --- 3) Iconos: silueta → alfa, relleno blanco (el shader los tiñe) -------------------------------
ICON_NAMES = ["icon_play", "icon_achievements", "icon_options", "icon_credits", "icon_quit"]
for i, name in enumerate(ICON_NAMES):
    ix0, iy0, ix1, iy1 = icon_box(i)
    pad = 1
    ix0, iy0 = max(0, ix0 - pad), max(0, iy0 - pad)
    ix1, iy1 = min(W, ix1 + pad), min(H, iy1 + pad)
    crop = src[iy0:iy1, ix0:ix1]
    g = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY).astype(np.float32)
    on_white = (i == 0)
    # Sobre la banda blanca, el fondo (gris claro con grano) no debe dejar un recuadro: sólo cuenta lo oscuro.
    a = np.clip((190.0 - g) / 140.0 if on_white else (g - 60.0) / 170.0, 0, 1)
    a = cv2.GaussianBlur(a, (3, 3), 0)
    white = np.full_like(crop, 255)
    cv2.imwrite(os.path.join(OUT, name + ".png"), np.dstack([white, (a * 255).astype(np.uint8)]))
    print("%-20s caja=(%d,%d,%d,%d) %dx%d" % (name, ix0, iy0, ix1, iy1, ix1 - ix0, iy1 - iy0))

# --- 4) Fondo sin los botones ---------------------------------------------------------------------
art = gray[300:880, 1040:SLOT_X]
print("brillo máximo de la ilustración junto a los botones:", int(art.max()))
clean = src.copy()
clean[295:880, 1068:W] = 0
cv2.imwrite(os.path.join(OUT, "background_clean.jpg"), clean, [cv2.IMWRITE_JPEG_QUALITY, 92])

print("Recursos generados en", os.path.abspath(OUT))
