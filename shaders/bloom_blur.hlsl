[[vk::binding(0)]] SamplerState linear_sampler;
[[vk::binding(1)]] Texture2D in_render_target;
[[vk::binding(2)]] RWTexture2D<float4> out_render_target;

struct PushConstants
{
    float2 step;
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
        const uint N_SAMPLES = 13;
        const float offsets[] = { -1.7688, -1.1984, -0.8694, -0.6151, -0.3957, -0.1940, 0, 0.1940, 0.3957, 0.6151, 0.8694, 1.1984, 1.7688 };
        const float n = 13.0;

        float4 color = float4(0.0, 0.0, 0.0, 0.0);

        for (int i = 0; i < int(n); i++)
            color += in_render_target.SampleLevel(linear_sampler, uv + push_constants.step * offsets[i], 0);

        out_render_target[thread_id.xy] = color / n;
    }
}