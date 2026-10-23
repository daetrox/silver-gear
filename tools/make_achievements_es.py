#!/usr/bin/env python3
"""Genera assets/lang/logros_es.txt: la traducción al español de los logros de RetroAchievements.

Entrada: tools/i18n/logros_es.tsv (título inglés, descripción inglesa, título español, descripción
española; no se versiona, porque lleva los textos originales de RetroAchievements).
Salida: una línea por texto, «T|D<hash FNV-1a de 32 bits del texto inglés>\\t<texto español>». El
launcher (src/translate.cpp) calcula el mismo hash sobre el texto que descarga y busca la traducción,
así que en el repositorio no queda ningún texto de RetroAchievements.

Uso: python3 tools/make_achievements_es.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "i18n", "logros_es.tsv")
DST = os.path.join(HERE, "..", "assets", "lang", "logros_es.txt")


def fnv1a(text):
    h = 0x811C9DC5
    for b in text.strip().encode("utf-8"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


lines = []
with open(SRC, encoding="utf-8") as f:
    for row in f:
        if not row.strip():
            continue
        title_en, desc_en, title_es, desc_es = row.rstrip("\n").split("\t")
        lines.append("T%08x\t%s" % (fnv1a(title_en), title_es))
        lines.append("D%08x\t%s" % (fnv1a(desc_en), desc_es))
os.makedirs(os.path.dirname(DST), exist_ok=True)
with open(DST, "w", encoding="utf-8") as f:
    f.write("# Logros de Metal Gear Solid en español (hash FNV-1a del texto inglés → traducción)\n")
    f.write("\n".join(lines) + "\n")
print(len(lines) // 2, "logros →", os.path.abspath(DST))
