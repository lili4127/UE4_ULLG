#pragma once

#include "CoreMinimal.h"
#include "Runtime/MovieSceneCapture/Public/MovieSceneCapture.h"

namespace HoloPlay
{
	/**
	 * @fn	UMovieSceneCapture* GetMovieSceneCapture();
	 *
	 * @brief	Gets movie scene capture
	 * 			It is using for check movie capture during capturing video
	 *
	 * @returns	Null if it fails, else the movie scene capture.
	 */

	UMovieSceneCapture* GetMovieSceneCapture();
};