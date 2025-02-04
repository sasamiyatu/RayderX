#include "tonemap_operators.hlsli"
#include "color.hlsli"

#define N_PASSES 6

[[vk::binding(0)]] SamplerState linear_sampler;
[[vk::binding(1)]] Texture2D in_render_target;
[[vk::binding(2)]] Texture2D in_bloom[N_PASSES];

struct PushConstants
{
    float2 dir;
    float2 pixel_size;
    float bloom_threshold;
    float exposure;
    float2 step;
    float bloom_intensity;
    float defocus;
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

FSOutput glare_detect(FSInput input)
{
    FSOutput output = (FSOutput)0;

    const float2 offsets[] = { 
        float2( 0.0,  0.0), 
        float2(-1.0,  0.0), 
        float2( 1.0,  0.0), 
        float2( 0.0, -1.0),
        float2( 0.0,  1.0),
    };

    float4 color = 1e36;
    for (int i = 0; i < 5; i++) 
        color = min(in_render_target.Sample(linear_sampler,  input.uv + offsets[i] * push_constants.pixel_size), color);

    color.rgb *= push_constants.exposure;

    output.color = float4(max(color.rgb - push_constants.bloom_threshold / (1.0 - push_constants.bloom_threshold), 0.0), color.a);

    return output;
}

FSOutput blur(FSInput input)
{
    FSOutput output = (FSOutput)0;

    const uint N_SAMPLES = 13;
    const float offsets[] = { -1.7688, -1.1984, -0.8694, -0.6151, -0.3957, -0.1940, 0, 0.1940, 0.3957, 0.6151, 0.8694, 1.1984, 1.7688 };
    const float n = 13.0;

    float4 color = float4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < int(n); i++)
        color += in_render_target.Sample(linear_sampler, input.uv + push_constants.step * offsets[i]);

    output.color = color / n;

    return output;
}

float4 pyramid_filter(Texture2D tex, float2 texcoord, float2 width) 
{
    float4 color = tex.Sample(linear_sampler, texcoord + float2(0.5, 0.5) * width);
    color += tex.Sample(linear_sampler, texcoord + float2(-0.5,  0.5) * width);
    color += tex.Sample(linear_sampler, texcoord + float2( 0.5, -0.5) * width);
    color += tex.Sample(linear_sampler, texcoord + float2(-0.5, -0.5) * width);
    return 0.25 * color;
}

FSOutput combine(FSInput input)
{
    FSOutput output = (FSOutput)0;

    const float w[] = {64.0, 32.0, 16.0, 8.0, 4.0, 2.0, 1.0};

    float4 color = pyramid_filter(in_render_target, input.uv, push_constants.pixel_size * push_constants.defocus);
    [unroll]
    for (int i = 0; i < N_PASSES; i++) 
    {
        float4 s = in_bloom[i].Sample(linear_sampler, input.uv);
        color.rgb += push_constants.bloom_intensity * w[i] * s.rgb / 127.0;
        color.a += s.a / N_PASSES;
    }

    color.rgb = 2.0f * filmic(push_constants.exposure * color.rgb);
    float3 white_scale = 1.0f / filmic(11.2);
    color.rgb *= white_scale;

    //color.rgb = linear_to_srgb(color.rgb);
    output.color = color;

    return output;
}
