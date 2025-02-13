#include "tonemap_operators.hlsli"

#define N_PASSES 6

[[vk::binding(0)]] SamplerState linear_sampler;
[[vk::binding(1)]] Texture2D in_render_target;
[[vk::binding(2)]] RWTexture2D<float4> out_render_target;
[[vk::binding(3)]] Texture2D in_bloom[N_PASSES];

struct PushConstants
{
    float defocus;
    float exposure;
    float bloom_intensity;
};

[[vk::push_constant]]
PushConstants push_constants;

float4 pyramid_filter(Texture2D tex, float2 texcoord, float2 width) 
{
    float4 color = tex.SampleLevel(linear_sampler, texcoord + float2(0.5, 0.5) * width, 0);
    color += tex.SampleLevel(linear_sampler, texcoord + float2(-0.5,  0.5) * width, 0);
    color += tex.SampleLevel(linear_sampler, texcoord + float2( 0.5, -0.5) * width, 0);
    color += tex.SampleLevel(linear_sampler, texcoord + float2(-0.5, -0.5) * width, 0);
    return 0.25 * color;
}

[numthreads(8, 8, 1)]
void cs_main(uint3 thread_id : SV_DispatchThreadID)
{
    float w, h;
    float2 uv;
    float2 pixel_size;
    {
        out_render_target.GetDimensions(w, h);
        uv = (thread_id.xy + 0.5) / float2(w, h);
        pixel_size = 1.0 / float2(w, h);
    }

    if (all(saturate(uv) == uv))
    {
        const float w[] = {64.0, 32.0, 16.0, 8.0, 4.0, 2.0, 1.0};

        float4 color = pyramid_filter(in_render_target, uv, pixel_size * push_constants.defocus);
        [unroll]
        for (int i = 0; i < N_PASSES; i++) 
        {
            float4 s = in_bloom[i].SampleLevel(linear_sampler, uv, 0);
            color.rgb += push_constants.bloom_intensity * w[i] * s.rgb / 127.0;
            color.a += s.a / N_PASSES;
        }

        color.rgb = 2.0f * filmic(push_constants.exposure * color.rgb);
        float3 white_scale = 1.0f / filmic(11.2);
        color.rgb *= white_scale;

        out_render_target[thread_id.xy] = color;
    }
}