// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "client_unrealGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class Aclient_unrealGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	Aclient_unrealGameMode();
};



