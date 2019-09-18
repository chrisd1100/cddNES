struct VS_OUTPUT {
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD;
};

SamplerState ss {
};

Texture2D frame: register(t0);

float4 main(VS_OUTPUT input) : SV_TARGET
{
	float4 rgba = frame.Sample(ss, input.texcoord);
	return float4(rgba.b, rgba.g, rgba.r, rgba.a);
}
