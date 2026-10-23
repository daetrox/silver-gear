#include "i18n.h"
#include "config.h"
#include "platform.h"

#include <fstream>
#include <unordered_map>

namespace {

// «\n» del archivo → salto de línea.
std::string Unescape(const std::string& s)
{
  std::string out;
  for (size_t i = 0; i < s.size(); i++)
  {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n')
    {
      out += '\n';
      i++;
    }
    else
      out += s[i];
  }
  return out;
}

const std::unordered_map<std::string, std::string>& FrenchTable()
{
  static const std::unordered_map<std::string, std::string> table = [] {
    std::unordered_map<std::string, std::string> t;
    std::ifstream in(Platform::Path(Paths::AssetsDir() + "/lang/fr.txt"));
    std::string line;
    while (std::getline(in, line))
    {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      const size_t tab = line.find('\t');
      if (line.empty() || line[0] == '#' || tab == std::string::npos)
        continue;
      t[Unescape(line.substr(0, tab))] = Unescape(line.substr(tab + 1));
    }
    return t;
  }();
  return table;
}

} // namespace

const char* I18n::French(const char* en)
{
  const auto& t = FrenchTable();
  const auto it = t.find(en);
  return it == t.end() ? en : it->second.c_str();
}
