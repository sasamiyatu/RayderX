[[vk::binding(0)]] SamplerState linear_sampler;
[[vk::binding(1)]] SamplerState point_sampler;
[[vk::binding(2)]] Texture2D linear_depth;
[[vk::binding(3)]] Texture2D coc_texture;
[[vk::binding(4)]] Texture2D in_render_target;

struct PushConstants
{
    float focus_distance;
    float focus_range;
    float2 focus_falloff;
    float2 step;
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

FSOutput circle_of_confusion(FSInput input)
{
    FSOutput output = (FSOutput)0;

    float depth = linear_depth.Sample(point_sampler, input.uv).r;
    if (abs(depth - push_constants.focus_distance) > push_constants.focus_range / 2.0) 
    {
        if (depth - push_constants.focus_distance > 0.0) 
        {
            float t = saturate(abs(depth - push_constants.focus_distance) - push_constants.focus_range / 2.0);
            output.color = saturate(t * push_constants.focus_falloff.x); 
        } 
        else 
        {
            float t = saturate(abs(depth - push_constants.focus_distance) - push_constants.focus_range / 2.0);
            output.color = saturate(t * push_constants.focus_falloff.y);
        }
    } 
    else
        output.color = 0;

    return output;
}

FSOutput blur(FSInput input)
{
    FSOutput output = (FSOutput)0;

    const float offsets[] = { -1.7688, -1.1984, -0.8694, -0.6151, -0.3957, -0.1940, 0.1940, 0.3957, 0.6151, 0.8694, 1.1984, 1.7688 };
    const float n = 12.0;

    float coc = coc_texture.Sample(linear_sampler, input.uv).r;

    float4 color = in_render_target.Sample(linear_sampler, input.uv);
    float sum = 1.0;
    for (int i = 0; i < int(n); i++) {
        float tap_coc = coc_texture.Sample(linear_sampler, input.uv + push_constants.step * offsets[i] * coc).r;
        float4 tap = in_render_target.Sample(linear_sampler, input.uv + push_constants.step * offsets[i] * coc);

        float contribution = tap_coc > coc?  1.0f : tap_coc;
        color += contribution * tap;
        sum += contribution;
    }

    output.color = color / sum;

    return output;
}
