#!/usr/bin/env python3
"""Lista los textos ingleses de la interfaz (segundo argumento de cada L("español", "English")) que
aún no están en assets/lang/fr.txt, para traducirlos. Uso: python3 tools/i18n_extract.py
"""
import glob
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
STR = r'"((?:[^"\\]|\\.)*)"'
PAT = re.compile(r'L\(\s*(?:' + STR + r'\s*)+,\s*((?:' + STR + r'\s*)+)\)', re.S)


def english_texts():
    seen = []
    for f in sorted(glob.glob(os.path.join(ROOT, "src", "*.cpp")) + glob.glob(os.path.join(ROOT, "src", "*.h"))):
        for m in PAT.finditer(open(f, encoding="utf-8").read()):
            en = "".join(re.findall(STR, m.group(2)))
            if en not in seen:
                seen.append(en)
    return seen


def known():
    path = os.path.join(ROOT, "assets", "lang", "fr.txt")
    if not os.path.exists(path):
        return set()
    return {line.split("\t", 1)[0] for line in open(path, encoding="utf-8") if "\t" in line}


if __name__ == "__main__":
    done = known()
    for en in english_texts():
        if en not in done:
            print(en)
