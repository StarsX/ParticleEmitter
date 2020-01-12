//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

// Output control point
struct DSCtrlPointIn
{
	float3	Pos	: POSITION;
	uint	VId	: VERTEXID;
};

// Output patch constant data.
struct HSConstDataOut
{
	float EdgeTessFactor[4]		: SV_TessFactor;
	float InsideTessFactor[2]	: SV_InsideTessFactor;
	float4 MinMaxPt				: AABB;
	float3x3 ToTangentSpace		: TANGENT_SPACE;
};

struct Emitter
{
	uint3 Indices;
	float2 Barycoord;
};

#define NUM_CONTROL_POINTS 3

//--------------------------------------------------------------------------------------
// Buffer
//--------------------------------------------------------------------------------------
RWStructuredBuffer<Emitter>	g_rwEmitters;

float determinant(float2 a, float2 b, float2 c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

[domain("quad")]
void main(
	HSConstDataOut input,
	float2 domain : SV_DomainLocation,
	const OutputPatch<DSCtrlPointIn, NUM_CONTROL_POINTS> patch)
{
	float3 v[3];
	[unroll] for (uint i = 0; i < 3; ++i)
		v[i] = mul(input.ToTangentSpace, patch[i].Pos);

	float3 p;
	p.xy = lerp(input.MinMaxPt.xy, input.MinMaxPt.zw, domain);
	p.z = v[0].z;

	// Triangle edge equation setup.
	const float a01 = v[0].y - v[1].y;
	const float b01 = v[1].x - v[0].x;
	const float a12 = v[1].y - v[2].y;
	const float b12 = v[2].x - v[1].x;
	const float a20 = v[2].y - v[0].y;
	const float b20 = v[0].x - v[2].x;

	// Calculate barycentric coordinates at min corner.
	float3 w;
	const float2 minPt = min(v[0].xy, min(v[1].xy, v[2].xy));
	w.x = determinant(v[1].xy, v[2].xy, minPt);
	w.y = determinant(v[2].xy, v[0].xy, minPt);
	w.z = determinant(v[0].xy, v[1].xy, minPt);

	// If pixel is inside of all edges, set vertex.
	const float2 dist = p.xy - minPt;
	w.x += (a12 * dist.x) + (b12 * dist.y);
	w.y += (a20 * dist.x) + (b20 * dist.y);
	w.z += (a01 * dist.x) + (b01 * dist.y);

	if (all(w <= 0.0))
	{
		// Normalize barycentric coordinates.
		const float area = determinant(v[0].xy, v[1].xy, v[2].xy);
		w /= area;

		Emitter emitter;
		emitter.Indices = uint3(patch[0].VId, patch[1].VId, patch[2].VId);
		emitter.Barycoord = w.xy;

		const uint i = g_rwEmitters.IncrementCounter();
		g_rwEmitters[i] = emitter;
	}
}
