//
//  ShaderTypes.h
//  realms Shared
//

#ifndef ShaderTypes_h
#define ShaderTypes_h

#ifdef __METAL_VERSION__
#define NS_ENUM(_type, _name) enum _name : _type _name; enum _name : _type
typedef metal::int32_t EnumBackingType;
#else
#import <Foundation/Foundation.h>
typedef NSInteger EnumBackingType;
#endif

#include <simd/simd.h>

typedef NS_ENUM(EnumBackingType, BufferIndex)
{
    BufferIndexMeshPositions = 0,
    BufferIndexMeshGenerics  = 1,
    BufferIndexUniforms      = 2
};

typedef NS_ENUM(EnumBackingType, VertexAttribute)
{
    VertexAttributePosition  = 0,
    VertexAttributeTexcoord  = 1,
};

typedef NS_ENUM(EnumBackingType, TextureIndex)
{
    TextureIndexColor    = 0,
};

typedef struct
{
    matrix_float4x4 projectionMatrix;
    matrix_float4x4 modelViewMatrix;
} Uniforms;

typedef struct
{
    simd_float4 eyeAspect;      // xyz = camera eye, w = viewport aspect
    simd_float4 anglesRoll;     // x = yaw, y = pitch, z = roll, w = unused
    simd_float4 lightDirection; // xyz = directional light vector
    simd_float4 lightColor;     // xyz = directional light linear RGB
    simd_float4 ambientColor;   // xyz = sky hemisphere linear RGB
    simd_int4   flags;          // x = texturesReady (0/1)
} SandboxSceneUniforms;

typedef struct
{
    matrix_float4x4 objectTransform;
    simd_float4     baseColorFactor;
    simd_float4     materialParams; // x = roughness, y = alphaCutoff
    simd_int4       drawFlags;      // x = modelPass, y = previewPass, z = modelTextured, w = alphaMode
} SandboxDrawUniforms;

// Realms map mode with the Quake 2 stack (quakeMapFragment, buffer 3):
// BSP lightmaps, Quake 2 entity light and the dynamic lights.
#define SANDBOX_MAX_DLIGHTS 32 // Quake 2's MAX_DLIGHTS
typedef struct
{
    simd_float4 lights[SANDBOX_MAX_DLIGHTS];      // xyz = render-space position, w = radius (metres)
    simd_float4 lightColors[SANDBOX_MAX_DLIGHTS]; // xyz = linear RGB
    simd_int4   modeCount;      // x = mode (0 sandbox sun, 1 lightmapped world,
                                //      2 entity lit by `light`, 3 fullbright),
                                // y = dynamic lights in use (0...SANDBOX_MAX_DLIGHTS),
                                // z = 1: the map's textures are on texture(3)
    simd_float4 light;          // xyz = Quake 2 light value at the entity (mode 2)
} SandboxQuakeUniforms;

typedef struct
{
    simd_float4 anglesRollAspect; // x = yaw, y = pitch, z = roll, w = aspect
} SandboxSkyUniforms;

typedef struct
{
    simd_float4 rectNdc;    // x = centerX, y = centerY, z = halfWidth, w = halfHeight
    simd_float4 borderSize; // x = borderU, y = borderV
} SandboxPipUniforms;

#ifndef __METAL_VERSION__
#include <TargetConditionals.h>
#if defined(__OBJC__) && TARGET_OS_IOS
#import "SandboxBridge.h"
#endif
#endif

#endif /* ShaderTypes_h */
