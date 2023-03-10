// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/HoloPlaySceneCaptureComponent2D.h"
#include "Game/HoloPlayDrawFrustumComponent.h"
#include "HoloPlaySettings.h"
#include "Misc/HoloPlayLog.h"
#include "Misc/HoloPlayStats.h"

#include "SceneInterface.h"
#include "Engine/World.h"
#include "Math/UnrealMathUtility.h"
#include "Engine/TextureRenderTarget2D.h"

// For focus plane mesh and component
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "IHoloPlayRuntime.h" // for Editor/GameHoloPlayCaptureComponents and HoloPlayDisplayManager
#include "Managers/HoloPlayDisplayManager.h"

UHoloPlaySceneCaptureComponent2D::UHoloPlaySceneCaptureComponent2D()
{
	TextureTarget = nullptr;
	SetHiddenInGame(false);
	bCaptureEveryFrame = false;
	bUseCustomProjectionMatrix = true;
	bUseRayTracingIfEnabled = true;
	bAlwaysPersistRenderingState = true;
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

#if WITH_EDITOR
	DrawFrustum = nullptr;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/ArtTools/RenderToTexture/Meshes/S_1_Unit_Plane.S_1_Unit_Plane"));
	FocusPlaneVisualizationMesh = PlaneMesh.Object;

	static ConstructorHelpers::FObjectFinder<UMaterial> PlaneMat(TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent"));
	FocusPlaneVisualizationMaterial = PlaneMat.Object;
#endif // WITH_EDITOR

	// Init Config Values
	const UHoloPlaySettings* HoloPlaySettings = GetDefault<UHoloPlaySettings>();
	TilingValues = HoloPlaySettings->AutomaticSettings;
	CustomTilingValues = HoloPlaySettings->CustomSettings;
}

void UHoloPlaySceneCaptureComponent2D::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UHoloPlaySceneCaptureComponent2D* This = CastChecked<UHoloPlaySceneCaptureComponent2D>(InThis);
	for (FHoloPlayRenderingConfig& RenderingConfig : This->RenderingConfigs)
	{
		RenderingConfig.AddReferencedObjects(Collector);
	}
	Super::AddReferencedObjects(InThis, Collector);
}

void UHoloPlaySceneCaptureComponent2D::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITOR
	if (DrawFrustum == nullptr)
	{
		if (AActor* MyOwner = GetOwner())
		{
			// Note: if we'll use SetIsVisualizationComponent(true), this component will be hidden in actor's component tree.
			// Things which affects representation in tree:
			// - RF_Transactional | RF_TextExportTransient in NewObject
			// - SetIsVisualizationComponent(true)
			DrawFrustum = NewObject<UHoloPlayDrawFrustumComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			DrawFrustum->SetupAttachment(this);
			DrawFrustum->SetIsVisualizationComponent(true);
			DrawFrustum->CreationMethod = CreationMethod;
			DrawFrustum->RegisterComponentWithWorld(GetWorld());
		}
	}

	if (GIsEditor)
	{
		IHoloPlayRuntime::Get().EditorHoloPlayCaptureComponents.Insert(this, 0);
		// Reset scale if it was changed by property editor
		SetRelativeScale3D(FVector::OneVector);
	}

	RefreshVisualRepresentation();
#endif // WITH_EDITOR

	UpdateTillingProperties();
}

void UHoloPlaySceneCaptureComponent2D::OnUnregister()
{
	Super::OnUnregister();

#if WITH_EDITOR
	if (GIsEditor)
	{
		IHoloPlayRuntime::Get().EditorHoloPlayCaptureComponents.Remove(this);
	}
#endif // WITH_EDITOR
}

void UHoloPlaySceneCaptureComponent2D::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);

	ReleaseRenderingConfigs();

#if WITH_EDITOR
	if (DrawFrustum)
	{
		DrawFrustum->DestroyComponent();
	}
#endif
}

#if WITH_EDITOR
void UHoloPlaySceneCaptureComponent2D::RefreshVisualRepresentation()
{
	if (DrawFrustum != nullptr)
	{
		float AspectRatio = ( float ) TilingValues.TileSizeX / ( float ) TilingValues.TileSizeY;

		DrawFrustum->FrustumAngle = FOV;
		DrawFrustum->FrustumStartDist = NearClipPlane;
		DrawFrustum->FrustumMidDist = GetCameraDistance();
		DrawFrustum->FrustumEndDist = FarClipPlane;
		DrawFrustum->VerticalAngle = 0.0f;
		DrawFrustum->FrustumAspectRatio = AspectRatio;
		DrawFrustum->MidPlaneLineLength = MidPlaneLineLength;
		DrawFrustum->FrustumLineThickness = FrustumLineThickness;
		DrawFrustum->MidLineThickness = MidLineThickness;
		DrawFrustum->MarkRenderStateDirty();
		DrawFrustum->FrustumColor = FrustumColor;
		DrawFrustum->MidFrustumColor = MidPlaneFrustumColor;
	}
}

void UHoloPlaySceneCaptureComponent2D::CreateDebugFocusPlane()
{
	if (AActor* const MyOwner = GetOwner())
	{
		if (DebugFocusPlaneComponent == nullptr)
		{
			DebugFocusPlaneComponent = NewObject<UStaticMeshComponent>(MyOwner, NAME_None, RF_Transient | RF_Transactional | RF_TextExportTransient);
			DebugFocusPlaneComponent->SetupAttachment(this);
			DebugFocusPlaneComponent->SetIsVisualizationComponent(true);
			DebugFocusPlaneComponent->SetStaticMesh(FocusPlaneVisualizationMesh);
			DebugFocusPlaneComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			DebugFocusPlaneComponent->bHiddenInGame = false;
			DebugFocusPlaneComponent->CastShadow = false;
			DebugFocusPlaneComponent->CreationMethod = CreationMethod;
			DebugFocusPlaneComponent->bSelectable = false;

			DebugFocusPlaneComponent->SetRelativeScale3D_Direct(FVector(10000.f, 10000.f, 1.f));
			DebugFocusPlaneComponent->SetRelativeRotation_Direct(FRotator(90.f, 0.f, 0.f));

			DebugFocusPlaneComponent->RegisterComponentWithWorld(GetWorld());

			DebugFocusPlaneMID = DebugFocusPlaneComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, FocusPlaneVisualizationMaterial);
			if (DebugFocusPlaneMID)
			{
				DebugFocusPlaneMID->SetVectorParameterValue(FName(TEXT("Color")), DebugFocusPlaneColor.ReinterpretAsLinear());
			}
		}
	}
}

void UHoloPlaySceneCaptureComponent2D::DestroyDebugFocusPlane()
{
	if (DebugFocusPlaneComponent)
	{
		DebugFocusPlaneComponent->SetVisibility(false);
		DebugFocusPlaneComponent = nullptr;

		DebugFocusPlaneMID = nullptr;
	}
}

void UHoloPlaySceneCaptureComponent2D::UpdateDebugFocusPlane()
{
	if (FocusPlaneVisualizationMesh && DebugFocusPlaneComponent)
	{
		FVector const CamLocation = GetComponentTransform().GetLocation();
		FVector const CamDir = GetComponentTransform().GetRotation().Vector();

		UWorld const* const World = GetWorld();
		float const FocusDistance = GetCameraDistance() + DOFAdjust;
		FVector const FocusPoint = GetComponentTransform().GetLocation() + CamDir * FocusDistance;

		DebugFocusPlaneComponent->SetWorldLocation(FocusPoint);
	}
}
#endif // WITH_EDITOR

void UHoloPlaySceneCaptureComponent2D::BeginPlay()
{
	Super::BeginPlay();

	// Initialize TilingQuality (important when it's set to "Automatic")
	UpdateTillingProperties(TilingQuality);

	IHoloPlayRuntime::Get().GameHoloPlayCaptureComponents.Insert(this, 0);
}

// Called when the game ends
void UHoloPlaySceneCaptureComponent2D::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// It should automatically clean all render texture targets
	ReleaseRenderingConfigs();

	IHoloPlayRuntime::Get().GameHoloPlayCaptureComponents.Remove(this);
}

void UHoloPlaySceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	// This function is called by USceneCaptureComponent2D::CaptureSceneDeferred() and UpdateDeferredCaptures()
	// when any property of this component is changed. This capture is useless because it is executed in a
	// different way, also it causes engine to crash with NULL render target. So, let's just declare an empty
	// implementation. The crash was caused by PostEditChangeProperty or OnRegister calls to parent class.

	if (bAllow2DCapture)
	{
		Super::UpdateSceneCaptureContents(Scene);
	}
}

/** 
 * This is called when property is modified by InterpPropertyTracks
 *
 * @param PropertyThatChanged	Property that changed
 */
void UHoloPlaySceneCaptureComponent2D::PostInterpChange(FProperty* PropertyThatChanged)
{
	if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UHoloPlaySceneCaptureComponent2D, Size))
	{
		OnHoloPlayObjectChanged.ExecuteIfBound(this);
	}
	else
	{
		Super::PostInterpChange(PropertyThatChanged);
	}
}

// This function is called when "Size" property is changed in blueprint or interpolated by movie sequence
void UHoloPlaySceneCaptureComponent2D::SetSize(float InSize)
{
	Size = InSize;
	UpdateCameraPosition();
#if WITH_EDITOR
	UpdateDebugFocusPlane();
#endif
	OnHoloPlayObjectChanged.ExecuteIfBound(this);
}

#if WITH_EDITOR

void UHoloPlaySceneCaptureComponent2D::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		FName PropertyName = PropertyChangedEvent.Property->GetFName();

		if (PropertyName == GET_MEMBER_NAME_CHECKED(UHoloPlaySceneCaptureComponent2D, TilingQuality))
		{
			UpdateTillingProperties(TilingQuality);
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHoloPlayTilingQuality, TilesX) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FHoloPlayTilingQuality, TilesY) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FHoloPlayTilingQuality, QuiltW) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FHoloPlayTilingQuality, QuiltH) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UHoloPlaySceneCaptureComponent2D, bSingleViewMode)
			)
		{
			// Reset our render textures and configuration after it
			RebuildRenderConfigs();
		}

		// Check if Game is running
		if (GetWorld() && GetWorld()->IsGameWorld())
		{
			UpdateCameraPosition();
		}

		// Focus plane controls
		bool bDrawFocusPlane = bDrawDebugFocusPlane && bOverrideDOF;
		if (bDrawFocusPlane && (DebugFocusPlaneComponent == nullptr))
		{
			CreateDebugFocusPlane();
		}
		else if (!bDrawFocusPlane && (DebugFocusPlaneComponent != nullptr))
		{
			DestroyDebugFocusPlane();
		}
		// set focus plane color in case that's what changed
		if (DebugFocusPlaneMID)
		{
			DebugFocusPlaneMID->SetVectorParameterValue(FName(TEXT("Color")), DebugFocusPlaneColor.ReinterpretAsLinear());
		}
		UpdateDebugFocusPlane();

		OnHoloPlayObjectChanged.ExecuteIfBound(this);
	}
}

void UHoloPlaySceneCaptureComponent2D::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);

/*
	// This function is called when using gizmo to modify the component. We'll force
	// scale to identity value.
	FVector Scale = GetRelativeScale3D();
	if (!Scale.IsUnit())
	{
		// Performing reverse math from
		// - FLevelEditorViewportClient::ApplyDeltaToComponent
		// - UEditorEngine::ApplyDeltaToComponent
		//todo: Unfortunately it doesn't work without having direct access to editor internal objects, what's not possible in Runtime module
	}
*/
}

#endif // WITH_EDITOR

void UHoloPlaySceneCaptureComponent2D::CaptureHoloPlayScene(FHoloPlayRenderingConfig& RenderingConfig)
{
	UWorld* World = GetWorld();
	if (World && World->Scene && IsVisible())
	{
		//? We must push any deferred render state recreations before causing any rendering to happen, to make sure that deleted resource references are updated
		World->SendAllEndOfFrameUpdates();
		UpdateHoloPlaySceneCaptureContents(this, RenderingConfig, World->Scene);
	}
}

// Recalculates tiling values and sets new values used for frustum visualization
void UHoloPlaySceneCaptureComponent2D::UpdateCameraPosition()
{
	float CameraDistance = GetCameraDistance();

	// Compute clipping planes
	float NearClipDistance = FMath::Min(NearClipFactor * Size, CameraDistance);
	NearClipPlane = FMath::Max(CameraDistance - NearClipDistance, NearClipMin);

	float FarClipDistance = FMath::Max(FarClipFactor * Size, 0.0f);
	FarClipPlane = CameraDistance + FarClipDistance;

#if WITH_EDITOR
	RefreshVisualRepresentation();
#endif // WITH_EDITOR
}

// Calculate and return matrix used for camera projection matrices
FMatrix UHoloPlaySceneCaptureComponent2D::GenerateProjectionMatrix(float OffsetX, float OffsetY) const
{
	float HalfFOV = FMath::DegreesToRadians(FOV) / 2.0f;

	auto HoloPlayDisplayManager = IHoloPlayRuntime::Get().GetHoloPlayDisplayManager();
	const FHoloPlayDisplayMetrics::FCalibration& Calibration = HoloPlayDisplayManager->GetCalibrationSettings();
	int32 Width = Calibration.ScreenWidth;
	int32 Height = Calibration.ScreenHeight;

	float MaxZ = 0.f;
	float MinZ = 0.f;
	if (bUseFarClipPlane)
	{
		MaxZ = ((NearClipPlane == FarClipPlane) ? 0.0f : NearClipPlane / (NearClipPlane - FarClipPlane));
		MinZ = ((NearClipPlane == FarClipPlane) ? NearClipPlane : -FarClipPlane * NearClipPlane / (NearClipPlane - FarClipPlane));
	}
	else
	{
		MinZ = NearClipPlane;
	}

	FMatrix PerspectiveMatrix(
		FPlane(1.0f / FMath::Tan(HalfFOV), 0.0f, 0.0f, 0.0f),
		FPlane(0.0f, Width / FMath::Tan(HalfFOV) / Height, 0.0f, 0.0f),
		FPlane(OffsetX, OffsetY, MaxZ, 1.0f),
		FPlane(0.0f, 0.0f, MinZ, 0.0f)
	);

	UE_LOG(HoloPlayLogGame, Verbose, TEXT("PerspectiveMatrix: %s"), *PerspectiveMatrix.ToString());

	return PerspectiveMatrix;
}

float UHoloPlaySceneCaptureComponent2D::GetCameraDistance() const
{
	return Size / FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f));
}

void UHoloPlaySceneCaptureComponent2D::RebuildRenderConfigs()
{
	int32 NumTiles = TilingValues.GetNumTiles();
	int32 MaxViewCount = FHoloPlayRenderingConfig::MaxView;
	if (bSingleViewMode)
	{
		MaxViewCount = 1;
	}
	int32 NumConfiguraions = (NumTiles + MaxViewCount - 1) / MaxViewCount;

	// Do not rebuild render targets if nothing has been changed. Compare parameters which are considered
	// for building a new configuration set.
	if (CachedTilingValues == TilingValues && RenderingConfigs.Num() == NumConfiguraions)
	{
		return;
	}
	CachedTilingValues = TilingValues;

	// Release previous setup for building a new one
	ReleaseRenderingConfigs();

	FIntPoint ViewSize(TilingValues.TileSizeX, TilingValues.TileSizeY);

	uint32 MinTextureIndex = 0;

	for (int32 CurrentView = 0; CurrentView < NumTiles; CurrentView++)
	{
		// Add new render target
		if (((CurrentView + 1) % MaxViewCount == 0) ||		// end of row
			(CurrentView + 1 == NumTiles))					// the last tile
		{
			FHoloPlayRenderingConfig& Config = RenderingConfigs.AddDefaulted_GetRef();
			Config.Init(this, MinTextureIndex, CurrentView, ViewSize);

			// Prepare for the next row
			MinTextureIndex = CurrentView + 1;
		}
	}
	check(RenderingConfigs.Num() == NumConfiguraions);

	if (RenderingConfigs.Num() == 0)
	{
		UE_LOG(HoloPlayLogGame, Warning, TEXT("Something went wrong, RenderingConfigs never should be 0"));
	}
}

void UHoloPlaySceneCaptureComponent2D::ReleaseRenderingConfigs()
{
	RenderingConfigs.Empty();
}

void UHoloPlaySceneCaptureComponent2D::UpdateTillingProperties(EHoloPlayQualitySettings TilingSettings)
{
	// Save edited CustomTilingValues for later use
	if (TilingValues.Text == "Custom")
	{
		CustomTilingValues = TilingValues;
	}

	const UHoloPlaySettings* HoloPlaySettings = GetDefault<UHoloPlaySettings>();

	switch (TilingSettings)
	{
	case EHoloPlayQualitySettings::Q_PortraitHighRes:
	{
		TilingValues = HoloPlaySettings->PortraitHiResSettings;
		break;
	}
	case EHoloPlayQualitySettings::Q_Portrait:
	{
		TilingValues = HoloPlaySettings->PortraitSettings;
		break;
	}
	case EHoloPlayQualitySettings::Q_Automatic:
	{
		// Recognize the current device to apply corresponding settings
		auto HoloPlayDisplayManager = IHoloPlayRuntime::Get().GetHoloPlayDisplayManager();
		EHoloPlayDeviceType DeviceType = HoloPlayDisplayManager->GetCalibrationSettings().Type;
		switch (DeviceType)
		{
		case EHoloPlayDeviceType::FourK:
			TilingValues = HoloPlaySettings->FourKSettings;
			break;

		case EHoloPlayDeviceType::EightK:
			TilingValues = HoloPlaySettings->EightKSettings;
			break;

		case EHoloPlayDeviceType::EightPointNineInchLegacy:
			TilingValues = HoloPlaySettings->EightNineLegacy;
			break;

		default:
			TilingValues = HoloPlaySettings->PortraitSettings;

		}
		break;
	}
	case EHoloPlayQualitySettings::Q_FourK:
	{
		TilingValues = HoloPlaySettings->FourKSettings;
		break;
	}
	case EHoloPlayQualitySettings::Q_EightK:
	{
		TilingValues = HoloPlaySettings->EightKSettings;
		break;
	}
	case EHoloPlayQualitySettings::Q_EightPointNineLegacy:
	{
		TilingValues = HoloPlaySettings->EightNineLegacy;
		break;
	}
	case EHoloPlayQualitySettings::Q_Custom:
	{
		TilingValues = CustomTilingValues;
		break;
	}
	}

	// Finalize setup of TilingValues
	TilingValues.Setup();

	// Reset our render textures and configuration after it
	RebuildRenderConfigs();

	UpdateCameraPosition();
}

void UHoloPlaySceneCaptureComponent2D::SetupPostprocessing()
{
	// Setup all render targets
	float CamDistance = GetCameraDistance();

	if (bOverrideDOF)
	{
		// Focus Depth of Field effect on target point
		PostProcessBlendWeight = 1.0f;
		PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
		PostProcessSettings.DepthOfFieldFocalDistance = CamDistance + DOFAdjust;
		PostProcessSettings.bOverride_DepthOfFieldFstop = true;
		PostProcessSettings.DepthOfFieldFstop = DepthOfFieldFstop;
	}
	else
	{
		// Reset Depth of Field settings
		PostProcessBlendWeight = 0.0f;
		PostProcessSettings.bOverride_DepthOfFieldFocalDistance = false;
		PostProcessSettings.DepthOfFieldFocalDistance = 0;
		PostProcessSettings.bOverride_DepthOfFieldFstop = false;
	}

	ShowFlags.EnableAdvancedFeatures();
	ShowFlags.SetTemporalAA(false);

	// Enable or disable motion blur rendering
	ShowFlags.SetMotionBlur(bEnableMotionBlur);
}

// Called by HoloPlayViewportClient used for capturing new snapshot of scene from SceneCapture (RenderCamera)
void UHoloPlaySceneCaptureComponent2D::RenderViews()
{
	SetupPostprocessing();

	// Release RT used for 2D rendering, if any
	TextureTarget2DRendering = nullptr;

	// Setup all render targets
	float CamDistance = GetCameraDistance();

	// Get the ViewCone value for current device
	auto HoloPlayDisplayManager = IHoloPlayRuntime::Get().GetHoloPlayDisplayManager();
	const FHoloPlayDisplayMetrics::FCalibration& Calibration = HoloPlayDisplayManager->GetCalibrationSettings();
	float ViewCone = Calibration.ViewCone;
	float ViewConeSweep = CamDistance * FMath::Tan(FMath::DegreesToRadians(ViewCone));

	int32 NumTiles = TilingValues.GetNumTiles();

	// Compute rotation matrix
	FTransform Transform = GetComponentToWorld();

	// Remove the translation from Transform because we only need rotation.
	Transform.SetTranslation(FVector::ZeroVector);
	Transform.SetScale3D(FVector::OneVector);
	FMatrix ViewRotationMatrix = Transform.ToInverseMatrixWithScale();

	// Swap axis st. x=z,y=x,z=y (Unreal coordinate space) so that z is up
	ViewRotationMatrix = ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	for (FHoloPlayRenderingConfig& RenderingConfig : RenderingConfigs)
	{
		// Rendering target is initialized as 1x1 texture, so it won't take much space until rendering starts.
		// We should resize the target before rendering.
		RenderingConfig.PrepareRT();

		// Set render target texture to SceneCaptureComponent. The rendering code which is called from
		// this function receives RenderingConfig as input, but it relies on TextureTarget to be set
		TextureTarget = RenderingConfig.GetRenderTarget();

		int32 NumViews = RenderingConfig.GetNumViews();
		check(NumViews);

		for (int32 ViewIndex = 0; ViewIndex < NumViews; ++ViewIndex)
		{
			// If NumTiles is 1, take the center view
			float CurrentViewLerp = 0.f;
			if (NumTiles > 1)
			{
				CurrentViewLerp = (float)(RenderingConfig.GetFirstViewIndex() + ViewIndex) / (NumTiles - 1.f) - .5f;
			}

			float ViewOffsetX = CurrentViewLerp * ViewConeSweep;
			float ProjModifier = 1.0f / Size;
			float ProjOffsetX = ViewOffsetX * ProjModifier;

			UE_LOG(HoloPlayLogGame, Verbose, TEXT("ViewOffsetX: %f, ProjOffsetX: %f"), ViewOffsetX, ProjOffsetX);

			FTransform RelativeTransform = FTransform::Identity;
			RelativeTransform.SetTranslation(FVector(0.0f, ViewOffsetX, 0.0f));

			FTransform WorldTransform = GetComponentToWorld();
			WorldTransform = RelativeTransform * WorldTransform;
			FVector ViewLocation = WorldTransform.GetTranslation();

			FSceneCaptureViewInfo& ViewInfo = RenderingConfig.GetViewInfoArr()[ViewIndex];
			ViewInfo.ViewRotationMatrix = ViewRotationMatrix;
			ViewInfo.ViewLocation = ViewLocation;
			ViewInfo.ProjectionMatrix = GenerateProjectionMatrix(ProjOffsetX, 0.f);
		}

		// Render view
		SCOPE_CYCLE_COUNTER(STAT_CaptureScene_GameThread);
		CaptureHoloPlayScene(RenderingConfig);

		// Do not hold TextureTarget after rendering
		TextureTarget = nullptr;
	}
}

void UHoloPlaySceneCaptureComponent2D::Render2DView(int32 SizeX, int32 SizeY)
{
	SetupPostprocessing();

	auto HoloPlayDisplayManager = IHoloPlayRuntime::Get().GetHoloPlayDisplayManager();
	const FHoloPlayDisplayMetrics::FCalibration& Calibration = HoloPlayDisplayManager->GetCalibrationSettings();

	// Check if the texture is exists
	if (TextureTarget2DRendering == nullptr)
	{
		TextureTarget2DRendering = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), UTextureRenderTarget2D::StaticClass());
		TextureTarget2DRendering->InitCustomFormat(Calibration.ScreenWidth, Calibration.ScreenHeight, PF_A16B16G16R16, false);
		TextureTarget2DRendering->ClearColor = FLinearColor::Red;
		TextureTarget2DRendering->UpdateResourceImmediate();
	}

	if (SizeX < 0 && SizeY < 0)
	{
		SizeX = Calibration.ScreenWidth;
		SizeY = Calibration.ScreenHeight;
	}

	if (SizeX != TextureTarget2DRendering->SizeX || SizeY != TextureTarget2DRendering->SizeY)
	{
		TextureTarget2DRendering->ResizeTarget(SizeX, SizeY);
		TextureTarget2DRendering->UpdateResourceImmediate();
		// Sync with rendering thread, in order to reset texture resolution
		FlushRenderingCommands();
	}

	// Set 2D texture 
	TextureTarget = TextureTarget2DRendering;
	CustomProjectionMatrix = GenerateProjectionMatrix(0.f, 0.f);

	// Do the simple 2D rendering
	bAllow2DCapture = true;
	CaptureScene();
	bAllow2DCapture = false;

	// Do not hold TextureTarget after rendering
	TextureTarget = nullptr;
}


/*
 * FHoloPlayRenderingConfig
 */

void FHoloPlayRenderingConfig::CalculateViewRect(int32& MinX, int32& MinY, int32& MaxX, int32& MaxY, uint32 SizeX, uint32 SizeY, int32 ViewRows, int32 ViewColumns, int32 ViewIndex)
{
	MinX = (ViewIndex % ViewColumns) * SizeX;
	MaxX = MinX + SizeX;
	int32 Row = ViewIndex / ViewColumns;
	MinY = Row * SizeY;
	MaxY = MinY + SizeY;

	//UE_LOG(HoloPlayLogGame, Warning, TEXT("ViewRows %d, ViewColumns %d, ViewIndex %d, Row %d,  MinX %d, MaxX %d, MinY %d, MaxY %d"), ViewRows, ViewColumns, ViewIndex, Row, MinX, MaxX, MinY, MaxY);
}

void FHoloPlayRenderingConfig::CalculateViewRect(float& U, float& V, float& SizeU, float& SizeV, int32 ViewRows, int32 ViewColumns, int32 ViewCount, int32 ViewIndex)
{
	SizeU = 1.f / ViewColumns;
	SizeV = 1.f / ViewRows;

	U = (ViewIndex % ViewColumns) * SizeU;
	int32 Row = ViewIndex / ViewColumns;
	V = Row * SizeV;

	//UE_LOG(HoloPlayLogGame, Warning, TEXT("U %f, V %f, SizeU %f, SizeV %f, ViewRows %d, ViewColumns %d, ViewCount %d, ViewIndex %d"), U, V, SizeU, SizeV, ViewRows, ViewColumns, ViewCount, ViewIndex);
}

FHoloPlayRenderingConfig::FHoloPlayRenderingConfig()
	: RenderTarget(nullptr)
	, FirstViewIndex(0)
	, TextureSize(1, 1)
{
}

FHoloPlayRenderingConfig::~FHoloPlayRenderingConfig()
{
	Release();
}

void FHoloPlayRenderingConfig::Release()
{
	if (RenderTarget != nullptr && RenderTarget->IsValidLowLevel())
	{
		RenderTarget->ConditionalBeginDestroy();
	}

	RenderTarget = nullptr;
}

void FHoloPlayRenderingConfig::Init(UObject* Parent, uint32 InMinTextureIndex, uint32 InMaxTextureIndex, const FIntPoint& InViewSize)
{
	FirstViewIndex = InMinTextureIndex;
	NumViews = InMaxTextureIndex - InMinTextureIndex + 1;

	if (NumViews > 0)
	{
		static int32 GMaxTextureDimensionsLocal = (int32)GMaxTextureDimensions;
		check(InViewSize.X < GMaxTextureDimensionsLocal);
		check(InViewSize.Y < GMaxTextureDimensionsLocal);
		int32 TextureSizeX = float(InViewSize.X * NumViews);
		ViewRows = 1;
		if ((TextureSizeX - GMaxTextureDimensionsLocal) > 0)
		{
			ViewRows = FMath::RoundFromZero(float(NumViews) / float(GMaxTextureDimensionsLocal / InViewSize.X));
		}

		ViewColumns = FMath::RoundFromZero(float(NumViews) / float(ViewRows));
		TextureSize.X = InViewSize.X * ViewColumns;
		TextureSize.Y = InViewSize.Y * ViewRows;

		const FName TargetName = MakeUniqueObjectName(Parent, UTextureRenderTarget2D::StaticClass());
		// Make a new UTextureRenderTarget2D object. Note: adding RF_TextExportTransient to not let it go to Copy or Duplicate
		// operation, as this object will be regenerated by C++ code anyway. Plus, it bypasses a warning in UTextureRenderTarget2D::PostEditChangeProperty
		// saying (for each render target) that RT is very large and performance will be poor.
		RenderTarget = NewObject<UTextureRenderTarget2D>(Parent, TargetName, RF_Transient | RF_Transactional | RF_TextExportTransient);
		// Initialize with 1x1 texture. Resolution will be changed later, dynamically (in PrepareRT())
		RenderTarget->InitCustomFormat(1, 1, PF_A16B16G16R16, false);
		RenderTarget->ClearColor = FLinearColor::Red;
		RenderTarget->UpdateResourceImmediate();

		ViewInfoArr.AddZeroed(NumViews);
		for (int32 CaptureIndex = 0; CaptureIndex < ViewInfoArr.Num(); ++CaptureIndex)
		{
			ViewInfoArr[CaptureIndex].StereoPass = EStereoscopicPass::eSSP_FULL;
			ViewInfoArr[CaptureIndex].StereoIPD = 0.0f;

			int32 MinX = 0;
			int32 MinY = 0;
			int32 MaxX = 0;
			int32 MaxY = 0;
			CalculateViewRect(MinX, MinY, MaxX, MaxY, InViewSize.X, InViewSize.Y, ViewRows, ViewColumns, CaptureIndex);
			ViewInfoArr[CaptureIndex].ViewRect = FIntRect(MinX, MinY, MaxX, MaxY);
		}
	}
	else
	{
		UE_LOG(HoloPlayLogGame, Warning, TEXT("Wrong new number index"));
	}
}

void FHoloPlayRenderingConfig::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (RenderTarget != nullptr)
	{
		Collector.AddReferencedObject(RenderTarget);
	}
}

void FHoloPlayRenderingConfig::PrepareRT()
{
	check(RenderTarget != nullptr);
	RenderTarget->ResizeTarget(TextureSize.X, TextureSize.Y);
}

void FHoloPlayRenderingConfig::ReduceMemoryUse()
{
	if (RenderTarget != nullptr)
	{
		RenderTarget->ResizeTarget(1, 1);
	}
}
