#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "HitProxies.h"
#include "RHIStaticStates.h"
#include "SceneManagement.h"
#include "Materials/Material.h"
#include "PostProcess/SceneRenderTargets.h"
#include "DBufferTextures.h"
#include "LightMapRendering.h"
#include "VelocityRendering.h"
#include "MeshMaterialShaderType.h"
#include "MeshMaterialShader.h"
#include "ShaderBaseClasses.h"
#include "FogRendering.h"
#include "TranslucentLighting.h"
#include "PlanarReflectionRendering.h"
#include "UnrealEngine.h"
#include "ReflectionEnvironment.h"
#include "Strata/Strata.h"
#include "OIT/OITParameters.h"
#include "VirtualShadowMaps/VirtualShadowMapArray.h"
#include "VolumetricCloudRendering.h"
#include "Nanite/NaniteMaterials.h"
#include "BlueNoise.h"
#include "StaticMeshBatch.h"

/** toon outline pass */

class FToonOutlineShaderVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FToonOutlineShaderVS,MeshMaterial);

public:

	FToonOutlineShaderVS() {}

	FToonOutlineShaderVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ToonOutlineThickness.Bind(Initializer.ParameterMap, TEXT("ToonOutlineThickness"));
	}

	static void ModifyCompilationEnvironment(const FShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return Parameters.MaterialParameters.bUseToonRendering;
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		float OutlineThickness = Material.GetToonOutlineThickness();

		ShaderBindings.Add(ToonOutlineThickness, OutlineThickness);
	}

	LAYOUT_FIELD(FShaderParameter, ToonOutlineThickness);

};

class FToonOutlineShaderPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FToonOutlineShaderPS, MeshMaterial);

public:

	FToonOutlineShaderPS() {}

	FToonOutlineShaderPS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ToonOutlineColor.Bind(Initializer.ParameterMap, TEXT("ToonOutlineColor"));
	}

	static void ModifyCompilationEnvironment(const FShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}


	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return Parameters.MaterialParameters.bUseToonRendering;
	}


	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);


		FLinearColor OutlineColor = Material.GetToonOutlineColor();

		ShaderBindings.Add(ToonOutlineColor, OutlineColor);
	}

	LAYOUT_FIELD(FShaderParameter, ToonOutlineColor);
};


class FToonOutlinePassProcessor : public FMeshPassProcessor
{
public:
	FToonOutlinePassProcessor(const FScene* InScene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext) :
		FToonOutlinePassProcessor(EMeshPass::Num, InScene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext) { }

	FToonOutlinePassProcessor(EMeshPass::Type InMeshPassType, const FScene* InScene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext) :
		FMeshPassProcessor(InMeshPassType, InScene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext) {}

	FToonOutlinePassProcessor(
		EMeshPass::Type InMeshPassType,
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FSceneView* InViewIfDynamicMeshCommand,
		const bool InbRespectUseAsOccluderFlag,
		const EDepthDrawingMode InEarlyZPassMode,
		const bool InbEarlyZPassMovable,
		/** Whether this mesh processor is being reused for rendering a pass that marks all fading out pixels on the screen */
		const bool bDitheredLODFadingOutMaskPass,
		FMeshPassDrawListContext* InDrawListContext,
		const bool bShadowProjection = false)
		: FMeshPassProcessor(InMeshPassType, Scene, FeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext) {}


	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:

	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);
};


/** toon shader pass */

class FToonShaderVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FToonShaderVS, MeshMaterial);

public:

	FToonShaderVS() {}

	FToonShaderVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{

	}


	static void ModifyCompilationEnvironment(const FShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}


	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return Parameters.MaterialParameters.bUseToonRendering;
	}


	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

	}
};


class FToonShaderPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FToonShaderPS, MeshMaterial);

public:

	FToonShaderPS() {}

	FToonShaderPS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ToonColor.Bind(Initializer.ParameterMap, TEXT("ToonColor"));
		ToonShininess.Bind(Initializer.ParameterMap, TEXT("ToonShininess"));
	}

	static void ModifyCompilationEnvironment(
		const FShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}


	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return Parameters.MaterialParameters.bUseToonRendering;
	}


	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy,
			MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		FLinearColor Color = Material.GetToonColor();

		float Shininess = Material.GetToonShininess();

		ShaderBindings.Add(ToonColor, Color);

		ShaderBindings.Add(ToonShininess, Shininess);
	}

	LAYOUT_FIELD(FShaderParameter, ToonColor);
	LAYOUT_FIELD(FShaderParameter, ToonShininess);
};


class FToonPassProcessor : public FMeshPassProcessor
{
public:

	FToonPassProcessor(const FScene* InScene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext) :
		FToonPassProcessor(EMeshPass::Num, InScene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext) { }

	FToonPassProcessor(EMeshPass::Type InMeshPassType, const FScene* InScene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext) :
		FMeshPassProcessor(InMeshPassType, InScene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext) {}

	FToonPassProcessor(
		EMeshPass::Type InMeshPassType,
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FSceneView* InViewIfDynamicMeshCommand,
		const bool InbRespectUseAsOccluderFlag,
		const EDepthDrawingMode InEarlyZPassMode,
		const bool InbEarlyZPassMovable,
		/** Whether this mesh processor is being reused for rendering a pass that marks all fading out pixels on the screen */
		const bool bDitheredLODFadingOutMaskPass,
		FMeshPassDrawListContext* InDrawListContext,
		const bool bShadowProjection = false)
		: FMeshPassProcessor(InMeshPassType, Scene, FeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext) {}


	virtual void AddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId = -1) override final;

private:

	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

};

