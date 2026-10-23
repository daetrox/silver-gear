<h1 align="center">⚙️ Silver Gear</h1>

<p align="center">
  Un lanzador para Linux y Windows que convierte tu copia de <i>Metal Gear Solid</i> de PlayStation<br>
  en un juego que se abre como cualquier programa: con su propio menú, a 60 Hz y en pantalla panorámica.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Linux-x86__64-555?logo=linux&logoColor=white" alt="Linux">
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-555?logo=windows&logoColor=white" alt="Windows 10/11">
  <img src="https://img.shields.io/badge/licencia-GPL--3.0-2c2c2c" alt="GPL-3.0">
  <img src="https://img.shields.io/badge/sin%20fines%20de%20lucro-%E2%9D%A4-a31515" alt="Sin fines de lucro">
</p>

> [!WARNING]
> **En desarrollo y sin publicar.** El fondo del menú lo genera `tools/make_background.py` a partir de
> la ilustración de la base nevada de daetrox (`tools/photo/base-nevada.png`), con el rótulo propio
> «SILVER GEAR». Para distribuir: `make_background.py --publicable` borra el emblema del zorro (recuerda
> al de FOXHOUND, de Konami); si la ilustración se hizo con IA, revisar los términos de la herramienta.
> Las bandas y los iconos de los botones siguen saliendo de la maqueta antigua (`tools/source.png`):
> confirmar que son propios antes de publicar.

---

## ¿Qué es esto?

**Silver Gear** es un *launcher*: una pequeña aplicación con su propio menú, música y animaciones que
lleva dentro un emulador de PlayStation muy preciso (SwanStation). Tú solo ves **un juego**: abres el
programa, pulsas **JUGAR** y estás dentro. Nace de [Kage no Mon](https://github.com/daetrox/kage-no-mon),
el launcher de *Tenchu 2*, con la misma arquitectura.

> [!IMPORTANT]
> **No incluye el juego.** Necesitas tu propia copia de *Metal Gear Solid* (los dos discos). La BIOS no
> hace falta: viene integrada **OpenBIOS**, una BIOS libre. Si lo prefieres, puedes usar la de tu consola.

## Características

| | |
|---|---|
| 💿 **Dos discos, una consola** | Los dos discos se cargan juntos (lista M3U): comparten tarjeta de memoria y se cambia de disco desde la pausa o con F8, sin reiniciar. |
| ⏱️ **60 Hz en la versión europea** | El parche «NTSC Mode» de PeterDelta quita la lentitud y las bandas del PAL. Activado por defecto. |
| 🖥️ **16:9 de verdad** | Parche panorámico del propio juego (PeterDelta), sin deformar la imagen. |
| ✨ **Gráficos mejorados** | Hasta 5× la resolución original, texturas suavizadas (xBR) y polígonos sin temblores (PGXP). |
| 🎮 **Teclado, Xbox o PlayStation** | Botones personalizables; los iconos del launcher cambian según lo que uses. El joystick analógico funciona en el juego. |
| ⏸️ **Menú de pausa** | Reanudar, guardar/cargar, cambiar de disco, controles, menú principal y salir. Esc, L3+R3 o el botón central. Nunca roba un botón del juego. |
| 🏆 **Logros sin conexión** | RetroAchievements: se descargan una vez y después se juegan *offline*. |
| 💾 **Ranuras con ficha** | Autoguardado al salir y tres ranuras manuales con miniatura, fecha y disco; JUGAR muestra la ficha de la última partida. |
| 📻 **Estilo códec** | Pantalla de carga sintonizando la 140.85, y avisos, diálogos y logros con el aspecto del códec. |
| 📺 **Filtros de pantalla** | Líneas de barrido o CRT (curvatura, máscara de color y viñeta); laterales con la ilustración, «ambiente» o negro. |
| 🧠 **Psycho Mantis** | Pausa › MANDO: PUERTO 2, el truco de la consola original. Ayuda sin destripes (frecuencia de Meryl) en CONTROLES. |
| 🌍 **Tres idiomas** | Español, inglés y francés; logros de RetroAchievements traducidos al español. |
| 🧪 **Trucos** | Los de la base de datos chtdb de DuckStation, con las variantes (tarjeta de nivel 1–7, temperatura de la tarjeta PAL…). |
| ❄️ **Menú ártico** | Base militar en la montaña nevada con soldados de camuflaje invernal, nieve con viento; español e inglés. |

## Discos

Copia los dos discos en la carpeta `game/` del launcher (o elígelos en **OPCIONES › DISCOS Y BIOS**). El
Disco 2 se encuentra solo si está junto al Disco 1 con el mismo nombre (`… (Disc 2) …`).

Probado con la versión española **Rev 1** (`SLES-01734` / `SLES-11734`, redump). Para la americana
(`SLUS-00594` / `SLUS-00776`) hay parche 16:9 y trucos; el modo NTSC no hace falta.

## Controles en partida

| Tecla | Acción |
|---|---|
| Esc · L3+R3 · botón central | Pausa |
| F5 / F7 | Guardar / cargar estado |
| F8 | Cambiar de disco |
| F10 | Captura de pantalla (carpeta `capturas/`) |
| F11 · Alt+Intro | Pantalla completa |
| F12 | Volcado de la RAM (diagnóstico) |

En los menús del juego, MGS confirma con **○** y cancela con **✕** (como en la consola).

## Compilar

```bash
./build.sh                    # SDL2 + SwanStation + launcher → build/dist/silvergear
./build-windows.sh            # Windows (llvm-mingw) → build-win/dist/SilverGear.exe
```

Pruebas sin ventana interactiva: `silvergear --screenshot salida.ppm --screen menu|options|game|continue --time N`
con las variables `SILVER_TEST_*` (por ejemplo `SILVER_TEST_SCRIPT="36:start,42:start,46:circle,60:swap"`;
`swap` cambia de disco). Usa siempre `XDG_CONFIG_HOME`/`XDG_DATA_HOME` aislados y `SILVER_TEST_STATE`
para no tocar tu partida.

## Créditos

- **daetrox** — idea, dirección y diseño. **Claude (Anthropic)** — programación.
- **SwanStation** (GPL-3.0), **OpenBIOS** de PCSX-Redux (MIT), **rcheevos** (MIT), **SDL2** (zlib), **stb** (dominio público).
- **PeterDelta** y la comunidad de **chtdb** — parches y trucos.
- Fuentes **Russo One**, **Oswald**, **Saira** (rótulo) y **Share Tech Mono** (códec), todas SIL OFL 1.1.
- Fondo: ilustración de daetrox. Alternativa libre (`--noruega`): foto del Sgt. Avery Cunningham, Ejército de EE. UU. (dominio público, DVIDS 8299563).

*Metal Gear Solid* es una marca registrada de Konami Digital Entertainment. Este proyecto no está
afiliado a Konami. Software libre (GPL-3.0), sin fines de lucro.
