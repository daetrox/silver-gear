#include "gl.h"
#include <SDL.h>

namespace tgl {
#define SILVER_GL_DEFINE(type, name) type name = nullptr;
SILVER_GL_FUNCS(SILVER_GL_DEFINE)
#undef SILVER_GL_DEFINE

bool LoadGLFunctions()
{
  bool ok = true;
#define SILVER_GL_LOAD(type, name)                                                                 \
  name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));                                     \
  if (!name)                                                                                       \
  {                                                                                                \
    SDL_Log("Falta la función OpenGL %s", #name);                                                 \
    ok = false;                                                                                    \
  }
  SILVER_GL_FUNCS(SILVER_GL_LOAD)
#undef SILVER_GL_LOAD
  return ok;
}
} // namespace tgl
