#include "color.hlsli"

[[vk::binding(0)]] SamplerState linear_sampler;
[[vk::binding(1)]] SamplerState linear_sampler_wrap;
[[vk::binding(2)]] Texture3D noise_texture;
[[vk::binding(3)]] Texture2D in_render_target;
[[vk::binding(4)]] RWTexture2D<float4> out_render_target;

struct PushConstants
{
    float noise_intensity;
    float exposure;
    float2 pixel_size;
    float time;
};

[[vk::push_constant]]
PushConstants push_constants;

float3 overlay(float3 a, float3 b)
{
    return select(pow(abs(b), 2.2) < 0.5, 2 * a * b, 1.0 - 2 * (1.0 - a) * (1.0 - b));
}

float3 add_noise(float3 color, float2 texcoord) 
{
    float2 coord = texcoord * 2.0;
    coord.x *= push_constants.pixel_size.y / push_constants.pixel_size.x;
    float noise = noise_texture.SampleLevel(linear_sampler_wrap, float3(coord, push_constants.time), 0).r;
    float exposure_factor = push_constants.exposure / 2.0;
    exposure_factor = sqrt(exposure_factor);
    float t = lerp(3.5 * push_constants.noise_intensity, 1.13 * push_constants.noise_intensity, exposure_factor);
    return overlay(color, lerp(0.5, noise, t));
}

[numthreads(8, 8, 1)]
void cs_main(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID, uint3 group_id : SV_GroupID)
{
    float w, h;
    float2 uv;
    uint2 tid = thread_id.xy;
    {
        out_render_target.GetDimensions(w, h);
        uv = (tid.xy + 0.5) / float2(w, h);
    }

    if (all(saturate(uv) == uv))
    {
        float3 color = in_render_target.SampleLevel(linear_sampler, uv, 0).rgb;
        color = add_noise(color, uv);
        color = linear_to_srgb(color);
        out_render_target[tid.xy] = float4(color, 1.0);
    }
}
