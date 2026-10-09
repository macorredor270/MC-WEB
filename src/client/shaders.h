#pragma once
// Shaders en GLSL compatible con GLSL 330 core y GLSL ES 3.00 (la cabecera #version se añade al compilar).

namespace mcw::shaders {

inline constexpr const char* kChunkVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aLayer;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec2 aLight;
layout(location = 5) in uint aSlot;  // hueco de la sección en la tabla (entero)
uniform mat4 uViewProj;
uniform highp isampler2D uSections;  // por hueco: x, y, z de la sección (en secciones) y un 1 si está en uso
uniform ivec3 uCamBlock;             // bloque en el que está la cámara...
uniform vec3 uCamFrac;               // ...y lo que le sobra: la resta es exacta aunque el mundo esté lejos del origen
out vec3 vUV;
out vec4 vColor;
out vec2 vLight;
out vec2 vPosXZ;
void main() {
  // Posición dentro de la sección (1/256 de bloque) + origen de la sección respecto a la cámara
  ivec4 sec = texelFetch(uSections, ivec2(int(aSlot & 255u), int(aSlot >> 8u)), 0);
  vec3 origin = vec3(sec.xyz * 16 - uCamBlock) - uCamFrac;
  vec3 p = aPos / 256.0 + origin;
  gl_Position = uViewProj * vec4(p, 1.0);
  vUV = vec3(aUV, aLayer);
  vColor = aColor;
  vLight = aLight * (255.0 / 240.0);
  vPosXZ = p.xz;
}
)";

inline constexpr const char* kChunkFS = R"(
uniform highp sampler2DArray uBlocks;
uniform sampler2D uLightmap;
uniform vec3 uFogColor;
uniform vec2 uFog;
#ifdef ALPHA_TEST
uniform float uAlphaCutoff;
#endif
in vec3 vUV;
in vec4 vColor;
in vec2 vLight;
in vec2 vPosXZ;
out vec4 fragColor;
void main() {
  vec4 tex = texture(uBlocks, vUV);
  // Alfa del vértice 0 = hojas rápidas: los huecos se pintan oscuros en vez de verse a través
  if (vColor.a < 0.5) tex = vec4(mix(vec3(0.12), tex.rgb, tex.a), 1.0);
#ifdef ALPHA_TEST
  // Solo en los pases con huecos (hojas, plantas, cristal, agua): sin esto el pase sólido deja a la GPU descartar
  // fragmentos por profundidad antes de sombrear (y en las GPU de móvil ahorra mucho dibujo repetido)
  if (tex.a < uAlphaCutoff) discard;
#endif
  vec3 light = texture(uLightmap, (vLight * 15.0 + 0.5) / 16.0).rgb;
  vec4 c = tex * vec4(vColor.rgb, 1.0) * vec4(light, 1.0);
  float f = clamp((length(vPosXZ) - uFog.x) / (uFog.y - uFog.x), 0.0, 1.0);
  fragColor = vec4(mix(c.rgb, uFogColor, f), c.a);
}
)";

inline constexpr const char* kSkyVS = R"(
out vec2 vNdc;
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
  vNdc = p;
  gl_Position = vec4(p, 0.9999, 1.0);
}
)";

inline constexpr const char* kSkyFS = R"(
uniform mat4 uInvViewProj;
uniform vec3 uSkyColor;
uniform vec3 uFogColor;
uniform vec3 uVoidColor;
uniform vec3 uSunDir;
uniform vec4 uSunset;
in vec2 vNdc;
out vec4 fragColor;
void main() {
  vec4 w = uInvViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(w.xyz / w.w);
  vec3 c = mix(uFogColor, uSkyColor, smoothstep(0.0, 0.35, dir.y));
  c = mix(c, uVoidColor, smoothstep(-0.35, -0.9, dir.y));
  vec3 sunH = normalize(vec3(uSunDir.x, 0.0, uSunDir.z) + 1e-5);
  float glow = pow(max(dot(normalize(vec3(dir.x, 0.0, dir.z) + 1e-5), sunH), 0.0), 6.0);
  glow *= 1.0 - smoothstep(0.0, 0.45, abs(dir.y - 0.05));
  c = mix(c, uSunset.rgb, glow * uSunset.a);
  fragColor = vec4(c, 1.0);
}
)";

// Quads texturizados en 3D (sol, luna, estrellas, nubes)
inline constexpr const char* kBillboardVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uViewProj;
out vec2 vUV;
out vec4 vColor;
out vec2 vPosXZ;
void main() {
  gl_Position = uViewProj * vec4(aPos, 1.0);
  vUV = aUV;
  vColor = aColor;
  vPosXZ = aPos.xz;
}
)";

inline constexpr const char* kBillboardFS = R"(
uniform sampler2D uTex;
uniform float uAlphaCutoff;
uniform vec3 uFogColor;
uniform vec2 uFog;
in vec2 vUV;
in vec4 vColor;
in vec2 vPosXZ;
out vec4 fragColor;
void main() {
  vec4 c = texture(uTex, vUV) * vColor;
  if (c.a < uAlphaCutoff) discard;
  float f = clamp((length(vPosXZ) - uFog.x) / (uFog.y - uFog.x), 0.0, 1.0);
  fragColor = vec4(mix(c.rgb, uFogColor, f), c.a * (1.0 - f));
}
)";

inline constexpr const char* kUiVS = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUV;
out vec4 vColor;
void main() {
  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
  vUV = aUV;
  vColor = aColor;
}
)";

inline constexpr const char* kUiFS = R"(
uniform sampler2D uTex;
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
void main() {
  vec4 c = texture(uTex, vUV) * vColor;
  if (c.a < 0.004) discard;
  fragColor = c;
}
)";

}  // namespace mcw::shaders

namespace mcw::shaders {

// Ítems, grietas, contornos y objeto en la mano: posición ya transformada + capa del texture array.
inline constexpr const char* kItemVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aUVL;
layout(location = 2) in vec4 aColor;
uniform mat4 uMVP;
out vec3 vUVL;
out vec4 vColor;
void main() {
  gl_Position = uMVP * vec4(aPos, 1.0);
  vUVL = aUVL;
  vColor = aColor;
}
)";

inline constexpr const char* kItemFS = R"(
uniform highp sampler2DArray uTex;
uniform float uAlphaCutoff;
uniform int uTextured;
in vec3 vUVL;
in vec4 vColor;
out vec4 fragColor;
void main() {
  vec4 c = uTextured != 0 ? texture(uTex, vUVL) * vColor : vColor;
  if (c.a < uAlphaCutoff) discard;
  fragColor = c;
}
)";

/// Criaturas y flechas: textura 2D, color (luz y sombreado) y un color superpuesto (golpe, destello).
inline constexpr const char* kEntityVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aOverlay;
uniform mat4 uViewProj;
out vec2 vUV;
out vec4 vColor;
out vec4 vOverlay;
out float vDist;
void main() {
  gl_Position = uViewProj * vec4(aPos, 1.0);
  vUV = aUV;
  vColor = aColor;
  vOverlay = aOverlay;
  vDist = length(aPos.xz);
}
)";

inline constexpr const char* kEntityFS = R"(
uniform sampler2D uTex;
uniform vec3 uFogColor;
uniform vec2 uFog;
in vec2 vUV;
in vec4 vColor;
in vec4 vOverlay;
in float vDist;
out vec4 fragColor;
void main() {
  vec4 t = texture(uTex, vUV);
  if (t.a < 0.1) discard;
  vec3 c = mix(t.rgb * vColor.rgb, vOverlay.rgb, vOverlay.a);
  float f = clamp((vDist - uFog.x) / (uFog.y - uFog.x), 0.0, 1.0);
  fragColor = vec4(mix(c, uFogColor, f), t.a * vColor.a);
}
)";

}  // namespace mcw::shaders
