#pragma once

float3 srgb_to_linear (in float3 srgb)
{ 
    float3 rgb_low = srgb / 12.92;
    float3 rgb_hi = pow((srgb + 0.055) / 1.055, 2.4);
    float3 rgb = select(srgb <= 0.04045, rgb_low, rgb_hi);
    return rgb;
}
float3 linear_to_srgb(in float3 rgb)
{
    float3 srgb_low = rgb * 12.92;
    float3 srgb_hi = (pow(abs(rgb), 1.0/2.4) * 1.055) - 0.055;
    float3 srgb = select(rgb <= 0.0031308, srgb_low, srgb_hi);
    return srgb;
}
