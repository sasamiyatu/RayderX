#include "color.hlsli"

[[vk::binding(0)]] SamplerState linear_sampler;
[[vk::binding(1)]] SamplerState linear_sampler_wrap;
[[vk::binding(2)]] Texture3D noise_texture;
[[vk::binding(3)]] Texture2D in_render_target;

struct PushConstants
{
    float2 pixel_size;
    float time;
    float exposure;
    float noise_intensity;
};

[[vk::push_constant]]
PushConstants push_constants;

struct VSInput
{
    uint vertex_id: SV_VertexID;
};

struct VSOutput
{
    float4 position: SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output = (VSOutput)0;
    
    float2 uv = float2((input.vertex_id << 1) & 2, input.vertex_id & 2);
    output.position = float4(uv * 2.0f + -1.0f, 0.0f, 1.0f);
    uv.y = 1 - uv.y;
    output.uv = uv;

    return output;
}

struct FSInput
{
    float4 position: SV_Position;
    float2 uv : TEXCOORD0;
};

struct FSOutput
{
    float4 color : SV_Target0;
};

float3 overlay(float3 a, float3 b)
{
    return select(pow(abs(b), 2.2) < 0.5, 2 * a * b, 1.0 - 2 * (1.0 - a) * (1.0 - b));
}

float3 add_noise(float3 color, float2 texcoord) 
{
    float2 coord = texcoord * 2.0;
    coord.x *= push_constants.pixel_size.y / push_constants.pixel_size.x;
    float noise = noise_texture.Sample(linear_sampler_wrap, float3(coord, push_constants.time)).r;
    float exposure_factor = push_constants.exposure / 2.0;
    exposure_factor = sqrt(exposure_factor);
    float t = lerp(3.5 * push_constants.noise_intensity, 1.13 * push_constants.noise_intensity, exposure_factor);
    return overlay(color, lerp(0.5, noise, t));
}

FSOutput film_grain(FSInput input)
{
    FSOutput output = (FSOutput)0;

    float3 color = in_render_target.Sample(linear_sampler, input.uv).rgb;
    color = add_noise(color, input.uv);
    color = linear_to_srgb(color);
    output.color = float4(color, 1.0);

    return output;
}
