// Traducción al español, en el propio equipo, de los logros descargados de RetroAchievements y de
// los nombres de los trucos. No se distribuye ningún texto de RetroAchievements; lo que no tiene
// traducción se deja en el idioma original.
#pragma once
#include <string>

std::string TranslateAchievementTitle(const std::string& title);
std::string TranslateAchievementDescription(const std::string& description);
// Nombre de un truco (chtdb); los grupos "A\B" se muestran como "A · B".
std::string TranslateCheatName(const std::string& name);
