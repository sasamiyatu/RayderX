// Approximates the directional-hemispherical reflectance of the micriofacet specular BRDF with GG-X distribution
// Source: "Accurate Real-Time Specular Reflections with Radiance Caching" in Ray Tracing Gems by Hirvonen et al.
float3 specularGGXReflectanceApprox(float3 specularF0, float alpha, float NdotV)
{
	const float2x2 A = transpose(float2x2(
		0.995367f, -1.38839f,
		-0.24751f, 1.97442f
	));

	const float3x3 B = transpose(float3x3(
		1.0f, 2.68132f, 52.366f,
		16.0932f, -3.98452f, 59.3013f,
		-5.18731f, 255.259f, 2544.07f
	));

	const float2x2 C = transpose(float2x2(
		-0.0564526f, 3.82901f,
		16.91f, -11.0303f
	));

	const float3x3 D = transpose(float3x3(
		1.0f, 4.11118f, -1.37886f,
		19.3254f, -28.9947f, 16.9514f,
		0.545386f, 96.0994f, -79.4492f
	));

	const float alpha2 = alpha * alpha;
	const float alpha3 = alpha * alpha2;
	const float NdotV2 = NdotV * NdotV;
	const float NdotV3 = NdotV * NdotV2;

	const float E = dot(mul(A, float2(1.0f, NdotV)), float2(1.0f, alpha));
	const float F = dot(mul(B, float3(1.0f, NdotV, NdotV3)), float3(1.0f, alpha, alpha3));

	const float G = dot(mul(C, float2(1.0f, NdotV)), float2(1.0f, alpha));
	const float H = dot(mul(D, float3(1.0f, NdotV2, NdotV3)), float3(1.0f, alpha, alpha3));

	// Turn the bias off for near-zero specular 
	const float biasModifier = saturate(dot(specularF0, float3(0.333333f, 0.333333f, 0.333333f)) * 50.0f);

	const float bias = max(0.0f, (E / F)) * biasModifier;
	const float scale = max(0.0f, (G / H));

	return float3(bias, bias, bias) + float3(scale, scale, scale) * specularF0;
};