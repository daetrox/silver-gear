#!/usr/bin/env python3
"""Genera la música y los efectos de Silver Gear (composición original, sin derechos).

El launcher va de espionaje táctico: la música es un ambiente industrial y tenso —bordón grave,
bajo pulsado, percusión metálica, notas de cristal y texturas de radio— sobre una pedal de Re menor
con el segundo grado rebajado (Mi♭) para dar inquietud. Los efectos son blips digitales secos, como
los de un codec.

Nada de esto copia temas de *Metal Gear Solid*: se sintetiza aquí desde cero.

Requiere: numpy y ffmpeg (para codificar a OGG Vorbis).
Uso: python3 tools/make_audio.py
"""
import os
import subprocess
import tempfile
import wave

import numpy as np

SR = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "audio")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(2001)


# --- Utilidades ---------------------------------------------------------------------------------

def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12.0)


def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def band_noise(n, lo, hi, seed=None):
    """Ruido blanco filtrado en frecuencia (FFT)."""
    r = np.random.default_rng(seed) if seed is not None else rng
    x = r.standard_normal(n)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1 / SR)
    X *= ((f >= lo) & (f <= hi)).astype(float)
    y = np.fft.irfft(X, n)
    return y / (np.max(np.abs(y)) + 1e-9)


def lowpass(x, cutoff, order=4):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    X *= 1.0 / (1.0 + (f / cutoff) ** order)
    return np.fft.irfft(X, len(x))


def highpass(x, cutoff, order=4):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    X *= 1.0 - 1.0 / (1.0 + (f / cutoff) ** order)
    return np.fft.irfft(X, len(x))


def pan(mono, p):
    """p en [-1, 1] -> estéreo con ley de potencia constante."""
    a = (p + 1) * np.pi / 4
    return np.stack([mono * np.cos(a), mono * np.sin(a)], axis=1)


def saw(freq, t, harmonics=40, odd_only=False):
    """Diente de sierra (o cuadrada) por suma aditiva: sin aliasing por encima de Nyquist."""
    out = np.zeros_like(t)
    for h in range(1, harmonics + 1):
        if odd_only and h % 2 == 0:
            continue
        fh = freq * h
        if fh > SR / 2.2:
            break
        out += np.sin(2 * np.pi * fh * t) / h
    return out


# --- Instrumentos -------------------------------------------------------------------------------

def drone(freq, dur, seed=None):
    """Bordón grave: dos senos desafinados que laten muy despacio."""
    t = t_axis(dur)
    out = (np.sin(2 * np.pi * freq * t) * 0.62 + np.sin(2 * np.pi * freq * 1.004 * t + 0.7) * 0.38)
    sub = np.sin(2 * np.pi * freq * 0.5 * t) * 0.28
    lfo = 0.72 + 0.28 * np.sin(2 * np.pi * t / 21.0) ** 2
    return (out + sub) * lfo


def static(dur, seed=None):
    """Textura de radio: ruido de banda estrecha con respiración lenta."""
    t = t_axis(dur)
    x = band_noise(len(t), 260, 1500, seed)
    x *= 0.45 + 0.55 * np.sin(2 * np.pi * t / 17.0 + 1.2) ** 2
    return x


def pulse(freq, dur, cutoff=1500, duty=0.5, decay=5.0):
    """Bajo sintético pulsado (cuadrada filtrada) con ataque duro."""
    t = t_axis(dur)
    out = saw(freq, t, odd_only=(abs(duty - 0.5) < 1e-6))
    out = lowpass(out, cutoff)
    env = np.minimum(1.0, t / 0.0035) * np.exp(-t * decay)
    return out * env


def glass(freq, dur, vel=1.0):
    """Nota de cristal/piano: parciales escalonados y un golpecito de tecla."""
    t = t_axis(dur)
    out = np.zeros_like(t)
    for h, a, d in [(1, 1.0, 1.5), (2, 0.42, 2.3), (3, 0.24, 3.2), (4, 0.13, 4.4), (5.4, 0.07, 6.0),
                    (7.2, 0.04, 8.0)]:
        f = freq * h
        if f > SR / 2.2:
            break
        out += a * np.sin(2 * np.pi * f * t) * np.exp(-t * d)
    knock = band_noise(len(t), 2000, 9000) * np.exp(-t * 260) * 0.16
    return (out + knock) * np.minimum(1.0, t / 0.0025) * vel


def metal(freq=190.0, dur=1.5, vel=1.0, tone=5200.0):
    """Golpe metálico seco: parciales inarmónicos + transitorio de ruido."""
    t = t_axis(dur)
    out = np.zeros_like(t)
    for r, a, d in [(1.0, 1.0, 3.0), (1.73, 0.62, 4.2), (2.41, 0.4, 5.4), (3.17, 0.24, 6.8), (4.61, 0.14, 8.6)]:
        out += a * np.sin(2 * np.pi * freq * r * t) * np.exp(-t * d)
    transient = band_noise(len(t), 1200, 8000) * np.exp(-t * 95) * 0.5
    return lowpass(out + transient, tone) * np.minimum(1.0, t / 0.0015) * vel


def tick(vel=1.0, pitch=1.0):
    """Clic corto y seco, como el del radar."""
    t = t_axis(0.09)
    out = (np.sin(2 * np.pi * 1750 * pitch * t) * np.exp(-t * 70)
           + 0.5 * np.sin(2 * np.pi * 2900 * pitch * t) * np.exp(-t * 110)
           + 0.35 * band_noise(len(t), 4000, 11000) * np.exp(-t * 260))
    return out * vel * np.minimum(1.0, t / 0.0008)


def thump(low=44.0, dur=2.0, vel=1.0):
    """Golpe grave y sordo (el corazón de la escena)."""
    t = t_axis(dur)
    f = low + 90 * np.exp(-t * 14)
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 3.6)
    beater = lowpass(band_noise(len(t), 30, 1800), 700) * np.exp(-t * 30) * 0.5
    return (body + beater) * vel


def pad(freq, dur, bright=1.0, seed=None):
    """Cuerdas sintéticas: tres dientes de sierra desafinados, filtrados y con aire."""
    t = t_axis(dur)
    out = np.zeros_like(t)
    for det, gain in [(-0.005, 0.5), (0.0, 1.0), (0.006, 0.5)]:
        out += gain * saw(freq * (1 + det), t, harmonics=24)
    out = lowpass(out, 800 + 2600 * bright)
    air = band_noise(len(t), 1600, 7000, seed) * 0.05
    env = np.clip(t / 0.9, 0, 1) * np.clip((dur - t) / 1.3, 0, 1)
    return (out * 0.5 + air) * env


def riser(dur, f0=110.0, f1=2200.0, seed=None):
    """Subida tensa: seno que asciende + aire, para cerrar secciones."""
    t = t_axis(dur)
    f = f0 * (f1 / f0) ** (t / dur)
    tone = np.sin(2 * np.pi * np.cumsum(f) / SR)
    air = band_noise(len(t), 400, 9000, seed)
    env = (t / dur) ** 1.6 * np.clip((dur - t) / 0.25, 0, 1)
    return (tone * 0.35 + air * 0.65) * env


def chime(freq, dur, vel=1.0):
    """Campana digital limpia (para el logro)."""
    t = t_axis(dur)
    out = np.zeros_like(t)
    for r, a, d in [(1.0, 1.0, 1.1), (2.0, 0.5, 1.8), (3.01, 0.28, 2.8), (5.02, 0.14, 4.2)]:
        out += a * np.sin(2 * np.pi * freq * r * t) * np.exp(-t * d)
    return out * np.minimum(1.0, t / 0.002) * vel


# --- Mezcla -------------------------------------------------------------------------------------

def reverb(stereo, seconds=2.8, wet=0.3, seed=5):
    n = int(seconds * SR)
    t = np.arange(n) / SR
    r = np.random.default_rng(seed)
    out = np.zeros((stereo.shape[0] + n, 2))
    for ch in range(2):
        ir = r.standard_normal(n) * np.exp(-t * 2.8)
        ir = lowpass(ir, 4800)
        ir /= np.sqrt(np.sum(ir ** 2))
        size = 1 << int(np.ceil(np.log2(stereo.shape[0] + n)))
        y = np.fft.irfft(np.fft.rfft(stereo[:, ch], size) * np.fft.rfft(ir, size), size)[: stereo.shape[0] + n]
        out[:, ch] = y * wet
    out[: stereo.shape[0]] += stereo * (1 - wet * 0.5)
    return out


def add(buf, sig, start_s, p=0.0, gain=1.0):
    i = int(start_s * SR)
    s = pan(sig, p) if sig.ndim == 1 else sig
    end = min(buf.shape[0], i + s.shape[0])
    if i < end:
        buf[i:end] += s[: end - i] * gain


def loopify(wet, n, overlap=0.6):
    """Cierra un bucle sin cortes: pliega la cola de reverberación y funde el empalme.

    Se toma un trozo del final y se funde con el principio, de modo que al reproducir en bucle el
    paso del último al primer bloque sea continuo (en vez de un salto que se oiría como chasquido).
    El bucle resultante es algo más corto que `n`.
    """
    L = int(overlap * SR)
    loop = wet[: n + L].copy()
    extra = wet.shape[0] - (n + L)
    if extra > 0:
        loop[:extra] += wet[n + L:]  # la cola que sobrepasa vuelve al principio
    w = np.linspace(0.0, 1.0, L)[:, None]
    loop[:L] = loop[:L] * w + loop[n - L : n] * (1.0 - w)
    return loop[: n - L]


def finish(stereo, peak=0.82):
    stereo = np.tanh(stereo * 1.1)
    return stereo / (np.max(np.abs(stereo)) + 1e-9) * peak


def write_ogg(stereo, name, quality=5):
    pcm = (np.clip(stereo, -1, 1) * 32767).astype(np.int16)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        path = tmp.name
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    out = os.path.join(OUT, name)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", path, "-c:a", "libvorbis", "-q:a",
                    str(quality), out], check=True)
    os.remove(path)
    print("  %-18s %6.1f s  %5.0f KB" % (name, len(stereo) / SR, os.path.getsize(out) / 1024))


# --- Música -------------------------------------------------------------------------------------

def music():
    """Bucle de 32 compases (~101 s) en Re menor con el segundo grado rebajado."""
    bpm = 76.0
    beat = 60.0 / bpm
    bar = beat * 4
    bars = 32
    length = bars * bar
    buf = np.zeros((int((length + 6) * SR), 2))

    # Notas (MIDI): pedal de Re con Mi♭ (b2) para la inquietud y Si♭/Do para moverse.
    D2, A2, Bb2, C3 = 38, 45, 46, 48
    D3, F3, A3, Bb3, C4 = 50, 53, 57, 58, 60
    D4, Eb4, F4, G4, A4, Bb4 = 62, 63, 65, 67, 69, 70

    def at(b, pos):
        return b * bar + pos * beat

    # 1) Lecho continuo: bordón + radio. Suena de principio a fin (y empalma el bucle).
    add(buf, drone(midi(D2), length + 5), 0, 0.0, 0.085)
    stat = static(length + 5, seed=11)
    add(buf, np.stack([stat, np.roll(stat, 7000)], axis=1) * 0.030, 0)

    # 2) Percusión: latidos graves y clics metálicos.
    for b in range(0, bars, 2):
        if b >= 8:  # el latido entra con el pulso
            add(buf, thump(44.0, 2.4, 0.55), at(b, 0), -0.05)
    for b in range(9, bars, 2):
        add(buf, tick(0.30, 1.0), at(b, 2.0), 0.30)   # contratiempo
        add(buf, tick(0.22, 1.18), at(b, 3.5), -0.30)
    for b in range(16, bars, 2):
        add(buf, tick(0.20, 1.4), at(b, 1.0), 0.45)

    # 3) Golpes metálicos: marcan los cambios de sección.
    for b, freq, vel in [(0, 150, 0.5), (8, 190, 0.7), (16, 170, 0.8), (24, 210, 0.9), (30, 190, 0.6)]:
        add(buf, metal(freq, 1.8, vel), at(b, 0), 0.15)
    add(buf, metal(240, 1.2, 0.45), at(31, 2.0), -0.35)  # relleno antes del bucle

    # 4) Bajo pulsado: entra en B, se mantiene y se despeja al final.
    figure = [(0.0, D2), (0.5, D2), (1.0, D2), (1.5, D2), (2.0, Bb2), (2.5, D2), (3.0, D2), (3.5, C3)]
    for b in range(8, bars):
        if b >= 30:
            figure_b = [(0.0, D2), (1.0, D2), (2.0, D2), (3.0, Bb2)]  # se aclara para volver
        else:
            figure_b = figure
        for pos, note in figure_b:
            vel = 0.44 if pos in (0.0, 2.0) else 0.30
            add(buf, pulse(midi(note), 0.9, cutoff=1100, decay=6.0) * vel, at(b, pos), 0.0)

    # 5) Armonía (colchones): se insinúa sin llegar a dominar.
    chords = [(D3, F3), (D3, A3), (Bb3, D4), (C4, F4)]
    for i, b in enumerate(range(8, bars, 4)):
        root, fifth = chords[i % 4]
        add(buf, pad(midi(root), bar * 4, 0.7, seed=20 + i), at(b, 0), -0.15, 0.16)
        add(buf, pad(midi(fifth), bar * 4, 0.9, seed=40 + i), at(b, 0), 0.2, 0.09)

    # 6) Notas de cristal: introducción dispersa y melodía en C.
    intro = [(0, 0.0, D4), (2, 2.0, A4), (4, 0.0, F4), (6, 2.0, Eb4)]
    for b, pos, note in intro:
        add(buf, glass(midi(note), 4.0, 0.5), at(b, pos), 0.25)
    melody = [(16, 0.0, 1.5, D4), (16, 1.5, 1.5, F4), (16, 3.0, 1.0, A4),
              (17, 0.0, 2.0, G4), (17, 2.0, 2.0, F4),
              (18, 0.0, 2.0, D4), (18, 2.0, 2.0, A3),
              (19, 0.0, 3.0, A4),
              (20, 0.0, 1.5, Bb4), (20, 1.5, 1.5, A4), (20, 3.0, 1.0, G4),
              (21, 0.0, 2.0, F4), (21, 2.0, 1.0, Eb4), (21, 3.0, 1.0, D4),
              (22, 0.0, 3.0, C4),
              (23, 0.0, 4.0, A3)]
    for b, pos, ln, note in melody:
        add(buf, glass(midi(note), ln * beat + 1.6, 0.62), at(b, pos), 0.2)
    # fragmentos en D (desarrollo)
    for b, pos, note in [(24, 0.0, A4), (25, 2.0, F4), (26, 0.0, D4), (27, 2.0, Eb4)]:
        add(buf, glass(midi(note), 3.0, 0.45), at(b, pos), -0.25)

    # 7) Subida final: cierra el bucle con tensión y vuelve a la introducción.
    add(buf, riser(bar * 2, 120, 2600, seed=77) * 0.5, at(30, 0), 0.1)

    wet = reverb(buf, 3.2, 0.32)
    write_ogg(finish(loopify(wet, int(length * SR), 0.6), 0.8), "music.ogg", 5)


# --- Efectos ------------------------------------------------------------------------------------

def mix_in(buf, sig, start_s, gain=1.0):
    i = int(start_s * SR)
    n = min(len(sig), len(buf) - i)
    if n > 0:
        buf[i:i + n] += sig[:n] * gain


def blip(f0, f1, dur, decay, vel=1.0):
    """Pitido digital corto con barrido de frecuencia."""
    t = t_axis(dur)
    f = f0 + (f1 - f0) * (t / dur)
    tone = np.sin(2 * np.pi * np.cumsum(f) / SR)
    click = band_noise(len(t), 3000, 10000) * np.exp(-t * 200) * 0.25
    return (tone + click) * np.exp(-t * decay) * np.minimum(1.0, t / 0.0015) * vel


def sfx():
    def render(sig, name, dur=None, rev=0.2, seed=5):
        st = pan(sig, 0) if sig.ndim == 1 else sig
        out = reverb(st, 1.6, rev, seed)
        if dur:
            out = out[: int(dur * SR)]
            fade = int(0.04 * SR)
            out[-fade:] *= np.linspace(1, 0, fade)[:, None]
        write_ogg(finish(out, 0.85), name, 6)

    # mover: blip corto y seco (suena mucho, no debe cansar)
    mv = np.zeros(int(0.30 * SR))
    mix_in(mv, blip(880, 1320, 0.07, 55), 0, 0.5)
    render(mv, "move.ogg", 0.16, 0.10, seed=1)

    # aceptar: dos tonos ascendentes + un toque metálico lejano
    se = np.zeros(int(1.2 * SR))
    mix_in(se, blip(660, 700, 0.10, 26), 0, 0.5)
    mix_in(se, blip(990, 1050, 0.14, 20), 0.09, 0.5)
    mix_in(se, metal(320, 0.7, 0.22, tone=6000), 0.10, 0.5)
    render(se, "select.ogg", 0.85, 0.22, seed=2)

    # cancelar: dos tonos descendentes + golpe sordo
    ca = np.zeros(int(1.0 * SR))
    mix_in(ca, blip(1000, 940, 0.10, 26), 0, 0.5)
    mix_in(ca, blip(700, 520, 0.16, 18), 0.09, 0.5)
    mix_in(ca, thump(70, 0.6, 0.3), 0.02, 0.5)
    render(ca, "cancel.ogg", 0.7, 0.2, seed=3)

    # arrancar el juego: golpe grave + subida + metal (cinematográfico)
    st = np.zeros(int(4.0 * SR))
    mix_in(st, thump(40, 3.0, 0.8), 0.0)
    mix_in(st, thump(34, 2.4, 0.5), 0.22)
    mix_in(st, riser(1.1, 130, 3000, seed=9) * 0.8, 0.0)
    mix_in(st, metal(150, 2.4, 0.7, tone=5000), 0.10, 0.6)
    mix_in(st, glass(midi(62), 3.2, 0.4), 0.30, -0.2)
    mix_in(st, glass(midi(69), 3.0, 0.32), 0.52, 0.25)
    render(st, "start.ogg", 3.2, 0.30, seed=4)

    # salir: apagado — grave que cae y aire que se va
    cl = np.zeros(int(3.0 * SR))
    t = t_axis(2.6)
    mix_in(cl, np.sin(2 * np.pi * np.cumsum(58 - 26 * (t / 2.6)) / SR) * np.exp(-t * 1.5) * 0.6, 0.0)
    mix_in(cl, highpass(band_noise(len(t), 200, 8000, seed=13), 400) * np.exp(-t * 1.2) * 0.25, 0.0)
    mix_in(cl, blip(1200, 300, 0.5, 7), 0.0, 0.25)
    mix_in(cl, metal(120, 1.6, 0.35, tone=3000), 0.05, 0.4)
    render(cl, "close.ogg", 2.6, 0.35, seed=6)

    # logro: campana digital limpia + arpegio (reservado)
    ac = np.zeros(int(3.6 * SR))
    mix_in(ac, chime(midi(86), 3.2, 1.0), 0.0)
    for i, n in enumerate([74, 81, 86, 93]):
        mix_in(ac, chime(midi(n), 2.2, 0.42), 0.10 + i * 0.10)
    render(ac, "achievement.ogg", 3.3, 0.34, seed=7)


if __name__ == "__main__":
    print("Música:")
    music()
    print("Efectos:")
    sfx()
