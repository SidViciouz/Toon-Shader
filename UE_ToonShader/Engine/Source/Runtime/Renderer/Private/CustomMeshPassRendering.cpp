#include "CustomMeshPassRendering.h"
#include "DeferredShadingRenderer.h"
#include "DistortionRendering.h"
#include "DynamicPrimitiveDrawing.h"
#include "ScenePrivate.h"
#include "LightMapDensityRendering.h"
#include "MeshPassProcessor.inl"
#include "EditorPrimitivesRendering.h"
#include "TranslucentRendering.h"
#include "SingleLayerWaterRendering.h"
#include "Rendering/SkyAtmosphereCommonData.h"
#include "SceneTextureParameters.h"
#include "CompositionLighting/CompositionLighting.h"
#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "SceneViewExtension.h"
#include "VariableRateShadingImageManager.h"
#include "OneColorShader.h"
#include "ClearQuad.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "DebugProbeRendering.h"
#include "AnisotropyRendering.h"
#include "Nanite/NaniteVisualize.h"
#include "RenderCore.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "VolumetricFog.h"


/** toon outline pass */

IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonOutlineShaderVS, TEXT("/Engine/Private/ToonOutlineMeshPassShader.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonOutlineShaderPS, TEXT("/Engine/Private/ToonOutlineMeshPassShader.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_SHADERPIPELINE_TYPE_VSPS(ToonOutlineShaderPipeline, FToonOutlineShaderVS, FToonOutlineShaderPS, true);

void FToonOutlinePassProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

	if (MaterialRenderProxy && Material && Material->UseToonRendering())
	{
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode FillMode = ComputeMeshFillMode(*Material, OverrideSettings);
		const ERasterizerCullMode CullMode = ComputeMeshCullMode(*Material, OverrideSettings);

		Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, FillMode, CullMode);
	}
}


bool FToonOutlinePassProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	// pso에 포함되는 정보
	FMeshPassProcessorRenderState RenderState;
	RenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	RenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_Always>::GetRHI());

	// get shaders
	TMeshProcessorShaders<FToonOutlineShaderVS, FToonOutlineShaderPS> Shaders;
	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.AddShaderType<FToonOutlineShaderVS>();
	ShaderTypes.AddShaderType<FToonOutlineShaderPS>();
	FMaterialShaders MaterialShaders;
	if (!MaterialResource.TryGetShaders(ShaderTypes, MeshBatch.VertexFactory->GetType(), MaterialShaders))
		return false;
	MaterialShaders.TryGetVertexShader(Shaders.VertexShader);
	MaterialShaders.TryGetPixelShader(Shaders.PixelShader);

	// sort key
	FMeshDrawCommandSortKey SortKey = FMeshDrawCommandSortKey::Default;
	SortKey = CalculateMeshStaticSortKey(Shaders.VertexShader.GetShader(), Shaders.PixelShader.GetShader());


	// c++ FMeshMaterialShader로 이동해서 GetShaderBindings()에 입력되는 데이터
	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		RenderState,
		Shaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData
	);

	return true;
}

FMeshPassProcessor* CreateToonOutlinePassProcessor(ERHIFeatureLevel::Type FeatureLevel, const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	return new FToonOutlinePassProcessor(EMeshPass::ToonOutlinePass, Scene, FeatureLevel, InViewIfDynamicMeshCommand, true,
		EDepthDrawingMode::DDM_AllOpaque, true, true, InDrawListContext);
}

REGISTER_MESHPASSPROCESSOR_AND_PSOCOLLECTOR(ToonOutlinePass, CreateToonOutlinePassProcessor, EShadingPath::Deferred, EMeshPass::ToonOutlinePass, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);



BEGIN_SHADER_PARAMETER_STRUCT(FToonOutlinePassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FDeferredLightUniformStruct, DeferredLight)
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


FToonOutlinePassParameters* GetToonOutlinePassParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FRenderTargetBindingSlots& BasePassRenderTargets)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FToonOutlinePassParameters>();
	PassParameters->View = View.GetShaderParameters();
	PassParameters->RenderTargets = BasePassRenderTargets;
	return PassParameters;
}

void FDeferredShadingSceneRenderer::RenderToonOutlinePass(
	FRDGBuilder& GraphBuilder,
	FInstanceCullingManager& InstanceCullingManager,
	FSortedLightSetSceneInfo& SortedLightSet,
	FSceneTextures& SceneTextures,
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess)
{
	RDG_EVENT_SCOPE(GraphBuilder, "ToonOutlinePass");
	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderToonOutlinePass);



	TStaticArray<FTextureRenderTargetBinding, MaxSimultaneousRenderTargets> BasePassTextures;
	uint32 BasePassTextureCount = SceneTextures.GetGBufferRenderTargets(BasePassTextures);
	TArrayView<FTextureRenderTargetBinding> BasePassTexturesView = MakeArrayView(BasePassTextures.GetData(), BasePassTextureCount);
	const FExclusiveDepthStencil ExclusiveDepthStencil(BasePassDepthStencilAccess);
	FRDGTextureRef BasePassDepthTexture = SceneTextures.Depth.Target;

	FRenderTargetBindingSlots BasePassRenderTargets = GetRenderTargetBindings(ERenderTargetLoadAction::ELoad, BasePassTexturesView);
	BasePassRenderTargets.DepthStencil = FDepthStencilBinding(BasePassDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, ExclusiveDepthStencil);


	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		const bool bShouldRenderView = View.ShouldRenderView();

		if (bShouldRenderView)
		{
			View.BeginRenderView();

			FToonOutlinePassParameters* PassParameters = GetToonOutlinePassParameters(GraphBuilder, View, BasePassRenderTargets);

			View.ParallelMeshDrawCommandPasses[EMeshPass::ToonOutlinePass].BuildRenderingCommands(GraphBuilder, Scene->GPUScene, PassParameters->InstanceCullingDrawParams);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ToonOutlinePass"),
				PassParameters,
				ERDGPassFlags::Raster,
				[this, &View, PassParameters](FRHICommandList& RHICmdList)
			{
				SetStereoViewport(RHICmdList, View, 1.0f);
				View.ParallelMeshDrawCommandPasses[EMeshPass::ToonOutlinePass].DispatchDraw(nullptr, RHICmdList, &PassParameters->InstanceCullingDrawParams);
			});
		}
	}

}









/** toon shader pass */

void FToonPassProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

	if (MaterialRenderProxy && Material && Material->UseToonRendering())
	{
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode FillMode = ComputeMeshFillMode(*Material, OverrideSettings);
		const ERasterizerCullMode CullMode = ComputeMeshCullMode(*Material, OverrideSettings);

		Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, FillMode, CullMode);
	}

}

bool FToonPassProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	// pso에 포함되는 정보
	FMeshPassProcessorRenderState RenderState;
	RenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	RenderState.SetDepthStencilState(TStaticDepthStencilState<true,CF_DepthNearOrEqual>::GetRHI());

	// get shaders
	TMeshProcessorShaders<FToonShaderVS,FToonShaderPS> Shaders;
	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.AddShaderType<FToonShaderVS>();
	ShaderTypes.AddShaderType<FToonShaderPS>();
	FMaterialShaders MaterialShaders;
	if (!MaterialResource.TryGetShaders(ShaderTypes, MeshBatch.VertexFactory->GetType(), MaterialShaders))
		return false;
	MaterialShaders.TryGetVertexShader(Shaders.VertexShader);
	MaterialShaders.TryGetPixelShader(Shaders.PixelShader);

	// sort key
	FMeshDrawCommandSortKey SortKey = FMeshDrawCommandSortKey::Default;
	SortKey = CalculateMeshStaticSortKey(Shaders.VertexShader.GetShader(), Shaders.PixelShader.GetShader());


	// c++ FMeshMaterialShader로 이동해서 GetShaderBindings()에 입력되는 데이터
	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		RenderState,
		Shaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData
	);

	return true;
}

FMeshPassProcessor* CreateToonPassProcessor(ERHIFeatureLevel::Type FeatureLevel, const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	return new FToonPassProcessor(EMeshPass::ToonPass, Scene, FeatureLevel, InViewIfDynamicMeshCommand, true,
		EDepthDrawingMode::DDM_AllOpaque, true, true, InDrawListContext);
}

REGISTER_MESHPASSPROCESSOR_AND_PSOCOLLECTOR(ToonPass, CreateToonPassProcessor, EShadingPath::Deferred, EMeshPass::ToonPass, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);


IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonShaderVS, TEXT("/Engine/Private/ToonShader.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonShaderPS, TEXT("/Engine/Private/ToonShader.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_SHADERPIPELINE_TYPE_VSPS(ToonShaderPipeline, FToonShaderVS, FToonShaderPS, true);



BEGIN_SHADER_PARAMETER_STRUCT(FToonPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FDeferredLightUniformStruct, DeferredLight)
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


FToonPassParameters* GetToonPassParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FRenderTargetBindingSlots& BasePassRenderTargets )
{
	auto* PassParameters = GraphBuilder.AllocParameters<FToonPassParameters>();
	PassParameters->View = View.GetShaderParameters();
	PassParameters->RenderTargets = BasePassRenderTargets;
	return PassParameters;
}

void FDeferredShadingSceneRenderer::RenderToonPass(
	FRDGBuilder& GraphBuilder,
	FInstanceCullingManager& InstanceCullingManager,
	FSortedLightSetSceneInfo& SortedLightSet,
	FSceneTextures& SceneTextures,
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess)
{
	RDG_EVENT_SCOPE(GraphBuilder, "ToonPass");
	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderToonPass);

	TStaticArray<FTextureRenderTargetBinding, MaxSimultaneousRenderTargets> BasePassTextures;
	uint32 BasePassTextureCount = SceneTextures.GetGBufferRenderTargets(BasePassTextures);
	TArrayView<FTextureRenderTargetBinding> BasePassTexturesView = MakeArrayView(BasePassTextures.GetData(), BasePassTextureCount);
	const FExclusiveDepthStencil ExclusiveDepthStencil(BasePassDepthStencilAccess);
	FRDGTextureRef BasePassDepthTexture = SceneTextures.Depth.Target;

	FRenderTargetBindingSlots BasePassRenderTargets = GetRenderTargetBindings(ERenderTargetLoadAction::ELoad, BasePassTexturesView);
	BasePassRenderTargets.DepthStencil = FDepthStencilBinding(BasePassDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, ExclusiveDepthStencil);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		const bool bShouldRenderView = View.ShouldRenderView();

		if (bShouldRenderView)
		{
			View.BeginRenderView();

			FToonPassParameters* PassParameters = GetToonPassParameters(GraphBuilder, View, BasePassRenderTargets);

			View.ParallelMeshDrawCommandPasses[EMeshPass::ToonPass].BuildRenderingCommands(GraphBuilder, Scene->GPUScene, PassParameters->InstanceCullingDrawParams);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ToonPass"),
				PassParameters,
				ERDGPassFlags::Raster,
				[this, &View, PassParameters](FRHICommandList& RHICmdList)
			{
				SetStereoViewport(RHICmdList, View, 1.0f);
				View.ParallelMeshDrawCommandPasses[EMeshPass::ToonPass].DispatchDraw(nullptr, RHICmdList, &PassParameters->InstanceCullingDrawParams);
			});
		}
	}

}



/** Toon lighting shader*/

class FToonLightShaderVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FToonLightShaderVS, Global);

	SHADER_USE_PARAMETER_STRUCT(FToonLightShaderVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		//SHADER_PARAMETER_STRUCT_INCLUDE(FDrawFullScreenRectangleParameters, FullScreenRect)
		//SHADER_PARAMETER_STRUCT_INCLUDE(FStencilingGeometryShaderParameters::FParameters, Geometry)
		END_SHADER_PARAMETER_STRUCT()

public:

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

};


class FToonLightShaderPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FToonLightShaderPS, Global);

	SHADER_USE_PARAMETER_STRUCT(FToonLightShaderPS, FGlobalShader);


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, ForwardLightData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LightAttenuationTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LightAttenuationTextureSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LightingChannelsTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LightingChannelsSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenShadowMaskSubPixelTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FDeferredLightUniformStruct, DeferredLight)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

public:


	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};



IMPLEMENT_GLOBAL_SHADER(FToonLightShaderVS, "/Engine/Private/ToonLightingShader.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FToonLightShaderPS, "/Engine/Private/ToonLightingShader.usf", "MainPS", SF_Pixel);


BEGIN_SHADER_PARAMETER_STRUCT(FToonLightingParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FToonLightShaderVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FToonLightShaderPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()



static void RenderToonLight_Internal(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	FToonLightingParameters* PassParameters,
	const TCHAR* ShaderName)
{
	const FLightSceneProxy* RESTRICT LightProxy = LightSceneInfo->Proxy;
	const bool bTransmission = LightProxy->Transmission();
	const FSphere LightBounds = LightProxy->GetBoundingSphere();
	const ELightComponentType LightType = (ELightComponentType)LightProxy->GetLightType();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("%s", ShaderName),
		PassParameters,
		ERDGPassFlags::Raster,
		[Scene, &View, LightSceneInfo, PassParameters, LightBounds, LightType](FRHICommandList& RHICmdList)
	{

		const bool bIsRadial = LightType != LightType_Directional;

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		// Set the device viewport for the view.
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
		
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		TShaderMapRef<FToonLightShaderVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FToonLightShaderPS> PixelShader(View.ShaderMap);

		// Turn DBT back off
		GraphicsPSOInit.bDepthBounds = false;
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0x0);

		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);
		//FToonLightShaderVS::FParameters VSParameters = FToonLightShaderVS::GetParameters(View);
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);

		// Apply the directional light as a full screen quad
		DrawRectangle(
			RHICmdList,
			0, 0,
			View.ViewRect.Width(), View.ViewRect.Height(),
			View.ViewRect.Min.X, View.ViewRect.Min.Y,
			View.ViewRect.Width(), View.ViewRect.Height(),
			View.ViewRect.Size(),
			View.GetSceneTexturesConfig().Extent,
			VertexShader,
			EDRF_Default);
	}); // RenderPass
}

void FDeferredShadingSceneRenderer::RenderToonLight(
	FRDGBuilder& GraphBuilder,
	const FScene* SceneData,
	const FViewInfo& View,
	const FMinimalSceneTextures& SceneTextures,
	const FLightSceneInfo* LightSceneInfo,
	const TCHAR* ShaderName)
{
	FToonLightingParameters* PassParameter = GraphBuilder.AllocParameters< FToonLightingParameters>();
	PassParameter->PS.View = View.ViewUniformBuffer;
	PassParameter->PS.SceneTextures = SceneTextures.UniformBuffer;
	PassParameter->PS.RenderTargets[0] = FRenderTargetBinding(SceneTextures.Color.Target, ERenderTargetLoadAction::ELoad);
	PassParameter->VS.View = View.ViewUniformBuffer;

	auto* DeferredLightStruct = GraphBuilder.AllocParameters<FDeferredLightUniformStruct>();
	*DeferredLightStruct = GetDeferredLightParameters(View, *LightSceneInfo);
	PassParameter->PS.DeferredLight = GraphBuilder.CreateUniformBuffer(DeferredLightStruct);


	RenderToonLight_Internal(GraphBuilder, SceneData, View, LightSceneInfo, PassParameter, ShaderName);
}