#include "translate.h"

#include <utility>

// Los logros de Metal Gear Solid se muestran todavía en el idioma en que los publica
// RetroAchievements (inglés): falta un glosario con los nombres oficiales de la versión española.
std::string TranslateAchievementTitle(const std::string& title) { return title; }
std::string TranslateAchievementDescription(const std::string& description) { return description; }

std::string TranslateCheatName(const std::string& name)
{
  static const std::pair<const char*, const char*> table[] = {
    // Versión española: grupos y nombres de chtdb a medio traducir.
    {"Equipo", "Equipo"},
    {"Armas", "Armas"},
    {"Salud", "Salud"},
    {"Munición Infinita", "Munición infinita"},
    {"255 Diazepams", "255 de diazepam"},
    {"Temperatura tarjeta PAL", "Temperatura de la tarjeta PAL"},
    {"Prismaticos", "Prismáticos"},
    {"Caja de carton A", "Caja de cartón A"},
    {"Caja de carton B", "Caja de cartón B"},
    {"Caja de carton C", "Caja de cartón C"},
    {"Lentes de vision nocturna", "Gafas de visión nocturna"},
    {"Lentes Termicas", "Gafas térmicas"},
    {"Mascara Anti-Gas", "Máscara antigás"},
    {"Body Armor", "Chaleco antibalas"},
    {"Kepchup", "Kétchup"},
    {"Pañuelo de Sniper Worf", "Pañuelo de Sniper Wolf"},
    {"Pistola Socom", "Pistola SOCOM"},
    {"Rifle Famas", "Fusil FAMAS"},
    {"Rifle PSG1", "Fusil PSG1"},
    {"Activar Silenciador", "Silenciador puesto"},
    {"Stealth Camuflaje Optico", "Camuflaje óptico (Stealth)"},
    {"Infinite Vida", "Vida infinita"},
    {"Maxima vida", "Vida máxima"},
    {"Sin Acatarrarse", "Sin resfriados"},
    {"Nunca tener frio", "Nunca tener frío"},
    {"Infinite Aire", "Aire infinito"},
    {"Infinite Ammo SOCOM", "SOCOM"},
    {"Infinite C4 ammo", "C4"},
    {"Infinite Stun grenades", "Granadas aturdidoras"},
    {"Infinite Chaff grenades", "Granadas chaff"},
    {"Infinite Assault rifle ammo", "Fusil de asalto"},
    {"Todos Los Modos Vr Training", "Todos los modos de VR Training"},
    {"No salta el radar", "El radar no se bloquea al ser descubierto"},
    {"No tener nunca la bomba de relojeria", "Nunca llevar la bomba de relojería"},
    {"Frio", "Frío"},
    // Versión de EE. UU.
    {"Infinite Health On Disk 1", "Vida infinita (Disco 1)"},
    {"Infinite Air", "Aire infinito"},
    {"Never Reload", "Sin recargar nunca"},
    {"Infinite Time In Jeep Chase", "Tiempo infinito en la persecución en jeep"},
    {"Infinite Time-Jeep Chase", "Tiempo infinito en la persecución en jeep (alternativo)"},
    {"Invincibility", "Invencible"},
    {"Max Life", "Vida máxima"},
    {"Ghost Mode", "Modo fantasma"},
    {"Never Get A Cold", "Sin resfriados"},
    {"Radar Not Jammed When Spotted", "El radar no se bloquea al ser descubierto"},
    {"Ocelot Has 0 Bullets", "Ocelot sin balas"},
    {"Infinite 3.00 Fight Time", "Tiempo infinito en el combate de 3:00"},
    {"Liquid-No Health Final Fight", "Liquid sin vida en el combate final"},
    {"Press R1 For An Easy Fight Against Ninja", "R1: combate fácil contra Ninja"},
    {"Press R1 For An Easy Fight Against Mantis", "R1: combate fácil contra Mantis"},
    {"Press R1 For Meryl+Health S.Wolf", "R1: Meryl y vida de Sniper Wolf"},
    {"Press R1 For A Quick Battle With Hind D", "R1: combate rápido contra el Hind D"},
    {"Press R1 For Quick S.Wolf Snow Fight", "R1: combate rápido contra Sniper Wolf en la nieve"},
    {"Press R1 For A Quick Metal Gear Battle", "R1: combate rápido contra Metal Gear"},
    {"Press R1 For Quick Battle With Raven", "R1: combate rápido contra Raven"},
    {"99 Diazepam", "99 de diazepam"},
    {"Have Access To Special Modes (SELECT+R2+L2)", "Modos especiales (SELECT + R2 + L2)"},
    {"Have All VR Training Stages + Tech Demo", "Todas las fases de VR Training y la demo técnica"},
    {"All Modes VR Training", "Todos los modos de VR Training"},
    {"Have Socom Pistol", "Pistola SOCOM"},
    {"Have Famas", "Fusil FAMAS"},
    {"Have Grenades", "Granadas"},
    {"Have Nikita", "Nikita"},
    {"Have Stinger", "Stinger"},
    {"Have Claymore Mines", "Minas Claymore"},
    {"Have C4", "C4"},
    {"Have Stun Grenades", "Granadas aturdidoras"},
    {"Have Chaff Grenades", "Granadas chaff"},
    {"Have Sniper Rifle", "Fusil de francotirador"},
    {"Havecigs", "Cigarrillos"},
    {"Have Scope", "Prismáticos"},
    {"Have C.Box A", "Caja de cartón A"},
    {"Have C.Box B", "Caja de cartón B"},
    {"Have C.Box c", "Caja de cartón C"},
    {"Have N.V.G.", "Gafas de visión nocturna"},
    {"Have Therm.G", "Gafas térmicas"},
    {"Have Gas Mask", "Máscara antigás"},
    {"Have B.Armor", "Chaleco antibalas"},
    {"Have Ketchup", "Kétchup"},
    {"Have Stealth", "Camuflaje óptico (Stealth)"},
    {"Have Bandana", "Bandana"},
    {"Have Camera", "Cámara"},
    {"Have 255 Rations", "255 raciones"},
    {"Have 255 Medicine", "255 medicinas"},
    {"Have 255 Diazepam", "255 de diazepam"},
    {"Have Pal Key", "Tarjeta PAL"},
    {"Have Level 10 Key Card", "Tarjeta de nivel 10"},
    {"Never Have Time Bomb", "Nunca llevar la bomba de relojería"},
    {"Have Mine Detector", "Detector de minas"},
    {"Have Mo Disc", "Disco MO"},
    {"Have Rope", "Cuerda"},
    {"Have Handker", "Pañuelo de Sniper Wolf"},
    {"Have Suppressor Active", "Silenciador puesto"},
    {"Press R1 For Normal Pal Key", "R1: tarjeta PAL normal"},
    {"Press R2 For Hot Pal Key", "R2: tarjeta PAL caliente"},
    {"Press L1 For Cold Pal Key", "L1: tarjeta PAL fría"},
    {"Play In First Person Viewmode", "Jugar en primera persona"},
    {"Pause doesn't darken screen or mute music", "La pausa no oscurece la pantalla ni silencia la música"},
    {"Stealth camo doesn't make you translucent", "El camuflaje óptico no te vuelve translúcido"},
    {"FPS unlocker", "Desbloquear los FPS"},
  };
  std::string out;
  size_t start = 0;
  while (start <= name.size())
  {
    const size_t end = name.find('\\', start);
    const std::string seg = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::string tr = seg;
    for (const auto& [from, to] : table)
      if (seg == from)
      {
        tr = to;
        break;
      }
    out += (out.empty() ? "" : " · ") + tr;
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return out;
}
