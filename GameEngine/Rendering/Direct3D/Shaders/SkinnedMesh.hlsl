// 조명 계산은 Mesh.hlsl의 PS와 같은 코드다. 이 소스 컴파일 경로는 include 핸들러를 넘기지
// 않으므로(ShaderCompiler.cpp 주석 참고) 여기서 다시 적는다 — 둘을 고칠 때는 함께 고친다.

#define MAX_LIGHTS 3
#define MAX_BONES 128

struct Light
{
    // 방향광: xyz가 빛이 나아가는 방향, w = 0. 점광: xyz가 위치, w = 1.
    float4 positionOrDirection;
    // rgb는 세기를 곱한 색, w는 점광의 range.
    float4 colorAndRange;
};

cbuffer MeshConstants : register(CONSTANT_SLOT)
{
    float4x4 worldViewProjection;
    float4x4 world;
    float4x4 normalToWorld;
    // 알베도 텍스처에 곱해질 재질 색이다. 기본값은 흰색이라 곱해도 텍스처를 바꾸지 않는다.
    float4 tint;
    float4 ambientAndLightCount;
    Light lights[MAX_LIGHTS];
};

// 뼈마다 모델 공간 스키닝 행렬 하나다. MeshConstants와 별개 버퍼인 이유는 ShaderBindings.h의
// SkinnedMeshBoneConstantBufferSlot 주석에 있다.
cbuffer BoneConstants : register(BONE_CONSTANT_SLOT)
{
    float4x4 boneMatrices[MAX_BONES];
};

Texture2D albedoTexture : register(TEXTURE_SLOT);
SamplerState albedoSampler : register(SAMPLER_SLOT);

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD;
    uint4 boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD;
};

VS_OUTPUT VS(VS_INPUT input)
{
    // 네 뼈의 가중 평균이 이 정점의 모델 공간 스키닝 행렬이다. 가중치 합이 1이 아니어도 여기서
    // 다시 정규화하지 않는다 — 임포터가 임포트 시점에 보장할 계약이고, 매 정점·매 프레임 나눗셈을
    // 추가하는 대신 그 계약을 믿는다.
    const float4x4 skinMatrix =
        boneMatrices[input.boneIndices.x] * input.boneWeights.x +
        boneMatrices[input.boneIndices.y] * input.boneWeights.y +
        boneMatrices[input.boneIndices.z] * input.boneWeights.z +
        boneMatrices[input.boneIndices.w] * input.boneWeights.w;

    const float4 skinnedPosition = mul(float4(input.position, 1.0f), skinMatrix);
    // The blended skin transform can contain scale too. Its cofactor matrix is the inverse
    // transpose up to the determinant's magnitude, which normalization below removes.
    const float3 cofactor0 = cross(skinMatrix[1].xyz, skinMatrix[2].xyz);
    const float3 cofactor1 = cross(skinMatrix[2].xyz, skinMatrix[0].xyz);
    const float3 cofactor2 = cross(skinMatrix[0].xyz, skinMatrix[1].xyz);
    const float orientation = dot(skinMatrix[0].xyz, cofactor0) < 0.0f ? -1.0f : 1.0f;
    const float3 skinNormal = (input.normal.x * cofactor0 + input.normal.y * cofactor1 +
        input.normal.z * cofactor2) * orientation;
    const float4 skinnedNormal = float4(skinNormal, 0.0f);

    VS_OUTPUT output;
    output.position = mul(skinnedPosition, worldViewProjection);
    output.worldPosition = mul(skinnedPosition, world).xyz;
    output.normal = normalize(mul(skinnedNormal, normalToWorld).xyz);
    output.textureCoordinate = input.textureCoordinate;
    return output;
}

float4 PS(VS_OUTPUT input) : SV_Target
{
    const float3 normal = normalize(input.normal);
    float3 lighting = ambientAndLightCount.rgb;
    const int lightCount = (int) ambientAndLightCount.w;
    [unroll]
    for (int i = 0; i < MAX_LIGHTS; ++i)
    {
        if (i >= lightCount)
        {
            break;
        }
        const Light light = lights[i];
        float3 toLight;
        float attenuation;
        if (light.positionOrDirection.w > 0.5f)
        {
            const float3 offset = light.positionOrDirection.xyz - input.worldPosition;
            const float distance = length(offset);
            toLight = offset / max(distance, 0.0001f);
            // range에서 0이 되는 부드러운 감쇠: (1 - (d/r)^4)^2 / (1 + d^2). 물리 역제곱을
            // range 안에서 잘라 낸 흔한 꼴이다.
            const float ratio = distance / light.colorAndRange.w;
            const float window = saturate(1.0f - ratio * ratio * ratio * ratio);
            attenuation = window * window / (1.0f + distance * distance);
        }
        else
        {
            toLight = -normalize(light.positionOrDirection.xyz);
            attenuation = 1.0f;
        }
        lighting += light.colorAndRange.rgb * (saturate(dot(normal, toLight)) * attenuation);
    }
    return albedoTexture.Sample(albedoSampler, input.textureCoordinate) * tint *
        float4(lighting, 1.0f);
}
