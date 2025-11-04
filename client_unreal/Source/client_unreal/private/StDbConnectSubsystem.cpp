#include "StDbConnectSubsystem.h"
#include "Connection/Credentials.h"
#include "Containers/Ticker.h"
#include "ModuleBindings/SpacetimeDBClient.g.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

UStDbConnectSubsystem::UStDbConnectSubsystem()
	: Conn(nullptr)
{
}

void UStDbConnectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Initialize credentials helper with token file path (no connection started)
	UCredentials::Init(*TokenFilePath);

	// Only auto-start connection if configured to do so.
	if (bAutoStart)
	{
		StartConnection();
	}

	// Register the ticker so we can call FrameTick() when connected
	RegisterTicker();
}

void UStDbConnectSubsystem::Deinitialize()
{
	UnregisterTicker();
	Disconnect();
	Super::Deinitialize();
}

bool UStDbConnectSubsystem::IsConnected() const
{
	return Conn != nullptr && Conn->IsActive();
}

void UStDbConnectSubsystem::Disconnect()
{
	if (Conn != nullptr)
	{
		Conn->Disconnect();
		Conn = nullptr;
	}
}

void UStDbConnectSubsystem::StartConnection()
{
	// Prevent starting twice
	if (Conn != nullptr && Conn->IsActive())
	{
		return;
	}

	BuildAndStartConnection();
}

void UStDbConnectSubsystem::BuildAndStartConnection()
{
	FOnConnectDelegate ConnectDelegate;
	BIND_DELEGATE_SAFE(ConnectDelegate, this, UStDbConnectSubsystem, HandleConnect);

	FOnDisconnectDelegate DisconnectDelegate;
	BIND_DELEGATE_SAFE(DisconnectDelegate, this, UStDbConnectSubsystem, HandleDisconnect);

	FOnConnectErrorDelegate ConnectErrorDelegate;
	BIND_DELEGATE_SAFE(ConnectErrorDelegate, this, UStDbConnectSubsystem, HandleConnectError);

	FString Token = UCredentials::LoadToken();

	UDbConnectionBuilder* Builder = UDbConnection::Builder()
		->WithUri(ServerUri)
		->WithModuleName(ModuleName)
		->OnConnect(ConnectDelegate)
		->OnDisconnect(DisconnectDelegate)
		->OnConnectError(ConnectErrorDelegate);

	if (!Token.IsEmpty())
	{
		Builder->WithToken(Token);
	}

	Conn = Builder->Build();
}

void UStDbConnectSubsystem::HandleConnect(UDbConnection* InConn, FSpacetimeDBIdentity Identity, const FString& Token)
{
	UE_LOG(LogTemp, Log, TEXT("Connected."));
	UCredentials::SaveToken(Token);
	LocalIdentity = Identity;

	FOnSubscriptionApplied AppliedDelegate;
	BIND_DELEGATE_SAFE(AppliedDelegate, this, UStDbConnectSubsystem, HandleSubscriptionApplied);

	if (Conn)
	{
		Conn->SubscriptionBuilder()
			->OnApplied(AppliedDelegate)
			->SubscribeToAllTables();
	}
}

void UStDbConnectSubsystem::HandleConnectError(const FString& Error)
{
	UE_LOG(LogTemp, Log, TEXT("Connection error %s"), *Error);
}

void UStDbConnectSubsystem::HandleDisconnect(UDbConnection* InConn, const FString& Error)
{
	UE_LOG(LogTemp, Log, TEXT("Disconnected."));
	if (!Error.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("Disconnect error %s"), *Error);
	}
}

void UStDbConnectSubsystem::HandleSubscriptionApplied(FSubscriptionEventContext& Context)
{
	UE_LOG(LogTemp, Log, TEXT("Subscription applied!"));

	// Ensure we have a valid connection
	if (!Conn || !Conn->IsActive())
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleSubscriptionApplied: No active connection"));
		return;
	}

	// Find the local player by matching Identity
	if (!Conn->Db || !Conn->Db->Players || !Conn->Db->Players->Identity)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleSubscriptionApplied: Database tables not initialized"));
		return;
	}

	FPlayerType LocalPlayer = Conn->Db->Players->Identity->Find(LocalIdentity);
	if (LocalPlayer.PlayerId == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleSubscriptionApplied: Local player not found"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("HandleSubscriptionApplied: Found local player with ID %d"), LocalPlayer.PlayerId);

	// Search for PlayerCharacter rows for this player
	if (!Conn->Db->PlayerCharacters || !Conn->Db->PlayerCharacters->PlayerId)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleSubscriptionApplied: PlayerCharacters table not initialized"));
		return;
	}

	TArray<FPlayerCharacterType> PlayerCharacters = Conn->Db->PlayerCharacters->PlayerId->Filter(LocalPlayer.PlayerId);
	
	// Look for any character that needs spawning
	for (const FPlayerCharacterType& Character : PlayerCharacters)
	{
		// Check if NeedsSpawn field exists in generated type - for now we'll assume it will be regenerated
		// Since we can't modify generated files, we note this requires regeneration of bindings
		UE_LOG(LogTemp, Log, TEXT("HandleSubscriptionApplied: Found character %d"), Character.CharacterId);
		
		// NOTE: The following code assumes NeedsSpawn field will be added by regenerating bindings
		// For now, we'll comment it out since the field doesn't exist yet in the generated code
		/*
		if (Character.NeedsSpawn)
		{
			UE_LOG(LogTemp, Log, TEXT("HandleSubscriptionApplied: Character %d needs spawning"), Character.CharacterId);
			
			// Load the world level
			UGameplayStatics::OpenLevel(this, FName(TEXT("Lvl_World")));
			
			// Get the player controller
			APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
			if (!PC)
			{
				UE_LOG(LogTemp, Warning, TEXT("HandleSubscriptionApplied: No player controller found"));
				continue;
			}

			// Load the pawn class
			UClass* PawnClass = PlayerPawnClass.LoadSynchronous();
			if (!PawnClass)
			{
				UE_LOG(LogTemp, Warning, TEXT("HandleSubscriptionApplied: Failed to load pawn class"));
				continue;
			}

			// Create transform from Character.Transform
			FVector Location(Character.Transform.X, Character.Transform.Y, Character.Transform.Z);
			FRotator Rotation(Character.Transform.Pitch, Character.Transform.Yaw, Character.Transform.Roll);
			FTransform SpawnTransform(Rotation, Location);

			// Spawn the pawn
			UWorld* World = GetWorld();
			if (!World)
			{
				UE_LOG(LogTemp, Warning, TEXT("HandleSubscriptionApplied: No world found"));
				continue;
			}

			APawn* NewPawn = World->SpawnActor<APawn>(PawnClass, SpawnTransform);
			if (!NewPawn)
			{
				UE_LOG(LogTemp, Warning, TEXT("HandleSubscriptionApplied: Failed to spawn pawn"));
				continue;
			}

			// Possess the pawn
			PC->Possess(NewPawn);

			// Call player_spawned reducer
			// NOTE: This assumes the reducer will be generated when bindings are regenerated
			// For now we comment this out
			// Conn->Reducers->PlayerSpawned(Character.CharacterId);

			UE_LOG(LogTemp, Log, TEXT("HandleSubscriptionApplied: Spawned and possessed character %d"), Character.CharacterId);
		}
		*/
	}
}

void UStDbConnectSubsystem::Tick(float DeltaSeconds)
{
	if (IsConnected() && Conn)
	{
		Conn->FrameTick();
	}
}

void UStDbConnectSubsystem::RegisterTicker()
{
	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UStDbConnectSubsystem::OnTick), 0.016f);
	}
}

void UStDbConnectSubsystem::UnregisterTicker()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

bool UStDbConnectSubsystem::OnTick(float DeltaSeconds)
{
	Tick(DeltaSeconds);
	return true;
}