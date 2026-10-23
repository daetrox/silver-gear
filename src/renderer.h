// Dibujo 2D sencillo sobre OpenGL 3.3: escena procedural de fondo, figuras con shader y texto.
#pragma once
#include "gl.h"

#include <string>
#include <unordered_map>
#include <vector>

struct Color
{
  float r, g, b, a;
};

enum class UiMode : int
{
  Solid = 0,
  Text = 1,
  Image = 2,
  Brush = 3,
  Enso = 4,
  Icon = 5,
  Panel = 6,
  Sprite = 7, // textura RGBA teñida por uColor
  PadGlyph = 8,
  SpriteGray = 9,
  InkWipe = 10,
  Codec = 11, // pantalla del códec: verde oscuro con líneas de barrido y esquinas (uColor2 = borde)
};

enum class Icon : int
{
  Shuriken = 0,
  Gear = 1,
  Door = 2,
  Scroll = 3,
  Chevron = 4,
  Close = 5,
  Minimize = 6,
  Maximize = 7,
  Seal = 8,
  Underline = 9,
  ArrowLeft = 10,
  ArrowRight = 11,
  LockMark = 12,
};

enum class Align
{
  Left,
  Center,
  Right,
};

struct Glyph
{
  float x0, y0, x1, y1;     // caja respecto a la línea base, en px de horneado
  float u0, v0, u1, v1;     // coordenadas en el atlas
  float advance;
};

struct Texture
{
  GLuint id = 0;
  int w = 0, h = 0;
};

struct Font
{
  GLuint texture = 0;
  float bake_size = 0;
  float ascent = 0;
  float tracking = 0; // espacio extra entre letras, en fracción del tamaño
  std::unordered_map<uint32_t, Glyph> glyphs;
};

class Renderer
{
public:
  bool Init(const std::string& assets_dir, std::string* error);
  bool ReloadShaders(std::string* error);
  void Shutdown();

  void BeginFrame(int width, int height, float time);

  // Fondo a partir de la imagen: `x,y,w,h` es el rectángulo que ocupa la imagen en pantalla (con `w`
  // negativo, reflejada); `blur` desenfoca la imagen (nivel de mipmap).
  void DrawBackground(const Texture& bg, float x, float y, float w, float h, float intensity, float blur = 0);
  void Sprite(const Texture& tex, float x, float y, float w, float h, Color tint, bool gray = false);
  bool LoadTexture(const std::string& path, Texture* tex, bool mipmaps);
  void Rect(float x, float y, float w, float h, UiMode mode, Color c, Color c2 = {0, 0, 0, 0},
            float p0 = 0, float p1 = 0, float p2 = 0, float p3 = 0);
  void DrawIcon(Icon icon, float x, float y, float size, Color c, float p1 = 0);
  void Image(GLuint tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
             bool smooth);
  // Devuelve el ancho dibujado. `effect` > 0 activa el desgaste tipo pincel (logotipo).
  float Text(const Font& font, const std::string& text, float x, float y, float size, Color c,
             Align align = Align::Left, Color c2 = {0, 0, 0, 0}, float effect = 0);
  float TextWidth(const Font& font, const std::string& text, float size) const;
  // Recorte rectangular (píxeles, origen arriba-izquierda) para lo que se dibuje a continuación.
  // Se pueden anidar: SetClip intersecta con el recorte actual y ClearClip restaura el anterior.
  void SetClip(float x, float y, float w, float h);
  void ClearClip();
  // Centra el texto en (cx, cy) usando la caja real de los glifos (útil para signos sueltos).
  void TextCentered(const Font& font, const std::string& text, float cx, float cy, float size, Color c);

  Font ui, display, brush, mono; // texto, signos grandes, títulos/botones y pantalla del códec

  int Width() const { return m_w; }
  int Height() const { return m_h; }

private:
  GLuint Compile(const std::string& vs_path, const std::string& fs_path, std::string* error);
  bool LoadFont(Font& font, const std::string& path, float size, const std::vector<uint32_t>& codepoints,
                std::string* error);
  void SetCommonUniforms(GLuint prog);
  void Flush(const std::vector<float>& verts);

  std::string m_assets;
  GLuint m_ui_prog = 0, m_bg_prog = 0;
  GLuint m_vao = 0, m_vbo = 0;
  int m_w = 1, m_h = 1;
  float m_time = 0;
  std::vector<float> m_scratch;
  struct ClipRect
  {
    float x0, y0, x1, y1;
  };
  std::vector<ClipRect> m_clips;
  void ApplyClip();
};

// Decodifica UTF-8 a códigos Unicode.
std::vector<uint32_t> DecodeUtf8(const std::string& s);
