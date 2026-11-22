// Stretches the eyes' world, drawn at a lower resolution, over the game's
// screen, and gives back part of the sharpness the stretch takes away.
//
// Contrast-adaptive sharpening: each pixel is pushed away from its four
// neighbours one source texel away, less where the local contrast is
// already high, so edges do not ring and flat areas keep their noise level.
//
// Compiled into vr_upscale_sharpen_ps.h with compile.sh, next to this file:
// the build has no shader compiler. Keep the two in step.
//
// c0.xy  size of a source texel, in texture coordinates
// c0.z   weight of the neighbours at full sharpening, negative

sampler2D image : register(s0);
float4    state : register(c0);

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
  float4 centre = tex2D(image, uv);

  float3 north = tex2D(image, uv - float2(0.0, state.y)).rgb;
  float3 south = tex2D(image, uv + float2(0.0, state.y)).rgb;
  float3 west  = tex2D(image, uv - float2(state.x, 0.0)).rgb;
  float3 east  = tex2D(image, uv + float2(state.x, 0.0)).rgb;

  float3 low  = min(centre.rgb, min(min(north, south), min(west, east)));
  float3 high = max(centre.rgb, max(max(north, south), max(west, east)));

  // Room left before the result would clip, as a share of the brightest
  float3 room   = saturate(min(low, 1.0 - high) / max(high, 1.0 / 256.0));
  float3 weight = sqrt(room) * state.z;

  float3 sharp = (centre.rgb + (north + south + west + east) * weight) / (1.0 + 4.0 * weight);

  return float4(saturate(sharp), centre.a);
}
