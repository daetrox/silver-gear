// Idioma de la interfaz del launcher: español, inglés o francés. Los textos se escriben en el propio
// sitio donde se usan, en español e inglés: L("Opciones", "Options"). El francés sale de una tabla
// (assets/lang/fr.txt, texto inglés → francés); lo que no esté en ella se muestra en inglés.
#pragma once
#include <string>

enum class Lang { Es, En, Fr };

namespace I18n {
inline Lang& Current()
{
  static Lang lang = Lang::Es;
  return lang;
}
// Traducción francesa del texto inglés `en` (el propio `en` si no la hay). El puntero es estable.
const char* French(const char* en);
} // namespace I18n

// Cualquier idioma que no sea el español parte del texto inglés.
inline bool LangEn() { return I18n::Current() != Lang::Es; }
inline const char* L(const char* es, const char* en)
{
  switch (I18n::Current())
  {
    case Lang::Es: return es;
    case Lang::En: return en;
    default: return I18n::French(en);
  }
}
