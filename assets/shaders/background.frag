#version 330 core
// Fondo basado en la ilustración del launcher: nieve de Shadow Moses que cae con ráfagas de viento
// (tres capas de profundidad) y un grano de película muy suave; la imagen en sí queda estable.
uniform vec2 uResolution;
uniform float uTime;
uniform vec4 uImgRect;     // x, y, w, h de la imagen en píxeles de pantalla
uniform float uIntensity;  // fundido de entrada 0..1
uniform float uBlur;       // desenfoque de la imagen (nivel de mipmap); 0 = nítida
uniform sampler2D uBg;
out vec4 fragColor;

vec3 sampleBg(vec2 uv, float lod) { return textureLod(uBg, uv, lod).rgb; }

// Viento: brisa constante con ráfagas lentas, w(t) = 0.35 + 0.25 sin(0.21 t) + 0.15 sin(0.53 t + 1.7).
// Se usa su integral (el desplazamiento acumulado), así la velocidad de los copos es w(t) y nunca da
// saltos por mucho tiempo que pase.
float windTravel(float t)
{
  return 0.35 * t - (0.25 / 0.21) * cos(0.21 * t) - (0.15 / 0.53) * cos(0.53 * t + 1.7);
}

void main()
{
  // Coordenadas de pantalla con origen arriba-izquierda (como la imagen).
  vec2 px = vec2(gl_FragCoord.x, uResolution.y - gl_FragCoord.y);
  vec2 uv = (px - uImgRect.xy) / uImgRect.zw;
  float T = uTime;

  vec3 col;
  // Fuera de la imagen (ventanas más anchas que 16:9): la ilustración reflejada, desenfocada y oscura.
  if (uv.x > 1.0 || uv.y > 1.0 || uv.x < 0.0 || uv.y < 0.0)
  {
    vec2 m = vec2(1.0 - fract(uv.x), clamp(uv.y, 0.0, 1.0));
    col = sampleBg(m, 5.0) * 0.3;
  }
  else
  {
    col = sampleBg(uv, uBlur);
  }

  // Nieve: copos redondos y suaves en una rejilla por capa; los de delante, más grandes y rápidos.
  // Se calcula en coordenadas de pantalla (en altura) para que no se estire con la ventana.
  vec2 sp = px / uResolution.y;
  float snow = 0.0;
  for (int layer = 0; layer < 3; layer++)
  {
    float fl = float(layer);
    float scale = 22.0 - fl * 6.5;           // celdas por altura de pantalla
    float speed = 0.07 + fl * 0.05;          // caída (alturas de pantalla por segundo)
    vec2 p = sp;
    p.y -= T * speed;
    // El viento empuja más a los copos cercanos.
    p.x += 0.06 * (0.6 + fl * 0.5) * windTravel(T) + sin(p.y * 3.0 + T * 0.4 + fl) * 0.02;
    vec2 g = p * scale;
    vec2 cell = floor(g);
    vec2 f = fract(g);
    vec2 rnd = hash22(cell + fl * 53.0);
    if (rnd.x > 0.38 - fl * 0.08)
      continue;
    // Vaivén propio de cada copo dentro de su celda.
    vec2 c = 0.2 + 0.6 * hash22(cell * 1.7 + 4.0);
    c.x += sin(T * (0.9 + rnd.y) + rnd.x * 40.0) * 0.12;
    float r = (0.035 + 0.05 * rnd.y) * (0.75 + fl * 0.35);
    float d = length(f - c);
    float m = smoothstep(r, r * 0.25, d);
    // Parpadeo leve, como copos que giran.
    m *= 0.65 + 0.35 * sin(T * (1.5 + rnd.y * 2.0) + rnd.x * 60.0);
    snow = max(snow, m * (0.45 + fl * 0.22));
  }
  // Menos nieve sobre lo muy claro (la luna y la tinta blanca), para no lavar el dibujo.
  float lum = dot(col, vec3(0.299, 0.587, 0.114));
  col = mix(col, vec3(0.93, 0.95, 1.0), snow * (1.0 - 0.7 * smoothstep(0.55, 0.95, lum)));

  // Grano de película muy suave.
  float grain = hash21(px + fract(T * 7.0) * 113.0) - 0.5;
  col += grain * 0.025;

  fragColor = vec4(col * uIntensity, 1.0);
}
