#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ModuleBindings/SpacetimeDBClient.g.h"
#include "StDbConnectSubsystem.generated.h"

class UDbConnection;
class UDbConnectionBuilder;
struct FSubscriptionEventContext;
struct FEventContext;
struct FPlayerType;
struct FPlayerCharacterType;
struct FEntityType;

UCLASS()
class CLIENT_UNREAL_API UStDbConnectSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UStDbConnectSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "MMORPG|Connection")
	bool IsConnected() const;

	UFUNCTION(BlueprintCallable, Category = "MMORPG|Connection")
	void Disconnect();

	// Expose StartConnection so UI/GameMode can trigger connecting.
	UFUNCTION(BlueprintCallable, Category = "MMORPG|Connection")
	void StartConnection();

	// Config: whether to auto-start connection on Initialize. Default false to require user action.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMORPG|Connection")
	bool bAutoStart = false;

	// Public connection configuration that previously lived on the Actor
	UPROPERTY(EditAnywhere, Category = "MMORPG|Connection")
	FString ServerUri = TEXT("172.25.80.1:3000");

	UPROPERTY(EditAnywhere, Category = "MMORPG|Connection")
	FString ModuleName = TEXT("mmorpg");

	UPROPERTY(EditAnywhere, Category = "MMORPG|Connection")
	FString TokenFilePath = TEXT(".spacetime_mmorpg");

	UPROPERTY(BlueprintReadOnly, Category = "MMORPG|Connection")
	FSpacetimeDBIdentity LocalIdentity;

	UPROPERTY(BlueprintReadOnly, Category = "MMORPG|Connection")
	UDbConnection* Conn = nullptr;

	// Local player display name cached from Players table
	UPROPERTY(BlueprintReadOnly, Category = "MMORPG|Player")
	FString LocalPlayerDisplayName;

	// Delegate fired when local player display name changes
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerDisplayNameChanged, const FString&, NewDisplayName);

	UPROPERTY(BlueprintAssignable, Category = "MMORPG|Player")
	FOnPlayerDisplayNameChanged OnPlayerDisplayNameChanged;

	UFUNCTION(BlueprintPure, Category = "MMORPG|Player")
	FString GetLocalPlayerDisplayName() const { return LocalPlayerDisplayName; }

private:
	UFUNCTION()
	void HandleConnect(UDbConnection* InConn, FSpacetimeDBIdentity Identity, const FString& Token);

	UFUNCTION()
	void HandleConnectError(const FString& Error);

	UFUNCTION()
	void HandleDisconnect(UDbConnection* InConn, const FString& Error);

	UFUNCTION()
	void HandleSubscriptionApplied(FSubscriptionEventContext& Context);

	// Event handlers for Players table
	UFUNCTION()
	void OnPlayerInsert(const FEventContext& Context, const FPlayerType& NewRow);

	UFUNCTION()
	void OnPlayerUpdate(const FEventContext& Context, const FPlayerType& OldRow, const FPlayerType& NewRow);

	// Event handlers for PlayerCharacters table
	UFUNCTION()
	void OnPlayerCharacterInsert(const FEventContext& Context, const FPlayerCharacterType& NewRow);

	UFUNCTION()
	void OnPlayerCharacterUpdate(const FEventContext& Context, const FPlayerCharacterType& OldRow, const FPlayerCharacterType& NewRow);

	UFUNCTION()
	void OnPlayerCharacterDelete(const FEventContext& Context, const FPlayerCharacterType& RemovedRow);

	// Event handlers for Entity table (optional, minimal logging)
	UFUNCTION()
	void OnEntityUpdate(const FEventContext& Context, const FEntityType& OldRow, const FEntityType& NewRow);

	UFUNCTION()
	void OnEntityDelete(const FEventContext& Context, const FEntityType& RemovedRow);

	// internal helper used by StartConnection
	void BuildAndStartConnection();

	FTSTicker::FDelegateHandle TickerHandle;
	void RegisterTicker();
	void UnregisterTicker();
	bool OnTick(float DeltaSeconds);

	// If you prefer to keep an exposed Tick wrapper:
public:
	// Public tick helper (optional)
	void Tick(float DeltaSeconds);
};