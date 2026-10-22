// Trucos y parches (códigos GameShark) del juego, en formato chtdb:
//   assets/cheats/<serie>.cht   trucos que elige el usuario (Opciones › Trucos)
//   assets/patches/<serie>.cht  parches del launcher: 16:9, modo NTSC (60 Hz), sin subtítulos…
// Los dos discos de un juego comparten archivo de trucos (el del Disco 1).
#pragma once
#include "i18n.h"

#include <string>
#include <vector>

struct Cheat
{
  std::string name;  // nombre original (clave para guardar si está activo)
  std::string label; // nombre traducido para mostrar
  std::string label_en; // nombre original, con los grupos "A\B" como "A · B"
  const std::string& Label() const { return LangEn() ? label_en : label; }
  std::string codes; // códigos en formato libretro: "AAAAAAAA VVVV+AAAAAAAA VVVV"
};

// Un truco con «Option = Nombre:valor» (código con "??") se ofrece como un truco por opción.
std::vector<Cheat> LoadCheats(const std::string& assets_dir, const std::string& serial);

struct Patch
{
  std::string codes;   // vacío si el disco no tiene ese parche
  int overclock = 0;   // OverrideCPUOverclock (porcentaje), 0 = sin cambio
  std::string aspect;  // OverrideAspectRatio ("16:9"…), vacío = sin cambio
};
// Parche `name` ("Widescreen 16:9", "NTSC Mode", "Subtitles off") para el disco `serial`.
Patch LoadPatch(const std::string& assets_dir, const std::string& serial, const std::string& name);
