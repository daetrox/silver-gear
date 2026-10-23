#version 330 core
// Fondo basado en la ilustración del launcher (la base nevada): balizas rojas que parpadean, focos que
// titilan, un foco que barre la ladera, niebla que avanza por la parte baja, nieve con ráfagas de
// viento y un grano de película muy suave.
uniform vec2 uResolution;
uniform float uTime;
uniform vec4 uImgRect;     // x, y, w, h de la imagen en píxeles de pantalla
uniform float uIntensity;  // fundido de entrada 0..1
uniform float uBlur;       // desenfoque de la imagen (nivel de mipmap); 0 = nítida
uniform sampler2D uBg;
out vec4 fragColor;

vec3 sampleBg(vec2 uv, float lod) { return textureLod(uBg, uv, lod).rgb; }

// Luces de la ilustración, en píxeles de la imagen (1672x941). Si cambia el fondo, hay que medirlas
// de nuevo (tools/make_background.py).
const vec2 kArt = vec2(1672.0, 941.0);
const vec2 kRed[3] = vec2[3](vec2(545.0, 17.0), vec2(558.0, 26.0), vec2(1002.0, 370.0)); // antena y torre
const vec2 kLamp[5] = vec2[5](vec2(257.0, 215.0), vec2(214.0, 217.0), vec2(33.0, 281.0), vec2(504.0, 413.0),
                              vec2(638.0, 427.0)); // focos de la ladera y del hangar
const vec2 kWindow = vec2(1005.0, 417.0); // ventana de la torre de vigilancia

// Halo de una luz puntual en `p` (píxeles de la imagen) con radio `r`.
float glow(vec2 q, vec2 p, float r)
{
  float d = length(q - p) / r;
  return exp(-d * d) + 0.25 * exp(-d * 1.5);
}

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
    if (uBlur < 0.5) // sólo en el menú, no en los laterales de la partida
    {
      vec2 q = uv * kArt;
      // Balizas rojas: cada una con su ritmo; encendido corto, como las de las antenas.
      for (int i = 0; i < 3; i++)
      {
        float ph = fract(T * 0.55 + float(i) * 0.37);
        float on = smoothstep(0.0, 0.05, ph) * (1.0 - smoothstep(0.32, 0.45, ph));
        col += vec3(1.0, 0.16, 0.1) * glow(q, kRed[i], 9.0) * on * 0.9;
      }
      // Focos: titileo leve e irregular.
      for (int i = 0; i < 5; i++)
      {
        float fl = 0.75 + 0.25 * noise(vec2(T * 7.0, float(i) * 13.0));
        col += vec3(0.75, 0.85, 1.0) * glow(q, kLamp[i], 14.0) * fl * 0.35;
      }
      col += vec3(1.0, 0.85, 0.6) * glow(q, kWindow, 7.0) * (0.25 + 0.15 * noise(vec2(T * 2.0, 5.0)));

      // Haz del foco grande, barriendo despacio la ladera hacia abajo a la derecha. Lo cruza la nieve
      // que cae (ruido que se desplaza) y se apaga con la distancia.
      vec2 dv = q - kLamp[0];
      float ang = atan(dv.y, dv.x);
      float aim = radians(38.0) + radians(22.0) * sin(T * 0.23);
      float dist = length(dv);
      float cone = smoothstep(radians(7.0), 0.0, abs(ang - aim)) * smoothstep(20.0, 80.0, dist) *
                   (1.0 - smoothstep(250.0, 900.0, dist));
      float dust = 0.6 + 0.4 * noise(vec2(q.x * 0.05 - T * 0.6, q.y * 0.05 + T * 1.3));
      col += vec3(0.7, 0.8, 0.95) * cone * dust * 0.12;

      // Niebla: bancos que avanzan despacio por la mitad inferior.
      float fog = fbm3(vec2(uv.x * 3.0 - T * 0.025, uv.y * 7.0 + T * 0.01));
      fog = smoothstep(0.45, 0.85, fog) * smoothstep(0.45, 0.8, uv.y);
      col = mix(col, vec3(0.62, 0.68, 0.78), fog * 0.18);
    }
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
    if (rnd.x > 0.22 - fl * 0.05) // poca: la ilustración ya trae nieve pintada
      continue;
    // Vaivén propio de cada copo dentro de su celda.
    vec2 c = 0.2 + 0.6 * hash22(cell * 1.7 + 4.0);
    c.x += sin(T * (0.9 + rnd.y) + rnd.x * 40.0) * 0.12;
    float r = (0.035 + 0.05 * rnd.y) * (0.75 + fl * 0.35);
    float d = length(f - c);
    float m = smoothstep(r, r * 0.25, d);
    // Parpadeo leve, como copos que giran.
    m *= 0.65 + 0.35 * sin(T * (1.5 + rnd.y * 2.0) + rnd.x * 60.0);
    snow = max(snow, m * (0.32 + fl * 0.16));
  }
  // Menos nieve sobre lo muy claro (la luna y la tinta blanca), para no lavar el dibujo.
  float lum = dot(col, vec3(0.299, 0.587, 0.114));
  col = mix(col, vec3(0.93, 0.95, 1.0), snow * (1.0 - 0.7 * smoothstep(0.55, 0.95, lum)));

  // Grano de película muy suave.
  float grain = hash21(px + fract(T * 7.0) * 113.0) - 0.5;
  col += grain * 0.025;

  fragColor = vec4(col * uIntensity, 1.0);
}
