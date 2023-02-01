#include "Render/HoloPlayViewportClient.h"
#include "Render/HoloPlayRendering.h"
#include "Game/HoloPlayCapture.h"
#include "Misc/HoloPlayLog.h"
#include "Misc/HoloPlayStats.h"
#include "IHoloPlayRuntime.h"
#include "Managers/HoloPlayDisplayManager.h"

#include "CanvasTypes.h"
#include "ClearQuad.h"

#include "CanvasItem.h"
#include "Engine.h"
#include "ImageUtils.h"

#include "Game/HoloPlaySceneCaptureComponent2D.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
#include "Runtime/Engine/Public/ScreenRendering.h"
#include "Engine/Console.h"

DECLARE_GPU_STAT_NAMED(CopyToQuilt, TEXT("Copy to quilt"));

void FHoloPlayScreenshotRequest::RequestScreenshot()
{
	// empty string means we'll later pick the name
	RequestScreenshot(TEXT(""), true);
}

void FHoloPlayScreenshotRequest::RequestScreenshot(const FString & InFilename, bool bAddFilenameSuffix)
{
	FString GeneratedFilename = InFilename;
	CreateViewportScreenShotFilename(GeneratedFilename);

	if (bAddFilenameSuffix)
	{
		const bool bRemovePath = false;
		GeneratedFilename = FPaths::GetBaseFilename(GeneratedFilename, bRemovePath);
		FFileHelper::GenerateNextBitmapFilename(GeneratedFilename, TEXT("png"), Filename);
	}
	else
	{
		Filename = GeneratedFilename;
		if (FPaths::GetExtension(Filename).Len() == 0)
		{
			Filename += TEXT(".png");
		}
	}
}

void FHoloPlayScreenshotRequest::CreateViewportScreenShotFilename(FString & InOutFilename)
{
	FString TypeName;

	TypeName = InOutFilename.IsEmpty() ? TEXT("Screenshot") : InOutFilename;
	check(!TypeName.IsEmpty());

	//default to using the path that is given
	InOutFilename = TypeName;
	if (!TypeName.Contains(TEXT("/")))
	{
		InOutFilename = GetDefault<UEngine>()->GameScreenshotSaveDirectory.Path / TypeName;
	}
}

TArray<FColor>* FHoloPlayScreenshotRequest::GetHighresScreenshotMaskColorArray()
{
	return &HighresScreenshotMaskColorArray;
}


void FHoloPlayLenticularScreenshotRequest::RequestScreenshot(bool bInShowUI)
{
	// empty string means we'll later pick the name
	RequestScreenshot(TEXT(""), bInShowUI, true);
}

void FHoloPlayLenticularScreenshotRequest::RequestScreenshot(const FString & InFilename, bool bInShowUI, bool bAddFilenameSuffix)
{
	FHoloPlayScreenshotRequest::RequestScreenshot(InFilename, bAddFilenameSuffix);

	// Register the screenshot
	if (!Filename.IsEmpty())
	{
		bShowUI = bInShowUI;
	}
}

FHoloPlayViewportClient::FHoloPlayViewportClient()
	: bIgnoreInput(false)
	, CurrentMouseCursor(EMouseCursor::Default)
	, QuiltRT(nullptr)
	, Viewport(nullptr)
{
}

FHoloPlayViewportClient::~FHoloPlayViewportClient()
{
}

void FHoloPlayViewportClient::Draw(FViewport * InViewport, FCanvas * InCanvas)
{
	check(IsInGameThread());

	SCOPE_CYCLE_COUNTER(STAT_Draw_GameThread);

	const UHoloPlaySettings* HoloPlaySettings = GetDefault<UHoloPlaySettings>();
	const FHoloPlayRenderingSettings& RenderingSettings = HoloPlaySettings->HoloPlayRenderingSettings;
	TWeakObjectPtr<UHoloPlaySceneCaptureComponent2D> HoloPlayCaptureComponent = GetGameHoloPlayCaptureComponent();

	// Clear entire canvas
	InCanvas->Clear(FLinearColor::Black);

	if (!HoloPlayCaptureComponent.IsValid())
	{
		InCanvas->Clear(FLinearColor::Blue);
		return;
	}
	
	// create render QuiltRT if not exists
	GetQuiltRT(HoloPlayCaptureComponent);

	if (HoloPlayCaptureComponent->GetRenderingConfigs().Num() == 0)
	{
		ensureMsgf(false, TEXT("There is no rendering configs"));
		InCanvas->Clear(FLinearColor::Green);
		return;
	}

	// If we render in 2D mode, just render one full view and return
	if (RenderingSettings.bRender2D == true)
	{
		HoloPlayCaptureComponent->Render2DView();

		UTextureRenderTarget2D* RenderTarget = HoloPlayCaptureComponent->GetTextureTarget2DRendering();

		HoloPlay::FRender2DViewContext Render2DViewContext =
		{
			InViewport,
			RenderTarget->Resource
		};

		ENQUEUE_RENDER_COMMAND(Render2DView)(
			[&, Render2DViewContext](FRHICommandListImmediate& RHICmdList)
		{
			HoloPlay::Render2DView_RenderThread(RHICmdList, Render2DViewContext);
		});

		return;
	}

	bool bIsOverrideQuiltTexture2D = HoloPlayCaptureComponent->GetOverrideQuiltTexture2D() != nullptr;
	// Process Screenshot 2Ds before offset Tiling scene capture
	ProcessScreenshot2D(HoloPlayCaptureComponent);

	// Copy to quilt. Render only if no quilt override
	if (!bIsOverrideQuiltTexture2D)
	{
		HoloPlayCaptureComponent->RenderViews();

		// Copy data from multiple render targets into a single quilt image
		uint32 CurrentViewIndex = 0;
		for (const FHoloPlayRenderingConfig& RenderingConfig : HoloPlayCaptureComponent->GetRenderingConfigs())
		{
			UTextureRenderTarget2D* RenderTarget = RenderingConfig.GetRenderTarget();
			if (RenderTarget == nullptr || RenderTarget->Resource == nullptr)
			{
				UE_LOG(HoloPlayLogRender, Error, TEXT("RenderTarget is null"));

				return;
			}

			for (int32 ViewIndex = 0; ViewIndex < RenderingConfig.GetViewInfoArr().Num(); ++ViewIndex)
			{
				HoloPlay::FCopyToQuiltRenderContext RenderContext =
				{
					QuiltRT->GameThread_GetRenderTargetResource(),
					HoloPlayCaptureComponent->GetTilingValues(),
					RenderTarget->Resource,
					CurrentViewIndex,
					ViewIndex,
					RenderingConfig.GetViewInfoArr().Num(),
					RenderingConfig.GetViewRows(),
					RenderingConfig.GetViewColumns(),
					RenderingConfig.GetViewInfoArr()[ViewIndex]
				};

				ENQUEUE_RENDER_COMMAND(CopyToQuiltCommand)(
					[&, RenderContext, CurrentViewIndex](FRHICommandListImmediate& RHICmdList)
				{
					SCOPE_CYCLE_COUNTER(STAT_CopyToQuiltShader_RenderThread);
					SCOPED_GPU_STAT(RHICmdList, CopyToQuilt);

					HoloPlay::CopyToQuiltShader_RenderThread(RHICmdList, RenderContext);
				});

				CurrentViewIndex++;
			}
		}
	}

	// Synchronize game and rendering thread before lenticular shader
	FlushRenderingCommands();

	// Process Quilt screenshot we should call. but ProcessScreenShots for Quilt Screenshots called in FViewport->Draw()
	ProcessScreenshotQuilts(QuiltRT);

	// Lenticular shader rendering
	HoloPlay::FLenticularRenderContext RenderContext =
	{
		InViewport,
		QuiltRT->GameThread_GetRenderTargetResource(),
		HoloPlayCaptureComponent->GetTilingValues(),
		bIsOverrideQuiltTexture2D ? HoloPlayCaptureComponent->GetOverrideQuiltTexture2D()->Resource : nullptr,
		DuplicateObject(GetDefault<UHoloPlaySettings>(), nullptr)
	};
	ENQUEUE_RENDER_COMMAND(RenderLenticularShaderCommand)(
		[&, RenderContext](FRHICommandListImmediate& RHICmdList)
	{
		SCOPE_CYCLE_COUNTER(STAT_RenderLenticularShader_RenderThread);
		HoloPlay::RenderLenticularShader_RenderThread(RHICmdList, RenderContext);
	});
}

bool FHoloPlayViewportClient::InputKey(FViewport * InViewport, int32 ControllerId, FKey Key, EInputEvent EventType, float AmountDepressed, bool bGamepad)
{
	GHoloPlayRuntime->OnHoloPlayInputKeyDelegate().Broadcast(InViewport, ControllerId, Key, EventType, AmountDepressed, bGamepad);

	auto HoloPlaySettings = GetDefault<UHoloPlaySettings>();

	// Process special input first
	if (Key == EKeys::Escape && EventType == EInputEvent::IE_Pressed)
	{
		IHoloPlayRuntime::Get().StopPlayer();
	}

	if (HoloPlaySettings->HoloPlayLenticularScreenshotSettings.InputKey == Key && EventType == EInputEvent::IE_Pressed)
	{
		PreparePlayLenticularScreenshot(HoloPlaySettings->HoloPlayLenticularScreenshotSettings.FileName, false, true);
	}

	if (HoloPlaySettings->HoloPlayScreenshotQuiltSettings.InputKey == Key && EventType == EInputEvent::IE_Pressed)
	{
		PreparePlayScreenshotQuilt(HoloPlaySettings->HoloPlayScreenshotQuiltSettings.FileName, true);
	}

	if (HoloPlaySettings->HoloPlayScreenshot2DSettings.InputKey == Key && EventType == EInputEvent::IE_Pressed)
	{
		PreparePlayScreenshot2D(HoloPlaySettings->HoloPlayScreenshot2DSettings.FileName, true);
	}

	if (IgnoreInput())
	{
		return false;
	}

	bool bResult = false;

	// Make sure we are playing in separate window
	if (IHoloPlayRuntime::Get().GetCurrentHoloPlayModeType() == EHoloPlayModeType::PlayMode_InSeparateWindow)
	{
		// Make sure we are in game play mode
		if (GEngine->GameViewport != nullptr)
		{
			ULocalPlayer* FirstLocalPlayerFromController = GEngine->GameViewport->GetWorld()->GetFirstLocalPlayerFromController();

			UE_LOG(HoloPlayLogInput, Verbose, TEXT(">> InputKey %s, FirstLocalPlayerFromController %p, ControllerId %d"), *Key.ToString(), FirstLocalPlayerFromController, ControllerId);

			if (FirstLocalPlayerFromController && FirstLocalPlayerFromController->PlayerController)
			{
				bResult = FirstLocalPlayerFromController->PlayerController->InputKey(Key, EventType, AmountDepressed, bGamepad);
			}

			// A gameviewport is always considered to have responded to a mouse buttons to avoid throttling
			if (!bResult && Key.IsMouseButton())
			{
				bResult = true;
			}
		}
	}


	return bResult;
}

bool FHoloPlayViewportClient::InputAxis(FViewport * InViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
{
	if (IgnoreInput())
	{
		return false;
	}

	if (GWorld == nullptr || GEngine == nullptr || GEngine->GameViewport == nullptr || &GEngine->GameViewport->Viewport == nullptr)
	{
		return false;
	}

	bool bResult = false;

	// Don't allow mouse/joystick input axes while in PIE and the console has forced the cursor to be visible.  It's
	// just distracting when moving the mouse causes mouse look while you are trying to move the cursor over a button
	// in the editor!
	if (!(GEngine->GameViewport->Viewport->IsSlateViewport() && GEngine->GameViewport->Viewport->IsPlayInEditorViewport()) || GEngine->GameViewport->ViewportConsole == NULL || !GEngine->GameViewport->ViewportConsole->ConsoleActive())
	{
		// route to subsystems that care
		if (GEngine->GameViewport->ViewportConsole != NULL)
		{
			bResult = GEngine->GameViewport->ViewportConsole->InputAxis(ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
		}
		if (!bResult)
		{
			ULocalPlayer* const TargetPlayer = GEngine->GetLocalPlayerFromControllerId(GEngine->GameViewport, ControllerId);
			if (TargetPlayer && TargetPlayer->PlayerController)
			{
				UE_LOG(HoloPlayLogInput, Verbose, TEXT(">> FHoloPlayViewportClient::InputAxis"));
				bResult = TargetPlayer->PlayerController->InputAxis(Key, Delta, DeltaTime, NumSamples, bGamepad);
			}
		}

		// For PIE, let the next PIE window handle the input if none of our players did
		// (this allows people to use multiple controllers to control each window)
		if (InViewport->IsPlayInEditorViewport())
		{
			UGameViewportClient *NextViewport = GEngine->GetNextPIEViewport(GEngine->GameViewport);
			if (NextViewport)
			{
				bResult = NextViewport->InputAxis(InViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
			}
		}

		if (InViewport->IsSlateViewport() && InViewport->IsPlayInEditorViewport())
		{
			// Absorb all keys so game input events are not routed to the Slate editor frame
			bResult = true;
		}
	}

	return bResult;
}

bool FHoloPlayViewportClient::InputChar(FViewport * InViewport, int32 ControllerId, TCHAR Character)
{
	return false;
}

bool FHoloPlayViewportClient::InputTouch(FViewport * InViewport, int32 ControllerId, uint32 Handle, ETouchType::Type Type, const FVector2D & TouchLocation, float Force, FDateTime DeviceTimestamp, uint32 TouchpadIndex)
{
	if (IgnoreInput())
	{
		return false;
	}

	if (GWorld == nullptr || GEngine == nullptr || GEngine->GameViewport == nullptr || &GEngine->GameViewport->Viewport == nullptr)
	{
		return false;
	}

	// route to subsystems that care
	bool bResult = (GEngine->GameViewport->ViewportConsole ? GEngine->GameViewport->ViewportConsole->InputTouch(ControllerId, Handle, Type, TouchLocation, Force, DeviceTimestamp, TouchpadIndex) : false);
	if (!bResult)
	{
		ULocalPlayer* const TargetPlayer = GEngine->GetLocalPlayerFromControllerId(GEngine->GameViewport, ControllerId);
		if (TargetPlayer && TargetPlayer->PlayerController)
		{
			UE_LOG(HoloPlayLogInput, Verbose, TEXT(">> FHoloPlayViewportClient::InputTouch TouchLocation %s"), *TouchLocation.ToString());
			bResult = TargetPlayer->PlayerController->InputTouch(Handle, Type, TouchLocation, Force, DeviceTimestamp, TouchpadIndex);
		}
	}

	return bResult;
}

bool FHoloPlayViewportClient::InputMotion(FViewport * InViewport, int32 ControllerId, const FVector & Tilt, const FVector & RotationRate, const FVector & Gravity, const FVector & Acceleration)
{
	return false;
}

void FHoloPlayViewportClient::RedrawRequested(FViewport * InViewport)
{
	Viewport->Draw();
}

bool FHoloPlayViewportClient::GetRenderTargetScreenShot(TWeakObjectPtr<UTextureRenderTarget2D> TextureRenderTarget2D, TArray<FColor>& Bitmap, const FIntRect& ViewRect)
{
	// Read the contents of the viewport into an array.
	auto ReadSurfaceDataFlags = FReadSurfaceDataFlags();
	ReadSurfaceDataFlags.SetLinearToGamma(false); // This is super important to disable this!

	bool bIsSuccess = false;
	FTextureRenderTargetResource* RenderTarget = TextureRenderTarget2D->GameThread_GetRenderTargetResource();
	if (RenderTarget->ReadPixels(Bitmap, ReadSurfaceDataFlags, ViewRect))
	{
		check(Bitmap.Num() == ViewRect.Area() || (Bitmap.Num() == TextureRenderTarget2D->SizeX * TextureRenderTarget2D->SizeY));
		for (FColor& color : Bitmap)
		{
			color.A = 255;
		}

		bIsSuccess = true;
	}

	return bIsSuccess;
}

static void ClipScreenshot(FIntVector& Size, FIntRect& SourceRect, TArray<FColor>& Bitmap)
{
	// Clip the bitmap to just the capture region if valid
	if (!SourceRect.IsEmpty())
	{
		FColor* const Data = Bitmap.GetData();
		const int32 OldWidth = Size.X;
		const int32 OldHeight = Size.Y;
		const int32 NewWidth = SourceRect.Width();
		const int32 NewHeight = SourceRect.Height();
		const int32 CaptureTopRow = SourceRect.Min.Y;
		const int32 CaptureLeftColumn = SourceRect.Min.X;

		for (int32 Row = 0; Row < NewHeight; Row++)
		{
			FMemory::Memmove(Data + Row * NewWidth, Data + (Row + CaptureTopRow) * OldWidth + CaptureLeftColumn, NewWidth * sizeof(*Data));
		}

		Bitmap.RemoveAt(NewWidth * NewHeight, OldWidth * OldHeight - NewWidth * NewHeight, false);
		Size = FIntVector(NewWidth, NewHeight, 0);
	}
}

void FHoloPlayViewportClient::ProcessScreenshotQuilts(UTextureRenderTarget2D* InQuiltRT)
{
	if (HoloPlayQuilScreenshotRequest.IsValid())
	{
		if (HoloPlayQuilScreenshotRequest->GetFilename().IsEmpty())
		{
			HoloPlayQuilScreenshotRequest.Reset();
			return;
		}

		TArray<FColor> Bitmap;
		bool bScreenshotSuccessful = GetRenderTargetScreenShot(InQuiltRT, Bitmap);
		FIntVector Size(InQuiltRT->SizeX, InQuiltRT->SizeY, 0);

		if (bScreenshotSuccessful)
		{
			FString ScreenShotName = HoloPlayQuilScreenshotRequest->GetFilename();

//			int32 ScreenshotResolutionX = GetDefault<UHoloPlaySettings>()->HoloPlayScreenshotQuiltSettings.Resolution.X;
//			int32 ScreenshotResolutionY = GetDefault<UHoloPlaySettings>()->HoloPlayScreenshotQuiltSettings.Resolution.Y;

//			if (ScreenshotResolutionX < 0) ScreenshotResolutionX = 0;
//			if (ScreenshotResolutionY < 0) ScreenshotResolutionY = 0;

//			FIntRect SourceRect(0, 0, ScreenshotResolutionX, ScreenshotResolutionY);
//			ClipScreenshot(Size, SourceRect, Bitmap);

			if (!FPaths::GetExtension(ScreenShotName).IsEmpty())
			{
				ScreenShotName = FPaths::GetBaseFilename(ScreenShotName, false);
				ScreenShotName += TEXT(".png");
			}

			// Save the contents of the array to a png file.
			TArray<uint8> CompressedBitmap;
			FImageUtils::CompressImageArray(Size.X, Size.Y, Bitmap, CompressedBitmap);
			FFileHelper::SaveArrayToFile(CompressedBitmap, *ScreenShotName);
		}

		HoloPlayQuilScreenshotRequest.Reset();
		OnScreenshotQuiltRequestProcessed().Broadcast();
	}
}

bool FHoloPlayViewportClient::ProcessScreenShotLenticular(FViewport * InViewport)
{
	if (HoloPlayLenticularScreenshotRequest.IsValid())
	{
		if (HoloPlayLenticularScreenshotRequest->GetFilename().IsEmpty())
		{
			HoloPlayLenticularScreenshotRequest.Reset();
			return false;
		}

		TArray<FColor> Bitmap;
		bool bShowUI = false;
		TSharedPtr<SWindow> WindowPtr = GetWindow();
		if (HoloPlayLenticularScreenshotRequest->ShouldShowUI() && WindowPtr.IsValid())
		{
			bShowUI = true;
		}

		bool bScreenshotSuccessful = false;
		FIntVector Size(InViewport->GetSizeXY().X, InViewport->GetSizeXY().Y, 0);
		if (bShowUI && FSlateApplication::IsInitialized())
		{
			TSharedRef<SWidget> WindowRef = WindowPtr.ToSharedRef();
			bScreenshotSuccessful = FSlateApplication::Get().TakeScreenshot(WindowRef, Bitmap, Size);
			GScreenshotResolutionX = Size.X;
			GScreenshotResolutionY = Size.Y;
		}
		else
		{
			bScreenshotSuccessful = GetViewportScreenShot(InViewport, Bitmap);
		}

		if (bScreenshotSuccessful)
		{
			for (FColor& color : Bitmap)
			{
				color.A = 255;
			}

			FString ScreenShotName = HoloPlayLenticularScreenshotRequest->GetFilename();

//			int32 ScreenshotResolutionX = GetDefault<UHoloPlaySettings>()->HoloPlayLenticularScreenshotSettings.Resolution.X;
//			int32 ScreenshotResolutionY = GetDefault<UHoloPlaySettings>()->HoloPlayLenticularScreenshotSettings.Resolution.Y;

//			if (ScreenshotResolutionX < 0) ScreenshotResolutionX = 0;
//			if (ScreenshotResolutionY < 0) ScreenshotResolutionY = 0;

//			FIntRect SourceRect(0, 0, ScreenshotResolutionX, ScreenshotResolutionY);
//			ClipScreenshot(Size, SourceRect, Bitmap);

			if (!FPaths::GetExtension(ScreenShotName).IsEmpty())
			{
				ScreenShotName = FPaths::GetBaseFilename(ScreenShotName, false);
				ScreenShotName += TEXT(".png");
			}

			// Save the contents of the array to a png file.
			TArray<uint8> CompressedBitmap;
			FImageUtils::CompressImageArray(Size.X, Size.Y, Bitmap, CompressedBitmap);
			FFileHelper::SaveArrayToFile(CompressedBitmap, *ScreenShotName);
		}

		HoloPlayLenticularScreenshotRequest.Reset();
		OnScreenshot3DRequestProcessed().Broadcast();

		return true;
	}

	return false;
}

void FHoloPlayViewportClient::ProcessScreenshot2D(TWeakObjectPtr<UHoloPlaySceneCaptureComponent2D> HoloPlayCaptureComponent)
{
	if (HoloPlayScreenshot2DRequest.IsValid())
	{
		FString ScreenShotName = HoloPlayScreenshot2DRequest->GetFilename();

		if (ScreenShotName.IsEmpty())
		{
			HoloPlayScreenshot2DRequest.Reset();
			return;
		}

		int32 ScreenshotResolutionX = GetDefault<UHoloPlaySettings>()->HoloPlayScreenshot2DSettings.Resolution.X;
		int32 ScreenshotResolutionY = GetDefault<UHoloPlaySettings>()->HoloPlayScreenshot2DSettings.Resolution.Y;

		if (ScreenshotResolutionX <= 0 || ScreenshotResolutionY <= 0)
		{
			return;
		}

		// Render picture
		HoloPlayCaptureComponent->Render2DView(ScreenshotResolutionX, ScreenshotResolutionY);
		// grab the render target where picture was rendered
		UTextureRenderTarget2D* RenderTarget = HoloPlayCaptureComponent->GetTextureTarget2DRendering();

		TArray<FColor> Bitmap;
		bool bScreenshotSuccessful = GetRenderTargetScreenShot(RenderTarget, Bitmap);

		if (bScreenshotSuccessful)
		{
			if (!FPaths::GetExtension(ScreenShotName).IsEmpty())
			{
				ScreenShotName = FPaths::GetBaseFilename(ScreenShotName, false);
				ScreenShotName += TEXT(".png");
			}

			// Save the contents of the array to a png file.
			TArray<uint8> CompressedBitmap;
			FImageUtils::CompressImageArray(RenderTarget->SizeX, RenderTarget->SizeY, Bitmap, CompressedBitmap);
			FFileHelper::SaveArrayToFile(CompressedBitmap, *ScreenShotName);
		}

		HoloPlayScreenshot2DRequest.Reset();
		OnScreenshot2DRequestProcessed().Broadcast();
	}
}

EMouseCursor::Type FHoloPlayViewportClient::GetCursor(FViewport* InViewport, int32 X, int32 Y)
{
	return CurrentMouseCursor;
}

bool FHoloPlayViewportClient::IsFocused(FViewport* InViewport)
{
	return InViewport->HasFocus() || InViewport->HasMouseCapture();
}

void FHoloPlayViewportClient::LostFocus(FViewport* InViewport)
{
	CurrentMouseCursor = EMouseCursor::Default;
}

void FHoloPlayViewportClient::ReceivedFocus(FViewport* InViewport)
{
	CurrentMouseCursor = EMouseCursor::None;
}

bool FHoloPlayViewportClient::Exec(UWorld * InWorld, const TCHAR * Cmd, FOutputDevice & Ar)
{
	if (FParse::Command(&Cmd, TEXT("HoloPlay.LenticularScreenshot")))
	{
		return HandleLenticularScreenshotCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("HoloPlay.ScreenshotQuilt")))
	{
		return HandleScreenshotQuiltCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("HoloPlay.Screenshot2D")))
	{
		return HandleScreenshot2DCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("HoloPlay.Window")))
	{
		return HandleWindowCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("HoloPlay.Shader")))
	{
		return HandleShaderCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("HoloPlay.Scene")))
	{
		return HandleSceneCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("HoloPlay.Tilling")))
	{
		return HandleTillingCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("HoloPlay.Rendering")))
	{
		return HandleRenderingCommand(Cmd, Ar);
	}
	else
	{
		return false;
	}
}

bool FHoloPlayViewportClient::HandleLenticularScreenshotCommand(const TCHAR * Cmd, FOutputDevice & Ar)
{
	if (Viewport)
	{
		FString FileName;
		bool bAddFilenameSuffix = true;
		ParseScreenshotCommand(Cmd, FileName, bAddFilenameSuffix);

		return PreparePlayLenticularScreenshot(FileName, false, bAddFilenameSuffix);
	}
	return true;
}

bool FHoloPlayViewportClient::HandleScreenshotQuiltCommand(const TCHAR * Cmd, FOutputDevice & Ar)
{
	if (Viewport)
	{
		FString FileName;
		bool bAddFilenameSuffix = true;
		ParseScreenshotCommand(Cmd, FileName, bAddFilenameSuffix);

		return PreparePlayScreenshotQuilt(FileName, bAddFilenameSuffix);
	}
	return true;
}

bool FHoloPlayViewportClient::HandleScreenshot2DCommand(const TCHAR * Cmd, FOutputDevice & Ar)
{
	if (Viewport)
	{
		FString FileName;
		bool bAddFilenameSuffix = true;
		ParseScreenshotCommand(Cmd, FileName, bAddFilenameSuffix);

		return PreparePlayScreenshot2D(FileName, bAddFilenameSuffix);
	}
	return true;
}

bool FHoloPlayViewportClient::HandleWindowCommand(const TCHAR * Cmd, FOutputDevice & Ar)
{
	UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();

	bool bWasHandled = false;

	if (FParse::Command(&Cmd, TEXT("ClientSize")))
	{
		uint32 X = 0;
		uint32 Y = 0; 
		ParseResolution(Cmd, X, Y);

		if (X > 0 && Y > 0)
		{
			HoloPlaySettings->HoloPlayWindowSettings.CustomWindowLocation.ClientSize = FIntPoint(X, Y);
		}

		// Restart player
		IHoloPlayRuntime::Get().RestartPlayer(HoloPlaySettings->HoloPlayWindowSettings.LastExecutedPlayModeType);

		bWasHandled = true;
	}
	if (FParse::Command(&Cmd, TEXT("PlacementMode")))
	{
		if (FString(Cmd).IsNumeric())
		{
			int32 NewVal = FCString::Atoi(*FString(Cmd));
			HoloPlaySettings->HoloPlayWindowSettings.PlacementMode = (EHoloPlayPlacement)NewVal;
			bWasHandled = true;
		}
	}

	if (bWasHandled)
	{
		HoloPlaySettings->HoloPlaySave();
	}

	return bWasHandled;

	return true;
}

bool FHoloPlayViewportClient::HandleShaderCommand(const TCHAR * Cmd, FOutputDevice & Ar)
{
	UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();
	auto HoloPlayDisplayManager = IHoloPlayRuntime::Get().GetHoloPlayDisplayManager();
	FHoloPlayDisplayMetrics::FCalibration& Calibration = HoloPlayDisplayManager->GetCalibrationSettingsMutable();

	bool bWasHandled = true;
	if (FParse::Command(&Cmd, TEXT("QuiltMode")))
	{
		int32 NewVal = FCString::Atoi(*FString(Cmd));
		HoloPlaySettings->HoloPlayRenderingSettings.QuiltMode = (NewVal != 0);
	}
	else if (FParse::Command(&Cmd, TEXT("Pitch")))
	{
		float NewVal = FCString::Atof(*FString(Cmd));
		Calibration.Pitch = NewVal;
	}
	else if (FParse::Command(&Cmd, TEXT("Center")))
	{
		float NewVal = FCString::Atof(*FString(Cmd));
		Calibration.Center = NewVal;
	}
	else if (FParse::Command(&Cmd, TEXT("ViewCone")))
	{
		float NewVal = FCString::Atof(*FString(Cmd));
		Calibration.ViewCone = NewVal;
	}
	else if (FParse::Command(&Cmd, TEXT("DPI")))
	{
		float NewVal = FCString::Atof(*FString(Cmd));
		Calibration.DPI = NewVal;
	}
//	else if (FParse::Command(&Cmd, TEXT("FlipImageX")))
//	{
//		float NewVal = FCString::Atof(*FString(Cmd));
//		HoloPlaySettings->HoloPlayShaderSettings.FlipImageX = (NewVal != 0);
//	}
	else if (FParse::Command(&Cmd, TEXT("CustomAspect")))
	{
		int32 NewVal = FCString::Atoi(*FString(Cmd));
		HoloPlaySettings->HoloPlayRenderingSettings.bUseCustomAspect = !!NewVal;
	}
	else if (FParse::Command(&Cmd, TEXT("CustomAspectX")))
	{
		float NewVal = FCString::Atof(*FString(Cmd));
		HoloPlaySettings->HoloPlayRenderingSettings.CustomAspect.X = NewVal;
	}
	else if (FParse::Command(&Cmd, TEXT("CustomAspectY")))
	{
		float NewVal = FCString::Atof(*FString(Cmd));
		HoloPlaySettings->HoloPlayRenderingSettings.CustomAspect.Y = NewVal;
	}
	else
	{
		bWasHandled = false;
	}

	if (bWasHandled)
	{
		HoloPlaySettings->HoloPlaySave();
	}

	return bWasHandled;
}

bool FHoloPlayViewportClient::HandleSceneCommand(const TCHAR * Cmd, FOutputDevice & Ar)
{
	return true;
}

bool FHoloPlayViewportClient::HandleTillingCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
	UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();
	auto GameHoloPlayCaptureComponent = GetGameHoloPlayCaptureComponent();

	if (!GameHoloPlayCaptureComponent.IsValid())
	{
		UE_LOG(HoloPlayLogInput, Verbose, TEXT(">> HoloPlayCaptureComponent is not valid"));
		return false;
	}

	EHoloPlayQualitySettings TilingSettings;
	if (FParse::Command(&Cmd, TEXT("Automatic")))
	{
		TilingSettings = EHoloPlayQualitySettings::Q_Automatic;
	}
    else if ( FParse::Command( &Cmd, TEXT( "Portrait" ) ) )
    {
		TilingSettings = EHoloPlayQualitySettings::Q_Portrait;
    }
	else if (FParse::Command(&Cmd, TEXT("PortraitHiRes")))
	{
		TilingSettings = EHoloPlayQualitySettings::Q_PortraitHighRes;
	}
	else if (FParse::Command(&Cmd, TEXT("FourK")))
	{
		TilingSettings = EHoloPlayQualitySettings::Q_FourK;
	}
	else if (FParse::Command(&Cmd, TEXT("EightK")))
	{
		TilingSettings = EHoloPlayQualitySettings::Q_EightK;
	}
	else if (FParse::Command(&Cmd, TEXT("EightNineLegacy")))
	{
		TilingSettings = EHoloPlayQualitySettings::Q_EightPointNineLegacy;
	}
	else
	{
		UE_LOG(HoloPlayLogInput, Verbose, TEXT("Unknown tiling settings mode %s"), Cmd);
		return false;
	}

	GameHoloPlayCaptureComponent->UpdateTillingProperties(TilingSettings);
	HoloPlaySettings->HoloPlaySave();

	return true;
}

bool FHoloPlayViewportClient::HandleRenderingCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
	UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();

	bool bWasHandled = true;

	if (FParse::Command(&Cmd, TEXT("Render2D")))
	{
		if (FString(Cmd).IsNumeric())
		{
			int32 NewVal = FCString::Atoi(*FString(Cmd));
			HoloPlaySettings->HoloPlayRenderingSettings.bRender2D = !!NewVal;
		}
	}

	else
	{
		bWasHandled = false;
	}

	if (bWasHandled)
	{
		HoloPlaySettings->HoloPlaySave();
	}

	return bWasHandled;
}

void FHoloPlayViewportClient::ParseScreenshotCommand(const TCHAR * Cmd, FString& InName, bool& InSuffix)
{
	FString CmdString(Cmd);
	TArray<FString> Args;
	CmdString.ParseIntoArray(Args, TEXT(" "));
	if (Args.Num() > 1)
	{
		InName = Args[0];
	}
	else
	{
		InName = CmdString;
	}

	if (FParse::Param(Cmd, TEXT("nosuffix")))
	{
		InSuffix = false;
	}
}

bool FHoloPlayViewportClient::ParseResolution(const TCHAR * InResolution, uint32 & OutX, uint32 & OutY)
{
	//todo: refactor
	if (*InResolution)
	{
		FString CmdString(InResolution);
		CmdString.TrimStartAndEndInline();
		CmdString.ToLowerInline();

		//Retrieve the X dimensional value

		// Determine whether the user has entered a resolution and extract the Y dimension.
		FString YString;

		// Find separator between values (Example of expected format: 1280x768)
		const TCHAR* YValue = NULL;
		if (FCString::Strchr(*CmdString, 'x'))
		{
			YValue = const_cast<TCHAR*> (FCString::Strchr(*CmdString, 'x') + 1);
			YString = YValue;
			// Remove any whitespace from the end of the string
			YString = YString.TrimStartAndEnd();
		}

		// If the Y dimensional value exists then setup to use the specified resolution.
		if (YValue && YString.Len() > 0)
		{
			if (YString.IsNumeric())
			{
				uint32 X = FMath::Max(FCString::Atof(*CmdString), 0.0f);
				uint32 Y = FMath::Max(FCString::Atof(YValue), 0.0f);
				OutX = X;
				OutY = Y;
				return true;
			}
		}
	}
	return false;
}

bool FHoloPlayViewportClient::PreparePlayLenticularScreenshot(const FString& FileName, bool bInShowUI, bool bAddFilenameSuffix)
{
	if (!HoloPlayLenticularScreenshotRequest.IsValid())
	{
		HoloPlayLenticularScreenshotRequest = MakeShareable(new FHoloPlayLenticularScreenshotRequest());
		HoloPlayLenticularScreenshotRequest->RequestScreenshot(FileName, bInShowUI, bAddFilenameSuffix);

		return true;
	}

	return false;
}

bool FHoloPlayViewportClient::PreparePlayScreenshotQuilt(const FString& FileName, bool bAddFilenameSuffix)
{
	if (!HoloPlayQuilScreenshotRequest.IsValid())
	{
		HoloPlayQuilScreenshotRequest = MakeShareable(new FHoloPlayScreenshotRequest());
		HoloPlayQuilScreenshotRequest->RequestScreenshot(FileName, bAddFilenameSuffix);

		return true;
	}

	return false;
}

bool FHoloPlayViewportClient::PreparePlayScreenshot2D(const FString& FileName, bool bAddFilenameSuffix)
{
	if (!HoloPlayScreenshot2DRequest.IsValid())
	{
		HoloPlayScreenshot2DRequest = MakeShareable(new FHoloPlayScreenshotRequest());
		HoloPlayScreenshot2DRequest->RequestScreenshot(FileName, bAddFilenameSuffix);

		return true;
	}

	return false;
}

TWeakObjectPtr<UHoloPlaySceneCaptureComponent2D> FHoloPlayViewportClient::GetGameHoloPlayCaptureComponent()
{
	IHoloPlayRuntime& HoloPlayRuntime = IHoloPlayRuntime::Get();
	TArray<TWeakObjectPtr<UHoloPlaySceneCaptureComponent2D> >* ComponentArray = &HoloPlayRuntime.GameHoloPlayCaptureComponents;
#if WITH_EDITOR
	if (HoloPlayRuntime.EditorHoloPlayCaptureComponents.Num())
	{
		ComponentArray = &HoloPlayRuntime.EditorHoloPlayCaptureComponents;
	}
#endif
	return ComponentArray->Num() ? (*ComponentArray)[0] : nullptr;
}

UTextureRenderTarget2D* FHoloPlayViewportClient::GetQuiltRT(TWeakObjectPtr<UHoloPlaySceneCaptureComponent2D> HoloPlayCaptureComponent)
{
	const FHoloPlayTilingQuality& TilingValues = HoloPlayCaptureComponent->GetTilingValues();

	if (QuiltRT == nullptr)
	{
		QuiltRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), UTextureRenderTarget2D::StaticClass());
		QuiltRT->InitCustomFormat(TilingValues.QuiltW, TilingValues.QuiltH, PF_A16B16G16R16, false);
		QuiltRT->ClearColor = FLinearColor::Red;
		QuiltRT->AddToRoot();
		QuiltRT->UpdateResourceImmediate();
	}

	// Resize Quilt texture
	if (TilingValues.QuiltW != QuiltRT->SizeX ||
		TilingValues.QuiltH != QuiltRT->SizeY)
	{
		QuiltRT->ResizeTarget(TilingValues.QuiltW, TilingValues.QuiltH);
		QuiltRT->UpdateResourceImmediate();
	}


	return QuiltRT;
}

