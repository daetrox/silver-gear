// Silver Gear - launcher para Metal Gear Solid (PlayStation)
// Lanzador independiente que integra el núcleo SwanStation (libretro) para que el juego se
// sienta como una aplicación nativa: sin menús ni mensajes del emulador.
//
// La interfaz se diseña en un lienzo de 1536x1024 que se escala a la altura de la ventana. El
// fondo y los botones del menú principal van alineados con la ilustración (assets/ui/, 1672x941).
#include "achievements.h"
#include "audio.h"
#include "cheats.h"
#include "i18n.h"
#include "platform.h"
#include "config.h"
#include "emulator.h"
#include "gl.h"
#include "libretro.h"
#include "renderer.h"

extern "C" {
#include "md5.h"
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "ext/stb_image_write.h"

#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kVersion = "v0.1.0";
constexpr float kImgW = 1536.0f; // lienzo de la interfaz
constexpr float kImgH = 1024.0f;
constexpr float kArtW = 1672.0f; // ilustración del fondo (con el logotipo y los botones del menú)
constexpr float kArtH = 941.0f;
constexpr float kTitleH = 40.0f;

const Color kWhite = {0.95f, 0.95f, 0.94f, 1.0f};
const Color kGrey = {0.74f, 0.74f, 0.74f, 1.0f};
const Color kDim = {0.55f, 0.55f, 0.56f, 1.0f};
const Color kRed = {0.86f, 0.1f, 0.07f, 1.0f};   // alerta («!»), errores
const Color kGold = {1.0f, 0.78f, 0.3f, 1.0f};   // puntos y contadores
const Color kSilver = {0.78f, 0.8f, 0.83f, 1.0f}; // filetes de los paneles
const Color kInk = {0.06f, 0.06f, 0.06f, 1.0f};   // texto sobre la banda blanca (seleccionada)
const Color kPanelBg = {0.03f, 0.03f, 0.035f, 0.92f};
const Color kCodecBg = {0.015f, 0.06f, 0.04f, 0.93f};  // pantalla del códec
const Color kCodec = {0.45f, 1.0f, 0.62f, 1.0f};       // verde del códec (texto y borde)
const Color kCodecText = {0.82f, 1.0f, 0.88f, 1.0f};

Color WithAlpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float EaseOut(float x)
{
  x = Clamp01(x);
  return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}
float EaseInOut(float x)
{
  x = Clamp01(x);
  return x < 0.5f ? 4 * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) / 2.0f;
}
float Smooth(float a, float b, float x)
{
  const float t = Clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}

// Duraciones de las animaciones de apertura y cierre (segundos).
constexpr float kIntroLen = 2.5f;
constexpr float kOutroLen = 1.25f;

std::string BaseName(const std::string& path)
{
  const size_t s = path.find_last_of("/\\");
  return s == std::string::npos ? path : path.substr(s + 1);
}

std::string StemName(const std::string& path)
{
  std::string b = BaseName(path);
  const size_t d = b.find_last_of('.');
  return d == std::string::npos ? b : b.substr(0, d);
}

std::string Lower(std::string s)
{
  for (char& c : s)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

bool HasExt(const std::string& path, const char* ext)
{
  const std::string l = Lower(path);
  const size_t n = std::strlen(ext);
  return l.size() > n && l.compare(l.size() - n, n, ext) == 0;
}

bool IsDiscImage(const std::string& path)
{
  for (const char* ext : {".cue", ".chd", ".bin", ".iso", ".img", ".pbp", ".ecm", ".m3u"})
    if (HasExt(path, ext))
      return true;
  return false;
}

// Quita el último carácter UTF-8 completo.
void PopUtf8(std::string& s)
{
  while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)
    s.pop_back();
  if (!s.empty())
    s.pop_back();
}

// Busca la imagen del disco: primero en <exe>/game, después junto al ejecutable y carpetas superiores.
std::string AutoDetectGame()
{
  std::vector<std::string> dirs = {Paths::GameDir(), Paths::UserDir()};
  std::string d = Paths::ExeDir();
  for (int i = 0; i < 4; i++)
  {
    dirs.push_back(d);
    const size_t s = d.find_last_of("/\\");
    if (s == std::string::npos || s == 0)
      break;
    d = d.substr(0, s);
  }
  for (const std::string& dir : dirs)
  {
    std::string best;
    std::error_code ec;
    for (const std::filesystem::directory_entry& e : std::filesystem::directory_iterator(std::filesystem::u8path(dir), ec))
    {
      const std::string name = e.path().filename().u8string();
      if (!IsDiscImage(name))
        continue;
      const bool pref = HasExt(name, ".cue") || HasExt(name, ".chd");
      if (best.empty() || pref)
        best = dir + "/" + name;
    }
    if (!best.empty())
      return best;
  }
  return {};
}

std::vector<std::string> SplitLines(const std::string& s)
{
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string line;
  while (std::getline(ss, line))
    out.push_back(line);
  return out;
}

// --- Selector de archivos nativo (Windows: diálogo del sistema; Linux: zenity / kdialog), en un
// hilo aparte ---
class FilePicker
{
public:
  static bool Available()
  {
#ifdef _WIN32
    return true;
#else
    return std::system("command -v zenity >/dev/null 2>&1") == 0 ||
           std::system("command -v kdialog >/dev/null 2>&1") == 0;
#endif
  }

  void Open(const std::string& title, const std::string& patterns, std::function<void(const std::string&)> cb)
  {
    if (m_busy)
      return;
    m_busy = true;
    m_done = false;
    m_callback = std::move(cb);
    std::thread([this, title, patterns]() {
#ifdef _WIN32
      const std::string result = Platform::OpenFileDialog(title, patterns);
#else
      std::string cmd;
      if (std::system("command -v zenity >/dev/null 2>&1") == 0)
        cmd = "zenity --file-selection --title=\"" + title + "\" --file-filter=\"" + title + " | " + patterns +
              "\" --file-filter=\"" + L("Todos", "All files") + " | *\" 2>/dev/null";
      else
        cmd = "kdialog --getopenfilename \"$HOME\" \"" + patterns + "\" --title \"" + title + "\" 2>/dev/null";
      std::string result;
      if (FILE* f = popen(cmd.c_str(), "r"))
      {
        char buf[4096];
        while (fgets(buf, sizeof(buf), f))
          result += buf;
        pclose(f);
      }
      while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
#endif
      std::lock_guard<std::mutex> lock(m_mutex);
      m_result = result;
      m_done = true;
    }).detach();
  }

  void Poll()
  {
    if (!m_busy || !m_done)
      return;
    std::string r;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      r = m_result;
    }
    m_busy = false;
    if (!r.empty() && m_callback)
      m_callback(r);
  }

private:
  std::atomic<bool> m_busy{false};
  std::atomic<bool> m_done{false};
  std::mutex m_mutex;
  std::string m_result;
  std::function<void(const std::string&)> m_callback;
};

enum class Screen
{
  Menu,
  Options,
  Controls,
  Remap,
  Credits,
  Cheats,
  Achievements,
  Game,
};

enum class Modal
{
  None,
  Dialog,
  PlayChoice,
  ConfirmReset, // confirmar el reinicio de los logros
  Slots,        // elegir ranura para guardar o cargar
};

// Ranuras de partida: 0 = autoguardado (al salir), 1..3 = manuales.
constexpr int kSlots = 4;
struct SlotInfo
{
  bool exists = false;
  int64_t time = 0; // epoch del guardado (0 si no consta)
  int disc = 0;     // disco insertado (0 = Disco 1)
  Texture thumb;    // miniatura de la imagen del juego
  std::string thumb_key;
};

// Guarda una imagen RGBA como PNG (con rutas UTF-8 también en Windows).
bool WritePng(const std::string& path, const uint8_t* rgba, int w, int h)
{
  FILE* f = Platform::OpenFile(path.c_str(), "wb");
  if (!f)
    return false;
  const int ok = stbi_write_png_to_func([](void* ctx, void* data, int size) { std::fwrite(data, 1, size, static_cast<FILE*>(ctx)); },
                                        f, w, h, 4, rgba, w * 4);
  std::fclose(f);
  return ok != 0;
}

// Reduce una imagen RGBA a `tw`x`th` promediando bloques.
std::vector<uint8_t> Downscale(const std::vector<uint8_t>& src, int sw, int sh, int tw, int th)
{
  std::vector<uint8_t> out(static_cast<size_t>(tw) * th * 4);
  for (int y = 0; y < th; y++)
    for (int x = 0; x < tw; x++)
    {
      const int x0 = x * sw / tw, x1 = std::max(x0 + 1, (x + 1) * sw / tw);
      const int y0 = y * sh / th, y1 = std::max(y0 + 1, (y + 1) * sh / th);
      unsigned sum[4] = {};
      int n = 0;
      for (int yy = y0; yy < y1; yy += std::max(1, (y1 - y0) / 4))
        for (int xx = x0; xx < x1; xx += std::max(1, (x1 - x0) / 4))
        {
          const uint8_t* p = &src[(static_cast<size_t>(yy) * sw + xx) * 4];
          for (int c = 0; c < 4; c++)
            sum[c] += p[c];
          n++;
        }
      for (int c = 0; c < 4; c++)
        out[(static_cast<size_t>(y) * tw + x) * 4 + c] = static_cast<uint8_t>(sum[c] / n);
    }
  return out;
}

enum class PromptStyle
{
  Keyboard,
  Xbox,
  PlayStation,
};

// Botones del mando de PlayStation (lo que espera el juego) y acciones de interfaz.
enum class PsButton
{
  Cross,
  Circle,
  Square,
  Triangle,
  L1,
  R1,
  L2,
  R2,
  L3,
  R3,
  Start,
  Select,
  DPad,
  LStick,
  RStick,
  DUp,    // direcciones sueltas de la cruceta (pantalla de personalización)
  DDown,
  DLeft,
  DRight,
  Accept, // aceptar en menús
  Back,   // volver en menús
  Pause,  // abrir la pausa durante el juego
};

struct Rect
{
  float x = 0, y = 0, w = 0, h = 0;
  bool Contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

// Zona interactiva registrada al dibujar; los eventos de ratón usan las del último frame.
struct HotZone
{
  Rect r;
  int group; // 0 menú, 1 opciones, 2 pausa, 3 elección, 4 login, 5 barra de título, 6 genérico
  int index;
  std::function<void(float, float)> click;
};

struct Popup
{
  uint32_t id;
  float time;
};

// Botones del menú en el orden de la maqueta, en píxeles de la ilustración: están pintados a la
// derecha, bajo el logotipo, y se dibujan alineados con el fondo.
enum MenuItem
{
  MENU_PLAY,
  MENU_ACHIEVEMENTS,
  MENU_OPTIONS,
  MENU_CREDITS,
  MENU_QUIT,
};
constexpr int kMenuItems = 5;
const Rect kMenuSlots[kMenuItems] = {
  {1075, 314, 513, 88},
  {1075, 422, 513, 88},
  {1075, 527, 513, 88},
  {1075, 632, 513, 88},
  {1075, 737, 513, 88},
};
// Dentro de las bandas (en fracción del ancho y del alto): centro del icono, separador pintado en la
// banda y comienzo del texto.
constexpr float kBandIconX = 0.172f, kBandIconH = 0.56f, kBandTextX = 0.38f;

enum OptRow
{
  OPT_GAME,
  OPT_DISC2,
  OPT_BIOS,
  OPT_BIOS_LOAD,
  OPT_FASTBOOT,
  OPT_HARDCORE,
  OPT_RENDERER,
  OPT_SCALE,
  OPT_TEXFILTER,
  OPT_PGXP,
  OPT_WIDESCREEN,
  OPT_SIDEART,
  OPT_SCREENFILTER,
  OPT_SMOOTH,
  OPT_FULLSCREEN,
  OPT_VOLUME,
  OPT_MUSIC,
  OPT_SFX,
  OPT_PROMPTS,
  OPT_REMAP_KB,
  OPT_REMAP_PAD,
  OPT_RESET_CONTROLS,
  OPT_CONTROLS,
  OPT_NTSC,
  OPT_SUBTITLES,
  OPT_AUTOSAVE,
  OPT_BACK,
};

// Menú de pausa.
enum PauseItem
{
  PAUSE_RESUME,
  PAUSE_SAVE,
  PAUSE_LOAD,
  PAUSE_DISC,
  PAUSE_PORT, // mando en el puerto 2 (truco contra Psycho Mantis)
  PAUSE_CONTROLS,
  PAUSE_MENU,
  PAUSE_QUIT,
};
constexpr int kPauseItems = 8;

// Opciones: lista de categorías (página -1) y sus páginas.
enum OptCategoryId
{
  CAT_DISCS,
  CAT_GRAPHICS,
  CAT_GAME,
  CAT_SOUND,
  CAT_CONTROLS,
  CAT_CHEATS, // abre la pantalla de trucos en lugar de una página de filas
};
constexpr int kOptCategoryCount = 6;
const char* OptCategory(int i)
{
  static const char* const es[kOptCategoryCount] = {"DISCOS Y BIOS", "GRÁFICOS", "JUEGO", "SONIDO", "CONTROLES", "TRUCOS"};
  static const char* const en[kOptCategoryCount] = {"DISCS & BIOS", "GRAPHICS", "GAME", "SOUND", "CONTROLS", "CHEATS"};
  return LangEn() ? en[i] : es[i];
}
// En el lugar del icono, el número de la categoría (como las fichas de un expediente).
std::string OptCategoryNumber(int i) { return (i < 9 ? "0" : "") + std::to_string(i + 1); }
const char* OptCategoryHelp(int i)
{
  switch (i)
  {
    case CAT_DISCS: return L("Los dos discos del juego, BIOS de la consola y arranque.",
                             "The game's two discs, console BIOS and boot.");
    case CAT_GRAPHICS: return L("Resolución, filtros, pantalla panorámica y pantalla completa.",
                                "Resolution, filters, widescreen and fullscreen.");
    case CAT_GAME: return L("60 Hz para la versión europea, subtítulos de las escenas y modo hardcore.",
                            "60 Hz for the European version, cutscene subtitles and hardcore mode.");
    case CAT_SOUND: return L("Volumen del juego, de la música y de los efectos.", "Game, music and sound effects volume.");
    case CAT_CONTROLS: return L("Iconos de botones, personalización y esquema de controles.",
                                "Button icons, remapping and controls layout.");
    default: return L("Vida infinita, armas, objetos y más. Mientras haya trucos activos, los logros se pausan.",
                      "Infinite health, weapons, items and more. While cheats are active, achievements are paused.");
  }
}

const std::vector<int>& OptPageRows(int page)
{
  static const std::vector<int> pages[kOptCategoryCount] = {
    {OPT_GAME, OPT_DISC2, OPT_BIOS, OPT_BIOS_LOAD, OPT_FASTBOOT, OPT_BACK},
    {OPT_RENDERER, OPT_SCALE, OPT_TEXFILTER, OPT_PGXP, OPT_WIDESCREEN, OPT_SCREENFILTER, OPT_SIDEART, OPT_SMOOTH,
     OPT_FULLSCREEN, OPT_BACK},
    {OPT_NTSC, OPT_SUBTITLES, OPT_AUTOSAVE, OPT_HARDCORE, OPT_BACK},
    {OPT_VOLUME, OPT_MUSIC, OPT_SFX, OPT_BACK},
    {OPT_PROMPTS, OPT_REMAP_KB, OPT_REMAP_PAD, OPT_RESET_CONTROLS, OPT_CONTROLS, OPT_BACK},
    {OPT_BACK}, // TRUCOS tiene su propia pantalla
  };
  return pages[std::clamp(page, 0, kOptCategoryCount - 1)];
}

// Fila de una página de opciones (para volver a ella desde otra pantalla).
int OptRowIndex(int page, int row)
{
  const std::vector<int>& rows = OptPageRows(page);
  for (size_t i = 0; i < rows.size(); i++)
    if (rows[i] == row)
      return static_cast<int>(i);
  return 0;
}

// Nombre legible de una BIOS de bios/: modelo y región si se reconoce su huella MD5.
std::string BiosLabel(const std::string& file)
{
  static std::map<std::string, std::string> cache;
  const std::string key = (LangEn() ? "en:" : "es:") + file;
  auto it = cache.find(key);
  if (it != cache.end())
    return it->second;
  static const std::pair<const char*, const char*> known[] = {
    {"239665b1a3dade1b5a52c06338011044", "SCPH-1000 · Japón"},
    {"924e392ed05558ffdb115408c263dccf", "SCPH-1001 · EE. UU."},
    {"54847e693405ffeb0359c6287434cbef", "SCPH-1002 · Europa"},
    {"8dd7d5296a650fac7319bce665a6a53c", "SCPH-5500 · Japón"},
    {"490f666e1afb15b7362b406ed1cea246", "SCPH-5501 · EE. UU."},
    {"32736f17079d0b2b7024407c39bd3050", "SCPH-5502 · Europa"},
    {"1e68c231d0896b7eadcad1d7d8e76129", "SCPH-7001 · EE. UU."},
    {"b9d9a0286c33dc6b7237bb13cd46fdee", "SCPH-7502 · Europa"},
    {"6e3735ff4c7dc899ee98981385f6f3d0", "SCPH-101 · EE. UU."},
  };
  std::string label = file;
  if (FILE* f = Platform::OpenFile((Paths::BiosDir() + "/" + file).c_str(), "rb"))
  {
    std::vector<unsigned char> data(512 * 1024);
    const size_t n = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    md5_state_t st;
    md5_byte_t digest[16];
    md5_init(&st);
    md5_append(&st, data.data(), static_cast<int>(n));
    md5_finish(&st, digest);
    char hex[33];
    for (int i = 0; i < 16; i++)
      std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    for (const auto& [h, name] : known)
      if (std::strcmp(h, hex) == 0)
        label = std::string(name) + "  (" + file + ")";
    if (LangEn())
      for (const auto& [from, to] : {std::pair{"Japón", "Japan"}, std::pair{"EE. UU.", "USA"}, std::pair{"Europa", "Europe"}})
        if (const size_t at = label.find(from); at != std::string::npos)
          label.replace(at, std::strlen(from), to);
  }
  cache[key] = label;
  return label;
}

// --- Asignación de controles ---

// Orden de los botones en la pantalla de personalización (índices RETRO_DEVICE_ID_JOYPAD_*).
const int kRemapOrder[kPsButtons] = {
  RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
  RETRO_DEVICE_ID_JOYPAD_B,  RETRO_DEVICE_ID_JOYPAD_A,    RETRO_DEVICE_ID_JOYPAD_Y,    RETRO_DEVICE_ID_JOYPAD_X,
  RETRO_DEVICE_ID_JOYPAD_L,  RETRO_DEVICE_ID_JOYPAD_R,    RETRO_DEVICE_ID_JOYPAD_L2,   RETRO_DEVICE_ID_JOYPAD_R2,
  RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3,   RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT,
};

const char* PsButtonName(int retro_id)
{
  switch (retro_id)
  {
    case RETRO_DEVICE_ID_JOYPAD_UP: return L("Arriba", "Up");
    case RETRO_DEVICE_ID_JOYPAD_DOWN: return L("Abajo", "Down");
    case RETRO_DEVICE_ID_JOYPAD_LEFT: return L("Izquierda", "Left");
    case RETRO_DEVICE_ID_JOYPAD_RIGHT: return L("Derecha", "Right");
    case RETRO_DEVICE_ID_JOYPAD_B: return L("Cruz", "Cross");
    case RETRO_DEVICE_ID_JOYPAD_A: return L("Círculo", "Circle");
    case RETRO_DEVICE_ID_JOYPAD_Y: return L("Cuadrado", "Square");
    case RETRO_DEVICE_ID_JOYPAD_X: return L("Triángulo", "Triangle");
    case RETRO_DEVICE_ID_JOYPAD_L: return "L1";
    case RETRO_DEVICE_ID_JOYPAD_R: return "R1";
    case RETRO_DEVICE_ID_JOYPAD_L2: return "L2";
    case RETRO_DEVICE_ID_JOYPAD_R2: return "R2";
    case RETRO_DEVICE_ID_JOYPAD_L3: return "L3";
    case RETRO_DEVICE_ID_JOYPAD_R3: return "R3";
    case RETRO_DEVICE_ID_JOYPAD_START: return "Start";
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return "Select";
  }
  return "?";
}

// Teclas que el usuario no puede asignar: Esc abre la pausa y las F están ocupadas por el launcher.
bool IsReservedKey(SDL_Scancode sc)
{
  return sc == SDL_SCANCODE_ESCAPE || (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12);
}

std::string KeyLabel(int scancode)
{
  if (scancode <= 0)
    return "—";
  switch (scancode)
  {
    case kMouseLeft: return L("Clic izq.", "Left click");
    case kMouseRight: return L("Clic der.", "Right click");
    case kMouseMiddle: return L("Clic central", "Middle click");
    case kWheelUp: return L("Rueda arriba", "Wheel up");
    case kWheelDown: return L("Rueda abajo", "Wheel down");
    default: break;
  }
  const auto sc = static_cast<SDL_Scancode>(scancode);
  switch (sc)
  {
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_KP_ENTER: return L("Enter (num.)", "Enter (keypad)");
    case SDL_SCANCODE_BACKSPACE: return L("Retroceso", "Backspace");
    case SDL_SCANCODE_SPACE: return L("Espacio", "Space");
    case SDL_SCANCODE_TAB: return "Tab";
    case SDL_SCANCODE_UP: return L("Arriba", "Up");
    case SDL_SCANCODE_DOWN: return L("Abajo", "Down");
    case SDL_SCANCODE_LEFT: return L("Izquierda", "Left");
    case SDL_SCANCODE_RIGHT: return L("Derecha", "Right");
    case SDL_SCANCODE_LSHIFT: return L("Mayús izq.", "Left Shift");
    case SDL_SCANCODE_RSHIFT: return L("Mayús der.", "Right Shift");
    case SDL_SCANCODE_LCTRL: return L("Ctrl izq.", "Left Ctrl");
    case SDL_SCANCODE_RCTRL: return L("Ctrl der.", "Right Ctrl");
    case SDL_SCANCODE_LALT: return "Alt";
    case SDL_SCANCODE_RALT: return "Alt Gr";
    case SDL_SCANCODE_CAPSLOCK: return L("Bloq Mayús", "Caps Lock");
    default: break;
  }
  const char* name = SDL_GetScancodeName(sc);
  return (name && *name) ? name : "?";
}

} // namespace

class App
{
public:
  int Run(int argc, char** argv);

private:
  bool Init(std::string* error);
  void Shutdown();
  void HandleEvent(const SDL_Event& e);
  void Update(float dt);
  void Render();

  // Dibujo
  void Layout();
  void RenderTitleBar();
  // Selector de idioma (arriba a la derecha): bandera del idioma actual y menú con los dos.
  void RenderLanguage();
  void DrawFlag(Lang lang, float x, float y, float w, float h, float alpha);
  void SetLanguage(Lang lang);
  void RenderMenu();
  void RenderOptions();
  void RenderOptionCategories();
  void RenderRemap();
  void RenderCredits();
  void RenderCheats();
  bool CheatEnabled(const std::string& name) const;
  void SetCheatEnabled(const std::string& name, bool on);
  std::vector<std::string> ActiveCheatCodes() const;
  void RenderConfirmReset();
  void AssignBinding(int retro_id, int value);
  void RenderControls();
  void RenderAchievements();
  void RenderGame();
  void RenderPause();
  void RenderPlayChoice();
  void RenderDialog();
  void RenderToast();
  void RenderPopups();
  void RenderTransition();
  void RenderHints(const std::vector<std::pair<PsButton, std::string>>& hints, float right_x, float y);
  float DrawGlyph(PsButton b, float x, float y, float h, PromptStyle style, bool draw = true);
  float DrawGlyph(PsButton b, float x, float y, float h) { return DrawGlyph(b, x, y, h, CurrentStyle()); }
  // `reserve`: ancho (px de la ilustración) que se deja libre a la derecha de la etiqueta.
  void DrawMenuButton(const Rect& slot, const std::string& label, const Texture* icon, float sel, float font,
                      bool own_background, float alpha = 1.0f, float dx = 0.0f, float reserve = 0.0f);
  void Panel(float x, float y, float w, float h);
  // Recuadro con estilo de pantalla del códec, en píxeles de pantalla.
  void CodecBox(float x, float y, float w, float h, float alpha = 1.0f)
  {
    m_r.Rect(x, y, w, h, UiMode::Codec, WithAlpha(kCodecBg, alpha), WithAlpha(kCodec, alpha));
  }
  // Pantalla de carga: el códec sintonizando la frecuencia 140.85 (`t` en segundos desde que empezó).
  void RenderLoading(float t, float alpha);
  float m_load_t = -1; // >= 0 durante la pantalla de carga de la partida
  bool m_hint_reveal = false; // ayuda de CONTROLES: frecuencia de Meryl a la vista
  // Aviso de rendimiento: frames emulados frente a los que tocaban, en ventanas de 5 s de juego.
  float m_speed_t = 0, m_play_t = 0;
  uint64_t m_speed_frames = 0;
  bool m_speed_warned = false;
  SlotInfo m_slots[kSlots];
  int m_slot_sel = 1, m_slot_last = 1; // ranura elegida en el selector y la última usada (F5/F7)
  bool m_slots_save = false;           // el selector guarda (o carga)
  void DimScreen(float a);
  std::vector<std::string> Wrap(const Font& f, const std::string& text, float size, float max_w);
  std::string Truncate(const Font& f, const std::string& text, float size, float max_w);
  // Texto limitado a `max_w`: si no cabe se recorta con "…", o se desplaza si `scroll` es true.
  void FitText(const Font& f, const std::string& text, float x, float y, float max_w, float size, Color c,
               Align align, bool scroll, int key);

  // Acciones
  void ActivateMenu(int index);
  void ActivatePause(int index);
  void StartGame(int slot); // slot < 0: partida nueva
  void StopGame(bool autosave);
  void OptionChange(int row, int dir);
  void OpenOptionPage(int page);
  void CycleBios(int dir);
  void ShowDialog(const std::string& text);
  void Toast(const std::string& text)
  {
    m_toast = text;
    m_toast_time = 2.5f;
  }
  void RequestQuit();
  void ToggleFullscreen();
  void ImportBios(const std::string& path);
  void SetGamePath(const std::string& path);
  // Estado de una ranura (0 = autoguardado: el nombre de siempre, para conservar las partidas viejas).
  std::string StatePath(int slot) const
  {
    std::string base = Paths::StatesDir() + "/" + StemName(m_cfg.game_path) + ".state";
    if (const char* test = std::getenv("SILVER_TEST_STATE")) // pruebas: nunca el estado del usuario
      base = test;
    return slot == 0 ? base : base.substr(0, base.size() - 6) + ".slot" + std::to_string(slot) + ".state";
  }
  bool SaveSlot(int slot);
  bool LoadSlot(int slot);
  void RefreshSlots();
  int LatestSlot() const;
  bool AnySlot() const { return LatestSlot() >= 0; }
  void OpenSlots(bool save);
  void ActivateSlot(int slot);
  void RenderSlots();
  // Ficha de una ranura (miniatura, nombre, fecha y disco) en píxeles de la interfaz.
  void DrawSlotCard(int slot, float x, float y, float thumb_w, bool selected);
  std::string SlotName(int slot) const;
  std::string SlotDate(int slot) const;
  // Disco 2: el elegido en Opciones o, si no, el que haya junto al Disco 1.
  std::string DiscTwoPath() const
  {
    return m_cfg.disc2_path.empty() ? Paths::FindDiscTwo(m_cfg.game_path) : m_cfg.disc2_path;
  }
  // Cambia al otro disco en plena partida (como abrir la tapa de la consola).
  void SwapDisc();
  // Qué parches del juego (16:9, 60 Hz, subtítulos) hay para el disco elegido.
  void UpdatePatchAvailability()
  {
    m_ws_serial_path = m_cfg.game_path;
    const std::string serial = Paths::ReadDiscSerial(m_cfg.game_path);
    m_ws_available = Emulator::HasPatch(serial, "Widescreen 16:9");
    m_ntsc_available = Emulator::HasPatch(serial, "NTSC Mode");
    m_subs_available = Emulator::HasPatch(serial, "Subtitles off");
  }
  bool HasSonyBios() const
  {
    return !m_cfg.bios_file.empty() && Paths::FileExists(Paths::BiosDir() + "/" + m_cfg.bios_file);
  }
  // En hardcore no se pueden usar estados guardados (norma de RetroAchievements).
  bool StatesBlocked()
  {
    if (!m_hardcore_session)
      return false;
    m_audio.Play(Sfx::Cancel);
    Toast(L("No disponible en modo hardcore", "Not available in hardcore mode"));
    return true;
  }
  void StartLogin();
  bool IntroActive() const { return m_intro_t < kIntroLen; }

  // Entrada
  PromptStyle CurrentStyle() const;
  PromptStyle PadStyle() const;
  void Navigate(int dy, int dx);
  void NavigateImpl(int dy, int dx);
  void Accept();
  void Back();
  EmuInput GatherGameInput();
  void OpenController(int index);
  bool TextFocus() const
  {
    return m_screen == Screen::Achievements && m_modal == Modal::None && m_login_focus < 2 &&
           m_ach.GetState() == Achievements::State::NeedLogin;
  }

  // Coordenadas de imagen -> pantalla
  float IX(float x) const { return m_ox + x * m_s; }
  float IY(float y) const { return m_oy + y * m_s; }
  float IS(float v) const { return v * m_s; }
  Rect IR(const Rect& r) const { return {IX(r.x), IY(r.y), IS(r.w), IS(r.h)}; }
  void Hot(const Rect& screen_rect, int group, int index, std::function<void(float, float)> click)
  {
    m_hot.push_back({screen_rect, group, index, std::move(click)});
  }

  static SDL_HitTestResult HitTest(SDL_Window* win, const SDL_Point* pt, void* data);

  SDL_Window* m_window = nullptr;
  SDL_GLContext m_gl = nullptr;
  Renderer m_r;
  Emulator m_emu;
  Config m_cfg;
  FilePicker m_picker;
  Achievements m_ach;
  Audio m_audio;

  Texture m_bg, m_btn_sel, m_row_sel, m_btn_normal; // m_row_sel: banda blanca sin separador (filas)
  Texture m_ic_play, m_ic_ach, m_ic_opt, m_ic_quit, m_ic_credits, m_ic_disc, m_ic_home;

  Screen m_screen = Screen::Menu;
  Modal m_modal = Modal::None;
  bool m_running = true;
  bool m_paused = false;
  int m_sel = 0;
  int m_opt_page = -1; // -1 = categorías
  int m_opt_sel = 0;
  int m_pause_sel = 0;
  int m_choice_sel = 0;
  int m_login_focus = 0; // 0 usuario, 1 contraseña, 2 botón
  float m_ach_scroll = 0, m_ach_scroll_target = 0;
  int m_ach_sel = 0;
  std::string m_login_user, m_login_pass;
  float m_sel_anim[8] = {};
  float m_time = 0;
  float m_fade = 0;
  float m_intro_t = 0; // tiempo desde que se abrió el launcher
  bool m_intro_sound = false;
  float m_outro_t = -1; // >= 0 mientras se reproduce la animación de cierre
  int m_w = 1200, m_h = 800;
  float m_s = 1, m_ox = 0, m_oy = 0;
  Rect m_bg_rect; // dónde se dibuja la ilustración completa (puede salirse de la pantalla)
  float m_mouse_px = -1, m_mouse_py = -1;
  float m_mouse_idle = 0;
  std::string m_dialog;
  std::string m_toast;
  float m_toast_time = 0;
  std::vector<HotZone> m_hot;
  std::vector<Popup> m_popups;
  std::string m_ach_game_path;
  std::string m_ws_serial_path;
  bool m_ws_available = false;   // el disco tiene parche 16:9
  bool m_ntsc_available = false; // y modo NTSC (60 Hz)
  bool m_subs_available = false; // y el de quitar los subtítulos
  std::vector<Cheat> m_cheat_list; // trucos del disco actual (pantalla TRUCOS)
  int m_cheat_sel = 0;
  float m_cheat_scroll = 0;
  bool m_cheats_active = false;    // la partida en curso usa trucos: logros en pausa
  bool m_hardcore_session = false; // la partida en curso es hardcore: sin estados guardados
  bool m_test_saved = false;
  float m_test_rstick[2] = {}; // pruebas: joystick derecho simulado
  float m_test_lstick[2] = {}; // y el izquierdo
  float m_credits_scroll = 0, m_credits_target = 0;
  float m_credits_height = 0; // alto total del texto de créditos (px de la ilustración)
  int m_remap_device = 0;   // 0 teclado, 1 mando
  int m_remap_sel = 0;      // 0..15 botones, 16 restaurar, 17 volver
  int m_remap_wait = -1;    // botón (RETRO_DEVICE_ID_JOYPAD_*) que espera una tecla/botón nuevo
  float m_remap_timer = 0;
  float m_reset_confirm = 0; // > 0: "Restaurar controles" pide una segunda pulsación
  int m_marquee_key = -1; // fila cuyo texto se está desplazando
  float m_marquee_start = 0;

  Rect m_tb_min, m_tb_max, m_tb_close;
  Rect m_tb_lang, m_lang_menu; // botón del idioma y su menú desplegable
  bool m_lang_open = false;

  SDL_GameController* m_pad = nullptr;
  PromptStyle m_last_device = PromptStyle::Keyboard;
  float m_axis_repeat = 0;

  // Pruebas automáticas (--screenshot).
  uint16_t m_test_buttons = 0;
  std::string m_screenshot_path;
  float m_screenshot_time = 2.0f;
};

// ------------------------------------------------------------------------------------------------
// Inicialización
// ------------------------------------------------------------------------------------------------

bool App::Init(std::string* error)
{
  SDL_SetHint("SDL_VIDEO_WAYLAND_WMCLASS", "silvergear");
  SDL_SetHint("SDL_VIDEO_X11_WMCLASS", "silvergear");
  SDL_SetHint(SDL_HINT_APP_NAME, "Metal Gear Solid");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0)
  {
    *error = SDL_GetError();
    return false;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI;
  if (m_cfg.fullscreen)
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  // 16:9, la proporción de la ilustración.
  m_window = SDL_CreateWindow("Metal Gear Solid", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, flags);
  if (!m_window)
  {
    *error = SDL_GetError();
    return false;
  }
  SDL_SetWindowMinimumSize(m_window, 960, 540);

  m_gl = SDL_GL_CreateContext(m_window);
  if (!m_gl)
  {
    *error = std::string(L("OpenGL 3.3 no disponible: ", "OpenGL 3.3 not available: ")) + SDL_GetError();
    return false;
  }
  SDL_GL_MakeCurrent(m_window, m_gl);
  if (!LoadGLFunctions())
  {
    *error = L("Faltan funciones de OpenGL 3.3", "OpenGL 3.3 functions are missing");
    return false;
  }
  SDL_GL_SetSwapInterval(1);
  SDL_SetWindowHitTest(m_window, &App::HitTest, this);

  if (!m_r.Init(Paths::AssetsDir(), error))
    return false;

  const std::string ui = Paths::AssetsDir() + "/ui/";
  bool ok = m_r.LoadTexture(ui + "background_clean.jpg", &m_bg, true);
  ok &= m_r.LoadTexture(ui + "btn_selected.png", &m_btn_sel, false);
  ok &= m_r.LoadTexture(ui + "row_selected.png", &m_row_sel, false);
  ok &= m_r.LoadTexture(ui + "btn_normal.png", &m_btn_normal, false);
  ok &= m_r.LoadTexture(ui + "icon_play.png", &m_ic_play, true);
  ok &= m_r.LoadTexture(ui + "icon_achievements.png", &m_ic_ach, true);
  ok &= m_r.LoadTexture(ui + "icon_options.png", &m_ic_opt, true);
  ok &= m_r.LoadTexture(ui + "icon_quit.png", &m_ic_quit, true);
  ok &= m_r.LoadTexture(ui + "icon_credits.png", &m_ic_credits, true);
  ok &= m_r.LoadTexture(ui + "icon_disc.png", &m_ic_disc, true);
  ok &= m_r.LoadTexture(ui + "icon_home.png", &m_ic_home, true);
  if (!ok)
  {
    *error = L("Faltan imágenes de la interfaz en ", "Interface images missing in ") + ui;
    return false;
  }

  // Icono de la ventana: engranaje plateado sobre un disco negro.
  if (SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32))
  {
    auto* px = static_cast<Uint32*>(icon->pixels);
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++)
      {
        const float fx = (x - 31.5f) / 31.5f, fy = (y - 31.5f) / 31.5f;
        const float r = std::sqrt(fx * fx + fy * fy);
        const float a = std::atan2(fy, fx);
        const float teeth = std::cos(a * 8.0f) > 0.15f ? 0.8f : 0.66f; // ocho dientes
        Uint8 rr = 0, gg = 0, bb = 0, aa = 0;
        if (r < 0.98f)
        {
          rr = gg = bb = 14;
          aa = 255;
        }
        if (r < teeth && r > 0.26f)
        {
          const Uint8 v = static_cast<Uint8>(170 + 70 * (1.0f - r)); // brillo metálico hacia el centro
          rr = v;
          gg = v;
          bb = static_cast<Uint8>(v + 8 > 255 ? 255 : v + 8);
        }
        px[y * 64 + x] = SDL_MapRGBA(icon->format, rr, gg, bb, aa);
      }
    SDL_SetWindowIcon(m_window, icon);
    SDL_FreeSurface(icon);
  }

  if (!m_audio.Init(Paths::AssetsDir()))
    SDL_Log("La interfaz funcionará sin música ni efectos");
  m_audio.SetVolumes(m_cfg.music_volume, m_cfg.sfx_volume);

  for (int i = 0; i < SDL_NumJoysticks(); i++)
    if (SDL_IsGameController(i))
    {
      OpenController(i);
      break;
    }
  return true;
}

void App::OpenController(int index)
{
  if (m_pad)
    return;
  m_pad = SDL_GameControllerOpen(index);
  m_emu.SetRumbleTarget(m_pad);
}

void App::Shutdown()
{
  StopGame(m_screenshot_path.empty()); // en el modo de prueba no se toca la partida del usuario
  m_audio.Shutdown();
  m_r.Shutdown();
  if (m_pad)
    SDL_GameControllerClose(m_pad);
  if (m_gl)
    SDL_GL_DeleteContext(m_gl);
  if (m_window)
    SDL_DestroyWindow(m_window);
  SDL_Quit();
}

SDL_HitTestResult App::HitTest(SDL_Window* win, const SDL_Point* pt, void* data)
{
  App* app = static_cast<App*>(data);
  const Uint32 flags = SDL_GetWindowFlags(win);
  if (flags & SDL_WINDOW_FULLSCREEN)
    return SDL_HITTEST_NORMAL;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);
  const int border = 6;
  if (!(flags & SDL_WINDOW_MAXIMIZED))
  {
    const bool l = pt->x < border, r = pt->x >= w - border, t = pt->y < border, b = pt->y >= h - border;
    if (t && l) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (t && r) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (b && l) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (b && r) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (t) return SDL_HITTEST_RESIZE_TOP;
    if (b) return SDL_HITTEST_RESIZE_BOTTOM;
    if (l) return SDL_HITTEST_RESIZE_LEFT;
    if (r) return SDL_HITTEST_RESIZE_RIGHT;
  }

  int dw, dh;
  SDL_GL_GetDrawableSize(win, &dw, &dh);
  const float px = pt->x * static_cast<float>(dw) / w, py = pt->y * static_cast<float>(dh) / h;
  if (app->m_screen != Screen::Game && py < app->IS(kTitleH))
  {
    if (app->m_tb_min.Contains(px, py) || app->m_tb_max.Contains(px, py) || app->m_tb_close.Contains(px, py) ||
        app->m_tb_lang.Contains(px, py))
      return SDL_HITTEST_NORMAL;
    return SDL_HITTEST_DRAGGABLE;
  }
  return SDL_HITTEST_NORMAL;
}

// ------------------------------------------------------------------------------------------------
// Acciones
// ------------------------------------------------------------------------------------------------

void App::ShowDialog(const std::string& text)
{
  m_dialog = text;
  m_modal = Modal::Dialog;
}

void App::ActivateMenu(int index)
{
  if (index != MENU_PLAY && index != MENU_QUIT)
    m_audio.Play(Sfx::Select);
  switch (index)
  {
    case MENU_PLAY:
      RefreshSlots();
      if (AnySlot() && !m_cfg.hardcore)
      {
        m_modal = Modal::PlayChoice;
        m_choice_sel = 0;
      }
      else
      {
        StartGame(-1);
      }
      break;
    case MENU_ACHIEVEMENTS:
      m_screen = Screen::Achievements;
      m_ach_scroll = m_ach_scroll_target = 0;
      m_ach_sel = 0;
      m_login_focus = 0;
      break;
    case MENU_OPTIONS:
      m_screen = Screen::Options;
      m_opt_page = -1;
      m_opt_sel = 0;
      break;
    case MENU_CREDITS:
      m_screen = Screen::Credits;
      m_credits_scroll = m_credits_target = -60; // empieza un poco más abajo y sube
      break;
    case MENU_QUIT: RequestQuit(); break;
  }
}

void App::SetGamePath(const std::string& path)
{
  // Si eligen el Disco 2, se usa como tal y el Disco 1 se busca a su lado.
  const std::string serial = Paths::ReadDiscSerial(path);
  if (serial == "SLES-11734" || serial == "SLUS-00776" || path.find("Disc 2") != std::string::npos)
  {
    for (const auto& [two, one] : {std::pair{"Disc 2", "Disc 1"}, std::pair{"disc 2", "disc 1"}, std::pair{"Disc2", "Disc1"}})
      if (const size_t at = path.rfind(two); at != std::string::npos)
      {
        std::string first = path;
        first.replace(at, std::strlen(two), one);
        if (Paths::FileExists(first))
        {
          m_cfg.game_path = first;
          m_cfg.disc2_path.clear();
          m_cfg.Save();
          return;
        }
      }
  }
  m_cfg.game_path = path;
  m_cfg.disc2_path.clear(); // se vuelve a buscar junto al disco nuevo
  m_cfg.Save();
}

void App::SwapDisc()
{
  const int count = m_emu.DiscCount();
  if (count < 2)
  {
    m_audio.Play(Sfx::Cancel);
    Toast(L("No hay segundo disco: ponlo junto al primero o elígelo en OPCIONES", "No second disc: put it next to the first one or choose it in OPTIONS"));
    return;
  }
  const int next = (m_emu.CurrentDisc() + 1) % count;
  if (m_emu.SwapDisc(next))
  {
    m_audio.Play(Sfx::Select);
    Toast(LangEn() ? "Disc " + std::to_string(next + 1) + " inserted" : "Disco " + std::to_string(next + 1) + " insertado");
  }
  else
  {
    m_audio.Play(Sfx::Cancel);
    Toast(L("No se pudo cambiar de disco", "Could not change disc"));
  }
}

void App::StartGame(int slot)
{
  m_modal = Modal::None;
  if (m_cfg.game_path.empty() || !Paths::FileExists(m_cfg.game_path))
  {
    ShowDialog(L("No hay ninguna imagen de disco seleccionada.\n\nCópiala en la carpeta game/ del launcher,\n"
                 "elígela en OPCIONES · ISO y BIOS,\no arrástrala sobre la ventana.",
                 "No disc image selected.\n\nCopy it into the launcher's game/ folder,\n"
                 "choose it in OPTIONS · ISO & BIOS,\nor drag it onto the window."));
    return;
  }

  // Hardcore: sólo con la BIOS original de la consola y siempre desde el arranque.
  if (m_cfg.hardcore && !HasSonyBios())
  {
    ShowDialog(L("El modo hardcore funciona sólo con la BIOS original de tu PlayStation.\n\nCárgala en OPCIONES · "
                 "ISO y BIOS, o desactiva allí el modo hardcore.",
                 "Hardcore mode only works with your PlayStation's original BIOS.\n\nLoad it in OPTIONS · "
                 "ISO & BIOS, or turn hardcore mode off there."));
    return;
  }
  m_hardcore_session = m_cfg.hardcore;
  if (m_hardcore_session)
    slot = -1;

  // La pantalla de carga mientras arranca el núcleo (que bloquea un momento) y los primeros segundos de
  // la partida, que con el arranque rápido son negros.
  m_r.BeginFrame(m_w, m_h, m_time);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  RenderLoading(0, 1);
  SDL_GL_SwapWindow(m_window);
  m_load_t = 0;

  m_audio.Play(Sfx::Start);
  std::vector<std::string> cheats = m_hardcore_session ? std::vector<std::string>{} : ActiveCheatCodes();
  m_cheats_active = !cheats.empty();
  // Los parches del juego (16:9, 60 Hz, subtítulos) no son trucos: los aplica el emulador y no
  // pausan los logros.
  m_emu.SetCheats(cheats);
  std::string error;
  if (!m_emu.Start(m_cfg, m_window, m_gl, &error))
  {
    ShowDialog(error);
    return;
  }
  m_emu.SetRumbleTarget(m_pad);
  if (slot >= 0 && !m_emu.LoadState(StatePath(slot)))
    Toast(L("No se pudo cargar la partida guardada", "Could not load the saved game"));
  if (slot > 0)
    m_slot_last = slot;

  // Logros: se evalúan tras cada frame emulado leyendo la RAM (en pausa si hay trucos activos).
  if (m_cheats_active)
  {
    m_ach.EndSession();
    Toast(L("Trucos activos: los logros están en pausa", "Cheats active: achievements are paused"));
  }
  else
  {
    m_ach.BeginSession(m_hardcore_session);
    if (m_hardcore_session)
      Toast(L("Modo hardcore: sin estados guardados ni trucos", "Hardcore mode: no save states or cheats"));
  }
  m_emu.on_frame = [this]() {
    size_t n = 0;
    const uint8_t* ram = m_emu.SystemRam(&n);
    // Pruebas: SILVER_TEST_POKE="13f1ce+=8,92580:32=41000" escribe (o suma) palabras de 16 bits
    // (o de 32 con ":32") en la RAM tras cada frame.
    // SILVER_TEST_POKE_FRAMES=N: sólo durante los N primeros frames.
    // SILVER_TEST_POKE_FROM=N: sólo a partir del frame N.
    const char* poke_frames = std::getenv("SILVER_TEST_POKE_FRAMES");
    const char* poke_from = std::getenv("SILVER_TEST_POKE_FROM");
    if (const char* poke = std::getenv("SILVER_TEST_POKE");
        poke && ram && (!poke_frames || m_emu.FramesRun() < static_cast<uint64_t>(std::atoll(poke_frames))) &&
        (!poke_from || m_emu.FramesRun() >= static_cast<uint64_t>(std::atoll(poke_from))))
    {
      std::stringstream ss(poke);
      std::string item;
      while (std::getline(ss, item, ','))
      {
        const size_t eq = item.find('=');
        if (eq == std::string::npos)
          continue;
        const bool add = eq > 0 && item[eq - 1] == '+';
        std::string a = item.substr(0, add ? eq - 1 : eq);
        const bool wide = a.size() > 3 && a.compare(a.size() - 3, 3, ":32") == 0; // "92580:32=..."
        if (wide)
          a.resize(a.size() - 3);
        const uint32_t addr = std::stoul(a, nullptr, 16) & (wide ? 0x1FFFFC : 0x1FFFFE);
        const int v = std::stoi(item.substr(eq + 1));
        if (addr + 4 > n)
          continue;
        uint8_t* p = const_cast<uint8_t*>(ram) + addr;
        if (wide)
        {
          int32_t cur;
          std::memcpy(&cur, p, 4);
          cur = add ? cur + v : v;
          std::memcpy(p, &cur, 4);
        }
        else
        {
          int16_t cur;
          std::memcpy(&cur, p, 2);
          cur = static_cast<int16_t>(add ? cur + v : v);
          std::memcpy(p, &cur, 2);
        }
      }
    }
    m_ach.DoFrame(ram, n);
  };

  m_screen = Screen::Game;
  m_paused = false;
  m_pause_sel = 0;
  m_play_t = 0;
  m_speed_warned = false;
  m_mouse_idle = 0;
}

void App::StopGame(bool autosave)
{
  if (!m_emu.IsRunning())
    return;
  if (autosave && !m_hardcore_session && m_cfg.autosave)
    SaveSlot(0);
  m_emu.on_frame = nullptr;
  m_ach.EndSession();
  m_emu.Stop();
  SDL_ShowCursor(SDL_ENABLE);
}

void App::ActivatePause(int index)
{
  if (index != PAUSE_QUIT && index != PAUSE_DISC)
    m_audio.Play(index == PAUSE_RESUME ? Sfx::Cancel : Sfx::Select);
  auto resume = [this]() {
    m_paused = false;
    m_emu.SetPaused(false);
  };
  switch (index)
  {
    case PAUSE_RESUME: resume(); break;
    case PAUSE_SAVE:
    case PAUSE_LOAD:
      if (!StatesBlocked())
        OpenSlots(index == PAUSE_SAVE);
      break;
    case PAUSE_DISC:
      SwapDisc();
      if (m_emu.DiscCount() >= 2)
        resume();
      break;
    case PAUSE_PORT:
      m_emu.SetInputPort(1 - m_emu.InputPort());
      Toast(m_emu.InputPort() == 1 ? L("Mando en el puerto 2", "Controller in port 2") : L("Mando en el puerto 1", "Controller in port 1"));
      resume();
      break;
    case PAUSE_CONTROLS:
      m_screen = Screen::Controls;
      m_hint_reveal = false;
      break;
    case PAUSE_MENU:
      StopGame(true);
      m_screen = Screen::Menu;
      m_sel = MENU_PLAY;
      m_fade = 0;
      break;
    case PAUSE_QUIT: RequestQuit(); break;
  }
}

void App::RequestQuit()
{
  if (m_outro_t >= 0)
    return;
  StopGame(true); // si se sale desde la pausa, guarda y vuelve al fondo del menú
  m_screen = Screen::Menu;
  m_modal = Modal::None;
  m_intro_t = std::max(m_intro_t, kIntroLen);
  m_fade = 1.0f;
  m_outro_t = 0;
  m_audio.Play(Sfx::Close);
}

void App::ToggleFullscreen()
{
  m_cfg.fullscreen = !m_cfg.fullscreen;
  SDL_SetWindowFullscreen(m_window, m_cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  m_cfg.Save();
}

void App::ImportBios(const std::string& path)
{
  Paths::MakeDirs(Paths::BiosDir());
  const std::string name = BaseName(path);
  if (Paths::CopyFile(path, Paths::BiosDir() + "/" + name))
  {
    // La BIOS recién cargada queda elegida.
    m_cfg.bios_file = name;
    m_cfg.Save();
    Toast(L("BIOS añadida: ", "BIOS added: ") + name);
  }
  else
  {
    Toast(L("No se pudo copiar la BIOS", "Could not copy the BIOS"));
  }
}

void App::StartLogin()
{
  if (m_login_user.empty() || m_login_pass.empty())
  {
    Toast(L("Escribe tu usuario y contraseña de RetroAchievements", "Enter your RetroAchievements username and password"));
    return;
  }
  m_audio.Play(Sfx::Select);
  m_ach.StartDownload(m_login_user, m_login_pass);
  // La contraseña sólo se usa para obtener el token: se borra de memoria al instante.
  std::fill(m_login_pass.begin(), m_login_pass.end(), '\0');
  m_login_pass.clear();
}

void App::OpenOptionPage(int page)
{
  m_audio.Play(Sfx::Select);
  if (page == CAT_CHEATS)
  {
    m_screen = Screen::Cheats;
    m_cheat_list = LoadCheats(Paths::AssetsDir(), Paths::ReadDiscSerial(m_cfg.game_path));
    m_cheat_sel = 0;
    m_cheat_scroll = 0;
    return;
  }
  m_opt_page = page;
  m_opt_sel = 0;
}

bool App::CheatEnabled(const std::string& name) const
{
  std::stringstream ss(m_cfg.cheats);
  std::string item;
  while (std::getline(ss, item, '|'))
    if (item == name)
      return true;
  return false;
}

void App::SetCheatEnabled(const std::string& name, bool on)
{
  std::vector<std::string> names;
  std::stringstream ss(m_cfg.cheats);
  std::string item;
  while (std::getline(ss, item, '|'))
    if (!item.empty() && item != name)
      names.push_back(item);
  if (on)
    names.push_back(name);
  m_cfg.cheats.clear();
  for (const std::string& n : names)
    m_cfg.cheats += (m_cfg.cheats.empty() ? "" : "|") + n;
  m_cfg.Save();
}

std::vector<std::string> App::ActiveCheatCodes() const
{
  std::vector<std::string> codes;
  for (const Cheat& c : LoadCheats(Paths::AssetsDir(), Paths::ReadDiscSerial(m_cfg.game_path)))
    if (CheatEnabled(c.name))
      codes.push_back(c.codes);
  return codes;
}

// Recorre OpenBIOS (valor vacío) y las BIOS cargadas en bios/.
void App::CycleBios(int dir)
{
  std::vector<std::string> list = {""};
  for (const std::string& b : Paths::FindBiosFiles())
    list.push_back(b);
  int idx = 0;
  for (size_t i = 0; i < list.size(); i++)
    if (list[i] == m_cfg.bios_file)
      idx = static_cast<int>(i);
  const int n = static_cast<int>(list.size());
  idx = (idx + (dir == 0 ? 1 : dir) + n) % n;
  m_cfg.bios_file = list[idx];
}

void App::OptionChange(int row, int dir)
{
  const bool activate = (dir == 0);
  m_audio.Play(activate ? (row == OPT_BACK ? Sfx::Cancel : Sfx::Select) : Sfx::Move);
  switch (row)
  {
    case OPT_GAME:
      if (!activate)
        return;
      if (!FilePicker::Available())
      {
        ShowDialog(L("No se encontró zenity ni kdialog.\nArrastra la imagen del disco sobre la ventana.", "Neither zenity nor kdialog was found.\nDrag the disc image onto the window."));
        return;
      }
      m_picker.Open(L("Imagen del disco 1", "Disc 1 image"), "*.cue *.CUE *.chd *.CHD *.bin *.BIN *.iso *.img *.pbp",
                    [this](const std::string& p) { SetGamePath(p); });
      return;
    case OPT_DISC2:
      if (!activate)
        return;
      if (!FilePicker::Available())
      {
        ShowDialog(L("No se encontró zenity ni kdialog.\nPon el Disco 2 junto al Disco 1 con el mismo nombre (Disc 2).", "Neither zenity nor kdialog was found.\nPut Disc 2 next to Disc 1 with the same name (Disc 2)."));
        return;
      }
      m_picker.Open(L("Imagen del disco 2", "Disc 2 image"), "*.cue *.CUE *.chd *.CHD *.bin *.BIN *.iso *.img *.pbp",
                    [this](const std::string& p) {
                      m_cfg.disc2_path = p;
                      m_cfg.Save();
                    });
      return;
    case OPT_BIOS: CycleBios(dir); break;
    case OPT_BIOS_LOAD:
      if (!activate)
        return;
      if (!FilePicker::Available())
      {
        ShowDialog(L("No se encontró zenity ni kdialog.\nArrastra el archivo de la BIOS (512 KB) sobre la ventana.", "Neither zenity nor kdialog was found.\nDrag the BIOS file (512 KB) onto the window."));
        return;
      }
      m_picker.Open(L("BIOS de PlayStation", "PlayStation BIOS"), "*.bin *.BIN *.rom *.ROM", [this](const std::string& p) { ImportBios(p); });
      return;
    case OPT_RENDERER: m_cfg.hardware_renderer = !m_cfg.hardware_renderer; break;
    case OPT_SCALE:
    {
      // 3x se omite: en SwanStation corrompe el texto de algunos juegos (comprobado con Tenchu 2).
      static const int scales[] = {1, 2, 4, 5};
      int idx = 0;
      for (int i = 0; i < 4; i++)
        if (scales[i] == m_cfg.resolution_scale)
          idx = i;
      idx = activate ? (idx + 1) % 4 : std::clamp(idx + dir, 0, 3);
      m_cfg.resolution_scale = scales[idx];
      break;
    }
    case OPT_TEXFILTER:
    {
      static const char* filters[] = {"Nearest", "Bilinear", "JINC2", "xBR"};
      int idx = 3;
      for (int i = 0; i < 4; i++)
        if (m_cfg.texture_filter == filters[i])
          idx = i;
      idx = activate ? (idx + 1) % 4 : (idx + dir + 4) % 4;
      m_cfg.texture_filter = filters[idx];
      break;
    }
    case OPT_PGXP:
    {
      // No → Sí → Máxima (con la CPU)
      int level = !m_cfg.pgxp ? 0 : m_cfg.pgxp_cpu ? 2 : 1;
      level = activate ? (level + 1) % 3 : std::clamp(level + dir, 0, 2);
      m_cfg.pgxp = level > 0;
      m_cfg.pgxp_cpu = level == 2;
      break;
    }
    case OPT_WIDESCREEN:
      if (!m_ws_available)
        return;
      m_cfg.widescreen = !m_cfg.widescreen;
      break;
    case OPT_SIDEART: m_cfg.side_mode = (m_cfg.side_mode + (dir < 0 ? 2 : 1)) % 3; break;
    case OPT_SCREENFILTER: m_cfg.screen_filter = (m_cfg.screen_filter + (dir < 0 ? 2 : 1)) % 3; break;
    case OPT_SMOOTH: m_cfg.smooth_filter = !m_cfg.smooth_filter; break;
    case OPT_FULLSCREEN: ToggleFullscreen(); return;
    case OPT_VOLUME:
      m_cfg.volume =
        activate ? (m_cfg.volume >= 100 ? 0 : m_cfg.volume + 10) : std::clamp(m_cfg.volume + dir * 10, 0, 100);
      m_emu.SetVolume(m_cfg.volume);
      break;
    case OPT_MUSIC:
      m_cfg.music_volume = activate ? (m_cfg.music_volume >= 100 ? 0 : m_cfg.music_volume + 10)
                                    : std::clamp(m_cfg.music_volume + dir * 10, 0, 100);
      m_audio.SetVolumes(m_cfg.music_volume, m_cfg.sfx_volume);
      break;
    case OPT_NTSC:
      if (!m_ntsc_available)
        return;
      m_cfg.ntsc_mode = !m_cfg.ntsc_mode;
      break;
    case OPT_SUBTITLES:
      if (!m_subs_available)
        return;
      m_cfg.subtitles = !m_cfg.subtitles;
      break;
    case OPT_AUTOSAVE: m_cfg.autosave = !m_cfg.autosave; break;
    case OPT_SFX:
      m_cfg.sfx_volume = activate ? (m_cfg.sfx_volume >= 100 ? 0 : m_cfg.sfx_volume + 10)
                                  : std::clamp(m_cfg.sfx_volume + dir * 10, 0, 100);
      m_audio.SetVolumes(m_cfg.music_volume, m_cfg.sfx_volume);
      break;
    case OPT_FASTBOOT: m_cfg.fast_boot = !m_cfg.fast_boot; break;
    case OPT_HARDCORE:
      m_cfg.hardcore = !m_cfg.hardcore;
      if (m_cfg.hardcore && !HasSonyBios())
        ShowDialog(L("Modo hardcore activado.\n\nNecesita la BIOS original de tu PlayStation: cárgala con "
                     "«Cargar BIOS nueva…» y elígela en la fila BIOS.",
                     "Hardcore mode enabled.\n\nIt needs your PlayStation's original BIOS: load it with "
                     "“Load new BIOS…” and choose it in the BIOS row."));
      break;
    case OPT_PROMPTS:
    {
      static const char* styles[] = {"auto", "keyboard", "xbox", "playstation"};
      int idx = 0;
      for (int i = 0; i < 4; i++)
        if (m_cfg.prompt_style == styles[i])
          idx = i;
      idx = activate ? (idx + 1) % 4 : (idx + dir + 4) % 4;
      m_cfg.prompt_style = styles[idx];
      break;
    }
    case OPT_CONTROLS:
      if (activate)
        m_screen = Screen::Controls;
      return;
    case OPT_REMAP_KB:
    case OPT_REMAP_PAD:
      if (!activate)
        return;
      m_screen = Screen::Remap;
      m_remap_device = (row == OPT_REMAP_PAD) ? 1 : 0;
      m_remap_sel = 0;
      m_remap_wait = -1;
      return;
    case OPT_RESET_CONTROLS:
      if (!activate)
        return;
      // Doble pulsación para confirmar: evita borrar la configuración por accidente.
      if (m_reset_confirm > 0)
      {
        m_cfg.DefaultKeyboard();
        m_cfg.DefaultPad();
        m_reset_confirm = 0;
        Toast(L("Controles restaurados por defecto", "Controls restored to defaults"));
        break;
      }
      m_reset_confirm = 3.0f;
      return;
    case OPT_BACK:
      if (activate)
      {
        m_opt_sel = m_opt_page;
        m_opt_page = -1;
      }
      return;
  }
  m_cfg.Save();
}

// ------------------------------------------------------------------------------------------------
// Entrada
// ------------------------------------------------------------------------------------------------

PromptStyle App::PadStyle() const
{
  const SDL_GameControllerType t = m_pad ? SDL_GameControllerGetType(m_pad) : SDL_CONTROLLER_TYPE_UNKNOWN;
  return (t == SDL_CONTROLLER_TYPE_PS3 || t == SDL_CONTROLLER_TYPE_PS4 || t == SDL_CONTROLLER_TYPE_PS5)
           ? PromptStyle::PlayStation
           : PromptStyle::Xbox;
}

PromptStyle App::CurrentStyle() const
{
  if (m_cfg.prompt_style == "keyboard") return PromptStyle::Keyboard;
  if (m_cfg.prompt_style == "xbox") return PromptStyle::Xbox;
  if (m_cfg.prompt_style == "playstation") return PromptStyle::PlayStation;
  return m_last_device;
}

void App::Navigate(int dy, int dx)
{
  const int before[] = {m_sel, m_opt_sel, m_pause_sel, m_choice_sel, m_login_focus, m_ach_sel, m_remap_sel};
  NavigateImpl(dy, dx);
  const int after[] = {m_sel, m_opt_sel, m_pause_sel, m_choice_sel, m_login_focus, m_ach_sel, m_remap_sel};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after)))
    m_audio.Play(Sfx::Move);
}

void App::NavigateImpl(int dy, int dx)
{
  if (m_modal == Modal::Dialog)
    return;
  if (m_modal == Modal::PlayChoice)
  {
    if (dy)
      m_choice_sel = (m_choice_sel + dy + 4) % 4;
    return;
  }
  if (m_modal == Modal::Slots)
  {
    if (dy)
    {
      m_slot_sel = (m_slot_sel + dy + kSlots) % kSlots;
      if (m_slots_save && m_slot_sel == 0) // el autoguardado no se elige a mano
        m_slot_sel = (m_slot_sel + dy + kSlots) % kSlots;
    }
    return;
  }
  if (m_modal == Modal::ConfirmReset)
  {
    if (dy || dx)
      m_choice_sel = 1 - m_choice_sel;
    return;
  }
  switch (m_screen)
  {
    case Screen::Menu: m_sel = (m_sel + dy + kMenuItems) % kMenuItems; break;
    case Screen::Credits: m_credits_target += dy * 120.0f; break;
    case Screen::Cheats:
    {
      // Filas: los trucos, L("Desactivar todos", "Disable all") y "Volver".
      const int n = static_cast<int>(m_cheat_list.size()) + 2;
      if (dy)
        m_cheat_sel = (m_cheat_sel + dy + n) % n;
      if (dx && m_cheat_sel < static_cast<int>(m_cheat_list.size()))
        Accept();
      break;
    }
    case Screen::Options:
    {
      const int n = m_opt_page < 0 ? kOptCategoryCount + 1 : static_cast<int>(OptPageRows(m_opt_page).size());
      if (dy)
        m_opt_sel = (m_opt_sel + dy + n) % n;
      if (dx && m_opt_page >= 0)
        OptionChange(OptPageRows(m_opt_page)[m_opt_sel], dx);
      break;
    }
    case Screen::Achievements:
      if (m_ach.GetState() == Achievements::State::NeedLogin)
      {
        if (dy)
          m_login_focus = (m_login_focus + dy + 3) % 3;
      }
      else if (m_ach.GetState() == Achievements::State::Ready && dy)
      {
        // La última fila (índice n) es L("Reiniciar mis logros", "Reset my achievements").
        const int n = static_cast<int>(m_ach.List().size());
        m_ach_sel = std::clamp(m_ach_sel + dy, 0, n);
      }
      break;
    case Screen::Remap:
    {
      // Dos columnas de 8 botones y, debajo, "Restaurar" (16) y "Volver" (17).
      int& i = m_remap_sel;
      if (dy)
      {
        if (i < kPsButtons)
        {
          const int col = i / 8, row = i % 8 + dy;
          i = row < 0 ? kPsButtons + 1 : row > 7 ? kPsButtons : col * 8 + row;
        }
        else if (i == kPsButtons)
          i = dy > 0 ? kPsButtons + 1 : 7;
        else
          i = dy > 0 ? 0 : kPsButtons;
      }
      if (dx && i < kPsButtons)
        i = ((i / 8 + 1) % 2) * 8 + i % 8;
      break;
    }
    case Screen::Game:
      if (m_paused)
        m_pause_sel = (m_pause_sel + dy + kPauseItems) % kPauseItems;
      break;
    default: break;
  }
}

void App::Accept()
{
  if (m_modal == Modal::Dialog)
  {
    m_audio.Play(Sfx::Select);
    m_modal = Modal::None;
    return;
  }
  if (m_modal == Modal::ConfirmReset)
  {
    m_modal = Modal::None;
    if (m_choice_sel == 0)
    {
      m_ach.ResetUnlocks();
      m_audio.Play(Sfx::Close);
      Toast(L("Logros reiniciados", "Achievements reset"));
    }
    else
    {
      m_audio.Play(Sfx::Cancel);
    }
    return;
  }
  if (m_modal == Modal::PlayChoice)
  {
    if (m_choice_sel == 0)
      StartGame(LatestSlot());
    else if (m_choice_sel == 1)
      OpenSlots(false);
    else if (m_choice_sel == 2)
      StartGame(-1);
    else
    {
      m_audio.Play(Sfx::Cancel);
      m_modal = Modal::None;
    }
    return;
  }
  if (m_modal == Modal::Slots)
  {
    ActivateSlot(m_slot_sel);
    return;
  }
  switch (m_screen)
  {
    case Screen::Menu: ActivateMenu(m_sel); break;
    case Screen::Options:
      if (m_opt_page < 0)
      {
        if (m_opt_sel < kOptCategoryCount)
          OpenOptionPage(m_opt_sel);
        else
          Back();
      }
      else
      {
        OptionChange(OptPageRows(m_opt_page)[m_opt_sel], 0);
      }
      break;
    case Screen::Achievements:
      if (m_ach.GetState() == Achievements::State::NeedLogin)
      {
        if (m_login_focus < 2)
          m_login_focus++;
        else
          StartLogin();
      }
      else if (m_ach.GetState() == Achievements::State::Ready &&
               m_ach_sel == static_cast<int>(m_ach.List().size()))
      {
        m_audio.Play(Sfx::Select);
        m_modal = Modal::ConfirmReset;
        m_choice_sel = 1; // por seguridad, empieza en "Cancelar"
      }
      break;
    case Screen::Cheats:
    {
      const int n = static_cast<int>(m_cheat_list.size());
      if (m_cheat_sel < n)
      {
        const Cheat& c = m_cheat_list[m_cheat_sel];
        const bool on = !CheatEnabled(c.name);
        SetCheatEnabled(c.name, on);
        m_audio.Play(on ? Sfx::Select : Sfx::Cancel);
      }
      else if (m_cheat_sel == n)
      {
        m_cfg.cheats.clear();
        m_cfg.Save();
        m_audio.Play(Sfx::Cancel);
        Toast(L("Trucos desactivados", "Cheats disabled"));
      }
      else
      {
        Back();
      }
      break;
    }
    case Screen::Remap:
      if (m_remap_sel < kPsButtons)
      {
        m_audio.Play(Sfx::Select);
        m_remap_wait = kRemapOrder[m_remap_sel];
        m_remap_timer = 6.0f;
      }
      else if (m_remap_sel == kPsButtons)
      {
        m_audio.Play(Sfx::Select);
        if (m_remap_device == 0)
          m_cfg.DefaultKeyboard();
        else
          m_cfg.DefaultPad();
        m_cfg.Save();
        Toast(m_remap_device == 0 ? L("Teclado restaurado por defecto", "Keyboard restored to defaults") : L("Mando restaurado por defecto", "Controller restored to defaults"));
      }
      else
      {
        Back();
      }
      break;
    case Screen::Game:
      if (m_paused)
        ActivatePause(m_pause_sel);
      break;
    case Screen::Controls:
      m_hint_reveal = !m_hint_reveal;
      m_audio.Play(m_hint_reveal ? Sfx::Select : Sfx::Cancel);
      break;
    default: break;
  }
}

// Asigna `value` (tecla o botón del mando) al botón `retro_id`. Si ya estaba en uso por otro botón
// del mismo dispositivo, se intercambian: nunca quedan dos acciones en la misma tecla.
void App::AssignBinding(int retro_id, int value)
{
  int* table = m_remap_device == 0 ? m_cfg.key_bind : m_cfg.pad_bind;
  const int old = table[retro_id];
  for (int i = 0; i < kPsButtons; i++)
    if (i != retro_id && table[i] == value)
    {
      table[i] = old;
      Toast(LangEn() ? std::string(PsButtonName(i)) + " takes the previous binding of " + PsButtonName(retro_id)
                     : std::string(PsButtonName(i)) + " pasa a la asignación anterior de " + PsButtonName(retro_id));
    }
  table[retro_id] = value;
  m_cfg.Save();
  m_remap_wait = -1;
  m_audio.Play(Sfx::Select);
}

void App::Back()
{
  // Abrir la pausa suena como "elegir"; todo lo demás, como "cancelar".
  const bool opening_pause = m_screen == Screen::Game && !m_paused && m_modal == Modal::None;
  m_audio.Play(opening_pause ? Sfx::Select : Sfx::Cancel);
  if (m_lang_open)
  {
    m_lang_open = false;
    return;
  }
  if (m_modal != Modal::None)
  {
    m_modal = Modal::None;
    return;
  }
  switch (m_screen)
  {
    case Screen::Options:
      if (m_opt_page >= 0)
      {
        m_opt_sel = m_opt_page;
        m_opt_page = -1;
      }
      else
      {
        m_screen = Screen::Menu;
        m_sel = MENU_OPTIONS;
      }
      break;
    case Screen::Achievements:
      m_screen = Screen::Menu;
      m_sel = MENU_ACHIEVEMENTS;
      break;
    case Screen::Credits:
      m_screen = Screen::Menu;
      m_sel = MENU_CREDITS;
      break;
    case Screen::Cheats:
      m_screen = Screen::Options;
      m_opt_page = -1;
      m_opt_sel = CAT_CHEATS;
      break;
    case Screen::Remap:
      if (m_remap_wait >= 0)
      {
        m_remap_wait = -1;
        break;
      }
      m_screen = Screen::Options;
      m_opt_page = CAT_CONTROLS;
      m_opt_sel = OptRowIndex(CAT_CONTROLS, m_remap_device == 0 ? OPT_REMAP_KB : OPT_REMAP_PAD);
      break;
    case Screen::Controls:
      if (m_emu.IsRunning())
        m_screen = Screen::Game;
      else
      {
        m_screen = Screen::Options;
        m_opt_page = CAT_CONTROLS;
        m_opt_sel = OptRowIndex(CAT_CONTROLS, OPT_CONTROLS);
      }
      break;
    case Screen::Game:
      m_paused = !m_paused;
      m_pause_sel = 0;
      m_emu.SetPaused(m_paused);
      break;
    default: break;
  }
}

EmuInput App::GatherGameInput()
{
  EmuInput in;
  in.buttons = m_test_buttons;
  // Teclado y mando tienen asignaciones independientes (OPCIONES › Controles).
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  for (int id = 0; id < kPsButtons; id++)
  {
    bool on = false;
    const int key = m_cfg.KeyBind(id);
    if (key > 0 && key < SDL_NUM_SCANCODES && k[key])
      on = true;
    const int pad = m_cfg.pad_bind[id];
    if (m_pad && pad >= kPadAxisBase)
      on |= SDL_GameControllerGetAxis(m_pad, static_cast<SDL_GameControllerAxis>(pad - kPadAxisBase)) > 12000;
    else if (m_pad && pad >= 0)
      on |= SDL_GameControllerGetButton(m_pad, static_cast<SDL_GameControllerButton>(pad)) != 0;
    if (on)
      in.buttons |= static_cast<uint16_t>(1u << id);
  }
  if (m_pad)
  {
    auto ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(m_pad, a); };
    in.analog[0][0] = ax(SDL_CONTROLLER_AXIS_LEFTX);
    in.analog[0][1] = ax(SDL_CONTROLLER_AXIS_LEFTY);
    in.analog[1][0] = ax(SDL_CONTROLLER_AXIS_RIGHTX);
    in.analog[1][1] = ax(SDL_CONTROLLER_AXIS_RIGHTY);
  }
  if (m_test_lstick[0] != 0 || m_test_lstick[1] != 0)
  {
    in.analog[0][0] = static_cast<int16_t>(m_test_lstick[0] * 32767);
    in.analog[0][1] = static_cast<int16_t>(m_test_lstick[1] * 32767);
  }
  if (m_test_rstick[0] != 0 || m_test_rstick[1] != 0)
  {
    in.analog[1][0] = static_cast<int16_t>(m_test_rstick[0] * 32767);
    in.analog[1][1] = static_cast<int16_t>(m_test_rstick[1] * 32767);
  }
  return in;
}

void App::HandleEvent(const SDL_Event& e)
{
  // Durante el cierre se ignora todo (cerrar la ventana otra vez fuerza la salida inmediata).
  if (m_outro_t >= 0)
  {
    if (e.type == SDL_QUIT)
      m_running = false;
    return;
  }
  // La apertura se salta con cualquier tecla, botón o clic.
  if (IntroActive() &&
      (e.type == SDL_KEYDOWN || e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_MOUSEBUTTONDOWN))
  {
    m_intro_t = kIntroLen;
    m_fade = 1.0f;
    return;
  }

  // Esperando una tecla/botón nuevo en la pantalla de personalización.
  if (m_remap_wait >= 0)
  {
    if (e.type == SDL_KEYDOWN && !e.key.repeat)
    {
      const SDL_Scancode sc = e.key.keysym.scancode;
      if (sc == SDL_SCANCODE_ESCAPE)
      {
        m_audio.Play(Sfx::Cancel);
        m_remap_wait = -1;
      }
      else if (m_remap_device == 0)
      {
        if (IsReservedKey(sc))
          Toast(L("Esa tecla está reservada por el launcher", "That key is reserved by the launcher"));
        else
          AssignBinding(m_remap_wait, sc);
      }
      return;
    }
    if (m_remap_device == 1 && e.type == SDL_CONTROLLERBUTTONDOWN)
    {
      if (e.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE)
        Toast(L("El botón central está reservado para la pausa", "The centre button is reserved for pause"));
      else
        AssignBinding(m_remap_wait, e.cbutton.button);
      return;
    }
    if (m_remap_device == 1 && e.type == SDL_CONTROLLERAXISMOTION && e.caxis.value > 20000 &&
        (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
    {
      AssignBinding(m_remap_wait, kPadAxisBase + e.caxis.axis);
      return;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN)
    {
      m_audio.Play(Sfx::Cancel);
      m_remap_wait = -1;
      return;
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERAXISMOTION || e.type == SDL_KEYDOWN ||
        e.type == SDL_TEXTINPUT)
      return;
  }

  switch (e.type)
  {
    case SDL_QUIT: RequestQuit(); break;

    case SDL_DROPFILE:
    {
      const std::string path = e.drop.file;
      SDL_free(e.drop.file);
      FILE* f = Platform::OpenFile(path.c_str(), "rb");
      long size = 0;
      if (f)
      {
        std::fseek(f, 0, SEEK_END);
        size = std::ftell(f);
        std::fclose(f);
      }
      if (size == 512 * 1024)
        ImportBios(path);
      else if (IsDiscImage(path))
      {
        SetGamePath(path);
        Toast(L("Juego: ", "Game: ") + BaseName(path));
      }
      else
        Toast(L("Archivo no reconocido", "File not recognised"));
      break;
    }

    case SDL_CONTROLLERDEVICEADDED: OpenController(e.cdevice.which); break;
    case SDL_CONTROLLERDEVICEREMOVED:
      if (m_pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(m_pad)) == e.cdevice.which)
      {
        SDL_GameControllerClose(m_pad);
        m_pad = nullptr;
        m_emu.SetRumbleTarget(nullptr);
        m_last_device = PromptStyle::Keyboard;
        if (m_screen == Screen::Game && !m_paused)
          Back(); // pausa automática al desconectar el mando
      }
      break;

    case SDL_TEXTINPUT:
      if (TextFocus())
      {
        std::string& field = m_login_focus == 0 ? m_login_user : m_login_pass;
        if (field.size() < 64)
          field += e.text.text;
      }
      break;

    case SDL_KEYDOWN:
    {
      m_last_device = PromptStyle::Keyboard;
      const SDL_Keycode key = e.key.keysym.sym;
      if (key == SDLK_F11 || (key == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)))
      {
        ToggleFullscreen();
        break;
      }
      if (key == SDLK_F9 && !e.key.repeat)
      {
        std::string err;
        Toast(m_r.ReloadShaders(&err) ? L("Shaders recargados", "Shaders reloaded") : L("Error en shaders", "Shader error"));
        if (!err.empty())
          SDL_Log("%s", err.c_str());
        break;
      }
      // Edición de texto en el formulario de logros.
      if (TextFocus())
      {
        std::string& field = m_login_focus == 0 ? m_login_user : m_login_pass;
        if (key == SDLK_BACKSPACE)
        {
          PopUtf8(field);
          break;
        }
        if (key == SDLK_v && (e.key.keysym.mod & KMOD_CTRL))
        {
          if (char* clip = SDL_GetClipboardText())
          {
            field += clip;
            SDL_free(clip);
          }
          break;
        }
        if (key == SDLK_TAB)
        {
          m_login_focus = (m_login_focus + 1) % 3;
          break;
        }
        if (key != SDLK_RETURN && key != SDLK_KP_ENTER && key != SDLK_ESCAPE && key != SDLK_UP && key != SDLK_DOWN)
          break; // el resto de teclas escriben texto (SDL_TEXTINPUT)
      }
      if (m_screen == Screen::Game && !m_paused && m_modal == Modal::None)
      {
        if (key == SDLK_ESCAPE || key == SDLK_F1)
          Back();
        else if (key == SDLK_F12)
        {
          // Diagnóstico: volcado de la RAM de la consola (para revisar logros que no saltan).
          size_t n = 0;
          if (const uint8_t* ram = m_emu.SystemRam(&n))
          {
            const std::string dir = Paths::DataDir() + "/diagnostico";
            Paths::MakeDirs(dir);
            char name[64];
            const time_t t = std::time(nullptr);
            std::strftime(name, sizeof(name), "/ram-%Y%m%d-%H%M%S.bin", std::localtime(&t));
            if (FILE* f = Platform::OpenFile((dir + name).c_str(), "wb"))
            {
              std::fwrite(ram, 1, n, f);
              std::fclose(f);
              Toast(L("RAM guardada para diagnóstico", "RAM saved for diagnostics"));
            }
          }
        }
        else if (key == SDLK_F8 && !e.key.repeat)
          SwapDisc();
        else if ((key == SDLK_F5 || key == SDLK_F7) && StatesBlocked())
        {
        }
        else if (key == SDLK_F5)
          Toast(SaveSlot(m_slot_last) ? SlotName(m_slot_last) + L(": partida guardada", ": game saved")
                                      : std::string(L("No se pudo guardar", "Could not save")));
        else if (key == SDLK_F7)
          Toast(LoadSlot(m_slot_last) ? SlotName(m_slot_last) + L(": partida cargada", ": game loaded")
                                      : SlotName(m_slot_last) + L(": vacía", ": empty"));
        break;
      }
      switch (key)
      {
        case SDLK_UP: Navigate(-1, 0); break;
        case SDLK_DOWN: Navigate(1, 0); break;
        case SDLK_LEFT: Navigate(0, -1); break;
        case SDLK_RIGHT: Navigate(0, 1); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE: Accept(); break;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
          if (m_screen == Screen::Menu && m_modal == Modal::None)
            break;
          Back();
          break;
        default: break;
      }
      break;
    }

    case SDL_CONTROLLERAXISMOTION:
      if (std::abs(e.caxis.value) > 16000)
        m_last_device = PadStyle();
      break;

    case SDL_CONTROLLERBUTTONDOWN:
    {
      m_last_device = PadStyle();
      const auto btn = e.cbutton.button;
      // L3 + R3 a la vez (o el botón central) abre y cierra la pausa durante el juego.
      const bool sticks_combo =
        m_pad &&
        ((btn == SDL_CONTROLLER_BUTTON_LEFTSTICK && SDL_GameControllerGetButton(m_pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) ||
         (btn == SDL_CONTROLLER_BUTTON_RIGHTSTICK && SDL_GameControllerGetButton(m_pad, SDL_CONTROLLER_BUTTON_LEFTSTICK)));
      if (m_screen == Screen::Game && m_modal == Modal::None && (sticks_combo || btn == SDL_CONTROLLER_BUTTON_GUIDE))
      {
        Back();
        break;
      }
      if (m_screen == Screen::Game && !m_paused && m_modal == Modal::None)
        break;
      switch (btn)
      {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: Navigate(-1, 0); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: Navigate(1, 0); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: Navigate(0, -1); break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: Navigate(0, 1); break;
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_START: Accept(); break;
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_GUIDE:
          if (m_screen == Screen::Menu && m_modal == Modal::None)
            break;
          Back();
          break;
        default: break;
      }
      break;
    }

    case SDL_MOUSEWHEEL:
      if (m_screen == Screen::Achievements)
      {
        const int n = static_cast<int>(m_ach.List().size());
        m_ach_sel = std::clamp(m_ach_sel - e.wheel.y, 0, n);
      }
      else if (m_screen == Screen::Credits)
        m_credits_target -= e.wheel.y * 90.0f;
      else if (m_screen == Screen::Cheats)
        Navigate(e.wheel.y > 0 ? -1 : 1, 0);
      else if (m_screen == Screen::Options)
        Navigate(e.wheel.y > 0 ? -1 : 1, 0);
      break;

    case SDL_MOUSEMOTION:
    {
      int dw, dh, ww, wh;
      SDL_GL_GetDrawableSize(m_window, &dw, &dh);
      SDL_GetWindowSize(m_window, &ww, &wh);
      m_mouse_px = e.motion.x * static_cast<float>(dw) / ww;
      m_mouse_py = e.motion.y * static_cast<float>(dh) / wh;
      m_mouse_idle = 0;
      if (m_screen == Screen::Game)
        SDL_ShowCursor(SDL_ENABLE);
      const int before[] = {m_sel, m_opt_sel, m_pause_sel, m_choice_sel};
      for (const HotZone& z : m_hot)
      {
        if (!z.r.Contains(m_mouse_px, m_mouse_py))
          continue;
        switch (z.group)
        {
          case 0: m_sel = z.index; break;
          case 1: m_opt_sel = z.index; break;
          case 2: m_pause_sel = z.index; break;
          case 3: m_choice_sel = z.index; break;
          case 9: m_slot_sel = z.index; break;
          case 7: m_remap_sel = z.index; break;
          case 8: m_cheat_sel = z.index; break;
          default: break;
        }
      }
      const int after[] = {m_sel, m_opt_sel, m_pause_sel, m_choice_sel};
      if (!std::equal(std::begin(before), std::end(before), std::begin(after)))
        m_audio.Play(Sfx::Move);
      break;
    }

    case SDL_MOUSEBUTTONDOWN:
    {
      if (e.button.button == SDL_BUTTON_RIGHT)
      {
        if (!(m_screen == Screen::Menu && m_modal == Modal::None))
          Back();
        break;
      }
      if (e.button.button != SDL_BUTTON_LEFT)
        break;
      if (m_modal == Modal::Dialog)
      {
        m_audio.Play(Sfx::Select);
        m_modal = Modal::None;
        break;
      }
      // Con el menú de idioma abierto, un clic fuera de él sólo lo cierra.
      if (m_lang_open && !m_lang_menu.Contains(m_mouse_px, m_mouse_py) && !m_tb_lang.Contains(m_mouse_px, m_mouse_py))
      {
        m_lang_open = false;
        break;
      }
      // En orden inverso: lo último dibujado está encima.
      const std::vector<HotZone> zones = m_hot;
      for (auto it = zones.rbegin(); it != zones.rend(); ++it)
        if (it->r.Contains(m_mouse_px, m_mouse_py) && it->click)
        {
          it->click(m_mouse_px, m_mouse_py);
          break;
        }
      break;
    }

    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && m_screen == Screen::Game && !m_paused)
        Back(); // pausa al perder el foco
      break;

    default: break;
  }
}

// ------------------------------------------------------------------------------------------------
// Bucle principal
// ------------------------------------------------------------------------------------------------

void App::Update(float dt)
{
  m_time += dt;
  m_fade = std::min(1.0f, m_fade + dt * 1.2f);
  m_intro_t += dt;
  if (!m_intro_sound && m_intro_t > 0.12f)
  {
    m_intro_sound = true;
    if (IntroActive())
      m_audio.Play(Sfx::Start);
  }
  if (m_outro_t >= 0)
  {
    m_outro_t += dt;
    if (m_outro_t >= kOutroLen)
      m_running = false;
  }
  m_toast_time = std::max(0.0f, m_toast_time - dt);
  m_reset_confirm = std::max(0.0f, m_reset_confirm - dt);
  if (m_remap_wait >= 0)
  {
    m_remap_timer -= dt;
    if (m_remap_timer <= 0 || m_screen != Screen::Remap)
    {
      m_remap_wait = -1;
      Toast(L("Sin cambios", "No changes"));
    }
  }
  m_picker.Poll();
  m_ach.Poll();
  // Música: plena en los menús, baja en la pausa y en silencio mientras se juega.
  if (m_outro_t >= 0)
    m_audio.SetMusicLevel(0.0f);
  else if (m_emu.IsRunning())
    m_audio.SetMusicLevel(m_paused || m_screen == Screen::Controls ? 0.45f : 0.0f);
  else
    m_audio.SetMusicLevel(1.0f);
  if (m_cfg.game_path != m_ach_game_path)
  {
    m_ach_game_path = m_cfg.game_path;
    m_ach.SetGame(m_cfg.game_path);
  }
  if (m_cfg.game_path != m_ws_serial_path)
    UpdatePatchAvailability();

  // Entrada de texto sólo mientras se edita un campo.
  if (TextFocus() && !SDL_IsTextInputActive())
    SDL_StartTextInput();
  else if (!TextFocus() && SDL_IsTextInputActive())
    SDL_StopTextInput();

  // Navegación con el stick analógico en menús.
  if (m_pad && (m_screen != Screen::Game || m_paused))
  {
    const Sint16 y = SDL_GameControllerGetAxis(m_pad, SDL_CONTROLLER_AXIS_LEFTY);
    const Sint16 x = SDL_GameControllerGetAxis(m_pad, SDL_CONTROLLER_AXIS_LEFTX);
    m_axis_repeat -= dt;
    if (std::abs(y) > 20000 || std::abs(x) > 20000)
    {
      if (m_axis_repeat <= 0)
      {
        if (std::abs(y) > std::abs(x))
          Navigate(y > 0 ? 1 : -1, 0);
        else
          Navigate(0, x > 0 ? 1 : -1);
        m_axis_repeat = 0.22f;
      }
    }
    else
    {
      m_axis_repeat = 0;
    }
  }

  int active = -1;
  if (m_modal == Modal::PlayChoice || m_modal == Modal::ConfirmReset)
    active = m_choice_sel;
  else if (m_screen == Screen::Menu)
    active = m_sel;
  else if (m_screen == Screen::Game)
    active = m_pause_sel;
  else if (m_screen == Screen::Achievements)
    active = m_login_focus;
  else if (m_screen == Screen::Options && m_opt_page < 0)
    active = m_opt_sel;
  for (int i = 0; i < 8; i++)
  {
    const float target = (i == active) ? 1.0f : 0.0f;
    m_sel_anim[i] += (target - m_sel_anim[i]) * std::min(1.0f, dt * 14.0f);
  }

  // Desplazamiento suave de la lista de logros siguiendo la selección.
  if (m_screen == Screen::Achievements && m_ach.GetState() == Achievements::State::Ready)
  {
    const float row_h = 88.0f, view_h = 700.0f;
    const float sel_y = m_ach_sel * row_h;
    if (sel_y < m_ach_scroll_target)
      m_ach_scroll_target = sel_y;
    if (sel_y + row_h > m_ach_scroll_target + view_h)
      m_ach_scroll_target = sel_y + row_h - view_h;
    const float max_scroll = std::max(0.0f, (m_ach.List().size() + 1) * row_h - view_h);
    m_ach_scroll_target = std::clamp(m_ach_scroll_target, 0.0f, max_scroll);
    m_ach_scroll += (m_ach_scroll_target - m_ach_scroll) * std::min(1.0f, dt * 12.0f);
  }

  // Trucos: la fila elegida queda siempre a la vista.
  if (m_screen == Screen::Cheats)
  {
    const float rh = 56.0f, view_h = 560.0f;
    const int n = static_cast<int>(m_cheat_list.size());
    if (m_cheat_sel < n)
    {
      const float target = std::clamp(m_cheat_sel * rh - view_h / 2 + rh, 0.0f, std::max(0.0f, n * rh - view_h));
      m_cheat_scroll += (target - m_cheat_scroll) * std::min(1.0f, dt * 12.0f);
    }
  }

  // Créditos: avanzan solos despacio; las flechas o la rueda los mueven a mano.
  if (m_screen == Screen::Credits)
  {
    m_credits_target += dt * 32.0f;
    if (m_credits_height > 0 && m_credits_target > m_credits_height)
      m_credits_target = -60; // al terminar, vuelve a empezar
    m_credits_target = std::max(m_credits_target, -60.0f);
    m_credits_scroll += (m_credits_target - m_credits_scroll) * std::min(1.0f, dt * 8.0f);
  }

  if (m_load_t >= 0)
  {
    m_load_t += dt;
    if (m_load_t > 2.5f || m_screen != Screen::Game)
      m_load_t = -1;
  }
  // Si la emulación no llega a la velocidad real, se avisa una vez por partida.
  if (m_emu.IsRunning() && m_screen == Screen::Game && !m_paused && m_modal == Modal::None)
  {
    m_speed_t += dt;
    m_play_t += dt;
    if (m_speed_t >= 5.0f)
    {
      const double ran = static_cast<double>(m_emu.FramesRun() - m_speed_frames);
      // Desde los 25 s: al arrancar, el parche de 60 Hz cambia la velocidad objetivo hacia el segundo 20.
      if (m_play_t > 25.0f && !m_speed_warned && ran < m_emu.TargetFps() * m_speed_t * 0.9)
      {
        m_speed_warned = true;
        SDL_Log("Emulación lenta: %.0f de %.0f frames en %.1f s", ran, m_emu.TargetFps() * m_speed_t, m_speed_t);
        Toast(L("El juego va lento: baja la resolución interna en OPCIONES › GRÁFICOS",
                "The game is running slow: lower the internal resolution in OPTIONS › GRAPHICS"));
      }
      m_speed_t = 0;
      m_speed_frames = m_emu.FramesRun();
    }
  }
  else
  {
    m_speed_t = 0;
    m_speed_frames = m_emu.FramesRun();
  }
  if (m_screen == Screen::Game)
  {
    m_mouse_idle += dt;
    if (m_mouse_idle > 2.0f && !m_paused)
      SDL_ShowCursor(SDL_DISABLE);
    m_emu.Tick(GatherGameInput());
    for (uint32_t id : m_ach.TakeNewUnlocks())
    {
      m_popups.push_back({id, 6.0f});
      m_audio.Play(Sfx::Achievement);
    }
  }
  for (Popup& p : m_popups)
    p.time -= dt;
  m_popups.erase(std::remove_if(m_popups.begin(), m_popups.end(), [](const Popup& p) { return p.time <= 0; }),
                 m_popups.end());
}

void App::Layout()
{
  SDL_GL_GetDrawableSize(m_window, &m_w, &m_h);
  // La interfaz usa la altura de la ventana y se ancla a la izquierda.
  m_s = m_h / kImgH;
  m_ox = 0;
  m_oy = 0;
  // El fondo ocupa toda la altura y se ancla a la derecha, donde están el logotipo y los botones: en
  // ventanas más estrechas que 16:9 se recorta por la izquierda y, en las más anchas, el shader
  // rellena la izquierda con la ilustración reflejada y oscurecida.
  const float sb = m_h / kArtH;
  m_bg_rect.w = kArtW * sb;
  m_bg_rect.h = kArtH * sb;
  m_bg_rect.x = m_w - m_bg_rect.w;
  m_bg_rect.y = 0;
}

void App::Render()
{
  Layout();
  m_hot.clear();
  m_r.BeginFrame(m_w, m_h, m_time);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  if (m_emu.IsRunning() && (m_screen == Screen::Game || m_screen == Screen::Controls))
  {
    m_tb_lang = m_lang_menu = {};
    m_lang_open = false;
    RenderGame();
    if (m_load_t >= 0)
      RenderLoading(m_load_t, 1.0f - Smooth(1.9f, 2.5f, m_load_t));
    if (m_screen == Screen::Controls)
      RenderControls();
    else if (m_paused)
      RenderPause();
    if (m_modal == Modal::Slots)
      RenderSlots();
    RenderPopups();
  }
  else
  {
    m_r.DrawBackground(m_bg, m_bg_rect.x, m_bg_rect.y, m_bg_rect.w, m_bg_rect.h, 0.3f + 0.7f * m_fade);
    switch (m_screen)
    {
      case Screen::Menu: RenderMenu(); break;
      case Screen::Options: RenderOptions(); break;
      case Screen::Controls: RenderControls(); break;
      case Screen::Remap: RenderRemap(); break;
      case Screen::Credits: RenderCredits(); break;
      case Screen::Cheats: RenderCheats(); break;
      case Screen::Achievements: RenderAchievements(); break;
      default: break;
    }
    if (m_modal == Modal::PlayChoice)
      RenderPlayChoice();
    if (m_modal == Modal::ConfirmReset)
      RenderConfirmReset();
    if (m_modal == Modal::Slots)
      RenderSlots();
    RenderTitleBar();
    RenderLanguage();
    RenderTransition();
  }
  if (m_modal == Modal::Dialog)
    RenderDialog();
  RenderToast();
}

// ------------------------------------------------------------------------------------------------
// Elementos comunes
// ------------------------------------------------------------------------------------------------

void App::Panel(float x, float y, float w, float h)
{
  m_r.Rect(IX(x), IY(y), IS(w), IS(h), UiMode::Panel, kPanelBg, WithAlpha(kSilver, 0.9f));
}

void App::DimScreen(float a)
{
  m_r.Rect(0, 0, static_cast<float>(m_w), static_cast<float>(m_h), UiMode::Solid, {0, 0, 0, a});
}

std::vector<std::string> App::Wrap(const Font& f, const std::string& text, float size, float max_w)
{
  std::vector<std::string> lines;
  for (const std::string& para : SplitLines(text))
  {
    std::stringstream ss(para);
    std::string word, line;
    while (ss >> word)
    {
      const std::string test = line.empty() ? word : line + " " + word;
      if (!line.empty() && m_r.TextWidth(f, test, size) > max_w)
      {
        lines.push_back(line);
        line = word;
      }
      else
      {
        line = test;
      }
    }
    lines.push_back(line);
  }
  return lines;
}

std::string App::Truncate(const Font& f, const std::string& text, float size, float max_w)
{
  if (m_r.TextWidth(f, text, size) <= max_w)
    return text;
  std::string t = text;
  while (!t.empty() && m_r.TextWidth(f, t + "…", size) > max_w)
    PopUtf8(t);
  while (!t.empty() && t.back() == ' ')
    t.pop_back();
  return t + "…";
}

void App::FitText(const Font& f, const std::string& text, float x, float y, float max_w, float size, Color c,
                  Align align, bool scroll, int key)
{
  const float w = m_r.TextWidth(f, text, size);
  if (w <= max_w)
  {
    const float tx = align == Align::Left ? x : align == Align::Right ? x + max_w - w : x + (max_w - w) / 2;
    m_r.Text(f, text, tx, y, size, c);
    return;
  }
  if (!scroll)
  {
    m_r.Text(f, Truncate(f, text, size, max_w), x, y, size, c);
    return;
  }
  // Marquesina: pausa al inicio, avanza hasta mostrar el final, pausa y vuelve a empezar.
  if (key != m_marquee_key)
  {
    m_marquee_key = key;
    m_marquee_start = m_time;
  }
  const float overflow = w - max_w;
  const float speed = IS(70);
  const float pause = 1.3f;
  const float cycle = pause + overflow / speed + pause;
  const float t = std::fmod(m_time - m_marquee_start, cycle);
  const float off = std::clamp((t - pause) * speed, 0.0f, overflow);
  m_r.SetClip(x, y - size * 0.25f, max_w, size * 1.7f);
  m_r.Text(f, text, x - off, y, size, c);
  m_r.ClearClip();
}

// Dibuja el icono de un botón según el estilo (teclado, Xbox o PlayStation) y devuelve su ancho.
float App::DrawGlyph(PsButton b, float x, float y, float h, PromptStyle style, bool draw)
{
  struct G
  {
    int kind; // ver padGlyph() en ui.frag
    Color color;
    std::string text;
  };
  const Color psBlue = {0.49f, 0.66f, 0.9f, 1}, psRed = {0.93f, 0.42f, 0.42f, 1};
  const Color psPink = {0.86f, 0.55f, 0.84f, 1}, psGreen = {0.32f, 0.78f, 0.66f, 1};
  const Color xA = {0.38f, 0.72f, 0.22f, 1}, xB = {0.85f, 0.2f, 0.18f, 1};
  const Color xX = {0.15f, 0.45f, 0.86f, 1}, xY = {0.95f, 0.72f, 0.1f, 1};
  const Color key = {0.85f, 0.83f, 0.8f, 1};

  // Botón de PS1 -> índice de asignación (los de juego); -1 para acciones fijas de la interfaz.
  auto retro_id = [](PsButton pb) -> int {
    switch (pb)
    {
      case PsButton::Cross: return RETRO_DEVICE_ID_JOYPAD_B;
      case PsButton::Circle: return RETRO_DEVICE_ID_JOYPAD_A;
      case PsButton::Square: return RETRO_DEVICE_ID_JOYPAD_Y;
      case PsButton::Triangle: return RETRO_DEVICE_ID_JOYPAD_X;
      case PsButton::L1: return RETRO_DEVICE_ID_JOYPAD_L;
      case PsButton::R1: return RETRO_DEVICE_ID_JOYPAD_R;
      case PsButton::L2: return RETRO_DEVICE_ID_JOYPAD_L2;
      case PsButton::R2: return RETRO_DEVICE_ID_JOYPAD_R2;
      case PsButton::L3: return RETRO_DEVICE_ID_JOYPAD_L3;
      case PsButton::R3: return RETRO_DEVICE_ID_JOYPAD_R3;
      case PsButton::Start: return RETRO_DEVICE_ID_JOYPAD_START;
      case PsButton::Select: return RETRO_DEVICE_ID_JOYPAD_SELECT;
      case PsButton::DUp: return RETRO_DEVICE_ID_JOYPAD_UP;
      case PsButton::DDown: return RETRO_DEVICE_ID_JOYPAD_DOWN;
      case PsButton::DLeft: return RETRO_DEVICE_ID_JOYPAD_LEFT;
      case PsButton::DRight: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
      default: return -1;
    }
  };
  // Icono de un botón físico del mando según el estilo (Xbox o PlayStation).
  auto pad_glyph = [&](int v) -> G {
    const bool ps = (style == PromptStyle::PlayStation);
    if (v >= kPadAxisBase)
    {
      const bool left = (v - kPadAxisBase) == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
      return {6, key, ps ? (left ? "L2" : "R2") : (left ? "LT" : "RT")};
    }
    switch (v)
    {
      case SDL_CONTROLLER_BUTTON_A: return ps ? G{2, psBlue, ""} : G{1, xA, "A"};
      case SDL_CONTROLLER_BUTTON_B: return ps ? G{3, psRed, ""} : G{1, xB, "B"};
      case SDL_CONTROLLER_BUTTON_X: return ps ? G{4, psPink, ""} : G{1, xX, "X"};
      case SDL_CONTROLLER_BUTTON_Y: return ps ? G{5, psGreen, ""} : G{1, xY, "Y"};
      case SDL_CONTROLLER_BUTTON_BACK: return {6, key, ps ? "Select" : L("Vista", "View")};
      case SDL_CONTROLLER_BUTTON_GUIDE: return {6, key, ps ? "PS" : "Xbox"};
      case SDL_CONTROLLER_BUTTON_START: return {6, key, ps ? "Start" : L("Menú", "Menu")};
      case SDL_CONTROLLER_BUTTON_LEFTSTICK: return {6, key, ps ? "L3" : "LS"};
      case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return {6, key, ps ? "R3" : "RS"};
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return {6, key, ps ? "L1" : "LB"};
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return {6, key, ps ? "R1" : "RB"};
      case SDL_CONTROLLER_BUTTON_DPAD_UP: return {6, key, L("Cruceta arriba", "D-pad up")};
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return {6, key, L("Cruceta abajo", "D-pad down")};
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return {6, key, L("Cruceta izq.", "D-pad left")};
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return {6, key, L("Cruceta der.", "D-pad right")};
      case SDL_CONTROLLER_BUTTON_MISC1: return {6, key, ps ? "Micro" : L("Compartir", "Share")};
      case SDL_CONTROLLER_BUTTON_PADDLE1: return {6, key, "P1"};
      case SDL_CONTROLLER_BUTTON_PADDLE2: return {6, key, "P2"};
      case SDL_CONTROLLER_BUTTON_PADDLE3: return {6, key, "P3"};
      case SDL_CONTROLLER_BUTTON_PADDLE4: return {6, key, "P4"};
      case SDL_CONTROLLER_BUTTON_TOUCHPAD: return {6, key, L("Panel táctil", "Touchpad")};
      default: return {6, key, "—"};
    }
  };
  const int dpad_ids[4] = {RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT,
                           RETRO_DEVICE_ID_JOYPAD_RIGHT};

  G g{0, key, "?"};
  if (style == PromptStyle::Keyboard)
  {
    if (retro_id(b) >= 0)
      g.text = KeyLabel(m_cfg.KeyBind(retro_id(b)));
    else
      switch (b)
      {
        case PsButton::DPad:
        {
          const int arrows[4] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
          bool are_arrows = true;
          std::string joined;
          for (int i = 0; i < 4; i++)
          {
            are_arrows &= m_cfg.KeyBind(dpad_ids[i]) == arrows[i];
            joined += (i ? " " : "") + KeyLabel(m_cfg.KeyBind(dpad_ids[i]));
          }
          g.text = are_arrows ? L("Flechas", "Arrows") : joined;
          break;
        }
        case PsButton::Accept: g.text = "Enter"; break;
        case PsButton::Back:
        case PsButton::Pause: g.text = "Esc"; break;
        case PsButton::LStick:
        case PsButton::RStick: g.text = "—"; break;
        default: g.text = "—"; break;
      }
  }
  else
  {
    // En los menús siempre se acepta con el botón de abajo y se vuelve con el de la derecha.
    if (b == PsButton::Accept)
      g = pad_glyph(SDL_CONTROLLER_BUTTON_A);
    else if (b == PsButton::Back)
      g = pad_glyph(SDL_CONTROLLER_BUTTON_B);
    else if (retro_id(b) >= 0)
      g = pad_glyph(m_cfg.pad_bind[retro_id(b)]);
    else
    {
      const bool ps = (style == PromptStyle::PlayStation);
      switch (b)
      {
        case PsButton::DPad:
        {
          bool is_dpad = true;
          const int dpad[4] = {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                               SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
          for (int i = 0; i < 4; i++)
            is_dpad &= m_cfg.pad_bind[dpad_ids[i]] == dpad[i];
          g = {6, key, is_dpad ? L("Cruceta", "D-pad") : L("Personalizada", "Custom")};
          break;
        }
        case PsButton::LStick: g = {6, key, L("Stick izq.", "Left stick")}; break;
        case PsButton::RStick: g = {6, key, L("Stick der.", "Right stick")}; break;
        case PsButton::Pause: g = {6, key, ps ? "L3 + R3  /  PS" : "LS + RS  /  Xbox"}; break;
        default: break;
      }
    }
  }

  const float fs = h * 0.58f;
  float w = h;
  if (g.kind == 0 || g.kind == 6)
    w = std::max(h, m_r.TextWidth(m_r.ui, g.text, fs) + h * 0.75f);
  if (!draw)
    return w;
  m_r.Rect(x, y, w, h, UiMode::PadGlyph, g.color, {}, static_cast<float>(g.kind));
  if (!g.text.empty())
  {
    const Color tc = g.kind == 1 ? Color{1, 1, 1, 1} : Color{0.93f, 0.91f, 0.88f, 1};
    m_r.Text(m_r.ui, g.text, x + w / 2, y + (h - fs * 1.48f) / 2, fs, tc, Align::Center);
  }
  return w;
}

// Pistas de controles alineadas a la derecha: [icono] Texto   [icono] Texto
void App::RenderHints(const std::vector<std::pair<PsButton, std::string>>& hints, float right_x, float y)
{
  const float h = IS(36), fs = IS(25), gap = IS(30);
  const PromptStyle style = CurrentStyle();
  float x = right_x;
  for (auto it = hints.rbegin(); it != hints.rend(); ++it)
  {
    x -= m_r.TextWidth(m_r.ui, it->second, fs);
    m_r.Text(m_r.ui, it->second, x, y + (h - fs * 1.48f) / 2, fs, kGrey);
    x -= DrawGlyph(it->first, 0, 0, h, style, false) + IS(10);
    DrawGlyph(it->first, x, y, h, style);
    x -= gap;
  }
}

void App::DrawMenuButton(const Rect& slot, const std::string& label, const Texture* icon, float sel, float font,
                         bool own_background, float alpha, float dx, float reserve)
{
  if (alpha <= 0.001f)
    return;
  Rect r = IR(slot);
  r.x += dx;
  if (own_background)
    m_r.Sprite(m_btn_normal, r.x, r.y, r.w, r.h, {1, 1, 1, alpha});
  if (sel > 0.01f)
  {
    // La banda blanca de la ilustración (JUGAR en la maqueta), con un leve pulso.
    const float pulse = 1.0f + 0.01f * std::sin(m_time * 3.0f) * sel;
    const float w = r.w * pulse, h = r.h * pulse;
    m_r.Sprite(m_btn_sel, r.x + r.w / 2 - w / 2, r.y + r.h / 2 - h / 2, w, h, {1, 1, 1, sel * alpha});
  }
  // Sobre la banda blanca, icono y texto en negro; sobre la oscura, en blanco.
  const auto mix = [sel](float light, float dark) { return light + (dark - light) * sel; };
  const Color tc = {mix(0.95f, kInk.r), mix(0.95f, kInk.g), mix(0.95f, kInk.b), alpha};
  if (icon && icon->h > 0)
  {
    const float ih = r.h * kBandIconH, iw = ih * icon->w / icon->h;
    m_r.Sprite(*icon, r.x + r.w * kBandIconX - iw / 2, r.y + r.h / 2 - ih / 2, iw, ih, tc);
  }
  // La etiqueta nunca pasa del borde derecho del botón (ni de la flecha de selección): si es larga
  // se reduce la letra (hasta un 55 %) y, si aun así no cabe, se recorta.
  const float tx = r.x + r.w * kBandTextX;
  const float max_w = r.x + r.w - IS(78) - IS(reserve) - tx;
  float fs = IS(font);
  if (const float w = m_r.TextWidth(m_r.brush, label, fs); w > max_w)
    fs = std::max(fs * 0.55f, fs * max_w / w * 0.99f); // 0,99: que el redondeo no lo recorte
  FitText(m_r.brush, label, tx + IS(4) * sel, r.y + r.h / 2 - fs * 0.56f, max_w, fs, tc, Align::Left, false, -1);
  if (sel > 0.5f)
    m_r.DrawIcon(Icon::Chevron, r.x + r.w - IS(70), r.y + r.h / 2 - IS(12), IS(24), WithAlpha(kInk, sel * alpha));
}

// ------------------------------------------------------------------------------------------------
// Pantallas
// ------------------------------------------------------------------------------------------------

void App::RenderTitleBar()
{
  if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN)
  {
    m_tb_min = m_tb_max = m_tb_close = {};
    return;
  }
  const float h = IS(kTitleH);
  m_r.Rect(0, 0, static_cast<float>(m_w), h, UiMode::Solid, {0.025f, 0.025f, 0.028f, 1.0f});
  m_r.Rect(0, h - 1, static_cast<float>(m_w), 1, UiMode::Solid, WithAlpha(kSilver, 0.3f));
  m_r.Sprite(m_ic_opt, IS(16), IS(9), IS(22), IS(22), WithAlpha(kSilver, 0.95f));
  m_r.Text(m_r.ui, "Metal Gear Solid  -  Launcher", IS(52), IS(4), IS(22), {0.82f, 0.82f, 0.82f, 0.92f});

  const float bw = IS(58);
  m_tb_close = {m_w - bw, 0, bw, h};
  m_tb_max = {m_w - bw * 2, 0, bw, h};
  m_tb_min = {m_w - bw * 3, 0, bw, h};
  const Rect* btns[3] = {&m_tb_min, &m_tb_max, &m_tb_close};
  const Icon icons[3] = {Icon::Minimize, Icon::Maximize, Icon::Close};
  for (int i = 0; i < 3; i++)
  {
    const Rect& b = *btns[i];
    if (b.Contains(m_mouse_px, m_mouse_py))
      m_r.Rect(b.x, b.y, b.w, b.h, UiMode::Solid, i == 2 ? Color{0.75f, 0.06f, 0.05f, 0.95f} : Color{1, 1, 1, 0.1f});
    const float is = IS(14);
    m_r.DrawIcon(icons[i], b.x + (b.w - is) / 2, b.y + (b.h - is) / 2, is, {0.88f, 0.86f, 0.83f, 1});
  }
  Hot(m_tb_close, 5, 0, [this](float, float) { RequestQuit(); });
  Hot(m_tb_min, 5, 1, [this](float, float) { SDL_MinimizeWindow(m_window); });
  Hot(m_tb_max, 5, 2, [this](float, float) {
    if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED)
      SDL_RestoreWindow(m_window);
    else
      SDL_MaximizeWindow(m_window);
  });
}

void App::SetLanguage(Lang lang)
{
  I18n::Current() = lang;
  m_cfg.language = lang == Lang::En ? "en" : "es";
  m_cfg.Save();
}

// Banderas dibujadas con rectángulos (proporción 3:2).
void App::DrawFlag(Lang lang, float x, float y, float w, float h, float alpha)
{
  auto rect = [&](float rx, float ry, float rw, float rh, Color c) {
    c.a *= alpha;
    m_r.Rect(x + rx * w, y + ry * h, rw * w, rh * h, UiMode::Solid, c);
  };
  if (lang == Lang::Es)
  {
    // Rojo, gualda (el doble de ancha) y rojo; el escudo, simplificado, hacia el asta.
    const Color red = {0.67f, 0.08f, 0.11f, 1}, gold = {0.95f, 0.75f, 0.0f, 1};
    rect(0, 0, 1, 1, red);
    rect(0, 0.25f, 1, 0.5f, gold);
    rect(0.22f, 0.36f, 0.13f, 0.28f, {0.75f, 0.12f, 0.1f, 1});
    rect(0.22f, 0.36f, 0.13f, 0.08f, {0.9f, 0.62f, 0.12f, 1});
    rect(0.17f, 0.38f, 0.035f, 0.26f, {0.62f, 0.6f, 0.58f, 1});
    rect(0.375f, 0.38f, 0.035f, 0.26f, {0.62f, 0.6f, 0.58f, 1});
  }
  else
  {
    // 13 franjas rojas y blancas; cantón azul (7 franjas de alto) con estrellas en filas alternas.
    const Color red = {0.7f, 0.13f, 0.2f, 1}, white = {0.96f, 0.95f, 0.93f, 1}, blue = {0.24f, 0.23f, 0.43f, 1};
    rect(0, 0, 1, 1, white);
    for (int i = 0; i < 13; i += 2)
      rect(0, i / 13.0f, 1, 1 / 13.0f, red);
    const float ch = 7 / 13.0f, cw = 0.4f;
    rect(0, 0, cw, ch, blue);
    const float sw = std::max(1.0f, h * 0.045f);
    for (int row = 0; row < 5; row++)
      for (int col = 0; col < (row % 2 ? 4 : 5); col++)
      {
        const float sx = cw * ((row % 2 ? 0.2f : 0.1f) + col * 0.2f), sy = ch * (0.13f + row * 0.185f);
        m_r.Rect(x + sx * w - sw / 2, y + sy * h - sw / 2, sw, sw, UiMode::Solid, {1, 1, 1, alpha});
      }
  }
  // Borde fino oscuro.
  const float b = std::max(1.0f, IS(1));
  const Color edge = {0, 0, 0, 0.55f * alpha};
  m_r.Rect(x, y, w, b, UiMode::Solid, edge);
  m_r.Rect(x, y + h - b, w, b, UiMode::Solid, edge);
  m_r.Rect(x, y, b, h, UiMode::Solid, edge);
  m_r.Rect(x + w - b, y, b, h, UiMode::Solid, edge);
}

void App::RenderLanguage()
{
  const bool fullscreen = SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN;
  const float alpha = Smooth(2.0f, 2.4f, m_intro_t) * (m_outro_t >= 0 ? 1.0f - Smooth(0.05f, 0.4f, m_outro_t) : 1.0f);
  if (alpha <= 0.001f)
  {
    m_tb_lang = m_lang_menu = {};
    m_lang_open = false;
    return;
  }
  // En ventana, dentro de la barra de título junto a minimizar; en pantalla completa, flotando en
  // la misma esquina.
  const float bw = IS(58), bh = IS(kTitleH);
  if (fullscreen)
  {
    m_tb_lang = {m_w - bw - IS(16), IS(12), bw, bh};
    m_r.Rect(m_tb_lang.x, m_tb_lang.y, m_tb_lang.w, m_tb_lang.h, UiMode::Solid, {0.03f, 0.025f, 0.025f, 0.8f * alpha});
  }
  else
  {
    m_tb_lang = {m_tb_min.x - bw, 0, bw, bh};
  }
  const Rect& b = m_tb_lang;
  if (b.Contains(m_mouse_px, m_mouse_py) || m_lang_open)
    m_r.Rect(b.x, b.y, b.w, b.h, UiMode::Solid, {1, 1, 1, 0.1f * alpha});
  const float fh = IS(18), fw = fh * 1.5f;
  DrawFlag(I18n::Current(), b.x + (b.w - fw) / 2, b.y + (b.h - fh) / 2, fw, fh, alpha);
  Hot(b, 5, 3, [this](float, float) {
    m_lang_open = !m_lang_open;
    m_audio.Play(m_lang_open ? Sfx::Select : Sfx::Cancel);
  });

  if (!m_lang_open)
  {
    m_lang_menu = {};
    return;
  }
  // Menú: una fila por idioma, con su bandera y su nombre en ese idioma.
  const float mw = IS(230), rh = IS(52);
  m_lang_menu = {std::min(b.x + b.w, static_cast<float>(m_w)) - mw, b.y + b.h + IS(4), mw, rh * 2 + IS(12)};
  const Rect& m = m_lang_menu;
  m_r.Rect(m.x, m.y, m.w, m.h, UiMode::Panel, WithAlpha(kPanelBg, 1.05f), WithAlpha(kSilver, 0.9f));
  const std::pair<Lang, const char*> langs[2] = {{Lang::Es, "Español"}, {Lang::En, "English"}};
  for (int i = 0; i < 2; i++)
  {
    const Rect row = {m.x + IS(6), m.y + IS(6) + i * rh, m.w - IS(12), rh};
    const bool current = langs[i].first == I18n::Current();
    if (row.Contains(m_mouse_px, m_mouse_py))
      m_r.Rect(row.x, row.y, row.w, row.h, UiMode::Solid, {1, 1, 1, 0.12f});
    const float h = IS(22), w = h * 1.5f;
    DrawFlag(langs[i].first, row.x + IS(14), row.y + (rh - h) / 2, w, h, 1);
    m_r.Text(m_r.ui, langs[i].second, row.x + IS(14) + w + IS(16), row.y + (rh - IS(26) * 1.48f) / 2, IS(26),
             current ? kGold : kWhite);
    const Lang lang = langs[i].first;
    Hot(row, 5, 10 + i, [this, lang](float, float) {
      m_audio.Play(Sfx::Select);
      SetLanguage(lang);
      m_lang_open = false;
    });
  }
}

// Capa de transición: el «!» de alerta en un círculo de tinta y barrido de tinta (apertura y cierre).
void App::RenderTransition()
{
  float wipe = 1.0f; // 1 = nada tapado, 0 = todo negro
  float kanji_a = 0, kanji_s = 1;
  if (IntroActive())
  {
    wipe = EaseInOut((m_intro_t - 0.75f) / 0.75f);
    kanji_a = Smooth(0.05f, 0.4f, m_intro_t) * (1.0f - Smooth(0.85f, 1.25f, m_intro_t));
    kanji_s = 1.25f - 0.25f * EaseOut(m_intro_t / 0.6f);
  }
  else if (m_outro_t >= 0)
  {
    wipe = 1.0f - EaseInOut((m_outro_t - 0.2f) / 0.55f);
    kanji_a = Smooth(0.55f, 0.8f, m_outro_t) * (1.0f - Smooth(1.0f, 1.2f, m_outro_t));
    kanji_s = 1.0f + 0.15f * Smooth(0.55f, 1.2f, m_outro_t);
  }
  if (wipe < 0.999f)
    m_r.Rect(0, 0, static_cast<float>(m_w), static_cast<float>(m_h), UiMode::InkWipe, {0, 0, 0, 1}, {}, wipe);
  if (kanji_a > 0.001f)
  {
    const float cx = m_w / 2.0f, cy = m_h / 2.0f;
    const float size = std::min(m_w, m_h) * 0.28f * kanji_s;
    m_r.Rect(cx - size * 0.95f, cy - size * 0.95f, size * 1.9f, size * 1.9f, UiMode::Enso,
             {0.82f, 0.82f, 0.84f, 0.8f * kanji_a});
    m_r.TextCentered(m_r.display, "!", cx, cy, size * 1.3f, WithAlpha(kRed, kanji_a));
  }
}

void App::RenderMenu()
{
  // Animación de apertura/cierre: factores de aparición de cada elemento.
  const float T = m_intro_t, O = m_outro_t;
  const float out_fade = O >= 0 ? 1.0f - Smooth(0.05f, 0.4f, O) : 1.0f;
  const float info_a = Smooth(2.0f, 2.4f, T) * out_fade;

  // Los botones van pintados en la ilustración, bajo el logotipo: se dibujan con la misma escala y
  // posición que el fondo (que se ancla a la derecha) para que coincidan con el arte.
  const float saved_s = m_s, saved_ox = m_ox, saved_oy = m_oy;
  m_s = m_bg_rect.w / kArtW;
  m_ox = m_bg_rect.x;
  m_oy = m_bg_rect.y;

  const char* labels[kMenuItems] = {L("JUGAR", "PLAY"), L("LOGROS", "ACHIEVEMENTS"), L("OPCIONES", "OPTIONS"),
                                    L("CRÉDITOS", "CREDITS"), L("SALIR", "QUIT")};
  const Texture* icons[kMenuItems] = {&m_ic_play, &m_ic_ach, &m_ic_opt, &m_ic_credits, &m_ic_quit};
  constexpr float kFont = 41;
  // Progreso de logros: junto a la etiqueta si cabe; si no («ACHIEVEMENTS»), pequeño en la esquina.
  const bool counter = m_ach.GetState() == Achievements::State::Ready;
  const Rect& ach_slot = kMenuSlots[MENU_ACHIEVEMENTS];
  const bool counter_inline =
    m_r.TextWidth(m_r.brush, labels[MENU_ACHIEVEMENTS], IS(kFont)) <= IS(ach_slot.w * (1 - kBandTextX) - 78 - 80);
  for (int i = 0; i < kMenuItems; i++)
  {
    float e = EaseOut((T - 1.35f - i * 0.09f) / 0.38f);
    if (O >= 0)
      e *= 1.0f - EaseInOut((O - i * 0.05f) / 0.3f);
    const float sel = m_modal == Modal::None ? m_sel_anim[i] : 0.0f;
    if (m_modal != Modal::None)
      e *= 0.3f; // detrás de una ventana, apagados
    // Entran desde la derecha.
    DrawMenuButton(kMenuSlots[i], labels[i], icons[i], sel, kFont, true, e, (1.0f - e) * IS(460),
                   i == MENU_ACHIEVEMENTS && counter_inline ? 80.0f : 0.0f);
    Hot(IR(kMenuSlots[i]), 0, i, [this, i](float, float) {
      m_sel = i;
      ActivateMenu(i);
    });
  }

  // Progreso de logros dentro del botón LOGROS.
  if (counter)
  {
    const std::string p = std::to_string(m_ach.EarnedCount()) + "/" + std::to_string(m_ach.List().size());
    const Rect r = IR(ach_slot);
    // Sobre la banda blanca (seleccionado), en negro.
    const Color cc = m_sel_anim[MENU_ACHIEVEMENTS] > 0.5f && m_modal == Modal::None ? kInk : kGold;
    if (counter_inline)
      m_r.Text(m_r.ui, p, r.x + r.w - IS(100), r.y + r.h / 2 - IS(17), IS(24), WithAlpha(cc, info_a), Align::Right);
    else
      m_r.Text(m_r.ui, p, r.x + r.w - IS(40), r.y + r.h - IS(28), IS(18), WithAlpha(cc, info_a), Align::Right);
  }
  m_s = saved_s;
  m_ox = saved_ox;
  m_oy = saved_oy;

  // Estado del disco y pistas de controles (abajo a la derecha).
  std::string status;
  Color sc = {0.82f, 0.8f, 0.78f, 0.75f};
  if (m_cfg.game_path.empty() || !Paths::FileExists(m_cfg.game_path))
  {
    status = L("Sin disco: copia tu imagen en la carpeta game/ o arrástrala aquí", "No disc: copy your image into the game/ folder or drag it here");
    sc = {1.0f, 0.75f, 0.3f, 0.95f};
  }
  else
  {
    status = L("Disco: ", "Disc: ") + BaseName(m_cfg.game_path);
    if (DiscTwoPath().empty())
    {
      status += L("  ·  falta el Disco 2", "  ·  Disc 2 missing");
      sc = {1.0f, 0.75f, 0.3f, 0.95f};
    }
  }
  m_r.Text(m_r.ui, Truncate(m_r.ui, status, IS(20), m_w * 0.5f), m_w - IS(28), m_h - IS(92), IS(20),
           WithAlpha(sc, info_a), Align::Right);
  if (info_a > 0.99f)
    RenderHints({{PsButton::Accept, L("Aceptar", "Accept")}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderPlayChoice()
{
  DimScreen(0.55f);
  const float x = 300, y = 260, w = 1000, h = 520;
  Panel(x, y, w, h);
  m_r.Text(m_r.brush, L("JUGAR", "PLAY"), IX(x + 44), IY(y + 24), IS(52), kWhite);
  const char* labels[4] = {L("CONTINUAR", "CONTINUE"), L("CARGAR PARTIDA", "LOAD GAME"), L("NUEVA PARTIDA", "NEW GAME"),
                           L("CANCELAR", "CANCEL")};
  const Texture* icons[4] = {&m_ic_play, &m_ic_credits, &m_ic_disc, &m_ic_quit};
  for (int i = 0; i < 4; i++)
  {
    const Rect slot = {x + 40, y + 115 + i * 92.0f, 470, 84};
    DrawMenuButton(slot, labels[i], icons[i], m_sel_anim[i], 30, true);
    Hot(IR(slot), 3, i, [this, i](float, float) {
      m_choice_sel = i;
      Accept();
    });
  }
  // Ficha de la partida más reciente (la que carga CONTINUAR).
  if (const int latest = LatestSlot(); latest >= 0)
  {
    m_r.Text(m_r.ui, L("ÚLTIMA PARTIDA", "LATEST GAME"), IX(x + 560), IY(y + 118), IS(22), kDim);
    DrawSlotCard(latest, x + 560, y + 152, 400, false);
  }
  RenderHints({{PsButton::Accept, L("Aceptar", "Accept")}, {PsButton::Back, L("Volver", "Back")}}, IX(x + w - 30), IY(y + h + 16));
}

std::string App::SlotName(int slot) const
{
  if (slot == 0)
    return L("Autoguardado", "Autosave");
  return (LangEn() ? "Slot " : "Ranura ") + std::to_string(slot);
}

std::string App::SlotDate(int slot) const
{
  const SlotInfo& s = m_slots[slot];
  if (!s.exists)
    return L("Vacía", "Empty");
  std::string text = L("Fecha desconocida", "Unknown date");
  if (s.time > 0)
  {
    char date[48];
    const time_t t = static_cast<time_t>(s.time);
    std::strftime(date, sizeof(date), L("%d/%m/%Y  %H:%M", "%m/%d/%Y  %H:%M"), std::localtime(&t));
    text = date;
  }
  return text + (LangEn() ? "  ·  Disc " : "  ·  Disco ") + std::to_string(s.disc + 1);
}

// Relee qué ranuras hay, con su fecha, disco y miniatura (sólo recarga las miniaturas que cambian).
void App::RefreshSlots()
{
  for (int i = 0; i < kSlots; i++)
  {
    SlotInfo& s = m_slots[i];
    const std::string path = StatePath(i);
    s.exists = Paths::FileExists(path);
    s.time = 0;
    s.disc = 0;
    if (std::ifstream info(Platform::Path(path + ".txt")); info)
    {
      std::string line;
      while (std::getline(info, line))
      {
        if (line.rfind("time=", 0) == 0)
          s.time = std::atoll(line.c_str() + 5);
        else if (line.rfind("disc=", 0) == 0)
          s.disc = std::atoi(line.c_str() + 5);
      }
    }
    const std::string key = s.exists ? path + "#" + std::to_string(s.time) : std::string();
    if (key != s.thumb_key)
    {
      if (s.thumb.id)
        glDeleteTextures(1, &s.thumb.id);
      s.thumb = {};
      s.thumb_key = key;
      if (s.exists && Paths::FileExists(path + ".png"))
        m_r.LoadTexture(path + ".png", &s.thumb, true);
    }
  }
}

int App::LatestSlot() const
{
  int best = -1;
  for (int i = 0; i < kSlots; i++)
    if (m_slots[i].exists && (best < 0 || m_slots[i].time > m_slots[best].time))
      best = i;
  return best;
}

bool App::SaveSlot(int slot)
{
  const std::string path = StatePath(slot);
  Paths::MakeDirs(Paths::StatesDir());
  if (!m_emu.SaveState(path))
    return false;
  // Miniatura de la imagen del juego, con su proporción (4:3 o 16:9).
  std::vector<uint8_t> frame;
  int fw = 0, fh = 0;
  if (m_emu.CaptureFrame(&frame, &fw, &fh))
  {
    const int tw = 320, th = std::max(1, static_cast<int>(std::lround(tw / std::max(0.5f, m_emu.AspectRatio()))));
    const std::vector<uint8_t> small = Downscale(frame, fw, fh, tw, th);
    WritePng(path + ".png", small.data(), tw, th);
  }
  if (std::ofstream info(Platform::Path(path + ".txt"), std::ios::trunc); info)
    info << "time=" << static_cast<long long>(std::time(nullptr)) << "\ndisc=" << m_emu.CurrentDisc() << "\n";
  if (slot > 0)
    m_slot_last = slot;
  RefreshSlots();
  return true;
}

bool App::LoadSlot(int slot)
{
  if (!m_emu.LoadState(StatePath(slot)))
    return false;
  if (!m_cheats_active)
    m_ach.BeginSession(false); // reinicia la evaluación tras cambiar la memoria
  if (slot > 0)
    m_slot_last = slot;
  return true;
}

void App::OpenSlots(bool save)
{
  RefreshSlots();
  m_slots_save = save;
  m_slot_sel = save ? m_slot_last : std::max(0, LatestSlot());
  m_modal = Modal::Slots;
}

void App::ActivateSlot(int slot)
{
  if (m_slots_save)
  {
    if (slot == 0)
      return;
    const bool ok = SaveSlot(slot);
    m_audio.Play(ok ? Sfx::Select : Sfx::Cancel);
    Toast(ok ? SlotName(slot) + L(": partida guardada", ": game saved") : std::string(L("No se pudo guardar la partida", "Could not save the game")));
  }
  else if (!m_slots[slot].exists)
  {
    m_audio.Play(Sfx::Cancel);
    Toast(SlotName(slot) + L(": vacía", ": empty"));
    return;
  }
  else if (!m_emu.IsRunning())
  {
    StartGame(slot); // desde el menú principal
    return;
  }
  else
  {
    const bool ok = LoadSlot(slot);
    m_audio.Play(ok ? Sfx::Select : Sfx::Cancel);
    Toast(ok ? SlotName(slot) + L(": partida cargada", ": game loaded") : std::string(L("No se pudo cargar la partida", "Could not load the game")));
  }
  m_modal = Modal::None;
  if (m_emu.IsRunning())
  {
    m_paused = false;
    m_emu.SetPaused(false);
  }
}

void App::DrawSlotCard(int slot, float x, float y, float thumb_w, bool selected)
{
  const SlotInfo& s = m_slots[slot];
  const float th = thumb_w * 0.75f;
  const Rect r = IR({x, y, thumb_w, th});
  m_r.Rect(r.x - IS(2), r.y - IS(2), r.w + IS(4), r.h + IS(4), UiMode::Solid, WithAlpha(kSilver, 0.5f));
  if (s.thumb.id)
  {
    // Encajada en el recuadro 4:3 sin deformar (las de 16:9 quedan con franjas).
    const float a = static_cast<float>(s.thumb.w) / std::max(1, s.thumb.h);
    const float iw = std::min(r.w, r.h * a), ih = iw / a;
    m_r.Rect(r.x, r.y, r.w, r.h, UiMode::Solid, {0, 0, 0, 1});
    m_r.Sprite(s.thumb, r.x + (r.w - iw) / 2, r.y + (r.h - ih) / 2, iw, ih, {1, 1, 1, 1});
  }
  else
  {
    m_r.Rect(r.x, r.y, r.w, r.h, UiMode::Solid, {0.05f, 0.06f, 0.08f, 1});
    m_r.TextCentered(m_r.display, "?", r.x + r.w / 2, r.y + r.h / 2, r.h * 0.4f, WithAlpha(kDim, 0.5f));
  }
  const Color tc = selected ? kInk : kWhite;
  m_r.Text(m_r.brush, SlotName(slot), IX(x), IY(y + th + 14), IS(26), tc);
  m_r.Text(m_r.ui, SlotDate(slot), IX(x), IY(y + th + 50), IS(22), selected ? WithAlpha(kInk, 0.75f) : kGrey);
}

void App::RenderSlots()
{
  DimScreen(0.6f);
  const float x = 240, y = 170, w = 1140, h = 700;
  Panel(x, y, w, h);
  m_r.Text(m_r.brush, m_slots_save ? L("GUARDAR PARTIDA", "SAVE GAME") : L("CARGAR PARTIDA", "LOAD GAME"), IX(x + 44),
           IY(y + 24), IS(48), kWhite);
  // Cuatro fichas en fila: autoguardado y las tres ranuras.
  const float cw = 240, gap = 30, x0 = x + (w - (kSlots * cw + (kSlots - 1) * gap)) / 2, y0 = y + 140;
  for (int i = 0; i < kSlots; i++)
  {
    const float cx = x0 + i * (cw + gap);
    const bool disabled = m_slots_save && i == 0;
    const bool sel = i == m_slot_sel;
    const Rect card = IR({cx - 14, y0 - 14, cw + 28, cw * 0.75f + 112});
    if (sel)
      m_r.Sprite(m_row_sel, card.x - IS(10), card.y, card.w + IS(20), card.h, {1, 1, 1, 0.95f});
    if (disabled)
      m_r.Rect(card.x, card.y, card.w, card.h, UiMode::Solid, {0, 0, 0, 0.5f});
    DrawSlotCard(i, cx, y0, cw, sel);
    Hot(card, 9, i, [this, i](float, float) {
      m_slot_sel = i;
      Accept();
    });
  }
  const char* help = m_slots_save
                       ? L("Elige una ranura: si ya tiene una partida, se sustituye. El autoguardado se hace solo al salir.",
                           "Choose a slot: an existing game in it is replaced. The autosave happens on its own when you quit.")
                       : L("F5 / F7 guardan y cargan al momento en la última ranura usada.",
                           "F5 / F7 quick-save and quick-load in the last slot used.");
  float ly = y0 + cw * 0.75f + 150;
  for (const std::string& line : Wrap(m_r.ui, help, IS(23), IS(w - 100)))
  {
    m_r.Text(m_r.ui, line, IX(x + 50), IY(ly), IS(23), kGrey);
    ly += 32;
  }
  RenderHints({{PsButton::Accept, m_slots_save ? L("Guardar", "Save") : L("Cargar", "Load")}, {PsButton::Back, L("Volver", "Back")}},
              IX(x + w - 30), IY(y + h + 16));
}

void App::RenderOptionCategories()
{
  const float px = 40, py = 56, pw = 640, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.brush, L("OPCIONES", "OPTIONS"), IX(px + 44), IY(py + 20), IS(56), kWhite);
  m_r.Sprite(m_ic_opt, IX(px + pw - 110), IY(py + 26), IS(62), IS(62), {0.8f, 0.78f, 0.76f, 0.8f});

  for (int i = 0; i <= kOptCategoryCount; i++)
  {
    const bool back = (i == kOptCategoryCount);
    const Rect slot = {px + 60, py + 130 + i * 94.0f + (back ? 20.0f : 0.0f), 462, 84};
    DrawMenuButton(slot, back ? L("VOLVER", "BACK") : OptCategory(i), back ? &m_ic_quit : nullptr, m_sel_anim[i], 36, true);
    if (!back)
    {
      // Número de la categoría en el lugar del icono.
      const Rect r = IR(slot);
      const float t = m_sel_anim[i];
      const float v = 0.86f + (kInk.r - 0.86f) * t;
      m_r.TextCentered(m_r.display, OptCategoryNumber(i), r.x + r.w * kBandIconX, r.y + r.h / 2, r.h * 0.42f,
                       {v, v, v, 1});
    }
    Hot(IR(slot), 1, i, [this, i](float, float) {
      m_opt_sel = i;
      Accept();
    });
  }

  // Descripción de la categoría seleccionada.
  if (m_opt_sel < kOptCategoryCount)
  {
    float ly = py + 130 + (kOptCategoryCount + 1) * 94.0f + 40;
    for (const std::string& line : Wrap(m_r.ui, OptCategoryHelp(m_opt_sel), IS(25), IS(pw - 100)))
    {
      m_r.Text(m_r.ui, line, IX(px + 60), IY(ly), IS(25), {0.84f, 0.81f, 0.78f, 0.95f});
      ly += 34;
    }
  }

  // Créditos y licencias.
  const float cx = m_w - IS(28);
  const Color cc = {0.8f, 0.78f, 0.76f, 0.8f};
  m_r.Text(m_r.ui, std::string("Silver Gear ") + kVersion + L(" · software libre (GPL-3.0) · sin fines de lucro",
                                                               " · free software (GPL-3.0) · non-profit"),
           cx, m_h - IS(150), IS(19), cc, Align::Right);
  m_r.Text(m_r.ui, L("Núcleo: SwanStation (GPL-3.0) · Logros: rcheevos (MIT) / RetroAchievements",
                     "Core: SwanStation (GPL-3.0) · Achievements: rcheevos (MIT) / RetroAchievements"), cx, m_h - IS(124),
           IS(19), cc, Align::Right);
  m_r.Text(m_r.ui, L("Fuentes: Russo One, Oswald (OFL)  ·  Parches y trucos: chtdb (PeterDelta y otros)",
                     "Fonts: Russo One, Oswald (OFL)  ·  Patches and cheats: chtdb (PeterDelta and others)"), cx, m_h - IS(98),
           IS(19), cc, Align::Right);
  RenderHints({{PsButton::Accept, L("Aceptar", "Accept")}, {PsButton::Back, L("Volver", "Back")}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderOptions()
{
  DimScreen(0.45f);
  if (m_opt_page < 0)
  {
    RenderOptionCategories();
    return;
  }

  const float px = 40, py = 56, pw = 880, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.ui, L("OPCIONES", "OPTIONS"), IX(px + 48), IY(py + 18), IS(22), kDim);
  m_r.Text(m_r.brush, OptCategory(m_opt_page), IX(px + 44), IY(py + 42), IS(52), kWhite);
  m_r.TextCentered(m_r.display, OptCategoryNumber(m_opt_page), IX(px + pw - 90), IY(py + 70), IS(84),
                   {1, 1, 1, 0.14f});

  auto yesno = [](bool b) { return std::string(b ? L("Sí", "Yes") : "No"); };
  const bool hw = m_cfg.hardware_renderer;

  struct Row
  {
    std::string label, value;
    bool enabled = true, arrows = false;
    float bar = -1; // 0..1: la fila se dibuja como barra deslizante
  };
  auto info = [&](int id) -> Row {
    switch (id)
    {
      case OPT_GAME:
        return {L("Disco 1", "Disc 1"), m_cfg.game_path.empty() ? L("(sin seleccionar)", "(not selected)") : BaseName(m_cfg.game_path)};
      case OPT_DISC2:
      {
        const std::string two = DiscTwoPath();
        return {L("Disco 2", "Disc 2"), two.empty() ? L("(no encontrado)", "(not found)") : BaseName(two)};
      }
      case OPT_BIOS:
      {
        const bool has_files = !Paths::FindBiosFiles().empty();
        const bool chosen = !m_cfg.bios_file.empty() && Paths::FileExists(Paths::BiosDir() + "/" + m_cfg.bios_file);
        return {"BIOS", chosen ? BiosLabel(m_cfg.bios_file) : L("OpenBIOS (libre, integrada)", "OpenBIOS (free, built-in)"), true, has_files};
      }
      case OPT_BIOS_LOAD: return {L("Cargar BIOS nueva…", "Load new BIOS…"), ""};
      case OPT_FASTBOOT: return {L("Arranque rápido", "Fast boot"), yesno(m_cfg.fast_boot), true, true};
      case OPT_HARDCORE:
        return {L("Modo hardcore", "Hardcore mode"),
                m_cfg.hardcore ? (HasSonyBios() ? L("Sí", "Yes") : L("Sí (falta la BIOS original)", "Yes (original BIOS missing)")) : "No", true, true};
      case OPT_RENDERER: return {L("Renderizador", "Renderer"), hw ? L("OpenGL (mejorado)", "OpenGL (enhanced)") : L("Software (preciso)", "Software (accurate)"), true, true};
      case OPT_SCALE: return {L("Resolución interna", "Internal resolution"), std::to_string(m_cfg.resolution_scale) + "x", hw, true};
      case OPT_TEXFILTER:
      {
        const std::string tf = m_cfg.texture_filter == "Nearest"    ? "Original"
                               : m_cfg.texture_filter == "Bilinear" ? L("Bilineal", "Bilinear")
                                                                     : m_cfg.texture_filter + " (HD)";
        return {L("Filtro de texturas", "Texture filter"), tf, hw, true};
      }
      case OPT_PGXP:
        return {L("Corregir geometría (PGXP)", "Fix geometry (PGXP)"), !m_cfg.pgxp ? "No" : m_cfg.pgxp_cpu ? L("Máxima", "Maximum") : L("Sí", "Yes"), hw, true};
      case OPT_WIDESCREEN:
        return {L("Aspecto de la pantalla", "Aspect ratio"),
                m_ws_available ? (m_cfg.widescreen ? L("16:9 (panorámica)", "16:9 (widescreen)") : "4:3") : L("4:3 (16:9 no disponible para este disco)", "4:3 (16:9 not available for this disc)"),
                m_ws_available, m_ws_available};
      case OPT_SIDEART:
        return {L("Bordes laterales", "Side borders"),
                m_cfg.side_mode == 2 ? L("Ambiente (el juego)", "Ambient (the game)") : m_cfg.side_mode == 1 ? L("Ilustración", "Artwork") : L("Negro", "Black"),
                true, true};
      case OPT_SCREENFILTER:
        return {L("Filtro de pantalla", "Screen filter"),
                m_cfg.screen_filter == 2 ? L("CRT (tele de tubo)", "CRT (tube TV)") : m_cfg.screen_filter == 1 ? L("Líneas de barrido", "Scanlines") : L("Ninguno", "None"),
                true, true};
      case OPT_SMOOTH: return {L("Suavizar imagen", "Smooth image"), yesno(m_cfg.smooth_filter), true, true};
      case OPT_FULLSCREEN: return {L("Pantalla completa", "Fullscreen"), yesno(m_cfg.fullscreen), true, true};
      case OPT_VOLUME: return {L("Volumen del juego", "Game volume"), std::to_string(m_cfg.volume) + "%", true, true};
      case OPT_MUSIC: return {L("Volumen de la música", "Music volume"), std::to_string(m_cfg.music_volume) + "%", true, true};
      case OPT_SFX: return {L("Volumen de efectos", "Effects volume"), std::to_string(m_cfg.sfx_volume) + "%", true, true};
      case OPT_NTSC:
        return {L("Velocidad del juego", "Game speed"),
                !m_ntsc_available ? L("Original (no hace falta en este disco)", "Original (not needed for this disc)")
                : m_cfg.ntsc_mode ? L("60 Hz (NTSC)", "60 Hz (NTSC)")
                                  : L("50 Hz (PAL original)", "50 Hz (original PAL)"),
                m_ntsc_available, m_ntsc_available};
      case OPT_AUTOSAVE: return {L("Guardar al salir", "Save on quit"), yesno(m_cfg.autosave), true, true};
      case OPT_SUBTITLES:
        return {L("Subtítulos de las escenas", "Cutscene subtitles"),
                m_subs_available ? yesno(m_cfg.subtitles) : L("Sí (no se pueden quitar en este disco)", "Yes (cannot be removed on this disc)"),
                m_subs_available, m_subs_available};
      case OPT_PROMPTS:
      {
        const std::string p = m_cfg.prompt_style == "keyboard"      ? L("Teclado", "Keyboard")
                              : m_cfg.prompt_style == "xbox"        ? "Xbox"
                              : m_cfg.prompt_style == "playstation" ? "PlayStation"
                                                                    : L("Automático", "Automatic");
        return {L("Iconos de botones", "Button icons"), p, true, true};
      }
      case OPT_REMAP_KB: return {L("Personalizar teclado", "Customise keyboard"), ""};
      case OPT_REMAP_PAD: return {L("Personalizar mando", "Customise controller"), ""};
      case OPT_RESET_CONTROLS:
        return {L("Restaurar controles por defecto", "Restore default controls"), m_reset_confirm > 0 ? L("¿Seguro? Pulsa otra vez", "Sure? Press again") : ""};
      case OPT_CONTROLS: return {L("Ver esquema de controles", "View controls layout"), ""};
      case OPT_BACK: return {L("Volver", "Back"), ""};
    }
    return {};
  };

  const std::vector<int>& rows = OptPageRows(m_opt_page);
  m_opt_sel = std::clamp(m_opt_sel, 0, static_cast<int>(rows.size()) - 1);
  const float row0 = py + 150, rh = 62;
  const float label_x = px + 52;
  const float vx0 = px + 430, vx1 = px + pw - 64; // columna de valores (flechas en sus extremos)
  for (size_t i = 0; i < rows.size(); i++)
  {
    const int id = rows[i];
    const Row r = info(id);
    const float y = row0 + i * rh + (id == OPT_BACK ? 24.0f : 0.0f);
    const bool sel = static_cast<int>(i) == m_opt_sel;
    const Rect rr = IR({px + 22, y, pw - 44, rh - 6});
    if (sel)
      m_r.Sprite(m_row_sel, rr.x - IS(10), rr.y - IS(7), rr.w + IS(20), rr.h + IS(14), {1, 1, 1, 0.95f});
    const float a = r.enabled ? 1.0f : 0.4f;
    const Color lc = sel ? WithAlpha(kInk, a) : Color{0.86f, 0.86f, 0.86f, a};
    const float ty = y + 10;
    const bool has_value = !r.value.empty();
    // La etiqueta ocupa todo el ancho si la fila no tiene valor.
    FitText(m_r.ui, r.label, IX(label_x), IY(ty), IS((has_value ? vx0 - 30 : vx1 + 20) - label_x), IS(30), lc,
            Align::Left, sel, m_opt_page * 100 + static_cast<int>(i) * 2);
    if (has_value)
    {
      const Color vc = WithAlpha(lc, sel ? 1.0f : 0.88f);
      if (r.bar >= 0)
      {
        // Barra deslizante: flechas, pista con la parte llena y el valor a la derecha.
        m_r.DrawIcon(Icon::ArrowLeft, IX(vx0), IY(y + 16), IS(22), WithAlpha(lc, sel ? 1.0f : 0.5f));
        m_r.DrawIcon(Icon::ArrowRight, IX(vx1), IY(y + 16), IS(22), WithAlpha(lc, sel ? 1.0f : 0.5f));
        const float bx0 = vx0 + 44, bx1 = vx1 - 96, by = y + 24, bh = 8;
        const float fill = std::clamp(r.bar, 0.0f, 1.0f);
        m_r.Rect(IX(bx0), IY(by), IS(bx1 - bx0), IS(bh), UiMode::Solid, {1, 1, 1, 0.12f * a});
        m_r.Rect(IX(bx0), IY(by), IS((bx1 - bx0) * fill), IS(bh), UiMode::Solid,
                 sel ? Color{1.0f, 0.82f, 0.45f, a} : WithAlpha(kGold, 0.75f * a));
        m_r.Rect(IX(bx0 + (bx1 - bx0) * fill - 3), IY(by - 6), IS(6), IS(bh + 12), UiMode::Solid, WithAlpha(lc, a));
        m_r.Text(m_r.ui, r.value, IX(vx1 - 22), IY(ty + 2), IS(27), vc, Align::Right);
      }
      else if (r.arrows)
      {
        m_r.DrawIcon(Icon::ArrowLeft, IX(vx0), IY(y + 16), IS(22), WithAlpha(lc, sel ? 1.0f : 0.5f));
        m_r.DrawIcon(Icon::ArrowRight, IX(vx1), IY(y + 16), IS(22), WithAlpha(lc, sel ? 1.0f : 0.5f));
        FitText(m_r.ui, r.value, IX(vx0 + 40), IY(ty + 2), IS(vx1 - vx0 - 58), IS(27), vc, Align::Center, sel,
                m_opt_page * 100 + static_cast<int>(i) * 2 + 1);
      }
      else
      {
        FitText(m_r.ui, r.value, IX(vx0 - 20), IY(ty + 2), IS(vx1 + 6 - vx0), IS(27), vc, Align::Right, sel,
                m_opt_page * 100 + static_cast<int>(i) * 2 + 1);
      }
    }
    const int index = static_cast<int>(i);
    const bool bar = r.bar >= 0 && r.enabled;
    Hot(rr, 1, index, [this, index, id, vx0, vx1, bar](float mx, float) {
      m_opt_sel = index;
      const float bx0 = IX(vx0 + 44), bx1 = IX(vx1 - 96);
      if (bar && mx >= bx0 - 8 && mx <= bx1 + 8)
      {
        // Clic en la barra: un paso hacia ese lado del valor actual.
        OptionChange(id, mx < (bx0 + bx1) / 2 ? -1 : 1);
        return;
      }
      if (mx > IX(vx0 - 15) && mx < IX(vx0 + 40))
        OptionChange(id, -1);
      else if (mx > IX(vx1 - 15))
        OptionChange(id, 1);
      else
        OptionChange(id, 0);
    });
  }

  // Ayuda de la fila seleccionada.
  const char* help = "";
  switch (rows[m_opt_sel])
  {
    case OPT_GAME: help = L("Tu copia del Disco 1 (.cue, .bin, .chd…). La carpeta game/ del launcher se detecta sola.", "Your copy of Disc 1 (.cue, .bin, .chd…). The launcher's game/ folder is detected automatically."); break;
    case OPT_DISC2:
      help = L("El juego pide el Disco 2 a mitad de la historia. Se busca solo junto al Disco 1; con los dos, comparten "
               "tarjeta de memoria y se cambia desde la pausa (CAMBIAR DISCO) o con F8.",
               "The game asks for Disc 2 halfway through the story. It is found automatically next to Disc 1; with both, "
               "they share a memory card and you swap from the pause menu (SWAP DISC) or with F8.");
      break;
    case OPT_BIOS:
      help = L("OpenBIOS es libre y viene integrada. Si un juego da problemas, carga la BIOS de tu consola y elígela "
               "aquí.",
               "OpenBIOS is free and built in. If a game has problems, load your console's BIOS and choose it here.");
      break;
    case OPT_BIOS_LOAD: help = L("Copia una BIOS (archivo de 512 KB) a la carpeta bios/ y la deja elegida.", "Copies a BIOS (512 KB file) into the bios/ folder and selects it."); break;
    case OPT_FASTBOOT: help = L("Salta la animación de arranque de la consola.", "Skips the console's boot animation."); break;
    case OPT_HARDCORE:
      help = L("Como en la consola: BIOS original, sin estados guardados ni trucos; sólo la tarjeta de memoria. Los "
               "logros conseguidos así se marcan como hardcore.",
               "Like on the console: original BIOS, no save states or cheats; memory card only. Achievements earned "
               "this way are marked as hardcore.");
      break;
    case OPT_RENDERER: help = L("OpenGL permite alta resolución. Software es el más fiel al hardware original.", "OpenGL allows high resolution. Software is the most faithful to the original hardware."); break;
    case OPT_SCALE:
      help = L("Resolución de PlayStation multiplicada: 4x ≈ 960p, 5x ≈ 1080p. 3x no se ofrece porque en este núcleo "
               "puede corromper el texto.",
               "PlayStation resolution multiplied: 4x ≈ 960p, 5x ≈ 1080p. 3x is not offered because with this core it "
               "can corrupt text.");
      break;
    case OPT_TEXFILTER: help = L("xBR y JINC2 redibujan las texturas en alta definición. Original = píxeles tal cual.", "xBR and JINC2 redraw textures in high definition. Original = pixels as they are."); break;
    case OPT_PGXP:
      help = L("Evita que polígonos y texturas tiemblen o «respiren». Máxima corrige también el suelo y el "
               "escenario (usa algo más de CPU).",
               "Stops polygons and textures from wobbling or “breathing”. Maximum also fixes the floor and the "
               "scenery (uses a bit more CPU).");
      break;
    case OPT_WIDESCREEN:
      help = L("4:3 es el formato original. 16:9 usa un parche del propio juego (PeterDelta): más campo de visión "
               "con la imagen sin deformar.",
               "4:3 is the original format. 16:9 uses a patch of the game itself (PeterDelta): a wider field of view "
               "without stretching the image.");
      break;
    case OPT_SIDEART:
      help = L("Con el juego en 4:3, los lados muestran la ilustración del launcher oscurecida, la propia imagen del "
               "juego desenfocada (ambiente) o negro.",
               "With the game in 4:3, the sides show the launcher artwork dimmed, the game's own image blurred "
               "(ambient), or black.");
      break;
    case OPT_SCREENFILTER:
      help = L("Líneas de barrido: las líneas horizontales de una tele antigua. CRT: además, la curvatura del tubo, la "
               "máscara de colores y la viñeta. Se puede cambiar en plena partida.",
               "Scanlines: the horizontal lines of an old TV. CRT: also the tube's curvature, the colour mask and "
               "the vignette. It can be changed mid-game.");
      break;
    case OPT_SMOOTH: help = L("Suaviza la imagen al escalarla a la ventana (No = píxeles nítidos).", "Smooths the image when scaling it to the window (No = sharp pixels)."); break;
    case OPT_FULLSCREEN: help = L("También con F11 o Alt+Enter.", "Also with F11 or Alt+Enter."); break;
    case OPT_VOLUME: help = L("Volumen del sonido del juego.", "Volume of the game's sound."); break;
    case OPT_MUSIC: help = L("Música del launcher (menús y pausa).", "Launcher music (menus and pause)."); break;
    case OPT_NTSC:
      help = L("La versión europea va a 50 Hz: más lenta y con bandas negras. El parche de PeterDelta la pone a 60 Hz, "
               "como la americana y la japonesa (sube la velocidad de la CPU emulada). Se aplica al empezar la partida.",
               "The European version runs at 50 Hz: slower and letterboxed. PeterDelta's patch makes it 60 Hz, like the "
               "US and Japanese ones (it raises the emulated CPU speed). Applied when the game starts.");
      break;
    case OPT_AUTOSAVE:
      help = L("Al volver al menú o salir, la partida se guarda en la ranura de autoguardado (además de las tres "
               "ranuras manuales de la pausa). CONTINUAR carga la más reciente.",
               "When you return to the menu or quit, the game is saved in the autosave slot (besides the three manual "
               "slots in the pause menu). CONTINUE loads the most recent one.");
      break;
    case OPT_SUBTITLES:
      help = L("Texto de las escenas (parche de PeterDelta para quitarlo). Se aplica al empezar la partida.",
               "Cutscene text (PeterDelta's patch removes it). Applied when the game starts.");
      break;
    case OPT_SFX: help = L("Sonidos del launcher al moverte, elegir y cancelar.", "Launcher sounds when moving, choosing and cancelling."); break;
    case OPT_PROMPTS: help = L("Iconos de botones del launcher. Automático cambia según uses teclado o mando.", "Launcher button icons. Automatic switches depending on whether you use keyboard or controller."); break;
    case OPT_CONTROLS: help = L("Teclado, mando de Xbox y mando de PlayStation, con tus asignaciones.", "Keyboard, Xbox controller and PlayStation controller, with your bindings."); break;
    case OPT_REMAP_KB: help = L("Cambia qué tecla corresponde a cada botón. No afecta al mando.", "Changes which key maps to each button. Does not affect the controller."); break;
    case OPT_REMAP_PAD: help = L("Cambia qué botón del mando corresponde a cada botón. No afecta al teclado.", "Changes which controller button maps to each button. Does not affect the keyboard."); break;
    case OPT_RESET_CONTROLS: help = L("Vuelve a la asignación original de teclado y mando (pide confirmación).", "Goes back to the original keyboard and controller bindings (asks for confirmation)."); break;
    default: help = ""; break;
  }
  float ly = row0 + rows.size() * rh + 60;
  for (const std::string& line : Wrap(m_r.ui, help, IS(24), IS(pw - 100)))
  {
    if (ly > py + ph - 90)
      break;
    m_r.Text(m_r.ui, line, IX(px + 52), IY(ly), IS(24), {0.84f, 0.81f, 0.78f, 0.95f});
    ly += 33;
  }
  if (m_opt_page == CAT_GRAPHICS)
    m_r.Text(m_r.ui, L("Los cambios de imagen se aplican la próxima vez que inicies el juego.", "Graphics changes apply the next time you start the game."), IX(px + 52),
             IY(py + ph - 64), IS(20), kDim);
  RenderHints({{PsButton::Accept, L("Aceptar", "Accept")}, {PsButton::Back, L("Volver", "Back")}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderRemap()
{
  DimScreen(0.5f);
  const float px = 40, py = 56, pw = 900, ph = 950;
  Panel(px, py, pw, ph);
  const bool kb = (m_remap_device == 0);
  m_r.Text(m_r.ui, L("OPCIONES  ·  CONTROLES", "OPTIONS  ·  CONTROLS"), IX(px + 48), IY(py + 18), IS(22), kDim);
  m_r.Text(m_r.brush, kb ? L("TECLADO", "KEYBOARD") : L("MANDO", "CONTROLLER"), IX(px + 44), IY(py + 42), IS(52), kWhite);
  m_r.TextCentered(m_r.display, OptCategoryNumber(CAT_CONTROLS), IX(px + pw - 90), IY(py + 70), IS(84), {1, 1, 1, 0.14f});

  // Estilo de los iconos del mando: el elegido en Opciones o el del mando conectado.
  PromptStyle style = PromptStyle::Keyboard;
  if (!kb)
    style = m_cfg.prompt_style == "playstation" ? PromptStyle::PlayStation
            : m_cfg.prompt_style == "xbox"      ? PromptStyle::Xbox
                                                : PadStyle();
  // Botón de PS1 de cada índice de asignación, para reutilizar los iconos.
  auto as_button = [](int id) -> PsButton {
    switch (id)
    {
      case RETRO_DEVICE_ID_JOYPAD_UP: return PsButton::DUp;
      case RETRO_DEVICE_ID_JOYPAD_DOWN: return PsButton::DDown;
      case RETRO_DEVICE_ID_JOYPAD_LEFT: return PsButton::DLeft;
      case RETRO_DEVICE_ID_JOYPAD_RIGHT: return PsButton::DRight;
      case RETRO_DEVICE_ID_JOYPAD_B: return PsButton::Cross;
      case RETRO_DEVICE_ID_JOYPAD_A: return PsButton::Circle;
      case RETRO_DEVICE_ID_JOYPAD_Y: return PsButton::Square;
      case RETRO_DEVICE_ID_JOYPAD_X: return PsButton::Triangle;
      case RETRO_DEVICE_ID_JOYPAD_L: return PsButton::L1;
      case RETRO_DEVICE_ID_JOYPAD_R: return PsButton::R1;
      case RETRO_DEVICE_ID_JOYPAD_L2: return PsButton::L2;
      case RETRO_DEVICE_ID_JOYPAD_R2: return PsButton::R2;
      case RETRO_DEVICE_ID_JOYPAD_L3: return PsButton::L3;
      case RETRO_DEVICE_ID_JOYPAD_R3: return PsButton::R3;
      case RETRO_DEVICE_ID_JOYPAD_START: return PsButton::Start;
      default: return PsButton::Select;
    }
  };

  const float colx[2] = {px + 30, px + 465};
  const float cw = 405, y0 = py + 140, rh = 62;
  for (int i = 0; i < kPsButtons; i++)
  {
    const int id = kRemapOrder[i];
    const float x = colx[i / 8], y = y0 + (i % 8) * rh;
    const bool sel = (i == m_remap_sel);
    const bool waiting = sel && m_remap_wait == id;
    const Rect rr = IR({x, y, cw, rh - 8});
    if (sel)
      m_r.Sprite(m_row_sel, rr.x - IS(8), rr.y - IS(6), rr.w + IS(16), rr.h + IS(12), {1, 1, 1, 0.95f});
    const Color lc = sel ? kInk : Color{0.86f, 0.86f, 0.86f, 1};
    m_r.Text(m_r.ui, PsButtonName(id), rr.x + IS(20), rr.y + IS(8), IS(28), lc);
    if (waiting)
    {
      const float pulse = 0.6f + 0.4f * std::sin(m_time * 8.0f);
      const std::string msg = std::string(kb ? L("Pulsa una tecla", "Press a key") : L("Pulsa un botón", "Press a button")) + "… (" +
                              std::to_string(static_cast<int>(std::ceil(m_remap_timer))) + ")";
      m_r.Text(m_r.ui, msg, rr.x + rr.w - IS(20), rr.y + IS(10), IS(24), WithAlpha(kRed, pulse), Align::Right);
    }
    else
    {
      const float gh = IS(38);
      const float gw = DrawGlyph(as_button(id), 0, 0, gh, style, false);
      const float max_w = rr.w - IS(170);
      if (gw <= max_w)
        DrawGlyph(as_button(id), rr.x + rr.w - IS(18) - gw, rr.y + (rr.h - gh) / 2, gh, style);
      else
        FitText(m_r.ui, kb ? KeyLabel(m_cfg.key_bind[id]) : "…", rr.x + IS(150), rr.y + IS(10), max_w, IS(24),
                lc, Align::Right, sel, 20000 + i);
    }
    Hot(rr, 7, i, [this, i](float, float) {
      m_remap_sel = i;
      Accept();
    });
  }

  const char* extra[2] = {L("Restaurar por defecto", "Restore defaults"), L("Volver", "Back")};
  for (int k = 0; k < 2; k++)
  {
    const int i = kPsButtons + k;
    const float y = y0 + 8 * rh + 22 + k * rh;
    const bool sel = (i == m_remap_sel);
    const Rect rr = IR({px + 30, y, pw - 60, rh - 8});
    if (sel)
      m_r.Sprite(m_row_sel, rr.x - IS(8), rr.y - IS(6), rr.w + IS(16), rr.h + IS(12), {1, 1, 1, 0.95f});
    m_r.Text(m_r.ui, extra[k], rr.x + IS(20), rr.y + IS(8), IS(28), sel ? kInk : Color{0.86f, 0.86f, 0.86f, 1});
    Hot(rr, 7, i, [this, i](float, float) {
      m_remap_sel = i;
      Accept();
    });
  }

  std::string help;
  if (m_remap_wait >= 0)
    help = kb ? L("Pulsa la tecla nueva. Esc cancela. Las teclas F y Esc están reservadas.",
                  "Press the new key. Esc cancels. The F keys and Esc are reserved.")
              : L("Pulsa el botón nuevo en el mando. Esc o un clic cancelan.",
                  "Press the new button on the controller. Esc or a click cancels.");
  else
    help = L("Elige un botón y pulsa Aceptar para cambiarlo. Si la tecla ya la usa otro botón, se intercambian. "
             "Los menús del launcher siempre se manejan con las flechas, Enter y Esc (o cruceta, A y B).",
             "Choose a button and press Accept to change it. If another button already uses that key, they are "
             "swapped. The launcher menus are always driven with the arrows, Enter and Esc (or D-pad, A and B).");
  if (!kb && !m_pad)
    help += L("\nNo hay ningún mando conectado.", "\nNo controller connected.");
  float ly = y0 + 10 * rh + 50;
  for (const std::string& line : Wrap(m_r.ui, help, IS(23), IS(pw - 90)))
  {
    m_r.Text(m_r.ui, line, IX(px + 48), IY(ly), IS(23), {0.84f, 0.81f, 0.78f, 0.95f});
    ly += 31;
  }
  RenderHints({{PsButton::Accept, L("Cambiar", "Change")}, {PsButton::Back, L("Volver", "Back")}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderCheats()
{
  DimScreen(0.5f);
  const float px = 40, py = 56, pw = 900, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.ui, L("OPCIONES", "OPTIONS"), IX(px + 48), IY(py + 18), IS(22), kDim);
  m_r.Text(m_r.brush, L("TRUCOS", "CHEATS"), IX(px + 44), IY(py + 42), IS(52), kWhite);
  m_r.TextCentered(m_r.display, OptCategoryNumber(CAT_CHEATS), IX(px + pw - 90), IY(py + 70), IS(84), {1, 1, 1, 0.14f});

  const int n = static_cast<int>(m_cheat_list.size());
  int active = 0;
  for (const Cheat& c : m_cheat_list)
    active += CheatEnabled(c.name);
  const std::string state = n == 0 ? "" : active == 0 ? L("Ningún truco activo", "No cheats active")
                                                       : std::to_string(active) + (active == 1 ? L(" truco activo", " cheat active")
                                                                                               : L(" trucos activos", " cheats active"));
  m_r.Text(m_r.ui, state, IX(px + pw - 40), IY(py + 118), IS(22), active ? kGold : kDim, Align::Right);
  m_r.Text(m_r.ui, active ? L("Los logros se pausarán en la próxima partida.", "Achievements will be paused in the next game.") : L("Se aplican al iniciar la partida.", "They apply when the game starts."),
           IX(px + 48), IY(py + 118), IS(22), active ? Color{1.0f, 0.75f, 0.3f, 0.95f} : kDim);

  const float top = py + 160, view_h = 560, rh = 56;
  if (n == 0)
  {
    for (const std::string& line : Wrap(m_r.ui,
                                        L("No hay trucos disponibles para este disco.",
                                          "No cheats available for this disc."),
                                        IS(26), IS(pw - 100)))
    {
      m_r.Text(m_r.ui, line, IX(px + 52), IY(top), IS(26), kGrey);
    }
  }
  m_r.SetClip(IX(px + 10), IY(top), IS(pw - 20), IS(view_h));
  for (int i = 0; i < n; i++)
  {
    const float y = top + i * rh - m_cheat_scroll;
    if (y + rh < top || y > top + view_h)
      continue;
    const Cheat& c = m_cheat_list[i];
    const bool sel = (i == m_cheat_sel), on = CheatEnabled(c.name);
    const Rect rr = IR({px + 22, y, pw - 44, rh - 6});
    if (sel)
      m_r.Sprite(m_row_sel, rr.x - IS(10), rr.y - IS(7), rr.w + IS(20), rr.h + IS(14), {1, 1, 1, 0.95f});
    const Color lc = sel ? kInk : on ? kWhite : Color{0.8f, 0.8f, 0.8f, 1};
    FitText(m_r.ui, c.Label(), rr.x + IS(30), rr.y + IS(8), rr.w - IS(200), IS(27), lc, Align::Left, sel, 30000 + i);
    m_r.Text(m_r.ui, on ? L("Activado", "On") : "No", rr.x + rr.w - IS(26), rr.y + IS(9), IS(25),
             sel ? kInk : on ? kGold : Color{0.6f, 0.6f, 0.6f, 1}, Align::Right);
    Hot(rr, 8, i, [this, i](float, float) {
      m_cheat_sel = i;
      Accept();
    });
  }
  m_r.ClearClip();

  const char* extra[2] = {L("Desactivar todos", "Disable all"), L("Volver", "Back")};
  for (int k = 0; k < 2; k++)
  {
    const int i = n + k;
    const float y = top + view_h + 20 + k * rh;
    const bool sel = (i == m_cheat_sel);
    const Rect rr = IR({px + 22, y, pw - 44, rh - 6});
    if (sel)
      m_r.Sprite(m_row_sel, rr.x - IS(10), rr.y - IS(7), rr.w + IS(20), rr.h + IS(14), {1, 1, 1, 0.95f});
    m_r.Text(m_r.ui, extra[k], rr.x + IS(30), rr.y + IS(8), IS(27), sel ? kInk : Color{0.86f, 0.86f, 0.86f, 1});
    Hot(rr, 8, i, [this, i](float, float) {
      m_cheat_sel = i;
      Accept();
    });
  }
  m_r.Text(m_r.ui, L("Trucos de la base de datos comunitaria de DuckStation (chtdb).", "Cheats from DuckStation's community database (chtdb)."), IX(px + 48), IY(py + ph - 60),
           IS(19), kDim);
  RenderHints({{PsButton::Accept, L("Activar / desactivar", "Toggle")}, {PsButton::Back, L("Volver", "Back")}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderCredits()
{
  DimScreen(0.55f);
  const float px = 40, py = 56, pw = 1100, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.brush, L("CRÉDITOS", "CREDITS"), IX(px + 44), IY(py + 20), IS(56), kWhite);
  m_r.Sprite(m_ic_credits, IX(px + pw - 110), IY(py + 30), IS(50), IS(62), {1, 1, 1, 0.16f});

  struct Line
  {
    int kind; // 0 sección, 1 nombre + función, 2 título, 3 nota
    std::string a, b;
  };
  std::vector<Line> lines = {
    {2, "SILVER GEAR", ""},
    {3, std::string(L("Launcher de Metal Gear Solid  ·  ", "Metal Gear Solid launcher  ·  ")) + kVersion, ""},
    {0, L("PROYECTO", "PROJECT"), ""},
    {1, "daetrox", L("Idea, dirección, diseño e ilustración del launcher", "Launcher idea, direction, design and artwork")},
    {1, "Claude (Anthropic)", L("Programación, en colaboración con daetrox", "Programming, in collaboration with daetrox")},
    {1, L("Composición original", "Original composition"), L("Música y efectos de sonido, sintetizados por el propio launcher", "Music and sound effects, synthesised by the launcher itself")},
    {0, L("EMULACIÓN", "EMULATION"), ""},
    {1, "SwanStation", L("Núcleo de PlayStation para libretro, derivado de DuckStation  ·  GPL-3.0", "PlayStation core for libretro, derived from DuckStation  ·  GPL-3.0")},
    {1, L("Connor McLaughlin (stenzek) y colaboradores", "Connor McLaughlin (stenzek) and contributors"), L("Autores de DuckStation, base de SwanStation", "Authors of DuckStation, the basis of SwanStation")},
    {1, "OpenBIOS", L("BIOS libre de PCSX-Redux  ·  autores de PCSX-Redux  ·  MIT", "Free BIOS from PCSX-Redux  ·  PCSX-Redux authors  ·  MIT")},
    {1, "libretro", L("API de núcleos de emulación  ·  equipo de libretro", "Emulation core API  ·  libretro team")},
    {0, L("LOGROS", "ACHIEVEMENTS"), ""},
    {1, "RetroAchievements.org", L("Plataforma y base de datos de logros", "Achievement platform and database")},
    {1, "rcheevos", L("Biblioteca de logros de RetroAchievements  ·  MIT", "RetroAchievements achievement library  ·  MIT")},
  };
  if (m_ach.GetState() == Achievements::State::Ready)
  {
    std::string authors;
    for (const std::string& a : m_ach.Authors())
      authors += (authors.empty() ? "" : ", ") + a;
    if (!authors.empty())
      lines.push_back({1, authors, L("Autoría del set de logros de ", "Achievement set for ") + m_ach.GameTitle()});
  }
  const std::vector<Line> rest = {
    {0, L("FONDO DEL MENÚ", "MENU BACKGROUND"), ""},
    {1, "daetrox", L("Ilustración de la base nevada", "Snowy base illustration")},
    {0, L("PARCHES DEL JUEGO", "GAME PATCHES"), ""},
    {1, "PeterDelta", L("16:9 nativo, modo NTSC (60 Hz) y subtítulos  ·  base de datos chtdb de DuckStation", "Native 16:9, NTSC mode (60 Hz) and subtitles  ·  DuckStation's chtdb database")},
    {0, L("TRUCOS", "CHEATS"), ""},
    {1, L("Comunidad de chtdb (DuckStation)", "chtdb community (DuckStation)"), L("Códigos de trucos de Metal Gear Solid, de sus respectivos autores", "Metal Gear Solid cheat codes, by their respective authors")},
    {0, L("BIBLIOTECAS", "LIBRARIES"), ""},
    {1, "SDL2", L("Sam Lantinga y colaboradores  ·  zlib", "Sam Lantinga and contributors  ·  zlib")},
    {1, "stb_truetype, stb_image, stb_vorbis", L("Sean Barrett  ·  dominio público", "Sean Barrett  ·  public domain")},
    {0, L("TIPOGRAFÍAS", "TYPEFACES"), ""},
    {1, "Russo One", "Jovanny Lemonad  ·  SIL OFL 1.1"},
    {1, "Oswald", "The Oswald Project Authors  ·  SIL OFL 1.1"},
    {0, L("CON RESPETO A", "WITH RESPECT TO"), ""},
    {1, L("Hideo Kojima, KCE Japan y Konami", "Hideo Kojima, KCE Japan and Konami"), L("Creadores de Metal Gear Solid (1998)", "Creators of Metal Gear Solid (1998)")},
    {3, L("Metal Gear Solid es una marca registrada de Konami Digital Entertainment.", "Metal Gear Solid is a registered trademark of Konami Digital Entertainment."), ""},
    {3, L("Este proyecto no está afiliado a ellos.", "This project is not affiliated with them."), ""},
    {3, L("Software libre (GPL-3.0)  ·  sin fines de lucro", "Free software (GPL-3.0)  ·  non-profit"), ""},
  };
  lines.insert(lines.end(), rest.begin(), rest.end());

  // Texto centrado que sube; se recorta al interior del panel.
  const float cx = px + pw / 2, top = py + 110, bottom = py + ph - 80;
  m_r.SetClip(IX(px + 10), IY(top), IS(pw - 20), IS(bottom - top));
  float y = top + 40 - m_credits_scroll;
  for (const Line& l : lines)
  {
    switch (l.kind)
    {
      case 0:
        y += 40;
        m_r.Text(m_r.brush, l.a, IX(cx), IY(y), IS(30), kSilver, Align::Center);
        m_r.Rect(IX(cx - 60), IY(y + 52), IS(120), IS(2), UiMode::Solid, WithAlpha(kSilver, 0.7f));
        y += 74;
        break;
      case 1:
        m_r.Text(m_r.ui, Truncate(m_r.ui, l.a, IS(29), IS(pw - 120)), IX(cx), IY(y), IS(29), kWhite, Align::Center);
        m_r.Text(m_r.ui, Truncate(m_r.ui, l.b, IS(21), IS(pw - 120)), IX(cx), IY(y + 40), IS(21), kGrey,
                 Align::Center);
        y += 86;
        break;
      case 2:
        m_r.Text(m_r.brush, l.a, IX(cx), IY(y), IS(54), kWhite, Align::Center);
        y += 80;
        break;
      default:
        m_r.Text(m_r.ui, l.a, IX(cx), IY(y), IS(21), kDim, Align::Center);
        y += 34;
        break;
    }
  }
  m_r.ClearClip();
  m_credits_height = y + m_credits_scroll - top;
  // Fundidos arriba y abajo para que el texto entre y salga suavemente.
  for (int k = 0; k < 12; k++)
  {
    const float a = 0.9f * (1.0f - k / 12.0f);
    m_r.Rect(IX(px + 10), IY(top + k * 4), IS(pw - 20), IS(4), UiMode::Solid, WithAlpha(kPanelBg, a / 0.92f));
    m_r.Rect(IX(px + 10), IY(bottom - (k + 1) * 4), IS(pw - 20), IS(4), UiMode::Solid, WithAlpha(kPanelBg, a / 0.92f));
  }
  RenderHints({{PsButton::Back, L("Volver", "Back")}}, IX(px + pw - 30), IY(py + ph - 64));
}

void App::RenderConfirmReset()
{
  DimScreen(0.6f);
  const float x = 450, y = 300, w = 680, h = 470;
  Panel(x, y, w, h);
  m_r.Text(m_r.brush, L("REINICIAR LOGROS", "RESET ACHIEVEMENTS"), IX(x + 44), IY(y + 24), IS(44), kWhite);
  const int n = m_ach.EarnedCount();
  const std::string msg =
    LangEn() ? (n == 1 ? std::string("The unlocked achievement") : "The " + std::to_string(n) + " unlocked achievements") +
                 " on this computer will be deleted. This cannot be undone."
             : (n == 1 ? std::string("Se borrará el logro desbloqueado")
                       : "Se borrarán los " + std::to_string(n) + " logros desbloqueados") +
                 " en este equipo. No se puede deshacer.";
  float ty = y + 96;
  for (const std::string& line : Wrap(m_r.ui, msg, IS(25), IS(w - 90)))
  {
    m_r.Text(m_r.ui, line, IX(x + 46), IY(ty), IS(25), kGrey);
    ty += 34;
  }
  const char* labels[2] = {L("REINICIAR", "RESET"), L("CANCELAR", "CANCEL")};
  const Texture* icons[2] = {&m_ic_ach, &m_ic_quit};
  for (int i = 0; i < 2; i++)
  {
    const Rect slot = {x + 100, y + 200 + i * 100.0f, 462, 86};
    DrawMenuButton(slot, labels[i], icons[i], m_sel_anim[i], 34, true);
    Hot(IR(slot), 3, i, [this, i](float, float) {
      m_choice_sel = i;
      Accept();
    });
  }
  RenderHints({{PsButton::Accept, L("Aceptar", "Accept")}, {PsButton::Back, L("Cancelar", "Cancel")}}, IX(x + w - 30), IY(y + h + 16));
}

void App::RenderControls()
{
  DimScreen(m_emu.IsRunning() ? 0.75f : 0.5f);
  const float px = 40, py = 56, pw = 1060, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.brush, L("CONTROLES", "CONTROLS"), IX(px + 44), IY(py + 20), IS(56), kWhite);

  struct Row
  {
    const char* action;
    PsButton b;
  };
  const Row rows[] = {
    {L("Moverse", "Move"), PsButton::LStick}, {L("Cruceta", "D-pad"), PsButton::DPad},      {L("Cruz", "Cross"), PsButton::Cross},
    {L("Círculo", "Circle"), PsButton::Circle}, {L("Cuadrado", "Square"), PsButton::Square},   {L("Triángulo", "Triangle"), PsButton::Triangle},
    {"L1", PsButton::L1},          {"R1", PsButton::R1},             {"L2", PsButton::L2},
    {"R2", PsButton::R2},          {"Start", PsButton::Start},       {"Select", PsButton::Select},
    {L("Pausa del launcher", "Launcher pause"), PsButton::Pause},
  };
  const float c0 = px + 52, c1 = px + 430, c2 = px + 650, c3 = px + 860;
  m_r.Text(m_r.ui, L("BOTÓN DE PS1", "PS1 BUTTON"), IX(c0), IY(py + 104), IS(23), kDim);
  m_r.Text(m_r.ui, L("TECLADO", "KEYBOARD"), IX(c1), IY(py + 104), IS(23), kDim);
  m_r.Text(m_r.ui, "XBOX", IX(c2), IY(py + 104), IS(23), kDim);
  m_r.Text(m_r.ui, "PLAYSTATION", IX(c3), IY(py + 104), IS(23), kDim);
  float y = py + 146;
  for (const Row& r : rows)
  {
    m_r.Text(m_r.ui, r.action, IX(c0), IY(y + 1), IS(28), kWhite);
    DrawGlyph(r.b, IX(c1), IY(y), IS(40), PromptStyle::Keyboard);
    DrawGlyph(r.b, IX(c2), IY(y), IS(40), PromptStyle::Xbox);
    DrawGlyph(r.b, IX(c3), IY(y), IS(40), PromptStyle::PlayStation);
    y += 54;
  }
  m_r.Text(m_r.ui,
           m_cfg.hardcore ? L("F8 cambiar de disco  ·  F11 pantalla completa  ·  modo hardcore: sin F5 / F7",
                              "F8 swap disc  ·  F11 fullscreen  ·  hardcore mode: no F5 / F7")
                          : L("F5 guardar  ·  F7 cargar  ·  F8 cambiar de disco  ·  F11 pantalla completa",
                              "F5 save  ·  F7 load  ·  F8 swap disc  ·  F11 fullscreen"),
           IX(c0), IY(y + 14), IS(22), kDim);

  // Ayudas sin destripes, en una pantalla de códec a la derecha.
  const float hx = px + pw + 30, hy = py, hw = 440, hh = 420;
  CodecBox(IX(hx), IY(hy), IS(hw), IS(hh));
  m_r.Text(m_r.mono, L("AYUDA SIN DESTRIPES", "SPOILER-FREE HELP"), IX(hx + 26), IY(hy + 20), IS(24), kCodec);
  float ty = hy + 70;
  auto para = [&](const std::string& title, const std::string& text) {
    m_r.Text(m_r.brush, title, IX(hx + 26), IY(ty), IS(24), kCodecText);
    ty += 40;
    for (const std::string& line : Wrap(m_r.ui, text, IS(22), IS(hw - 52)))
    {
      m_r.Text(m_r.ui, line, IX(hx + 26), IY(ty), IS(22), WithAlpha(kCodecText, 0.85f));
      ty += 30;
    }
    ty += 18;
  };
  para(L("Frecuencia de Meryl", "Meryl's frequency"),
       L("Está en la contraportada de la caja del juego.", "It's on the back of the game's case."));
  m_r.Text(m_r.mono, m_hint_reveal ? "140.15" : L("[ Aceptar: mostrar ]", "[ Accept: reveal ]"), IX(hx + 26), IY(ty - 10),
           IS(m_hint_reveal ? 44 : 22), kCodec);
  ty += m_hint_reveal ? 60 : 36;
  para("Psycho Mantis",
       L("Si te lee la mente, haz lo que se hacía en la consola: cambia el mando al puerto 2 desde la pausa "
         "(MANDO: PUERTO 2) y vuelve al 1 al terminar.",
         "If he reads your mind, do what you did on the console: move the controller to port 2 from the pause "
         "menu (PAD: PORT 2) and switch back to 1 afterwards."));
  RenderHints({{PsButton::Accept, L("Mostrar frecuencia", "Reveal frequency")}, {PsButton::Back, L("Volver", "Back")}},
              IX(px + pw - 30), IY(py + ph - 64));
}

void App::RenderAchievements()
{
  DimScreen(0.5f);
  const float px = 40, py = 56, pw = 1100, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Sprite(m_ic_ach, IX(px + 40), IY(py + 22), IS(62), IS(62), kWhite);
  m_r.Text(m_r.brush, L("LOGROS", "ACHIEVEMENTS"), IX(px + 116), IY(py + 20), IS(56), kWhite);

  const Achievements::State st = m_ach.GetState();
  if (st == Achievements::State::Ready)
  {
    auto& list = m_ach.List();
    const int earned = m_ach.EarnedCount();
    m_r.Text(m_r.ui, Truncate(m_r.ui, m_ach.GameTitle(), IS(26), IS(pw - 420)), IX(px + pw - 40), IY(py + 24), IS(26),
             kGrey, Align::Right);
    const std::string prog = std::string(m_cfg.hardcore ? L("Modo hardcore  ·  ", "Hardcore mode  ·  ") : "") +
                             std::to_string(earned) + L(" de ", " of ") + std::to_string(list.size()) +
                             L(" logros (", " achievements (") + std::to_string(m_ach.HardcoreCount()) +
                             " hardcore)  ·  " + std::to_string(m_ach.EarnedPoints()) + " / " +
                             std::to_string(m_ach.TotalPoints()) + L(" puntos", " points");
    m_r.Text(m_r.ui, prog, IX(px + pw - 40), IY(py + 60), IS(24), kGold, Align::Right);
    const float bx = px + 44, by = py + 104, bw = pw - 88, bh = 10;
    m_r.Rect(IX(bx), IY(by), IS(bw), IS(bh), UiMode::Solid, {1, 1, 1, 0.1f});
    const float frac = list.empty() ? 0.0f : static_cast<float>(earned) / list.size();
    m_r.Rect(IX(bx), IY(by), IS(bw * frac), IS(bh), UiMode::Solid, kSilver);

    const float top = py + 132, view_h = 700, row_h = 88;
    // Las filas que asoman por arriba o por abajo se recortan contra el borde de la lista.
    m_r.SetClip(IX(px + 20), IY(top), IS(pw - 40), IS(view_h));
    for (size_t i = 0; i < list.size(); i++)
    {
      const float y = top + i * row_h - m_ach_scroll;
      if (y + row_h < top || y > top + view_h)
        continue;
      Achievement& a = list[i];
      const bool sel = static_cast<int>(i) == m_ach_sel;
      const Rect rr = IR({px + 30, y, pw - 60, row_h - 8});
      m_r.Rect(rr.x, rr.y, rr.w, rr.h, UiMode::Solid, sel ? Color{1, 1, 1, 0.16f} : Color{1, 1, 1, 0.04f});
      Hot(rr, 6, static_cast<int>(i), [this, i](float, float) { m_ach_sel = static_cast<int>(i); });
      m_ach.LoadIcon(a, m_r);
      const float isz = IS(64);
      if (a.icon.id)
        m_r.Sprite(a.icon, rr.x + IS(10), rr.y + IS(8), isz, isz, {1, 1, 1, 1}, !a.unlocked);
      else
        m_r.Rect(rr.x + IS(10), rr.y + IS(8), isz, isz, UiMode::Solid, {0.2f, 0.2f, 0.2f, 1});
      const Color tc = a.unlocked ? kWhite : Color{0.72f, 0.7f, 0.68f, 1};
      const float text_w = rr.w - IS(92) - IS(150);
      FitText(m_r.ui, a.Title(), rr.x + IS(92), rr.y + IS(4), text_w, IS(27), tc, Align::Left, sel,
              10000 + static_cast<int>(i) * 2);
      FitText(m_r.ui, a.Description(), rr.x + IS(92), rr.y + IS(42), text_w, IS(21), kDim, Align::Left, sel,
              10000 + static_cast<int>(i) * 2 + 1);
      m_r.Text(m_r.ui, std::to_string(a.points) + " pts", rr.x + rr.w - IS(20), rr.y + IS(8), IS(23),
               a.unlocked ? kGold : kDim, Align::Right);
      std::string when = L("Bloqueado", "Locked");
      if (a.unlocked)
      {
        char date[32];
        const time_t t = static_cast<time_t>(a.unlock_time);
        std::strftime(date, sizeof(date), L("%d/%m/%Y", "%m/%d/%Y"), std::localtime(&t));
        when = std::string(a.hardcore ? "Hardcore · " : "") + date;
      }
      m_r.Text(m_r.ui, when, rr.x + rr.w - IS(20), rr.y + IS(44), IS(19), kDim, Align::Right);
    }
    // Última fila: reiniciar los logros conseguidos (pide confirmación).
    {
      const size_t n = list.size();
      const float y = top + n * row_h - m_ach_scroll;
      if (y + row_h >= top && y <= top + view_h)
      {
        const bool sel = static_cast<int>(n) == m_ach_sel;
        const Rect rr = IR({px + 30, y, pw - 60, row_h - 8});
        if (sel)
          m_r.Sprite(m_row_sel, rr.x - IS(8), rr.y - IS(4), rr.w + IS(16), rr.h + IS(8), {1, 1, 1, 0.95f});
        else
          m_r.Rect(rr.x, rr.y, rr.w, rr.h, UiMode::Solid, {0.4f, 0.03f, 0.02f, 0.25f});
        m_r.DrawIcon(Icon::Close, rr.x + IS(30), rr.y + IS(24), IS(32), sel ? kInk : kRed);
        m_r.Text(m_r.ui, L("Reiniciar mis logros", "Reset my achievements"), rr.x + IS(92), rr.y + IS(4), IS(27), sel ? kInk : kGrey);
        m_r.Text(m_r.ui, L("Borra los logros desbloqueados en este equipo (pide confirmación).", "Deletes the achievements unlocked on this computer (asks for confirmation)."), rr.x + IS(92),
                 rr.y + IS(42), IS(21), sel ? WithAlpha(kInk, 0.8f) : kDim);
        Hot(rr, 6, static_cast<int>(n), [this, n](float, float) {
          m_ach_sel = static_cast<int>(n);
          Accept();
        });
      }
    }
    m_r.ClearClip();
    m_r.Text(m_r.ui, L("Los logros se desbloquean sin conexión y se guardan sólo en este equipo.", "Achievements unlock offline and are saved only on this computer."), IX(px + 44),
             IY(py + ph - 58), IS(21), kDim);
    RenderHints({{PsButton::Back, L("Volver", "Back")}}, IX(px + pw - 30), IY(py + ph - 64));
    return;
  }

  // Sin set descargado: estado y, si procede, formulario de inicio de sesión.
  float y = py + 130;
  for (const std::string& line : Wrap(m_r.ui, m_ach.Status(), IS(27), IS(pw - 100)))
  {
    m_r.Text(m_r.ui, line, IX(px + 52), IY(y), IS(27), st == Achievements::State::Working ? kGold : kGrey);
    y += 38;
  }
  RenderHints({{PsButton::Back, L("Volver", "Back")}}, IX(px + pw - 30), IY(py + ph - 64));
  if (st == Achievements::State::Working)
  {
    m_r.DrawIcon(Icon::Shuriken, IX(px + pw / 2 - 45), IY(py + 420), IS(90), kSilver, 3.0f);
    return;
  }
  if (st != Achievements::State::NeedLogin)
    return;

  m_r.Text(m_r.ui, L("Cuenta de RetroAchievements (retroachievements.org)", "RetroAchievements account (retroachievements.org)"), IX(px + 52), IY(y + 24), IS(26), kWhite);
  const char* labels[2] = {L("Usuario", "Username"), L("Contraseña", "Password")};
  for (int i = 0; i < 2; i++)
  {
    const float fy = y + 76 + i * 104;
    const bool focus = m_login_focus == i;
    m_r.Text(m_r.ui, labels[i], IX(px + 52), IY(fy), IS(23), kGrey);
    const Rect field = IR({px + 52, fy + 34, 580, 56});
    m_r.Rect(field.x, field.y, field.w, field.h, UiMode::Solid,
             focus ? Color{0.12f, 0.12f, 0.13f, 0.95f} : Color{0.06f, 0.06f, 0.065f, 0.92f});
    m_r.Rect(field.x, field.y + field.h - IS(3), field.w, IS(3), UiMode::Solid,
             focus ? kSilver : Color{0.4f, 0.4f, 0.4f, 1});
    const std::string shown = i == 0 ? m_login_user : std::string(m_login_pass.size(), '*');
    const bool caret = focus && std::fmod(m_time, 1.0f) < 0.55f;
    // Si el texto no cabe se muestra su parte final (donde se escribe).
    std::string visible = shown + (caret ? "|" : "");
    while (visible.size() > 1 && m_r.TextWidth(m_r.ui, visible, IS(29)) > field.w - IS(32))
      visible.erase(0, 1);
    m_r.Text(m_r.ui, visible, field.x + IS(16), field.y + IS(9), IS(29), kWhite);
    Hot(field, 4, i, [this, i](float, float) { m_login_focus = i; });
  }
  const Rect slot = {px + 46, y + 300, 462, 88};
  DrawMenuButton(slot, L("DESCARGAR", "DOWNLOAD"), &m_ic_ach, m_sel_anim[2], 34, true);
  Hot(IR(slot), 4, 2, [this](float, float) {
    m_login_focus = 2;
    StartLogin();
  });
  float ny = y + 420;
  for (const char* line : {L("Tu contraseña sólo se usa para obtener un token de acceso y no se guarda.", "Your password is only used to obtain an access token and is not stored."),
                           L("Los logros se descargan una vez; después se juegan y desbloquean sin conexión.", "Achievements are downloaded once; after that they are played and unlocked offline.")})
  {
    m_r.Text(m_r.ui, line, IX(px + 52), IY(ny), IS(21), kDim);
    ny += 32;
  }
}

void App::RenderGame()
{
  if (!m_emu.HasFrame())
    return;
  const float aspect = m_emu.AspectRatio();
  float w = static_cast<float>(m_w), h = w / aspect;
  if (h > m_h)
  {
    h = static_cast<float>(m_h);
    w = h * aspect;
  }
  const float x = (m_w - w) / 2, y = (m_h - h) / 2;
  const float u1 = static_cast<float>(m_emu.FrameWidth()) / m_emu.TextureWidth();
  const float v1 = static_cast<float>(m_emu.FrameHeight()) / m_emu.TextureHeight();
  const float tv0 = m_emu.FlippedY() ? v1 : 0, tv1 = m_emu.FlippedY() ? 0 : v1;
  // Laterales (juego en 4:3 en una pantalla más ancha), en lugar de franjas negras:
  //   ilustración: la del launcher desenfocada y oscura; a la derecha, reflejada (sin el rótulo);
  //   ambiente: la propia imagen del juego, ampliada, desenfocada y oscura.
  if (m_cfg.side_mode == 2 && x > 2.0f)
  {
    const float cw = static_cast<float>(m_w), ch = cw / aspect;
    m_r.Image(m_emu.Texture(), 0, (m_h - ch) / 2, cw, ch, 0, tv0, u1, tv1, true, 3);
  }
  if (m_cfg.side_mode == 1 && x > 2.0f)
  {
    const float aw = m_h * kArtW / kArtH;
    m_r.SetClip(0, 0, x, static_cast<float>(m_h));
    m_r.DrawBackground(m_bg, 0, 0, aw, static_cast<float>(m_h), 0.32f, 2.5f);
    m_r.ClearClip();
    m_r.SetClip(x + w, 0, m_w - x - w, static_cast<float>(m_h));
    m_r.DrawBackground(m_bg, static_cast<float>(m_w), 0, -aw, static_cast<float>(m_h), 0.32f, 2.5f);
    m_r.ClearClip();
    // sombra suave junto a la imagen del juego
    const float sw = IS(40);
    m_r.Rect(x - sw, 0, sw, static_cast<float>(m_h), UiMode::Solid, {0, 0, 0, 0.5f});
    m_r.Rect(x + w, 0, sw, static_cast<float>(m_h), UiMode::Solid, {0, 0, 0, 0.5f});
  }
  // Líneas de la imagen original (sin la escala interna), para el filtro de líneas de barrido.
  const int scale = m_cfg.hardware_renderer ? std::max(1, m_cfg.resolution_scale) : 1;
  const float lines = std::max(120.0f, static_cast<float>(m_emu.FrameHeight()) / scale);
  m_r.Image(m_emu.Texture(), x, y, w, h, 0, tv0, u1, tv1, m_cfg.smooth_filter || m_cfg.screen_filter == 2,
            m_cfg.screen_filter, lines);

}

void App::RenderPause()
{
  DimScreen(0.62f);
  const float px = 50, py = 70, pw = 620, ph = 900;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.brush, L("PAUSA", "PAUSE"), IX(px + 44), IY(py + 26), IS(54), kWhite);
  const int discs = m_emu.DiscCount();
  const std::string disc = discs >= 2 ? (LangEn() ? "Disc " : "Disco ") + std::to_string(m_emu.CurrentDisc() + 1) + " / 2"
                                      : std::string(L("Un solo disco", "Single disc"));
  m_r.Text(m_r.ui, disc, IX(px + pw - 44), IY(py + 44), IS(24), kDim, Align::Right);
  const std::string port = m_emu.InputPort() == 1 ? L("MANDO: PUERTO 2", "PAD: PORT 2") : L("MANDO: PUERTO 1", "PAD: PORT 1");
  const char* labels[kPauseItems] = {L("REANUDAR", "RESUME"),    L("GUARDAR", "SAVE"),           L("CARGAR", "LOAD"),
                                     L("CAMBIAR DISCO", "SWAP DISC"), port.c_str(),           L("CONTROLES", "CONTROLS"),
                                     L("MENÚ PRINCIPAL", "MAIN MENU"), L("SALIR", "QUIT")};
  const Texture* icons[kPauseItems] = {&m_ic_play, &m_ic_credits, &m_ic_credits, &m_ic_disc,
                                       &m_ic_opt,  &m_ic_opt,     &m_ic_home,    &m_ic_quit};
  for (int i = 0; i < kPauseItems; i++)
  {
    const Rect slot = {px + 50, py + 112 + i * 86.0f, 513, 80};
    const bool blocked = (m_hardcore_session && (i == PAUSE_SAVE || i == PAUSE_LOAD)) || (i == PAUSE_DISC && discs < 2);
    DrawMenuButton(slot, labels[i], icons[i], m_sel_anim[i], 30, true, blocked ? 0.35f : 1.0f);
    Hot(IR(slot), 2, i, [this, i](float, float) { ActivatePause(i); });
  }
  const float fy = py + ph - 58;
  if (m_cheats_active)
    m_r.Text(m_r.ui, L("Logros en pausa: hay trucos activos", "Achievements paused: cheats are active"), IX(px + 60), IY(fy), IS(24), kDim);
  else if (m_ach.GetState() == Achievements::State::Ready)
    m_r.Text(m_r.ui,
             std::string(m_hardcore_session ? L("MODO HARDCORE  ·  ", "HARDCORE MODE  ·  ") : "") + L("Logros: ", "Achievements: ") +
               std::to_string(m_hardcore_session ? m_ach.HardcoreCount() : m_ach.EarnedCount()) + " / " +
               std::to_string(m_ach.List().size()),
             IX(px + 60), IY(fy), IS(24), kGold);
  else if (m_hardcore_session)
    m_r.Text(m_r.ui, L("MODO HARDCORE", "HARDCORE MODE"), IX(px + 60), IY(fy), IS(24), kGold);
  RenderHints({{PsButton::Accept, L("Aceptar", "Accept")}, {PsButton::Back, L("Reanudar", "Resume")}}, m_w - IS(30), m_h - IS(60));
}

void App::RenderDialog()
{
  DimScreen(0.55f);
  const float w = 800, h = 380;
  const float x = std::max(40.0f, (m_w / m_s - w) / 2), y = (kImgH - h) / 2;
  CodecBox(IX(x), IY(y), IS(w), IS(h));
  m_r.Text(m_r.mono, "CODEC  ·  140.85", IX(x + 30), IY(y + 18), IS(22), WithAlpha(kCodec, 0.8f));
  m_r.TextCentered(m_r.display, "!", IX(x + w - 70), IY(y + 80), IS(90), WithAlpha(kRed, 0.9f));
  float ty = y + 62;
  for (const std::string& line : Wrap(m_r.ui, m_dialog, IS(27), IS(w - 160)))
  {
    m_r.Text(m_r.ui, line, IX(x + 46), IY(ty), IS(27), kCodecText);
    ty += 38;
  }
  RenderHints({{PsButton::Accept, L("Aceptar", "Accept")}}, IX(x + w - 40), IY(y + h - 66));
}

void App::RenderToast()
{
  if (m_toast_time <= 0)
    return;
  const float a = std::min(1.0f, m_toast_time * 2.0f);
  const float fs = IS(28);
  const std::string text = Truncate(m_r.ui, m_toast, fs, m_w * 0.7f);
  const float w = m_r.TextWidth(m_r.ui, text, fs) + IS(90);
  const float x = (m_w - w) / 2, y = m_h - IS(170);
  CodecBox(x, y, w, IS(72), a);
  m_r.Text(m_r.ui, text, m_w / 2.0f, y + IS(15), fs, WithAlpha(kCodecText, a), Align::Center);
}

void App::RenderPopups()
{
  float y = IS(30);
  for (const Popup& p : m_popups)
  {
    Achievement* a = m_ach.Find(p.id);
    if (!a)
      continue;
    const float t = 6.0f - p.time;
    const float in = std::min(1.0f, t * 4.0f), out = std::min(1.0f, p.time * 2.0f);
    const float alpha = std::min(in, out);
    const float w = IS(580), h = IS(118);
    const float x = m_w - w - IS(30) + (1.0f - in) * IS(80);
    CodecBox(x, y, w, h, alpha);
    m_ach.LoadIcon(*a, m_r);
    if (a->icon.id)
      m_r.Sprite(a->icon, x + IS(22), y + IS(19), IS(80), IS(80), {1, 1, 1, alpha});
    m_r.Text(m_r.mono, L("¡LOGRO DESBLOQUEADO!", "ACHIEVEMENT UNLOCKED!"), x + IS(122), y + IS(14), IS(22), WithAlpha(kCodec, alpha));
    m_r.Text(m_r.ui, Truncate(m_r.ui, a->Title(), IS(29), w - IS(230)), x + IS(122), y + IS(46), IS(29),
             WithAlpha(kWhite, alpha));
    m_r.Text(m_r.ui, std::to_string(a->points) + " pts", x + w - IS(22), y + IS(14), IS(23), WithAlpha(kGold, alpha),
             Align::Right);
    y += h + IS(12);
  }
}

void App::RenderLoading(float t, float alpha)
{
  if (alpha <= 0.001f)
    return;
  m_r.Rect(0, 0, static_cast<float>(m_w), static_cast<float>(m_h), UiMode::Solid, {0, 0, 0, alpha});
  // La pantalla del códec, centrada, con la frecuencia que se sintoniza en el primer medio segundo.
  const float w = IS(560), h = IS(300), x = (m_w - w) / 2, y = (m_h - h) / 2 - IS(20);
  CodecBox(x, y, w, h, alpha);
  const float tune = std::min(1.0f, t / 0.6f);
  char freq[16];
  std::snprintf(freq, sizeof(freq), "%.2f", 140.85 - (1.0 - tune) * 2.4 + (tune < 1 ? 0.03 * std::sin(t * 60.0) : 0.0));
  m_r.Text(m_r.mono, "PTT", x + IS(26), y + IS(20), IS(24), WithAlpha(kCodec, 0.7f * alpha));
  m_r.Text(m_r.mono, "MEMORY", x + w - IS(26), y + IS(20), IS(24), WithAlpha(kCodec, 0.7f * alpha), Align::Right);
  m_r.Text(m_r.mono, freq, x + w / 2, y + IS(70), IS(110), WithAlpha(kCodec, alpha), Align::Center);
  // Barras de señal que se llenan.
  const int bars = 10;
  for (int i = 0; i < bars; i++)
  {
    const float on = Clamp01(tune * bars - i);
    const float bh = IS(8 + i * 3.5f);
    m_r.Rect(x + IS(60) + i * IS(18), y + h - IS(54) - bh, IS(12), bh, UiMode::Solid,
             WithAlpha(kCodec, (0.2f + 0.8f * on) * alpha));
  }
  const bool blink = std::fmod(t, 0.8f) < 0.5f;
  m_r.Text(m_r.mono, L("CONECTANDO", "CONNECTING") + std::string(blink ? "..." : ""), x + w - IS(40), y + h - IS(70),
           IS(26), WithAlpha(kCodecText, alpha), Align::Right);
}

// ------------------------------------------------------------------------------------------------

int App::Run(int argc, char** argv)
{
  std::string screenshot_screen = "menu";
  for (int i = 1; i < argc; i++)
  {
    const std::string a = argv[i];
    if (a == "--screenshot" && i + 1 < argc) m_screenshot_path = argv[++i];
    else if (a == "--time" && i + 1 < argc) m_screenshot_time = static_cast<float>(std::atof(argv[++i]));
    else if (a == "--screen" && i + 1 < argc) screenshot_screen = argv[++i];
  }

  m_cfg.Load();
  // Idioma: el elegido en el selector o, si no se ha elegido nunca, el del sistema.
  {
    bool en = m_cfg.language == "en";
    if (m_cfg.language.empty())
    {
      en = true;
      if (SDL_Locale* locales = SDL_GetPreferredLocales())
      {
        if (locales[0].language)
          en = std::strcmp(locales[0].language, "es") != 0;
        SDL_free(locales);
      }
    }
    if (const char* test = std::getenv("SILVER_TEST_LANG")) // pruebas: "es" o "en"
      en = std::strcmp(test, "en") == 0;
    I18n::Current() = en ? Lang::En : Lang::Es;
  }
  if (m_cfg.game_path.empty() || !Paths::FileExists(m_cfg.game_path))
  {
    const std::string found = AutoDetectGame();
    if (!found.empty())
    {
      m_cfg.game_path = found;
      m_cfg.Save();
    }
  }

  std::string error;
  if (!Init(&error))
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tenchu", error.c_str(), m_window);
    SDL_Log("%s", error.c_str());
    return 1;
  }

  if (!m_screenshot_path.empty())
  {
    // Modo prueba: captura un frame. Con --screen game|continue arranca el juego de verdad.
    auto capture = [this](const std::string& path) {
      std::vector<unsigned char> px(static_cast<size_t>(m_w) * m_h * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, m_w, m_h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
      if (FILE* f = Platform::OpenFile(path.c_str(), "wb"))
      {
        std::fprintf(f, "P6\n%d %d\n255\n", m_w, m_h);
        for (int y = m_h - 1; y >= 0; y--)
          std::fwrite(&px[static_cast<size_t>(y) * m_w * 3], 1, static_cast<size_t>(m_w) * 3, f);
        std::fclose(f);
      }
    };
    m_ach_game_path = m_cfg.game_path;
    m_ach.SetGame(m_cfg.game_path);
    UpdatePatchAvailability();

    if (screenshot_screen == "game" || screenshot_screen == "continue")
    {
      if (const char* st = std::getenv("SILVER_TEST_STYLE"))
        m_cfg.prompt_style = st;
      m_cfg.hardcore = std::getenv("SILVER_TEST_HARDCORE") != nullptr; // pruebas: modo normal por defecto
      RefreshSlots();
      StartGame(screenshot_screen == "continue" ? LatestSlot() : -1);
      if (m_modal == Modal::Dialog)
        SDL_Log("Error al arrancar: %s", m_dialog.c_str());
      // SILVER_TEST_SCRIPT="12:start,15:cross,20:left:2.5" pulsa botones en segundos concretos
      // (el tercer campo, opcional, es cuánto tiempo se mantiene pulsado).
      struct Press
      {
        double t;
        int id;
        double hold;
      };
      std::vector<Press> script;
      if (const char* sc = std::getenv("SILVER_TEST_SCRIPT"))
      {
        std::stringstream ss(sc);
        std::string item;
        while (std::getline(ss, item, ','))
        {
          const size_t c = item.find(':');
          std::string b = item.substr(c + 1);
          double hold = 0.15;
          if (const size_t c2 = b.find(':'); c2 != std::string::npos)
          {
            hold = std::atof(b.substr(c2 + 1).c_str());
            b = b.substr(0, c2);
          }
          int id = RETRO_DEVICE_ID_JOYPAD_START;
          if (b == "cross") id = RETRO_DEVICE_ID_JOYPAD_B;
          else if (b == "circle") id = RETRO_DEVICE_ID_JOYPAD_A;
          else if (b == "triangle") id = RETRO_DEVICE_ID_JOYPAD_X;
          else if (b == "down") id = RETRO_DEVICE_ID_JOYPAD_DOWN;
          else if (b == "up") id = RETRO_DEVICE_ID_JOYPAD_UP;
          else if (b == "right") id = RETRO_DEVICE_ID_JOYPAD_RIGHT;
          else if (b == "left") id = RETRO_DEVICE_ID_JOYPAD_LEFT;
          else if (b == "square") id = RETRO_DEVICE_ID_JOYPAD_Y;
          else if (b == "l1") id = RETRO_DEVICE_ID_JOYPAD_L;
          else if (b == "r1") id = RETRO_DEVICE_ID_JOYPAD_R;
          else if (b == "l2") id = RETRO_DEVICE_ID_JOYPAD_L2;
          else if (b == "r2") id = RETRO_DEVICE_ID_JOYPAD_R2;
          else if (b == "r3") id = RETRO_DEVICE_ID_JOYPAD_R3;
          else if (b == "l3") id = RETRO_DEVICE_ID_JOYPAD_L3;
          else if (b == "select") id = RETRO_DEVICE_ID_JOYPAD_SELECT;
          else if (b == "swap") id = -1; // cambio de disco del launcher (F8)
          script.push_back({std::atof(item.substr(0, c).c_str()), id, hold});
        }
      }
      const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
      const uint64_t start = SDL_GetPerformanceCounter();
      uint64_t last = start;
      int shot = 0;
      while ((SDL_GetPerformanceCounter() - start) / freq < m_screenshot_time)
      {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}
        const uint64_t now = SDL_GetPerformanceCounter();
        const double el = (now - start) / freq;
        m_test_buttons = 0;
        // SILVER_TEST_SAVE_AT="50" guarda un estado (en SILVER_TEST_STATE) en ese segundo.
        if (const char* sv = std::getenv("SILVER_TEST_SAVE_AT"))
          if (el >= std::atof(sv) && !m_test_saved)
            m_test_saved = SaveSlot(0);
        // SILVER_TEST_RSTICK="2:4:1:0,5:6:0:-1" mueve el joystick derecho (inicio:fin:x:y).
        // SILVER_TEST_LSTICK igual para el joystick izquierdo.
        for (int side = 0; side < 2; side++)
        {
          float* stick = side ? m_test_lstick : m_test_rstick;
          stick[0] = stick[1] = 0;
          if (const char* rs = std::getenv(side ? "SILVER_TEST_LSTICK" : "SILVER_TEST_RSTICK"))
          {
            std::stringstream ss(rs);
            std::string item;
            while (std::getline(ss, item, ','))
            {
              float t0 = 0, t1 = 0, x = 0, y = 0;
              if (std::sscanf(item.c_str(), "%f:%f:%f:%f", &t0, &t1, &x, &y) == 4 && el >= t0 && el < t1)
              {
                stick[0] = x;
                stick[1] = y;
              }
            }
          }
        }
        // SILVER_TEST_STYLE_AT="55:xbox" cambia el estilo de iconos en plena partida.
        if (const char* sa = std::getenv("SILVER_TEST_STYLE_AT"))
          if (el >= std::atof(sa) && std::strchr(sa, ':'))
            m_cfg.prompt_style = std::strchr(sa, ':') + 1;
        for (Press& p : script)
          if (el >= p.t && el < p.t + p.hold)
          {
            if (p.id >= 0)
              m_test_buttons |= static_cast<uint16_t>(1u << p.id);
            else
            {
              SwapDisc();
              p.hold = 0; // una sola vez
            }
          }
        Update(static_cast<float>((now - last) / freq));
        last = now;
        Render();
        const double every = std::getenv("SILVER_TEST_SHOT_EVERY") ? std::atof(std::getenv("SILVER_TEST_SHOT_EVERY")) : 5.0;
        if (el > (shot + 1) * every)
        {
          shot++;
          SDL_Log("t=%.0fs frames_emu=%llu fps_obj=%.2f frame=%dx%d", el,
                  static_cast<unsigned long long>(m_emu.FramesRun()), m_emu.TargetFps(), m_emu.FrameWidth(),
                  m_emu.FrameHeight());
          capture(m_screenshot_path + "." + std::to_string(shot) + ".ppm");
          if (std::getenv("SILVER_TEST_STATE_EACH"))
            m_emu.SaveState(m_screenshot_path + "." + std::to_string(shot) + ".state");
          if (std::getenv("SILVER_DUMP_RAM"))
          {
            size_t n = 0;
            if (const uint8_t* ram = m_emu.SystemRam(&n))
              if (FILE* f = Platform::OpenFile((m_screenshot_path + "." + std::to_string(shot) + ".ram").c_str(), "wb"))
              {
                std::fwrite(ram, 1, n, f);
                std::fclose(f);
              }
          }
        }
        SDL_GL_SwapWindow(m_window);
        const double fr = (SDL_GetPerformanceCounter() - now) / freq;
        if (fr < 1.0 / 240.0)
          SDL_Delay(static_cast<Uint32>((1.0 / 240.0 - fr) * 1000.0) + 1);
      }
      SDL_Log("Emulación: %llu frames en %.1f s (objetivo %.2f fps)",
              static_cast<unsigned long long>(m_emu.FramesRun()), m_screenshot_time, m_emu.TargetFps());
      if (std::getenv("SILVER_TEST_PAUSE"))
      {
        m_paused = true;
        m_emu.SetPaused(true);
        for (float& f : m_sel_anim) f = 0;
        m_sel_anim[0] = 1;
      }
      Render();
    }
    else
    {
      m_time = m_screenshot_time;
      m_fade = 1.0f;
      m_intro_t = kIntroLen;
      if (const char* it = std::getenv("SILVER_TEST_INTRO"))
        m_intro_t = static_cast<float>(std::atof(it));
      if (const char* ot = std::getenv("SILVER_TEST_OUTRO"))
        m_outro_t = static_cast<float>(std::atof(ot));
      if (screenshot_screen == "options") m_screen = Screen::Options;
      if (screenshot_screen == "controls") m_screen = Screen::Controls;
      if (screenshot_screen == "remap") m_screen = Screen::Remap;
      if (screenshot_screen == "credits") m_screen = Screen::Credits;
      if (screenshot_screen == "cheats")
      {
        m_screen = Screen::Cheats;
        m_cheat_list = LoadCheats(Paths::AssetsDir(), Paths::ReadDiscSerial(m_cfg.game_path));
        if (const char* cs = std::getenv("SILVER_TEST_CHEATSEL"))
        {
          m_cheat_sel = std::atoi(cs);
          const int n = static_cast<int>(m_cheat_list.size());
          if (m_cheat_sel < n)
            m_cheat_scroll = std::clamp(m_cheat_sel * 56.0f - 224.0f, 0.0f, std::max(0.0f, n * 56.0f - 560.0f));
        }
      }
      if (const char* cs = std::getenv("SILVER_TEST_CREDITS"))
        m_credits_scroll = static_cast<float>(std::atof(cs));
      if (screenshot_screen == "confirmreset")
      {
        m_screen = Screen::Achievements;
        m_modal = Modal::ConfirmReset;
      }
      if (const char* as = std::getenv("SILVER_TEST_ACHSEL"))
      {
        m_ach_sel = std::atoi(as);
        m_ach_scroll = m_ach_scroll_target = std::max(0.0f, (m_ach_sel + 1) * 88.0f - 700.0f);
      }
      if (const char* dev = std::getenv("SILVER_TEST_DEVICE"))
        m_remap_device = std::atoi(dev);
      if (const char* rs = std::getenv("SILVER_TEST_REMAPSEL"))
        m_remap_sel = std::atoi(rs);
      if (std::getenv("SILVER_TEST_WAIT"))
      {
        m_remap_wait = kRemapOrder[m_remap_sel];
        m_remap_timer = 4.2f;
      }
      if (screenshot_screen == "achievements") m_screen = Screen::Achievements;
      m_lang_open = std::getenv("SILVER_TEST_LANGMENU") != nullptr;
      RefreshSlots();
      if (screenshot_screen == "choice") m_modal = Modal::PlayChoice;
      if (screenshot_screen == "slots") OpenSlots(std::getenv("SILVER_TEST_SAVE") != nullptr);
      if (screenshot_screen == "dialog") ShowDialog("Ejemplo de diálogo.\nSegunda línea de texto.");
      if (const char* st = std::getenv("SILVER_TEST_STYLE"))
        m_cfg.prompt_style = st;
      if (const char* pg = std::getenv("SILVER_TEST_PAGE"))
        m_opt_page = std::atoi(pg);
      if (const char* sel = std::getenv("SILVER_TEST_SEL"))
        m_sel = m_opt_sel = m_choice_sel = std::atoi(sel);
      int active = m_sel;
      if (m_modal == Modal::PlayChoice)
        active = m_choice_sel;
      else if (m_screen == Screen::Options)
        active = m_opt_page < 0 ? m_opt_sel : -1;
      for (int i = 0; i < 8; i++)
        m_sel_anim[i] = (i == active) ? 1.0f : 0.0f;
      // Para la marquesina: el texto empezó a desplazarse hace SILVER_TEST_MARQUEE segundos.
      if (const char* mq = std::getenv("SILVER_TEST_MARQUEE"))
      {
        Render(); // registra la fila activa
        m_marquee_start = m_time - static_cast<float>(std::atof(mq));
      }
      Render();
    }
    capture(m_screenshot_path);
    Shutdown();
    return 0;
  }

  uint64_t last = SDL_GetPerformanceCounter();
  while (m_running)
  {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      HandleEvent(e);

    const uint64_t now = SDL_GetPerformanceCounter();
    const float dt = static_cast<float>(now - last) / static_cast<float>(SDL_GetPerformanceFrequency());
    last = now;
    Update(std::min(dt, 0.1f));
    Render();
    SDL_GL_SwapWindow(m_window);

    // Respaldo por si el vsync no bloquea (ventana oculta, algunos compositores): máx. ~240 fps.
    const double frame = (SDL_GetPerformanceCounter() - now) / static_cast<double>(SDL_GetPerformanceFrequency());
    if (frame < 1.0 / 240.0)
      SDL_Delay(static_cast<Uint32>((1.0 / 240.0 - frame) * 1000.0) + 1);
  }

  m_cfg.Save();
  Shutdown();
  return 0;
}

int main(int argc, char** argv)
{
  App app;
  return app.Run(argc, argv);
}
