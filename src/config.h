#pragma once
#include <string>
#include <vector>

// Botones del mando de PlayStation; el índice es RETRO_DEVICE_ID_JOYPAD_* (0..15).
constexpr int kPsButtons = 16;
// Asignación del mando: 0..99 = botón SDL_GameControllerButton, 100+eje = gatillo, -1 = sin asignar.
constexpr int kPadAxisBase = 100;
// Botones del ratón en las asignaciones de teclado (esquema teclado y ratón).
constexpr int kMouseLeft = 1001, kMouseRight = 1002, kMouseMiddle = 1003, kWheelUp = 1004, kWheelDown = 1005;

struct Config
{
  Config() { DefaultKeyboard(); DefaultPad(); }
  void DefaultKeyboard();
  void DefaultPad();

  int key_bind[kPsButtons]; // SDL_Scancode por botón (0 = sin asignar)
  int KeyBind(int retro_id) const { return key_bind[retro_id]; }
  int pad_bind[kPsButtons]; // ver kPadAxisBase

  std::string game_path;        // Disco 1: .cue / .bin / .chd seleccionado por el usuario
  std::string disc2_path;       // Disco 2 (vacío = se busca junto al Disco 1)
  std::string bios_file;        // BIOS elegida dentro de bios/ (vacío = OpenBIOS integrada)

  bool hardware_renderer = true; // OpenGL (permite escalado) o software (máxima precisión)
  int resolution_scale = 4;      // 1, 2, 4 o 5 (3x corrompía texto en SwanStation con Tenchu 2)
  std::string texture_filter = "xBR"; // Nearest, Bilinear, JINC2, xBR (suavizado "HD" de texturas)
  bool pgxp = true;              // corrige el "temblor" de polígonos
  bool pgxp_cpu = true;          // PGXP también para la geometría que el juego calcula con la CPU
  bool widescreen = false;       // 16:9 mediante parche del propio juego (si existe para el disco)
  int side_mode = 1;             // laterales con el juego en 4:3: 0 negro, 1 ilustración, 2 ambiente
  int screen_filter = 0;         // filtro de la imagen del juego: 0 ninguno, 1 líneas de barrido, 2 CRT
  bool smooth_filter = false;    // filtrado bilineal al escalar la imagen
  bool fullscreen = false;
  int volume = 100;              // 0..100, sonido del juego
  int music_volume = 70;         // 0..100, música del launcher
  int sfx_volume = 80;           // 0..100, efectos del launcher
  bool fast_boot = true;         // salta la intro de la BIOS
  bool hardcore = false;         // modo hardcore: BIOS original, sin estados guardados ni trucos
  std::string cheats;            // nombres de los trucos activos, separados por '|'
  bool ntsc_mode = true;          // 60 Hz en las versiones PAL (parche «NTSC Mode» de PeterDelta)
  bool subtitles = true;         // subtítulos en las escenas (el parche «Subtitles off» los quita)
  bool autosave = true;          // guardar la partida (ranura de autoguardado) al salir del juego
  std::string prompt_style = "auto"; // iconos de botones: auto, keyboard, xbox, playstation
  std::string language;              // "es" o "en"; vacío = según el idioma del sistema

  void Load();
  void Save() const;
};

namespace Paths {
std::string ExeDir();       // carpeta del ejecutable
std::string AssetsDir();    // <exe>/assets
std::string CorePath();     // <exe>/core/swanstation_libretro.so
std::string ConfigDir();    // ~/.config/silvergear
std::string DataDir();      // ~/.local/share/silvergear
std::string SavesDir();     // tarjetas de memoria
std::string StatesDir();    // estados guardados
std::string UserDir();      // junto al ejecutable, o junto al .AppImage si se ejecuta así
std::string BiosDir();      // <exe>/bios: BIOS originales del usuario (no se distribuyen)
std::string GameDir();      // <exe>/game: imagen del juego del usuario (no se distribuye)
// Busca BIOS (archivos de 512 KB) en BiosDir(). Devuelve sus nombres.
std::vector<std::string> FindBiosFiles();
bool FileExists(const std::string& path);
// Número de serie del disco (p. ej. "SLES-01734") leído de SYSTEM.CNF; vacío si no se puede leer.
std::string ReadDiscSerial(const std::string& image_path);
// El Disco 2 del mismo juego junto al Disco 1 (mismo nombre con "Disc 2"…); vacío si no está.
std::string FindDiscTwo(const std::string& disc1);
bool CopyFile(const std::string& from, const std::string& to);
void MakeDirs(const std::string& path);
} // namespace Paths
