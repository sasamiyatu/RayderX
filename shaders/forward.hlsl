struct VSInput
{
    uint vertex_id: SV_VertexID;
};

struct VSOutput
{
    float4 position: SV_Position;
    float3 world_position : POSITION0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float2 uv : TEXCOORD0;
};

struct Vertex
{
	float3 position;
	float3 normal;
	float4 tangent;
	float2 uv;
};

struct Light {
    float3 position;
    float3 direction;
    float falloff_start;
    float falloff_width;
    float3 color;
    float attenuation;
    float far_plane;
    float bias;
    float4x4 view_projection;
};

[[vk::binding(0)]] StructuredBuffer<Vertex> vertex_buffer;
[[vk::binding(1)]] SamplerState anisotropic_sampler;
[[vk::binding(2)]] SamplerState linear_sampler;
[[vk::binding(3)]] Texture2D beckmann_texture;
[[vk::binding(4)]] Texture2D basecolor_texture;
[[vk::binding(5)]] Texture2D normal_texture;
[[vk::binding(6)]] Texture2D specular_texture;
[[vk::binding(7)]] StructuredBuffer<Light> lights;
[[vk::binding(8)]] SamplerComparisonState shadow_sampler;
[[vk::binding(9)]] Texture2D shadowmaps[5];

struct PushConstants
{
    float4x4 viewproj;
    uint num_lights;
    float3 camera_pos;
};

[[vk::push_constant]]
PushConstants push_constants;

VSOutput vs_main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    Vertex v = vertex_buffer[input.vertex_id];
    float4 position = mul(push_constants.viewproj, float4(v.position, 1.0f));

    output.position = position;
    output.world_position = v.position;
    output.normal = v.normal;
    output.tangent = v.tangent;
    output.uv = v.uv;

    return output;
}

struct FSInput
{
    float4 position: SV_Position;
    float3 world_position : POSITION0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float2 uv : TEXCOORD0;
};

struct FSOutput
{
    float4 color : SV_Target0;
};

float get_shadow(float3 world_pos, int i) 
{
    float4 shadow_pos = mul(lights[i].view_projection, float4(world_pos, 1.0));
    shadow_pos.xy /= shadow_pos.w;
    shadow_pos.xy = shadow_pos.xy * 0.5 + 0.5;
    shadow_pos.y = 1 - shadow_pos.y;
    shadow_pos.z += lights[i].bias;
    shadow_pos.z /= lights[i].far_plane;
    return shadowmaps[i].SampleCmpLevelZero(shadow_sampler, shadow_pos.xy, shadow_pos.z).r;
}

float get_shadow_pcf(float3 world_pos, int i, int samples, float width) {
    float4 shadow_pos = mul(lights[i].view_projection, float4(world_pos, 1.0));
    shadow_pos.xy /= shadow_pos.w;
    shadow_pos.xy = shadow_pos.xy * 0.5 + 0.5;
    shadow_pos.y = 1 - shadow_pos.y;
    shadow_pos.z += lights[i].bias;
    shadow_pos.z /= lights[i].far_plane;
    
    float w, h;
    shadowmaps[i].GetDimensions(w, h);

    float shadow = 0.0;
    float offset = (samples - 1.0) / 2.0;
    [unroll]
    for (float x = -offset; x <= offset; x += 1.0) 
    {
        [unroll]
        for (float y = -offset; y <= offset; y += 1.0) 
        {
            float2 pos = shadow_pos.xy + width * float2(x, y) / w;
            shadow += shadowmaps[i].SampleCmpLevelZero(shadow_sampler, pos, shadow_pos.z).r;
        }
    }

    shadow /= samples * samples;
    return shadow;
}

float3 fresnel_schlick(float3 f0, float f90, float NdotS) 
{
    return f0 + (f90 - f0) * pow(1.0f - NdotS, 5.0f);
}

float specular_ksk(Texture2D beckmann_lut, SamplerState tex_sampler, float3 normal, float3 light, float3 view, float roughness, float fresnel_strength) {
    float3 half_vec = view + light;
    float3 H = normalize(half_vec);

    float NoL = max(dot(normal, light), 0.0);
    float NoH = max(dot(normal, H), 0.0);
    float HoV = abs(dot(H, view));

    float ph = pow(2.0 * beckmann_lut.SampleLevel(tex_sampler, float2(NoH, roughness), 0).r, 10.0);
    float f = lerp(0.25, fresnel_schlick(0.028, 1.0, HoV).x, fresnel_strength);
    float ksk = max(ph * f / dot(half_vec, half_vec), 0.0);

    return NoL * ksk;
}

float3 normal_map(Texture2D tex, SamplerState tex_sampler, float2 uv) 
{
    float3 normal;
    normal.xy = -1.0 + 2.0 * tex.Sample(tex_sampler, uv).gr;
    normal.z = sqrt(1.0 - normal.x * normal.x - normal.y * normal.y);
    return normalize(normal);
}

FSOutput fs_main(FSInput input)
{
    FSOutput output = (FSOutput)0;

    input.normal = normalize(input.normal);
    input.tangent.xyz = normalize(input.tangent.xyz);
    float3 bitangent = cross(input.normal, input.tangent.xyz) * input.tangent.w;
    float3x3 tbn = float3x3(input.tangent.xyz, bitangent, input.normal);

    float3 view = normalize(push_constants.camera_pos - input.world_position);

    float4 basecolor = basecolor_texture.Sample(anisotropic_sampler, input.uv);
    float3 specular_ao = specular_texture.Sample(anisotropic_sampler, input.uv).rgb;

    // Transform bumped normal to world space, in order to use IBL for ambient lighting:
    const float bumpiness = 0.9;
    float3 tangent_space_normal = lerp(float3(0.0, 0.0, 1.0), normal_map(normal_texture, anisotropic_sampler, input.uv), bumpiness);
    float3 normal = mul(tangent_space_normal, tbn);

    const float specular_intensity = 1.88;
    const float specular_roughness = 0.3;
    const float specular_fresnel = 0.82;

    float intensity = specular_ao.r * specular_intensity;
    float roughness = (specular_ao.g / 0.3) * specular_roughness;

    float3 radiance = 0;

    for (uint i = 0; i < push_constants.num_lights; ++i)
    {
        Light l = lights[i];

        float3 light = l.position - input.world_position;
        float dist2 = dot(light, light);
        float dist = sqrt(dist2);
        light /= dist;

        float spot = dot(l.direction, light);

        if (spot > l.falloff_start)
        {        
            float curve = min(pow(dist / l.far_plane, 6.0), 1.0);
            float attenuation = lerp(1.0 / (1.0 + l.attenuation * dist2), 0.0, curve);
            spot = saturate((spot - l.falloff_start) / l.falloff_width);

            float3 f1 = l.color * attenuation * spot;
            float3 f2 = basecolor.rgb * f1;

            float diffuse = saturate(dot(normal, light));
            float specular = intensity * specular_ksk(beckmann_texture, linear_sampler, normal, light, view, roughness, specular_fresnel);

            float shadow = get_shadow_pcf(input.world_position, i, 3, 1.0);

            radiance += shadow * (f2 * diffuse + f1 * specular);
        }
    }

    output.color = float4(radiance, 1);

    return output;
}