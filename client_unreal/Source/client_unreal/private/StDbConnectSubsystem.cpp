#include "StDbConnectSubsystem.h"
#include "Connection/Credentials.h"
#include "Containers/Ticker.h"
#include "ModuleBindings/SpacetimeDBClient.g.h"
#include "ModuleBindings/Tables/PlayerTable.g.h"
#include "ModuleBindings/Tables/PlayerCharacterTable.g.h"
#include "ModuleBindings/Tables/EntityTable.g.h"
#include "ModuleBindings/Types/PlayerType.g.h"
#include "ModuleBindings/Types/PlayerCharacterType.g.h"
#include "ModuleBindings/Types/EntityType.g.h"

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

	// Register event delegates for reactive table updates (Blackholio pattern)
	if (Conn && Conn->Db)
	{
		// Players table events - for local player name caching
		Conn->Db->Players->OnInsert.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerInsert);
		Conn->Db->Players->OnUpdate.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerUpdate);

		// PlayerCharacters table events - for character lifecycle
		Conn->Db->PlayerCharacters->OnInsert.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerCharacterInsert);
		Conn->Db->PlayerCharacters->OnUpdate.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerCharacterUpdate);
		Conn->Db->PlayerCharacters->OnDelete.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerCharacterDelete);

		// Entity table events - optional minimal logging
		Conn->Db->Entity->OnUpdate.AddDynamic(this, &UStDbConnectSubsystem::OnEntityUpdate);
		Conn->Db->Entity->OnDelete.AddDynamic(this, &UStDbConnectSubsystem::OnEntityDelete);
	}

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

	// EFFICIENT: Use Identity index for O(1) lookup instead of O(n) row iteration
	FPlayerType Player = Context.Db->Players->Identity->Find(LocalIdentity);
	
	if (Player.PlayerId == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Local player not found in Players table yet"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("Found local player with PlayerId: %d"), Player.PlayerId);

	// Cache the local player display name if available
	if (!Player.DisplayName.IsEmpty() && Player.DisplayName != LocalPlayerDisplayName)
	{
		LocalPlayerDisplayName = Player.DisplayName;
		OnPlayerDisplayNameChanged.Broadcast(LocalPlayerDisplayName);
		UE_LOG(LogTemp, Log, TEXT("Local player display name set to: %s"), *LocalPlayerDisplayName);
	}

	// EFFICIENT: Use PlayerId index to filter characters for this player
	TArray<FPlayerCharacterType> Characters = Context.Db->PlayerCharacters->PlayerId->Filter(Player.PlayerId);
	
	UE_LOG(LogTemp, Log, TEXT("Found %d PlayerCharacter(s) for local player"), Characters.Num());
	
	if (Characters.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("No character found for player - will be created by server"));
		return;
	}

	// Check if any character needs spawn
	int32 NeedsSpawnCount = 0;
	for (const FPlayerCharacterType& Character : Characters)
	{
		if (Character.NeedsSpawn)
		{
			NeedsSpawnCount++;
		}
	}

	if (NeedsSpawnCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("%d character(s) need spawn - spawn logic will be implemented in future PR"), NeedsSpawnCount);
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

// Event Handlers for Players table
void UStDbConnectSubsystem::OnPlayerInsert(const FEventContext& Context, const FPlayerType& NewRow)
{
	UE_LOG(LogTemp, Log, TEXT("Player inserted: PlayerId=%d, DisplayName=%s"), NewRow.PlayerId, *NewRow.DisplayName);
	
	// If this is the local player, cache the display name
	if (NewRow.Identity == LocalIdentity)
	{
		if (!NewRow.DisplayName.IsEmpty() && NewRow.DisplayName != LocalPlayerDisplayName)
		{
			LocalPlayerDisplayName = NewRow.DisplayName;
			OnPlayerDisplayNameChanged.Broadcast(LocalPlayerDisplayName);
			UE_LOG(LogTemp, Log, TEXT("Local player display name cached: %s"), *LocalPlayerDisplayName);
		}
	}
}

void UStDbConnectSubsystem::OnPlayerUpdate(const FEventContext& Context, const FPlayerType& OldRow, const FPlayerType& NewRow)
{
	UE_LOG(LogTemp, Log, TEXT("Player updated: PlayerId=%d, DisplayName=%s"), NewRow.PlayerId, *NewRow.DisplayName);
	
	// If this is the local player and display name changed, update cache
	if (NewRow.Identity == LocalIdentity)
	{
		if (!NewRow.DisplayName.IsEmpty() && NewRow.DisplayName != LocalPlayerDisplayName)
		{
			LocalPlayerDisplayName = NewRow.DisplayName;
			OnPlayerDisplayNameChanged.Broadcast(LocalPlayerDisplayName);
			UE_LOG(LogTemp, Log, TEXT("Local player display name updated to: %s"), *LocalPlayerDisplayName);
		}
	}
}

// Event Handlers for PlayerCharacters table
void UStDbConnectSubsystem::OnPlayerCharacterInsert(const FEventContext& Context, const FPlayerCharacterType& NewRow)
{
	UE_LOG(LogTemp, Log, TEXT("PlayerCharacter inserted: CharacterId=%d, PlayerId=%d, NeedsSpawn=%d"), NewRow.CharacterId, NewRow.PlayerId, NewRow.NeedsSpawn);
}

void UStDbConnectSubsystem::OnPlayerCharacterUpdate(const FEventContext& Context, const FPlayerCharacterType& OldRow, const FPlayerCharacterType& NewRow)
{
	UE_LOG(LogTemp, Log, TEXT("PlayerCharacter updated: CharacterId=%d, PlayerId=%d"), NewRow.CharacterId, NewRow.PlayerId);
}

void UStDbConnectSubsystem::OnPlayerCharacterDelete(const FEventContext& Context, const FPlayerCharacterType& RemovedRow)
{
	UE_LOG(LogTemp, Log, TEXT("PlayerCharacter deleted: CharacterId=%d"), RemovedRow.CharacterId);
}

// Event Handlers for Entity table (optional minimal logging)
void UStDbConnectSubsystem::OnEntityUpdate(const FEventContext& Context, const FEntityType& OldRow, const FEntityType& NewRow)
{
	UE_LOG(LogTemp, Verbose, TEXT("Entity updated: EntityId=%d"), NewRow.EntityId);
}

void UStDbConnectSubsystem::OnEntityDelete(const FEventContext& Context, const FEntityType& RemovedRow)
{
	UE_LOG(LogTemp, Verbose, TEXT("Entity deleted: EntityId=%d"), RemovedRow.EntityId);
}