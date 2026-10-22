#include "cheats.h"
#include "platform.h"
#include "translate.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>

namespace {

std::string Trim(const std::string& s)
{
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    b--;
  return s.substr(a, b - a);
}

// "AAAAAAAA VVVV" o "AAAAAAAA VVVVVVVV" en hexadecimal; con `wildcard`, el valor puede llevar "??"
// (se sustituye por el de la opción elegida).
bool IsCodeLine(const std::string& l, bool wildcard = false)
{
  const size_t sp = l.find(' ');
  if (sp != 8 || l.size() < 13 || l.size() > 17)
    return false;
  for (size_t i = 0; i < l.size(); i++)
    if (i != sp && !std::isxdigit(static_cast<unsigned char>(l[i])) && !(wildcard && i > sp && l[i] == '?'))
      return false;
  return true;
}

// Los trucos y parches del Disco 2 son los del Disco 1 (chtdb los guarda juntos).
std::string CheatFileSerial(const std::string& serial)
{
  static const std::pair<const char*, const char*> disc2[] = {
    {"SLES-11734", "SLES-01734"}, // España
    {"SLUS-00776", "SLUS-00594"}, // EE. UU.
  };
  for (const auto& [two, one] : disc2)
    if (serial == two)
      return one;
  return serial;
}

// Un bloque [Nombre] de un archivo chtdb.
struct Block
{
  std::string name;
  std::vector<std::string> codes;                            // líneas de código (pueden llevar "??")
  std::vector<std::pair<std::string, unsigned>> options;     // Option = Nombre:valor
  int overclock = 0;
  std::string aspect;
};

std::vector<Block> ReadBlocks(const std::string& path)
{
  std::vector<Block> out;
  std::ifstream in(Platform::Path(path));
  std::string line;
  Block cur;
  auto flush = [&]() {
    if (!cur.name.empty() && !cur.codes.empty())
      out.push_back(cur);
    cur = Block{};
  };
  while (std::getline(in, line))
  {
    line = Trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#')
      continue;
    if (line.front() == '[' && line.back() == ']')
    {
      flush();
      cur.name = line.substr(1, line.size() - 2);
      continue;
    }
    if (IsCodeLine(line, true))
    {
      cur.codes.push_back(line);
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = Trim(line.substr(0, eq)), val = Trim(line.substr(eq + 1));
    if (key == "Option")
    {
      const size_t colon = val.rfind(':');
      if (colon != std::string::npos)
        cur.options.push_back({Trim(val.substr(0, colon)), static_cast<unsigned>(std::strtoul(val.c_str() + colon + 1, nullptr, 10))});
    }
    else if (key == "OverrideCPUOverclock")
      cur.overclock = std::atoi(val.c_str());
    else if (key == "OverrideAspectRatio")
      cur.aspect = val;
  }
  flush();
  return out;
}

// Une las líneas en formato libretro, con el valor de la opción en lugar de "??" (o "?" sueltos).
std::string JoinCodes(const std::vector<std::string>& lines, unsigned option)
{
  std::string out;
  for (std::string l : lines)
  {
    if (const size_t q = l.find('?'); q != std::string::npos)
    {
      const size_t n = l.find_last_of('?') - q + 1;
      char hex[16];
      std::snprintf(hex, sizeof(hex), "%0*X", static_cast<int>(n), option);
      l.replace(q, n, hex);
    }
    out += (out.empty() ? "" : "+") + l;
  }
  return out;
}

bool HasWildcard(const std::vector<std::string>& lines)
{
  for (const std::string& l : lines)
    if (l.find('?') != std::string::npos)
      return true;
  return false;
}

} // namespace

std::vector<Cheat> LoadCheats(const std::string& assets_dir, const std::string& serial)
{
  std::vector<Cheat> out;
  if (serial.empty())
    return out;
  for (const Block& b : ReadBlocks(assets_dir + "/cheats/" + CheatFileSerial(serial) + ".cht"))
  {
    // El aspecto de pantalla se controla en Gráficos: se omiten los trucos panorámicos.
    if (b.name.find("Widescreen") != std::string::npos)
      continue;
    auto add = [&](const std::string& name, const std::string& option, const std::string& codes) {
      Cheat c;
      c.name = option.empty() ? name : name + " (" + option + ")";
      c.label = TranslateCheatName(name) + (option.empty() ? "" : ": " + TranslateCheatName(option));
      for (char ch : name)
        c.label_en += ch == '\\' ? std::string(" · ") : std::string(1, ch);
      if (!option.empty())
        c.label_en += ": " + option;
      c.codes = codes;
      out.push_back(c);
    };
    if (!HasWildcard(b.codes))
      add(b.name, "", JoinCodes(b.codes, 0));
    else
      for (const auto& [opt, value] : b.options) // sin opciones, un "??" no se puede usar
        add(b.name, opt, JoinCodes(b.codes, value));
  }
  return out;
}

Patch LoadPatch(const std::string& assets_dir, const std::string& serial, const std::string& name)
{
  Patch p;
  if (serial.empty())
    return p;
  for (const Block& b : ReadBlocks(assets_dir + "/patches/" + serial + ".cht"))
    if (b.name == name && !HasWildcard(b.codes))
    {
      p.codes = JoinCodes(b.codes, 0);
      p.overclock = b.overclock;
      p.aspect = b.aspect;
    }
  return p;
}
