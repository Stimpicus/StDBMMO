#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ModuleBindings/SpacetimeDBClient.g.h"
#include "StDbConnectSubsystem.generated.h"

class UDbConnection;
class UDbConnectionBuilder;
struct FSubscriptionEventContext;

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

	// Pawn class to spawn when player character needs spawning
	UPROPERTY(EditAnywhere, Category = "MMORPG|Gameplay")
	TSoftClassPtr<APawn> PlayerPawnClass = TSoftClassPtr<APawn>(FSoftObjectPath(TEXT("/Game/Blueprints/BP_PlayerPawn.BP_PlayerPawn_C")));

	UPROPERTY(BlueprintReadOnly, Category = "MMORPG|Connection")
	FSpacetimeDBIdentity LocalIdentity;

	UPROPERTY(BlueprintReadOnly, Category = "MMORPG|Connection")
	UDbConnection* Conn = nullptr;

private:
	UFUNCTION()
	void HandleConnect(UDbConnection* InConn, FSpacetimeDBIdentity Identity, const FString& Token);

	UFUNCTION()
	void HandleConnectError(const FString& Error);

	UFUNCTION()
	void HandleDisconnect(UDbConnection* InConn, const FString& Error);

	UFUNCTION()
	void HandleSubscriptionApplied(FSubscriptionEventContext& Context);

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