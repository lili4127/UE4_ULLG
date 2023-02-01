#include "Misc/HoloPlayHelpers.h"
#include "Engine/Engine.h"

UMovieSceneCapture * HoloPlay::GetMovieSceneCapture()
{
	UMovieSceneCapture* MovieSceneCapture = FindObject<UMovieSceneCapture>((UObject*)ANY_PACKAGE, *UMovieSceneCapture::MovieSceneCaptureUIName.ToString());

	return MovieSceneCapture;
}
