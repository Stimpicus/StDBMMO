#include "StDbConnectSubsystem.h"
#include "Connection/Credentials.h"
#include "Containers/Ticker.h"
#include "ModuleBindings/SpacetimeDBClient.g.h"

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
	
	if (!Conn || !Conn->Db)
	{
		UE_LOG(LogTemp, Warning, TEXT("No connection or database available"));
		return;
	}

	// Find our player in the Players table by matching LocalIdentity
	uint32 LocalPlayerId = 0;
	bool bFoundPlayer = false;
	
	for (const auto& Player : Conn->Db->Players->Rows)
	{
		if (Player.Identity == LocalIdentity)
		{
			LocalPlayerId = Player.PlayerId;
			bFoundPlayer = true;
			UE_LOG(LogTemp, Log, TEXT("Found local player with PlayerId: %d"), LocalPlayerId);
			break;
		}
	}

	if (!bFoundPlayer)
	{
		UE_LOG(LogTemp, Warning, TEXT("Local player not found in Players table"));
		return;
	}

	// Find player_characters for this player
	for (const auto& PlayerChar : Conn->Db->PlayerCharacters->Rows)
	{
		if (PlayerChar.PlayerId == LocalPlayerId)
		{
			UE_LOG(LogTemp, Log, TEXT("Found PlayerCharacter: CharacterId=%d, NeedsSpawn=%d"), 
				PlayerChar.CharacterId, PlayerChar.NeedsSpawn);
			
			// Check if this character needs to spawn
			if (PlayerChar.NeedsSpawn)
			{
				UE_LOG(LogTemp, Log, TEXT("Character needs spawn. Loading world and spawning pawn..."));
				// TODO: Implement world loading and pawn spawning
				// For now, we'll just log that we detected the need to spawn
				// The actual implementation will require:
				// 1. Load level Lvl_World
				// 2. Spawn pawn at PlayerChar.Transform
				// 3. Possess the pawn
				// 4. Call Conn->Reducers->PlayerSpawned(PlayerChar.CharacterId)
			}
			
			break; // Found our character, no need to continue
		}
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