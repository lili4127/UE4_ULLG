#include "HoloPlaySettings.h"
#include "IHoloPlayRuntime.h"
#include "Misc/HoloPlayLog.h"

#include "Engine.h"

void FHoloPlayRenderingSettings::UpdateVsync() const
{
	if (GEngine)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VSync"));
		bool CVarbVsync = CVar->GetValueOnGameThread() != 0;
		if (CVarbVsync == bVsync)
		{
			return;
		}

		if (bVsync)
		{
			new(GEngine->DeferredCommands) FString(TEXT("r.vsync 1"));
		}
		else
		{
			new(GEngine->DeferredCommands) FString(TEXT("r.vsync 0"));
		}
	}
}

#if WITH_EDITOR
void UHoloPlaySettings::PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		FName PropertyName(PropertyChangedEvent.Property->GetFName());
		FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != NULL) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

		if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UHoloPlaySettings, CustomSettings))
		{
			// Changed custom values, recompute other fields
			CustomSettings.Setup();
		}
	}
}
#endif // WITH_EDITOR
