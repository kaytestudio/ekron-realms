//
//  Shaders.metal
//  realms Shared
//

#include <metal_stdlib>
#include <simd/simd.h>
#import "ShaderTypes.h"

using namespace metal;

// Legacy template shaders retained for macOS / tvOS targets in realms Shared.
typedef struct
{
    float3 position [[attribute(VertexAttributePosition)]];
    float2 texCoord [[attribute(VertexAttributeTexcoord)]];
} Vertex;

typedef struct
{
    float4 position [[position]];
    float2 texCoord;
} ColorInOut;

vertex ColorInOut vertexShader(Vertex in [[stage_in]],
                               constant Uniforms & uniforms [[ buffer(BufferIndexUniforms) ]])
{
    ColorInOut out;
    float4 position = float4(in.position, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * position;
    out.texCoord = in.texCoord;
    return out;
}

fragment float4 fragmentShader(ColorInOut in [[stage_in]],
                               constant Uniforms & uniforms [[ buffer(BufferIndexUniforms) ]],
                               texture2d<half> colorMap     [[ texture(TextureIndexColor) ]])
{
    (void)uniforms;
    constexpr sampler colorSampler(mip_filter::linear,
                                   mag_filter::linear,
                                   min_filter::linear);
    half4 colorSample = colorMap.sample(colorSampler, in.texCoord.xy);
    return float4(colorSample);
}

// Ekron Native Urban Sandbox shaders (Metal port of sys/android Renderer.cpp).

struct PackedSandboxVertex
{
    packed_float3 position;
    packed_float3 color;
    packed_float3 normal;
    packed_float2 uv;
    float         layer;
};

struct PackedModelVertex
{
    packed_float3 position;
    packed_float3 normal;
    packed_float2 uv;
};

struct SandboxVaryings
{
    float4 position [[position]];
    float3 tint;
    float3 worldNormal;
    float2 texUV;
    int    layer [[flat]];
    float3 toEye;
};

static inline float4 projectSandboxPoint(float3 worldPos,
                                         constant SandboxSceneUniforms &scene,
                                         thread float3 &outToEye)
{
    float3 p = worldPos - scene.eyeAspect.xyz;
    outToEye = p;
    float yaw = scene.anglesRoll.x;
    float pitch = scene.anglesRoll.y;
    float roll = scene.anglesRoll.z;
    float aspect = max(scene.eyeAspect.w, 0.001f);

    float cy = cos(yaw), sy = sin(yaw);
    float cp = cos(pitch), sp = sin(pitch);
    float3 q = float3(cy * p.x - sy * p.z, p.y, sy * p.x + cy * p.z);
    q = float3(q.x, cp * q.y + sp * q.z, -sp * q.y + cp * q.z);

    float cr = cos(roll), sr = sin(roll);
    q.xy = float2(cr * q.x - sr * q.y, sr * q.x + cr * q.y);

    constexpr float focal = 1.37638f;
    // Metal [0, 1] clip-space depth for near = 0.08m, far = 300.0m.
    float zClip = -1.0002667f * q.z - 0.0800213f;
    return float4(q.x * focal / aspect, q.y * focal, zClip, -q.z);
}

vertex SandboxVaryings sandboxVertex(uint vid [[vertex_id]],
                                     device const PackedSandboxVertex *vertices [[buffer(0)]],
                                     constant SandboxSceneUniforms &scene [[buffer(1)]],
                                     constant SandboxDrawUniforms &draw [[buffer(2)]])
{
    SandboxVaryings out;
    PackedSandboxVertex v = vertices[vid];
    float3 worldPos = (draw.objectTransform * float4(float3(v.position), 1.0f)).xyz;
    float3x3 normalMatrix = float3x3(draw.objectTransform[0].xyz,
                                     draw.objectTransform[1].xyz,
                                     draw.objectTransform[2].xyz);
    out.position = projectSandboxPoint(worldPos, scene, out.toEye);
    out.tint = float3(v.color);
    out.worldNormal = normalMatrix * float3(v.normal);
    out.texUV = float2(v.uv);
    out.layer = int(v.layer + 0.5f);
    return out;
}

vertex SandboxVaryings modelVertex(uint vid [[vertex_id]],
                                   device const PackedModelVertex *vertices [[buffer(0)]],
                                   constant SandboxSceneUniforms &scene [[buffer(1)]],
                                   constant SandboxDrawUniforms &draw [[buffer(2)]])
{
    SandboxVaryings out;
    PackedModelVertex v = vertices[vid];
    float3 worldPos = (draw.objectTransform * float4(float3(v.position), 1.0f)).xyz;
    float3x3 normalMatrix = float3x3(draw.objectTransform[0].xyz,
                                     draw.objectTransform[1].xyz,
                                     draw.objectTransform[2].xyz);
    out.position = projectSandboxPoint(worldPos, scene, out.toEye);
    out.tint = float3(1.0f, 1.0f, 1.0f);
    out.worldNormal = normalMatrix * float3(v.normal);
    out.texUV = float2(v.uv);
    out.layer = 4;
    return out;
}

fragment float4 sandboxFragment(SandboxVaryings in [[stage_in]],
                                bool frontFacing [[front_facing]],
                                constant SandboxSceneUniforms &scene [[buffer(1)]],
                                constant SandboxDrawUniforms &draw [[buffer(2)]],
                                texture2d_array<float> albedoArray [[texture(0)]],
                                texture2d<float> modelAlbedo [[texture(1)]],
                                sampler modelSampler [[sampler(0)]])
{
    constexpr sampler arraySampler(coord::normalized,
                                   address::repeat,
                                   mag_filter::linear,
                                   min_filter::linear,
                                   mip_filter::linear);

    const bool texturesReady = scene.flags.x != 0;
    const bool modelPass = draw.drawFlags.x != 0;
    const bool previewPass = draw.drawFlags.y != 0;
    const bool modelTextured = draw.drawFlags.z != 0;
    const int alphaMode = draw.drawFlags.w;
    const float modelRoughness = draw.materialParams.x;
    const float alphaCutoff = draw.materialParams.y;

    float3 base = pow(max(in.tint, float3(0.0f)), float3(2.2f));
    if (!modelPass && texturesReady) {
        uint slice = uint(clamp(in.layer, 0, 7));
        base *= albedoArray.sample(arraySampler, in.texUV, slice).rgb;
    }
    if (!modelPass && in.layer != 4) {
        float3 world = in.toEye + scene.eyeAspect.xyz;
        base *= 0.94f + 0.035f * sin(world.x * 0.21f + world.z * 0.17f)
                      + 0.025f * sin(world.y * 0.33f - world.x * 0.13f);
    }
    if (!modelPass && in.layer == 7) {
        float pattern = fmod(floor(in.position.x) + floor(in.position.y) * 2.0f, 4.0f);
        if (pattern > 0.0f) {
            discard_fragment();
        }
    }

    float alpha = 1.0f;
    if (modelPass) {
        float4 sampleColor = modelTextured ? modelAlbedo.sample(modelSampler, in.texUV) : float4(1.0f);
        base = draw.baseColorFactor.rgb * sampleColor.rgb;
        alpha = draw.baseColorFactor.a * sampleColor.a;
        if (alphaMode == 1 && alpha < alphaCutoff) {
            discard_fragment();
        }
        if (alphaMode != 2) {
            alpha = 1.0f;
        }
    }

    if (previewPass) {
        base = mix(base, base * float3(0.35f, 1.25f, 0.55f) + float3(0.01f, 0.035f, 0.015f), 0.32f);
        alpha *= 0.62f;
    }

    float3 nLenSq = in.worldNormal;
    float3 n = length_squared(nLenSq) > 1e-12f ? normalize(in.worldNormal) : float3(0.0f, 1.0f, 0.0f);
    if (modelPass && !frontFacing) {
        n = -n;
    }

    float3 lightDir = normalize(-scene.lightDirection.xyz);
    float diffuse = max(dot(n, lightDir), 0.0f);
    float sky = 0.78f + 0.22f * max(n.y, 0.0f);
    float3 lit = base * (scene.ambientColor.xyz * sky + scene.lightColor.xyz * diffuse);

    float roughness = modelPass ? modelRoughness : (in.layer == 7 ? 0.18f : (in.layer == 3 ? 0.48f : 0.94f));
    float strength = modelPass ? 0.035f : (in.layer == 7 ? 0.28f : (in.layer == 3 ? 0.12f : 0.006f));
    float3 viewDir = length_squared(in.toEye) > 1e-12f ? -normalize(in.toEye) : float3(0.0f, 0.0f, 1.0f);
    float3 halfVector = normalize(lightDir + viewDir);
    float shine = pow(max(dot(n, halfVector), 0.0f), mix(90.0f, 8.0f, clamp(roughness, 0.0f, 1.0f)));
    lit += scene.lightColor.xyz * (shine * strength * diffuse);

    float d = max(length(in.toEye) - 22.0f, 0.0f);
    float fog = 1.0f - exp(-d * d * 0.00022f);
    float3 fogColor = pow(float3(0.56f, 0.61f, 0.63f), float3(2.2f));
    float3 finalLinear = max(mix(lit, fogColor, fog), float3(0.0f));
    return float4(pow(finalLinear, float3(1.0f / 2.2f)), alpha);
}

struct SkyVaryings
{
    float4 position [[position]];
    float2 screen;
};

vertex SkyVaryings skyVertex(uint vid [[vertex_id]])
{
    SkyVaryings out;
    float2 p = float2(float((vid << 1) & 2u), float(vid & 2u));
    out.screen = p * 2.0f - 1.0f;
    out.position = float4(out.screen, 0.9999f, 1.0f);
    return out;
}

fragment float4 skyFragment(SkyVaryings in [[stage_in]],
                            constant SandboxSkyUniforms &sky [[buffer(0)]])
{
    float pitch = sky.anglesRollAspect.y;
    float roll = sky.anglesRollAspect.z;
    float aspect = max(sky.anglesRollAspect.w, 0.001f);
    float3 ray = normalize(float3(in.screen.x * aspect / 1.37638f, in.screen.y / 1.37638f, -1.0f));
    float cr = cos(roll), sr = sin(roll);
    ray.xy = float2(cr * ray.x + sr * ray.y, -sr * ray.x + cr * ray.y);
    float altitude = cos(pitch) * ray.y - sin(pitch) * ray.z;
    float blend = smoothstep(0.0f, 0.85f, max(altitude, 0.0f));
    return float4(mix(float3(0.56f, 0.61f, 0.63f), float3(0.32f, 0.41f, 0.51f), blend), 1.0f);
}

struct PipVaryings
{
    float4 position [[position]];
    float2 uv;
};

vertex PipVaryings pipVertex(uint vid [[vertex_id]],
                             constant SandboxPipUniforms &pip [[buffer(0)]])
{
    const float2 corners[6] = {
        float2(-1.0f, -1.0f), float2( 1.0f, -1.0f), float2( 1.0f,  1.0f),
        float2(-1.0f, -1.0f), float2( 1.0f,  1.0f), float2(-1.0f,  1.0f)
    };
    float2 c = corners[vid % 6u];
    PipVaryings out;
    out.position = float4(pip.rectNdc.xy + c * pip.rectNdc.zw, 0.0f, 1.0f);
    out.uv = float2(c.x * 0.5f + 0.5f, 0.5f - c.y * 0.5f);
    return out;
}

fragment float4 pipFragment(PipVaryings in [[stage_in]],
                            constant SandboxPipUniforms &pip [[buffer(0)]],
                            texture2d<float> feedTexture [[texture(0)]])
{
    constexpr sampler feedSampler(coord::normalized,
                                  address::clamp_to_edge,
                                  mag_filter::linear,
                                  min_filter::linear);
    float2 bx = pip.borderSize.xy;
    if (in.uv.x < bx.x || in.uv.x > (1.0f - bx.x) || in.uv.y < bx.y || in.uv.y > (1.0f - bx.y)) {
        return float4(0.12f, 0.65f, 0.58f, 1.0f);
    }
    float2 innerUV = (in.uv - bx) / max(float2(1.0f) - 2.0f * bx, float2(0.001f));
    return float4(feedTexture.sample(feedSampler, innerUV).rgb, 1.0f);
}
