cbuffer SpriteConstants : register(CONSTANT_SLOT)
{
    float4x4 worldViewProjection;
    float4 tint;
    // xy scales the unit UV rectangle and zw offsets it into the selected atlas region.
    // Keeping this in constants lets frames share geometry while selecting different sprites.
    float4 uvTransform;
};

Texture2D spriteTexture : register(TEXTURE_SLOT);
SamplerState spriteSampler : register(SAMPLER_SLOT);

struct VS_INPUT
{
    float2 position : POSITION;
    float2 textureCoordinate : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 textureCoordinate : TEXCOORD;
};

VS_OUTPUT VS(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = mul(float4(input.position, 0.0f, 1.0f), worldViewProjection);
    output.textureCoordinate = input.textureCoordinate * uvTransform.xy + uvTransform.zw;
    return output;
}

float4 PS(VS_OUTPUT input) : SV_Target
{
    return spriteTexture.Sample(spriteSampler, input.textureCoordinate) * tint;
}
