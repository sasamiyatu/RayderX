[[vk::binding(0)]] SamplerState linear_sampler;
[[vk::binding(1)]] Texture2D in_render_target;
[[vk::binding(2)]] RWTexture2D<float4> out_render_target;

struct PushConstants
{
    float bloom_threshold;
    float exposure;
};

[[vk::push_constant]]
PushConstants push_constants;

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
        const float2 offsets[] = { 
            float2( 0.0,  0.0), 
            float2(-1.0,  0.0), 
            float2( 1.0,  0.0), 
            float2( 0.0, -1.0),
            float2( 0.0,  1.0),
        };

        float4 color = 1e36;
        for (int i = 0; i < 5; i++) 
            color = min(in_render_target.SampleLevel(linear_sampler,  uv + offsets[i] * pixel_size, 0), color);

        color.rgb *= push_constants.exposure;

        out_render_target[thread_id.xy] = float4(max(color.rgb - push_constants.bloom_threshold / (1.0 - push_constants.bloom_threshold), 0.0), color.a);
    }
}