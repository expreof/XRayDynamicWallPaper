//***************************************************************************************
// color.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Transforms and colors geometry.
//***************************************************************************************

cbuffer cbPerObject : register(b0)
{
	int cursorPosx;
	int cursorPosy;
};

Texture2D gCrate:register(t0);
Texture2D gPIC1:register(t1);

SamplerState gSamPoint:register(s0);

struct VertexIn
{
	float3 PosL  : POSITION;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
	float4 PosH  : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout;
	
	// Transform to homogeneous clip space.
	vout.PosH = float4(vin.PosL, 1.0f);
	
	// Just pass vertex color into the pixel shader.
    vout.TexC = vin.TexC;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 color = gCrate.Sample(gSamPoint,pin.TexC)*gPIC1.Sample(gSamPoint,pin.TexC);
	float2 o=float2(cursorPosx,cursorPosy);

	float scale=0.1;
	float radius=300;
	float dis=1-clamp((distance(o,pin.PosH.xy)-radius)*scale,0,1);
	return color*float4(dis,dis,dis,dis);
}


