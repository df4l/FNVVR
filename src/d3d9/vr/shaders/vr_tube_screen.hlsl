// The camera's screen: a pre-war picture tube showing the view from its lens.
//
// Compiled into vr_tube_screen_ps.h with compile.sh, next to this file:
// the build has no shader compiler. Keep the two in step.
//
// c0.x  time in seconds, wrapped
// c0.y  random number of this picture
// c0.z  the shutter's flash, from 0 to 1
// c0.w  1 while the recording light is on
//
// The picture's intensity is kept in the alpha channel for the next picture,
// so that a bright thing leaves a trail on the phosphor.

sampler2D image    : register(s0);
sampler2D previous : register(s1);
float4    state    : register(c0);

static const float3 Luma      = float3(0.30, 0.59, 0.11);
static const float3 Phosphor  = float3(0.20, 1.00, 0.46);
static const float3 Hot       = float3(0.78, 1.00, 0.84);
static const float  Bulge     = 0.085;
static const float  Lines     = 150.0;
static const float  Afterglow = 0.58;
static const float  Pi        = 3.14159265;

// The picture is 4:3 and the image is square, the same field across
float intensityAt(float2 tube) {
  float2 at = float2(0.5 + 0.5 * tube.x, 0.5 + 0.375 * tube.y);
  return dot(tex2D(image, at).rgb, Luma);
}

float mark(float2 from, float2 size) {
  float2 inside = step(abs(from), size);
  return inside.x * inside.y;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
  float2 screen = 2.0 * uv - 1.0;

  // Through the curved glass, and the raster's unsteady sweep
  float2 tube = screen * (1.0 + Bulge * dot(screen, screen));
  tube.x += 0.004 * sin(9.0 * tube.y + 2.7 * state.x) + 0.0015 * sin(61.0 * tube.y - 11.0 * state.x);

  // The rounded edge of the lit part of the tube
  float2 edge = saturate(1.0 - tube * tube);
  float  lit  = saturate(26.0 * edge.x * edge.y);

  // A video signal is soft along its lines
  float  blur = 0.006;
  float  now  = 0.5 * intensityAt(tube)
              + 0.25 * intensityAt(tube + float2(blur, 0.0))
              + 0.25 * intensityAt(tube - float2(blur, 0.0));

  // Low contrast and lifted blacks, a tube is never quite dark
  now = 0.035 + 0.965 * saturate(1.18 * now * (1.25 - 0.25 * now) - 0.015);
  now = lerp(now, 1.0, state.z);

  // Phosphor still glowing from the last picture
  float glow = max(now, Afterglow * tex2D(previous, uv).a);

  // Slow hum bar rolling up the picture
  float bar = saturate(1.0 - 9.0 * abs(frac(0.5 * tube.y + 0.11 * state.x) - 0.5));

  // Film of static
  float grain = frac(sin(dot(uv + state.yy, float2(12.9898, 78.233))) * 43758.5453) - 0.5;

  // Framing marks and the recording light, drawn by the tube itself
  float2 corner = abs(screen) - float2(0.70, 0.66);
  float  marks  = mark(corner + float2(0.05, 0.0), float2(0.06, 0.012))
                + mark(corner + float2(0.0, 0.05), float2(0.009, 0.06))
                + mark(screen, float2(0.03, 0.004)) + mark(screen, float2(0.003, 0.04));
  float2 light  = screen - float2(0.55, -0.54);
  float  rec    = state.w * saturate(1.0 - 900.0 * dot(light, light));

  float shown = glow * (0.92 + 0.10 * bar) + 0.055 * grain;
  shown = saturate(shown + 0.5 * saturate(marks) + rec);

  // Dark scanlines, bright areas bloom over them
  float scan = 0.5 + 0.5 * cos(Pi * Lines * tube.y);
  shown *= lerp(0.56 + 0.44 * scan, 1.0, shown * shown);

  // The tube is dimmer away from the gun
  shown *= 1.0 - 0.34 * dot(screen, screen) * dot(screen, screen);
  shown *= lit;

  float3 colour = lerp(Phosphor, Hot, shown * shown * shown) * shown;

  return float4(colour, glow * lit);
}
