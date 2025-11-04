# Blackholio GameManager Analysis & Adaptation Guide for StDBMMO

## Executive Summary

This document provides a comprehensive analysis of Clockwork Labs' Blackholio demo GameManager implementation and adaptation recommendations for the StDBMMO client prototype. The Blackholio approach demonstrates efficient patterns for SpacetimeDB integration that can significantly reduce boilerplate and improve runtime efficiency in our MMO client.

**Key Findings:**
- Blackholio uses **event-driven architecture** with table event delegates, eliminating per-frame polling
- **Direct table index access** (e.g., `Db->Player->Identity->Find()`) is more efficient than manual row iteration
- **Actor-based singleton pattern** provides simpler lifecycle management than subsystem approach
- **Local entity maps** enable O(1) lookups instead of O(n) table scans
- **Guard conditions** prevent redundant reducer calls (e.g., checking for existing Circle before EnterGame)

**Impact on StDBMMO:**
- Current subsystem scans rows inefficiently
- Missing event delegate subscriptions for reactive updates
- Lacks guards for redundant EnterGame calls
- Can leverage generated indices for player/character lookups

---

## Table of Contents
1. [Function-by-Function Analysis](#function-by-function-analysis)
2. [Efficiency Comparison Matrix](#efficiency-comparison-matrix)
3. [Key Pattern Differences](#key-pattern-differences)
4. [Adaptation Recommendations](#adaptation-recommendations)
5. [Migration Checklist](#migration-checklist)
6. [Code Examples](#code-examples)

---

## Function-by-Function Analysis

### 1. Constructor (`AGameManager::AGameManager()`)

**Intent & Role:**
- Initializes the GameManager actor as a singleton
- Sets up tick enablement for FrameTick calls
- Creates border rendering component (ISM for instanced rendering)
- Loads default cube mesh for arena borders

**Efficiency Rating:**
- ✅ **Lifecycle Management**: Excellent - Uses actor lifecycle naturally
- ✅ **Initialization**: Excellent - One-time setup in constructor
- ⚠️ **Singleton Pattern**: Good - Simple but global state

**Adaptation for StDBMMO:**
```cpp
// Current: UStDbConnectSubsystem uses subsystem lifecycle
// Blackholio: Actor-based singleton

// Recommendation: Keep subsystem but add actor for world-specific entities
// Create AStDbWorldManager actor that handles spawning, similar to Blackholio
// Subsystem manages connection, WorldManager manages game entities

// In StDbWorldManager.h:
UCLASS()
class AStDbWorldManager : public AActor
{
    GENERATED_BODY()
public:
    static AStDbWorldManager* Instance;
    AStDbWorldManager();
    
    UPROPERTY()
    TMap<uint32, TWeakObjectPtr<APlayerCharacter>> PlayerCharacterMap;
    // Entity maps for O(1) lookup instead of table scans
};
```

**Efficiency Improvement:**
- Separates connection lifecycle (subsystem) from game entity lifecycle (actor)
- Actor provides better integration with UE4's world/level system
- Natural cleanup on level transitions

---

### 2. BeginPlay / EndPlay

**Intent & Role:**
- **BeginPlay**: Sets singleton, builds connection with delegates, initiates connection
- **EndPlay**: Cleans up singleton, disconnects

**Efficiency Rating:**
- ✅ **Connection Setup**: Excellent - One-time connection in BeginPlay
- ✅ **Delegate Binding**: Excellent - Uses BIND_DELEGATE_SAFE macro
- ✅ **Token Management**: Excellent - Loads/saves automatically with UCredentials

**Current StDBMMO Approach:**
```cpp
// StDbConnectSubsystem::Initialize()
void UStDbConnectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    UCredentials::Init(*TokenFilePath);
    if (bAutoStart) { StartConnection(); }
    RegisterTicker();
}
```

**Blackholio Approach:**
```cpp
void AGameManager::BeginPlay()
{
    Instance = this;
    // Build connection with all delegates immediately
    FOnConnectDelegate ConnectDelegate;
    BIND_DELEGATE_SAFE(ConnectDelegate, this, AGameManager, HandleConnect);
    // ... other delegates
    Conn = Builder->Build(); // Connection starts immediately
}
```

**Key Difference:**
- **StDBMMO**: Requires manual `StartConnection()` call, connection deferred
- **Blackholio**: Automatic connection on BeginPlay, simpler flow

**Adaptation Recommendation:**
```cpp
// Option 1: Keep manual start for UI-driven flow (current approach is fine)
// Option 2: Add convenience mode for auto-connect like Blackholio

// In StDbConnectSubsystem.h:
UPROPERTY(EditAnywhere, Category = "Connection")
bool bAutoConnectOnInitialize = false; // false by default for StDBMMO

// In StDbConnectSubsystem::Initialize():
if (bAutoConnectOnInitialize)
{
    BuildAndStartConnection(); // Connect immediately like Blackholio
}
```

---

### 3. Tick (`AGameManager::Tick()`)

**Intent & Role:**
- Calls `Conn->FrameTick()` every frame when connected
- Essential for processing SpacetimeDB message queue

**Efficiency Rating:**
- ✅ **Frame Processing**: Excellent - Minimal overhead, only when connected
- ✅ **Simplicity**: Excellent - No additional polling logic

**Current StDBMMO Approach:**
```cpp
// Uses FTSTicker for ticking
void UStDbConnectSubsystem::RegisterTicker()
{
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UStDbConnectSubsystem::OnTick), 
        0.016f
    );
}

bool UStDbConnectSubsystem::OnTick(float DeltaSeconds)
{
    Tick(DeltaSeconds);
    return true;
}

void UStDbConnectSubsystem::Tick(float DeltaSeconds)
{
    if (IsConnected() && Conn)
    {
        Conn->FrameTick();
    }
}
```

**Blackholio Approach:**
```cpp
void AGameManager::Tick(float DeltaTime)
{
    if (IsConnected())
    {
        Conn->FrameTick();
    }
}
```

**Comparison:**
- **StDBMMO**: Ticker-based, more complex setup, survives level transitions
- **Blackholio**: Actor tick, simpler, tied to actor lifecycle

**Efficiency:**
- Both are equally efficient at runtime
- Blackholio is simpler with less code
- StDBMMO ticker survives level loads (useful for persistent subsystem)

**Recommendation:**
✅ **Keep current ticker approach for subsystem** - It's appropriate since subsystem persists across levels. The ticker ensures FrameTick continues even during level transitions.

---

### 4. HandleConnect

**Intent & Role:**
- Called when connection succeeds
- Saves auth token
- **Registers table event delegates** (OnInsert, OnUpdate, OnDelete)
- Subscribes to all tables

**Efficiency Rating:**
- ⭐⭐⭐⭐⭐ **Event Registration**: EXCELLENT - This is the key efficiency pattern
- ✅ **Subscription**: Excellent - Single call to SubscribeToAllTables
- ✅ **Token Persistence**: Excellent - Automatic token save

**Blackholio Code:**
```cpp
void AGameManager::HandleConnect(UDbConnection* InConn, FSpacetimeDBIdentity Identity, const FString& Token)
{
    UCredentials::SaveToken(Token);
    LocalIdentity = Identity;

    // KEY PATTERN: Register event delegates for reactive updates
    Conn->Db->Circle->OnInsert.AddDynamic(this, &AGameManager::OnCircleInsert);
    Conn->Db->Entity->OnUpdate.AddDynamic(this, &AGameManager::OnEntityUpdate);
    Conn->Db->Entity->OnDelete.AddDynamic(this, &AGameManager::OnEntityDelete);
    Conn->Db->Food->OnInsert.AddDynamic(this, &AGameManager::OnFoodInsert);
    Conn->Db->Player->OnInsert.AddDynamic(this, &AGameManager::OnPlayerInsert);
    Conn->Db->Player->OnDelete.AddDynamic(this, &AGameManager::OnPlayerDelete);
    
    // Subscribe to receive table updates
    Conn->SubscriptionBuilder()->OnApplied(AppliedDelegate)->SubscribeToAllTables();
}
```

**Current StDBMMO Approach:**
```cpp
void UStDbConnectSubsystem::HandleConnect(...)
{
    UCredentials::SaveToken(Token);
    LocalIdentity = Identity;
    
    // Only subscribes, NO event delegate registration
    Conn->SubscriptionBuilder()
        ->OnApplied(AppliedDelegate)
        ->SubscribeToAllTables();
}
```

**CRITICAL DIFFERENCE:**
❌ **StDBMMO is missing event delegate registration!**

This is a **major efficiency gap**. Without event delegates:
- Must poll tables every frame or on-demand
- No automatic notification of entity spawns/updates/deletes
- Higher latency for reacting to server changes
- More complex manual synchronization logic

**Adaptation for StDBMMO:**
```cpp
void UStDbConnectSubsystem::HandleConnect(UDbConnection* InConn, FSpacetimeDBIdentity Identity, const FString& Token)
{
    UCredentials::SaveToken(Token);
    LocalIdentity = Identity;

    // ADD EVENT DELEGATES (following Blackholio pattern)
    // Register handlers for table changes - this is the key pattern!
    if (Conn && Conn->Db)
    {
        // Player table events
        Conn->Db->Players->OnInsert.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerInsert);
        Conn->Db->Players->OnUpdate.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerUpdate);
        
        // PlayerCharacter table events
        Conn->Db->PlayerCharacters->OnInsert.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerCharacterInsert);
        Conn->Db->PlayerCharacters->OnUpdate.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerCharacterUpdate);
        Conn->Db->PlayerCharacters->OnDelete.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerCharacterDelete);
        
        // Entity table events (for character spawns)
        Conn->Db->Entity->OnInsert.AddDynamic(this, &UStDbConnectSubsystem::OnEntityInsert);
        Conn->Db->Entity->OnUpdate.AddDynamic(this, &UStDbConnectSubsystem::OnEntityUpdate);
        Conn->Db->Entity->OnDelete.AddDynamic(this, &UStDbConnectSubsystem::OnEntityDelete);
    }
    
    // Subscribe to all tables
    FOnSubscriptionApplied AppliedDelegate;
    BIND_DELEGATE_SAFE(AppliedDelegate, this, UStDbConnectSubsystem, HandleSubscriptionApplied);
    Conn->SubscriptionBuilder()
        ->OnApplied(AppliedDelegate)
        ->SubscribeToAllTables();
}
```

**Benefits:**
1. ⚡ **Zero-latency updates**: Notified immediately when server changes data
2. 🎯 **Targeted handling**: Only process changed rows, not entire tables
3. 📉 **Less CPU usage**: No per-frame polling needed
4. 🔄 **Automatic sync**: Client stays in sync with server automatically

---

### 5. HandleSubscriptionApplied

**Intent & Role:**
- Called when initial table sync completes
- Uses **table indices** to efficiently find data
- Sets up game state (arena in Blackholio)
- **Guards against redundant reducer calls**

**Efficiency Rating:**
- ⭐⭐⭐⭐⭐ **Index Usage**: EXCELLENT - Uses generated indices for O(1) lookup
- ⭐⭐⭐⭐⭐ **Guard Logic**: EXCELLENT - Prevents redundant EnterGame calls
- ✅ **Context Access**: Excellent - Uses event context for database access

**Blackholio Code:**
```cpp
void AGameManager::HandleSubscriptionApplied(FSubscriptionEventContext& Context)
{
    bSubscriptionsApplied = true;
    
    // PATTERN 1: Use generated table index for O(1) lookup
    int64 WorldSize = Conn->Db->Config->Id->Find(0).WorldSize;
    SetupArena(WorldSize);

    // PATTERN 2: Use Identity index to find player
    FPlayerType Player = Context.Db->Player->Identity->Find(LocalIdentity);
    
    if (!Player.Name.IsEmpty())
    {
        this->PlayerNameAtStart = Player.Name;
        
        // PATTERN 3: GUARD - Check if player already has Circle before calling EnterGame
        if (Context.Db->Circle->PlayerId->Filter(Player.PlayerId).Num() == 0)
        {
            Context.Reducers->EnterGame(Player.Name);
        }
        // If Circle already exists, don't call EnterGame (prevents duplicates)
    }
}
```

**Current StDBMMO Approach:**
```cpp
void UStDbConnectSubsystem::HandleSubscriptionApplied(FSubscriptionEventContext& Context)
{
    UE_LOG(LogTemp, Log, TEXT("Subscription applied!"));
    
    // INEFFICIENT: Manual row iteration instead of using index
    uint32 LocalPlayerId = 0;
    bool bFoundPlayer = false;
    
    for (const auto& Player : Conn->Db->Players->Rows)  // O(n) scan!
    {
        if (Player.Identity == LocalIdentity)
        {
            LocalPlayerId = Player.PlayerId;
            bFoundPlayer = true;
            break;
        }
    }
    
    // Another O(n) scan
    for (const auto& PlayerChar : Conn->Db->PlayerCharacters->Rows)
    {
        if (PlayerChar.PlayerId == LocalPlayerId)
        {
            if (PlayerChar.NeedsSpawn)
            {
                // TODO: spawn logic
            }
            break;
        }
    }
}
```

**CRITICAL INEFFICIENCIES in StDBMMO:**

1. ❌ **Manual row iteration** instead of using generated indices
2. ❌ **No guard against redundant EnterGame calls**
3. ❌ **Two O(n) scans** instead of two O(1) lookups

**Adapted Code for StDBMMO:**
```cpp
void UStDbConnectSubsystem::HandleSubscriptionApplied(FSubscriptionEventContext& Context)
{
    UE_LOG(LogTemp, Log, TEXT("Subscription applied!"));
    
    // EFFICIENT: Use generated Identity index for O(1) lookup
    FPlayerType Player = Context.Db->Players->Identity->Find(LocalIdentity);
    
    if (Player.PlayerId == 0)
    {
        // No player found - might be first connection, wait for player creation
        UE_LOG(LogTemp, Warning, TEXT("No player found for local identity"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("Found local player: %d"), Player.PlayerId);
    
    // EFFICIENT: Use generated PlayerId index for O(1) lookup
    // Check if this player has a character
    TArray<FPlayerCharacterType> Characters = Context.Db->PlayerCharacters->PlayerId->Filter(Player.PlayerId);
    
    if (Characters.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("No character for player, will be created by server"));
        return;
    }
    
    // Get first character (assuming one character per player for now)
    const FPlayerCharacterType& Character = Characters[0];
    
    if (Character.NeedsSpawn)
    {
        UE_LOG(LogTemp, Log, TEXT("Character needs spawn, loading world..."));
        // TODO: Implement world loading and pawn spawning
        
        // After spawning, call:
        // Context.Reducers->PlayerSpawned(Character.CharacterId);
    }
}
```

**Efficiency Gains:**
- ⚡ **O(1) vs O(n)**: Using indices is instant regardless of player count
- 🛡️ **Type Safety**: Index methods return correct types, less error-prone
- 📖 **Readability**: Intent is clearer with named indices

---

### 6. Entity Management - OnCircleInsert, OnEntityUpdate, OnEntityDelete

**Intent & Role:**
- React to table changes from server
- Spawn/update/destroy game entities
- Maintain local EntityMap for fast lookups

**Efficiency Rating:**
- ⭐⭐⭐⭐⭐ **Event-Driven**: EXCELLENT - Zero polling, instant reaction
- ⭐⭐⭐⭐⭐ **Local Cache**: EXCELLENT - EntityMap enables O(1) lookups
- ✅ **Weak Pointers**: Excellent - Prevents dangling references

**Blackholio Pattern:**
```cpp
// OnInsert: Spawn new entity if not already in map
void AGameManager::OnCircleInsert(const FEventContext& Context, const FCircleType& NewRow)
{
    if (EntityMap.Contains(NewRow.EntityId)) return;  // Guard: prevent duplicates
    SpawnCircle(NewRow);
}

// OnUpdate: Update existing entity
void AGameManager::OnEntityUpdate(const FEventContext& Context, const FEntityType& OldRow, const FEntityType& NewRow)
{
    if (TWeakObjectPtr<AEntity>* WeakEntity = EntityMap.Find(NewRow.EntityId))
    {
        if (!WeakEntity->IsValid()) return;
        if (AEntity* Entity = WeakEntity->Get())
        {
            Entity->OnEntityUpdated(NewRow);  // Delegate to entity
        }
    }
}

// OnDelete: Remove and destroy entity
void AGameManager::OnEntityDelete(const FEventContext& Context, const FEntityType& RemovedRow)
{
    TWeakObjectPtr<AEntity> EntityPtr;
    if (EntityMap.RemoveAndCopyValue(RemovedRow.EntityId, EntityPtr))
    {
        if (EntityPtr.IsValid())
        {
            if (AEntity* Entity = EntityPtr.Get())
            {
                Entity->OnDelete(Context);  // Entity can clean up before destruction
            }
        }
    }
}
```

**Key Patterns:**
1. **EntityMap Cache**: TMap<int32, TWeakObjectPtr<AEntity>>
   - O(1) lookup by EntityId
   - Weak pointers prevent memory leaks
   - Single source of truth for spawned entities

2. **Guard Conditions**: Check map before spawning to prevent duplicates

3. **Delegation**: Entity actors handle their own update logic

**Adaptation for StDBMMO:**

Currently, StDBMMO has no entity event handlers! We need to add them.

```cpp
// In StDbConnectSubsystem.h or new AStDbWorldManager.h:
UPROPERTY()
TMap<uint32, TWeakObjectPtr<APlayerCharacter>> PlayerCharacterMap;

// Event handlers:
UFUNCTION()
void OnPlayerCharacterInsert(const FEventContext& Context, const FPlayerCharacterType& NewRow);

UFUNCTION()
void OnPlayerCharacterUpdate(const FEventContext& Context, const FPlayerCharacterType& OldRow, const FPlayerCharacterType& NewRow);

UFUNCTION()
void OnPlayerCharacterDelete(const FEventContext& Context, const FPlayerCharacterType& RemovedRow);

// In .cpp:
void UStDbConnectSubsystem::OnPlayerCharacterInsert(const FEventContext& Context, const FPlayerCharacterType& NewRow)
{
    // Guard: Check if already spawned
    if (PlayerCharacterMap.Contains(NewRow.CharacterId)) return;
    
    // Check if this is the local player's character
    if (NewRow.PlayerId == GetLocalPlayerId())
    {
        if (NewRow.NeedsSpawn)
        {
            SpawnLocalPlayerCharacter(NewRow);
        }
    }
    else
    {
        // Spawn remote player character
        SpawnRemotePlayerCharacter(NewRow);
    }
}

void UStDbConnectSubsystem::OnPlayerCharacterUpdate(const FEventContext& Context, const FPlayerCharacterType& OldRow, const FPlayerCharacterType& NewRow)
{
    TWeakObjectPtr<APlayerCharacter>* WeakChar = PlayerCharacterMap.Find(NewRow.CharacterId);
    if (!WeakChar || !WeakChar->IsValid()) return;
    
    if (APlayerCharacter* Character = WeakChar->Get())
    {
        // Update character transform from server
        Character->UpdateFromServer(NewRow);
    }
}

void UStDbConnectSubsystem::OnPlayerCharacterDelete(const FEventContext& Context, const FPlayerCharacterType& RemovedRow)
{
    TWeakObjectPtr<APlayerCharacter> CharPtr;
    if (PlayerCharacterMap.RemoveAndCopyValue(RemovedRow.CharacterId, CharPtr))
    {
        if (CharPtr.IsValid())
        {
            if (APlayerCharacter* Character = CharPtr.Get())
            {
                Character->Destroy();
            }
        }
    }
}
```

---

### 7. Spawn Functions - SpawnOrGetPlayer, SpawnCircle, SpawnFood

**Intent & Role:**
- Factory methods for creating game entities
- Retrieve from cache if already exists (SpawnOrGetPlayer)
- Register spawned actors in maps

**Efficiency Rating:**
- ✅ **Cache-First**: Excellent - Check map before spawning
- ✅ **Initialization**: Excellent - Pass data to actor for setup
- ⭐⭐⭐⭐⭐ **Index Usage**: EXCELLENT - Uses table indices to fetch related data

**Blackholio Example:**
```cpp
ACircle* AGameManager::SpawnCircle(const FCircleType& CircleRow)
{
    // PATTERN: Use PlayerId index to fetch related Player data
    const FPlayerType PlayerRow = Conn->Db->Player->PlayerId->Find(CircleRow.PlayerId);
    
    // Get or create owning player
    APlayerPawn* OwningPlayer = SpawnOrGetPlayer(PlayerRow);
    
    // Spawn circle actor
    FActorSpawnParameters Params;
    auto* Circle = GetWorld()->SpawnActor<ACircle>(CircleClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
    
    if (Circle)
    {
        Circle->Spawn(CircleRow, OwningPlayer);  // Initialize with data
        EntityMap.Add(CircleRow.EntityId, Circle);  // Cache for future lookups
        
        if (OwningPlayer)
            OwningPlayer->OnCircleSpawned(Circle);  // Notify owner
    }
    
    return Circle;
}
```

**Key Patterns:**
1. **Related Data Lookup**: Uses indices to get Player from PlayerId
2. **Lazy Player Creation**: SpawnOrGetPlayer ensures player exists
3. **Map Registration**: Adds to EntityMap immediately after spawn
4. **Data Initialization**: Passes table data to actor's Spawn() method

**Adaptation for StDBMMO:**
```cpp
APlayerCharacter* UStDbConnectSubsystem::SpawnPlayerCharacter(const FPlayerCharacterType& CharacterRow)
{
    // Check cache first
    TWeakObjectPtr<APlayerCharacter> Existing = PlayerCharacterMap.FindRef(CharacterRow.CharacterId);
    if (Existing.IsValid())
    {
        return Existing.Get();
    }
    
    // Get player data using index (not manual iteration!)
    const FPlayerType Player = Conn->Db->Players->PlayerId->Find(CharacterRow.PlayerId);
    
    // Spawn actor
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    
    APlayerCharacter* Character = GetWorld()->SpawnActor<APlayerCharacter>(
        PlayerCharacterClass,
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        Params
    );
    
    if (Character)
    {
        // Initialize with data from table
        Character->InitializeFromServer(CharacterRow, Player);
        
        // Cache in map
        PlayerCharacterMap.Add(CharacterRow.CharacterId, Character);
        
        // If this is local player and needs spawn, possess it
        if (CharacterRow.PlayerId == GetLocalPlayerId() && CharacterRow.NeedsSpawn)
        {
            PossessCharacter(Character);
            
            // Notify server that spawn is complete
            Conn->Reducers->PlayerSpawned(CharacterRow.CharacterId);
        }
    }
    
    return Character;
}
```

---

## Efficiency Comparison Matrix

| Aspect | StDBMMO Current | Blackholio | Efficiency Gap | Impact |
|--------|-----------------|------------|----------------|--------|
| **Player Lookup** | O(n) row iteration | O(1) Identity index | ⚠️ HIGH | Scales poorly with player count |
| **Character Lookup** | O(n) row iteration | O(1) index or map | ⚠️ HIGH | Every frame for movement |
| **Entity Updates** | ❌ None (would require polling) | ⚡ Event delegates | 🔴 CRITICAL | Miss updates without polling |
| **Entity Spawn** | ❌ Manual in HandleSubscriptionApplied | ⚡ OnInsert event | 🔴 CRITICAL | Cannot handle dynamic spawns |
| **EnterGame Guard** | ❌ No guard | ✅ Circle check | ⚠️ MEDIUM | Duplicate entities on reconnect |
| **Local Entity Cache** | ❌ No cache | ✅ EntityMap/PlayerMap | ⚠️ HIGH | Repeated table lookups |
| **Connection Lifecycle** | ✅ Subsystem (persistent) | ⚠️ Actor (per-level) | Neutral | Different use cases |
| **Tick Method** | ✅ Ticker (survives loads) | ⚠️ Actor Tick | Neutral | Both work, different scopes |

**Legend:**
- 🔴 CRITICAL: Must fix for correct functionality
- ⚠️ HIGH: Significant performance impact
- ⚠️ MEDIUM: Noticeable impact under load
- Neutral: Design choice, both valid

---

## Key Pattern Differences

### 1. Event-Driven vs Polling

**Blackholio (Event-Driven):**
```cpp
// Register once in HandleConnect
Conn->Db->Entity->OnUpdate.AddDynamic(this, &AGameManager::OnEntityUpdate);

// Gets called automatically when entity changes
void AGameManager::OnEntityUpdate(const FEventContext& Context, const FEntityType& OldRow, const FEntityType& NewRow)
{
    // Update entity immediately, no polling needed
}
```

**StDBMMO (Polling - if we added it):**
```cpp
// Would need to add in Tick()
void UStDbConnectSubsystem::Tick(float DeltaSeconds)
{
    if (IsConnected())
    {
        Conn->FrameTick();
        
        // Bad approach: Manual polling
        for (const auto& Character : Conn->Db->PlayerCharacters->Rows)
        {
            // Check if anything changed... expensive and high-latency
        }
    }
}
```

**Efficiency:**
- Event-driven: O(changed entities) - only process what changed
- Polling: O(all entities) - check everything every frame

---

### 2. Index Usage vs Manual Iteration

**Blackholio (Index):**
```cpp
// O(1) lookup using generated index
FPlayerType Player = Context.Db->Player->Identity->Find(LocalIdentity);
```

**StDBMMO Current (Manual):**
```cpp
// O(n) scan through all players
for (const auto& Player : Conn->Db->Players->Rows)
{
    if (Player.Identity == LocalIdentity)
    {
        // Found it after N comparisons
    }
}
```

**Available Indices (from SpacetimeDB generated code):**

Tables typically generate indices for:
- Primary keys (e.g., `PlayerId`, `CharacterId`)
- Foreign keys (e.g., `PlayerId` in PlayerCharacter table)
- Unique fields (e.g., `Identity`)

**How to find available indices:**
```cpp
// Look in generated table files, e.g., PlayerTable.g.h:
// Conn->Db->Players->PlayerId->Find(id)        // Primary key
// Conn->Db->Players->Identity->Find(identity)   // Unique index
// Conn->Db->PlayerCharacters->PlayerId->Filter(playerId)  // Foreign key (returns array)
```

---

### 3. Guard Patterns

**Blackholio Guard Example:**
```cpp
void AGameManager::HandleSubscriptionApplied(FSubscriptionEventContext& Context)
{
    FPlayerType Player = Context.Db->Player->Identity->Find(LocalIdentity);
    
    if (!Player.Name.IsEmpty())
    {
        // GUARD: Only call EnterGame if player doesn't have a Circle yet
        if (Context.Db->Circle->PlayerId->Filter(Player.PlayerId).Num() == 0)
        {
            Context.Reducers->EnterGame(Player.Name);
        }
    }
}
```

**Why This Matters:**
- Without guard: EnterGame called every time subscription is applied (reconnects, etc.)
- With guard: EnterGame only called if player truly needs to enter
- Prevents duplicate entities, unnecessary server processing

**Adaptation for StDBMMO:**
```cpp
void UStDbConnectSubsystem::HandleSubscriptionApplied(FSubscriptionEventContext& Context)
{
    FPlayerType Player = Context.Db->Players->Identity->Find(LocalIdentity);
    
    // GUARD: Check if player already has a character before creating one
    TArray<FPlayerCharacterType> Characters = Context.Db->PlayerCharacters->PlayerId->Filter(Player.PlayerId);
    
    if (Characters.Num() == 0)
    {
        // No character exists, server will create one
        UE_LOG(LogTemp, Log, TEXT("Waiting for server to create character"));
    }
    else
    {
        // Character exists, check if needs spawn
        const FPlayerCharacterType& Character = Characters[0];
        if (Character.NeedsSpawn)
        {
            // Spawn only if flagged by server
            SpawnPlayerCharacter(Character);
        }
    }
}
```

---

### 4. Local Caching Pattern

**Blackholio Pattern:**
```cpp
// Header
UPROPERTY()
TMap<int32, TWeakObjectPtr<AEntity>> EntityMap;
UPROPERTY()
TMap<int32, TWeakObjectPtr<APlayerPawn>> PlayerMap;

// Usage
AEntity* AGameManager::GetEntity(int32 EntityId) const
{
    if (const TWeakObjectPtr<AEntity>* WeakEntity = EntityMap.Find(EntityId))
    {
        if (WeakEntity->IsValid())
        {
            return WeakEntity->Get();
        }
    }
    return nullptr;
}
```

**Benefits:**
1. **O(1) entity lookup** by ID
2. **Weak pointers** prevent memory leaks if entity destroyed elsewhere
3. **Single source of truth** for spawned actors
4. **Avoids table scans** for entity references

**StDBMMO Should Add:**
```cpp
// In StDbConnectSubsystem.h or AStDbWorldManager.h
UPROPERTY()
TMap<uint32, TWeakObjectPtr<APlayerCharacter>> PlayerCharacterMap;

UPROPERTY()
TMap<uint32, TWeakObjectPtr<AEntity>> EntityMap;

// Getter methods
UFUNCTION(BlueprintPure, Category = "MMORPG|Entities")
APlayerCharacter* GetPlayerCharacter(uint32 CharacterId) const;

UFUNCTION(BlueprintPure, Category = "MMORPG|Entities")
AEntity* GetEntity(uint32 EntityId) const;
```

---

## Adaptation Recommendations

### Recommended Architecture Changes

**Current State:**
```
UStDbConnectSubsystem (GameInstanceSubsystem)
├─ Connection management ✅
├─ Token/auth ✅
├─ FrameTick ✅
├─ Event delegates ❌ MISSING
├─ Entity spawning ❌ TODO comments
└─ Entity lifecycle ❌ No tracking
```

**Recommended State (Hybrid Approach):**
```
UStDbConnectSubsystem (GameInstanceSubsystem)
├─ Connection management ✅
├─ Token/auth ✅
├─ FrameTick ✅
├─ Event delegates ✅ ADD
└─ Delegates entity events to WorldManager ✅ NEW

AStDbWorldManager (Actor, spawned in game level)
├─ Entity spawning ✅ NEW
├─ PlayerCharacterMap ✅ NEW
├─ EntityMap ✅ NEW
├─ Event handlers for entities ✅ NEW
└─ Level-specific game logic ✅ NEW
```

**Why Hybrid?**
1. **Subsystem** handles connection (persists across levels)
2. **WorldManager actor** handles entities (tied to level lifecycle)
3. **Separation of concerns**: Connection vs game entities
4. **Flexibility**: Different levels can have different WorldManager subclasses

---

### Priority 1 (Critical): Add Event Delegates

**File: `StDbConnectSubsystem.h`**
```cpp
// Add event handler declarations
UFUNCTION()
void OnPlayerInsert(const FEventContext& Context, const FPlayerType& NewRow);

UFUNCTION()
void OnPlayerUpdate(const FEventContext& Context, const FPlayerType& OldRow, const FPlayerType& NewRow);

UFUNCTION()
void OnPlayerCharacterInsert(const FEventContext& Context, const FPlayerCharacterType& NewRow);

UFUNCTION()
void OnPlayerCharacterUpdate(const FEventContext& Context, const FPlayerCharacterType& OldRow, const FPlayerCharacterType& NewRow);

UFUNCTION()
void OnPlayerCharacterDelete(const FEventContext& Context, const FPlayerCharacterType& RemovedRow);

UFUNCTION()
void OnEntityInsert(const FEventContext& Context, const FEntityType& NewRow);

UFUNCTION()
void OnEntityUpdate(const FEventContext& Context, const FEntityType& OldRow, const FEntityType& NewRow);

UFUNCTION()
void OnEntityDelete(const FEventContext& Context, const FEntityType& RemovedRow);
```

**File: `StDbConnectSubsystem.cpp` - HandleConnect**
```cpp
void UStDbConnectSubsystem::HandleConnect(UDbConnection* InConn, FSpacetimeDBIdentity Identity, const FString& Token)
{
    UE_LOG(LogTemp, Log, TEXT("Connected."));
    UCredentials::SaveToken(Token);
    LocalIdentity = Identity;

    // Register event delegates (following Blackholio pattern)
    if (Conn && Conn->Db)
    {
        Conn->Db->Players->OnInsert.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerInsert);
        Conn->Db->Players->OnUpdate.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerUpdate);
        
        Conn->Db->PlayerCharacters->OnInsert.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerCharacterInsert);
        Conn->Db->PlayerCharacters->OnUpdate.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerCharacterUpdate);
        Conn->Db->PlayerCharacters->OnDelete.AddDynamic(this, &UStDbConnectSubsystem::OnPlayerCharacterDelete);
        
        Conn->Db->Entity->OnInsert.AddDynamic(this, &UStDbConnectSubsystem::OnEntityInsert);
        Conn->Db->Entity->OnUpdate.AddDynamic(this, &UStDbConnectSubsystem::OnEntityUpdate);
        Conn->Db->Entity->OnDelete.AddDynamic(this, &UStDbConnectSubsystem::OnEntityDelete);
    }

    FOnSubscriptionApplied AppliedDelegate;
    BIND_DELEGATE_SAFE(AppliedDelegate, this, UStDbConnectSubsystem, HandleSubscriptionApplied);
    Conn->SubscriptionBuilder()
        ->OnApplied(AppliedDelegate)
        ->SubscribeToAllTables();
}
```

---

### Priority 2 (High): Use Table Indices

**File: `StDbConnectSubsystem.cpp` - HandleSubscriptionApplied**
```cpp
void UStDbConnectSubsystem::HandleSubscriptionApplied(FSubscriptionEventContext& Context)
{
    UE_LOG(LogTemp, Log, TEXT("Subscription applied!"));
    
    if (!Conn || !Conn->Db)
    {
        UE_LOG(LogTemp, Warning, TEXT("No connection or database available"));
        return;
    }

    // EFFICIENT: Use Identity index instead of manual iteration
    FPlayerType Player = Context.Db->Players->Identity->Find(LocalIdentity);
    
    if (Player.PlayerId == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Local player not found in Players table"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("Found local player with PlayerId: %d"), Player.PlayerId);

    // EFFICIENT: Use PlayerId index to filter characters
    TArray<FPlayerCharacterType> Characters = Context.Db->PlayerCharacters->PlayerId->Filter(Player.PlayerId);
    
    if (Characters.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("No character found for player"));
        return;
    }
    
    const FPlayerCharacterType& Character = Characters[0];
    UE_LOG(LogTemp, Log, TEXT("Found PlayerCharacter: CharacterId=%d, NeedsSpawn=%d"), 
        Character.CharacterId, Character.NeedsSpawn);
    
    if (Character.NeedsSpawn)
    {
        UE_LOG(LogTemp, Log, TEXT("Character needs spawn. Triggering spawn logic..."));
        // Delegate to WorldManager or spawn here
        // SpawnPlayerCharacter(Character);
    }
}
```

---

### Priority 3 (Medium): Add Local Entity Maps

**File: `StDbConnectSubsystem.h`**
```cpp
// Add maps for entity tracking
UPROPERTY()
TMap<uint32, TWeakObjectPtr<APlayerCharacter>> PlayerCharacterMap;

UPROPERTY()
TMap<uint32, TWeakObjectPtr<AEntity>> EntityMap;

// Helper methods
UFUNCTION(BlueprintPure, Category = "MMORPG|Entities")
APlayerCharacter* GetPlayerCharacter(uint32 CharacterId) const;

UFUNCTION(BlueprintPure, Category = "MMORPG|Entities")
AEntity* GetEntity(uint32 EntityId) const;

// Helper to get local player's PlayerId
UFUNCTION(BlueprintPure, Category = "MMORPG|Player")
uint32 GetLocalPlayerId() const;
```

**File: `StDbConnectSubsystem.cpp`**
```cpp
APlayerCharacter* UStDbConnectSubsystem::GetPlayerCharacter(uint32 CharacterId) const
{
    if (const TWeakObjectPtr<APlayerCharacter>* WeakChar = PlayerCharacterMap.Find(CharacterId))
    {
        if (WeakChar->IsValid())
        {
            return WeakChar->Get();
        }
    }
    return nullptr;
}

AEntity* UStDbConnectSubsystem::GetEntity(uint32 EntityId) const
{
    if (const TWeakObjectPtr<AEntity>* WeakEntity = EntityMap.Find(EntityId))
    {
        if (WeakEntity->IsValid())
        {
            return WeakEntity->Get();
        }
    }
    return nullptr;
}

uint32 UStDbConnectSubsystem::GetLocalPlayerId() const
{
    if (!Conn || !Conn->Db) return 0;
    
    FPlayerType Player = Conn->Db->Players->Identity->Find(LocalIdentity);
    return Player.PlayerId;
}
```

---

### Priority 4 (Medium): Add Guard Conditions

**File: `StDbConnectSubsystem.cpp` - Event Handlers**
```cpp
void UStDbConnectSubsystem::OnPlayerCharacterInsert(const FEventContext& Context, const FPlayerCharacterType& NewRow)
{
    // GUARD: Check if already spawned
    if (PlayerCharacterMap.Contains(NewRow.CharacterId))
    {
        UE_LOG(LogTemp, Warning, TEXT("Character %d already in map, skipping spawn"), NewRow.CharacterId);
        return;
    }
    
    // GUARD: Only spawn if needs spawn flag is set
    if (!NewRow.NeedsSpawn)
    {
        UE_LOG(LogTemp, Log, TEXT("Character %d doesn't need spawn yet"), NewRow.CharacterId);
        return;
    }
    
    // Spawn the character
    SpawnPlayerCharacter(NewRow);
}
```

---

## Migration Checklist

### Phase 1: Event Infrastructure (1-2 days)
- [ ] Add event handler method declarations to `StDbConnectSubsystem.h`
- [ ] Implement basic event handlers in `StDbConnectSubsystem.cpp`
- [ ] Register event delegates in `HandleConnect()`
- [ ] Add logging to event handlers to verify they're being called
- [ ] Test: Connect and verify event logs appear when server sends updates

### Phase 2: Index Usage (1 day)
- [ ] Replace manual row iteration in `HandleSubscriptionApplied()` with index lookups
- [ ] Document available indices for each table (add comments)
- [ ] Add helper method `GetLocalPlayerId()` using Identity index
- [ ] Test: Verify subscription handling still works correctly
- [ ] Measure: Compare performance before/after (player lookup time)

### Phase 3: Entity Maps (2-3 days)
- [ ] Add `PlayerCharacterMap` and `EntityMap` to subsystem
- [ ] Add `GetPlayerCharacter()` and `GetEntity()` helper methods
- [ ] Update event handlers to use maps for lookups
- [ ] Populate maps when entities spawn
- [ ] Clean up maps when entities are destroyed
- [ ] Test: Verify map stays in sync with spawned actors

### Phase 4: Character Spawning (3-5 days)
- [ ] Implement `SpawnPlayerCharacter()` method
- [ ] Handle NeedsSpawn flag in `OnPlayerCharacterInsert`
- [ ] Implement world loading (if needed)
- [ ] Implement pawn possession for local player
- [ ] Call `PlayerSpawned` reducer after successful spawn
- [ ] Test: Full spawn flow from server flag to possessed pawn

### Phase 5: Guards & Robustness (1-2 days)
- [ ] Add guard conditions to prevent duplicate spawns
- [ ] Add null checks and validation
- [ ] Add guard to prevent redundant reducer calls
- [ ] Handle edge cases (reconnection, level transitions)
- [ ] Test: Reconnect scenarios, verify no duplicates

### Phase 6: Optional - WorldManager Actor (3-5 days)
- [ ] Create `AStDbWorldManager` actor class
- [ ] Move entity spawning logic to WorldManager
- [ ] Move entity maps to WorldManager
- [ ] Subsystem delegates entity events to WorldManager
- [ ] Update GameMode to spawn WorldManager
- [ ] Test: Level transitions, WorldManager lifecycle

### Phase 7: Cleanup & Documentation (1-2 days)
- [ ] Remove or update TODO comments
- [ ] Update `IMPLEMENTATION_NOTES.md` with new patterns
- [ ] Document event delegate pattern for future developers
- [ ] Add code comments explaining index usage
- [ ] Performance profiling: Before/after comparison

**Total Estimated Time: 12-22 days**

**Minimal Viable Changes (Priority 1-3): 4-6 days**

---

## Code Examples

### Complete Event Handler Template

```cpp
// Header (.h file)
UFUNCTION()
void OnPlayerCharacterInsert(const FEventContext& Context, const FPlayerCharacterType& NewRow);

UFUNCTION()
void OnPlayerCharacterUpdate(const FEventContext& Context, const FPlayerCharacterType& OldRow, const FPlayerCharacterType& NewRow);

UFUNCTION()
void OnPlayerCharacterDelete(const FEventContext& Context, const FPlayerCharacterType& RemovedRow);

// Implementation (.cpp file)
void UStDbConnectSubsystem::OnPlayerCharacterInsert(const FEventContext& Context, const FPlayerCharacterType& NewRow)
{
    UE_LOG(LogTemp, Log, TEXT("PlayerCharacter inserted: CharacterId=%d, PlayerId=%d, NeedsSpawn=%d"),
        NewRow.CharacterId, NewRow.PlayerId, NewRow.NeedsSpawn);
    
    // Guard: Skip if already spawned
    if (PlayerCharacterMap.Contains(NewRow.CharacterId))
    {
        return;
    }
    
    // Only spawn if flagged
    if (NewRow.NeedsSpawn)
    {
        SpawnPlayerCharacter(NewRow);
    }
}

void UStDbConnectSubsystem::OnPlayerCharacterUpdate(const FEventContext& Context, const FPlayerCharacterType& OldRow, const FPlayerCharacterType& NewRow)
{
    UE_LOG(LogTemp, Log, TEXT("PlayerCharacter updated: CharacterId=%d"), NewRow.CharacterId);
    
    // Find actor in map
    TWeakObjectPtr<APlayerCharacter>* WeakChar = PlayerCharacterMap.Find(NewRow.CharacterId);
    if (!WeakChar || !WeakChar->IsValid())
    {
        return;
    }
    
    // Update actor with new data
    if (APlayerCharacter* Character = WeakChar->Get())
    {
        Character->UpdateFromServer(NewRow);
    }
}

void UStDbConnectSubsystem::OnPlayerCharacterDelete(const FEventContext& Context, const FPlayerCharacterType& RemovedRow)
{
    UE_LOG(LogTemp, Log, TEXT("PlayerCharacter deleted: CharacterId=%d"), RemovedRow.CharacterId);
    
    // Remove from map and destroy actor
    TWeakObjectPtr<APlayerCharacter> CharPtr;
    if (PlayerCharacterMap.RemoveAndCopyValue(RemovedRow.CharacterId, CharPtr))
    {
        if (CharPtr.IsValid())
        {
            if (APlayerCharacter* Character = CharPtr.Get())
            {
                Character->Destroy();
            }
        }
    }
}
```

### Complete Index Usage Examples

```cpp
// Example 1: Find by primary key
uint32 PlayerId = 123;
FPlayerType Player = Conn->Db->Players->PlayerId->Find(PlayerId);
if (Player.PlayerId != 0)  // Check if found (0 = not found)
{
    UE_LOG(LogTemp, Log, TEXT("Player name: %s"), *Player.DisplayName);
}

// Example 2: Find by unique index
FSpacetimeDBIdentity Identity = LocalIdentity;
FPlayerType Player = Conn->Db->Players->Identity->Find(Identity);

// Example 3: Filter by foreign key (returns array)
uint32 PlayerId = 456;
TArray<FPlayerCharacterType> Characters = Conn->Db->PlayerCharacters->PlayerId->Filter(PlayerId);
UE_LOG(LogTemp, Log, TEXT("Player has %d characters"), Characters.Num());

// Example 4: Check existence before calling reducer
FPlayerType Player = Conn->Db->Players->Identity->Find(LocalIdentity);
TArray<FPlayerCharacterType> Characters = Conn->Db->PlayerCharacters->PlayerId->Filter(Player.PlayerId);
if (Characters.Num() == 0)
{
    // No character exists, safe to create
    Conn->Reducers->EnterGame(Player.DisplayName);
}
```

### Complete Entity Map Usage

```cpp
// Header
UPROPERTY()
TMap<uint32, TWeakObjectPtr<APlayerCharacter>> PlayerCharacterMap;

// Spawn and add to map
APlayerCharacter* UStDbConnectSubsystem::SpawnPlayerCharacter(const FPlayerCharacterType& Data)
{
    // Check cache first
    if (PlayerCharacterMap.Contains(Data.CharacterId))
    {
        TWeakObjectPtr<APlayerCharacter> Existing = PlayerCharacterMap[Data.CharacterId];
        if (Existing.IsValid())
        {
            return Existing.Get();
        }
        else
        {
            // Weak pointer is stale, remove it
            PlayerCharacterMap.Remove(Data.CharacterId);
        }
    }
    
    // Spawn new actor
    FActorSpawnParameters Params;
    APlayerCharacter* Character = GetWorld()->SpawnActor<APlayerCharacter>(
        PlayerCharacterClass,
        Data.Transform.ToUnrealTransform(),
        Params
    );
    
    if (Character)
    {
        Character->InitializeFromServer(Data);
        PlayerCharacterMap.Add(Data.CharacterId, Character);  // Add to cache
    }
    
    return Character;
}

// Retrieve from map
APlayerCharacter* UStDbConnectSubsystem::GetPlayerCharacter(uint32 CharacterId) const
{
    if (const TWeakObjectPtr<APlayerCharacter>* WeakChar = PlayerCharacterMap.Find(CharacterId))
    {
        if (WeakChar->IsValid())
        {
            return WeakChar->Get();
        }
    }
    return nullptr;
}

// Remove from map
void UStDbConnectSubsystem::OnPlayerCharacterDelete(const FEventContext& Context, const FPlayerCharacterType& RemovedRow)
{
    TWeakObjectPtr<APlayerCharacter> CharPtr;
    if (PlayerCharacterMap.RemoveAndCopyValue(RemovedRow.CharacterId, CharPtr))
    {
        if (CharPtr.IsValid())
        {
            CharPtr->Destroy();
        }
    }
}
```

---

## Summary

### What Blackholio Does Right
1. ⭐ **Event-driven architecture** - Zero-latency updates, no polling
2. ⭐ **Table index usage** - O(1) lookups instead of O(n) scans
3. ⭐ **Local entity caching** - Fast entity access via maps
4. ⭐ **Guard conditions** - Prevents redundant reducer calls
5. ⭐ **Clean delegation** - Entities handle their own update logic

### What StDBMMO Should Adopt
1. 🔴 **CRITICAL: Event delegates** - Currently missing, prevents reactive updates
2. ⚠️ **HIGH: Table indices** - Currently using manual iteration, inefficient
3. ⚠️ **HIGH: Entity maps** - Would eliminate repeated table lookups
4. ⚠️ **MEDIUM: Guard logic** - Prevent duplicate spawns and reducer calls
5. ✅ **KEEP: Subsystem lifecycle** - Better than actor for persistent connection

### Quick Wins (< 1 day each)
1. Add event delegate registration in `HandleConnect()` (+logging)
2. Replace manual iteration with indices in `HandleSubscriptionApplied()`
3. Add `GetLocalPlayerId()` helper using Identity index

### Medium Effort (2-5 days)
1. Implement entity event handlers (OnInsert/OnUpdate/OnDelete)
2. Add entity maps (PlayerCharacterMap, EntityMap)
3. Implement character spawning logic

### Optional Enhancement (3-5 days)
1. Create separate `AStDbWorldManager` actor for entity management
2. Delegate entity events from subsystem to WorldManager
3. Better separation of connection vs. game entity concerns

---

## Conclusion

Blackholio's GameManager demonstrates that efficient SpacetimeDB integration doesn't require complex architecture. The key insights are:

1. **Let SpacetimeDB do the work**: Use generated indices, event delegates, and context snapshots instead of manual management
2. **Cache strategically**: Local maps for actors, but trust tables for authoritative data
3. **Be reactive, not proactive**: Event handlers instead of polling
4. **Guard your calls**: Check state before calling reducers to prevent duplication

By adopting these patterns, StDBMMO can achieve:
- ⚡ **Better performance**: O(1) lookups, zero polling overhead
- 🎯 **Simpler code**: Less boilerplate, more readable
- 🔄 **Instant sync**: Real-time updates from server
- 🛡️ **Fewer bugs**: Guards prevent edge cases

The migration can be done incrementally, starting with event delegates and index usage (1-2 days), then adding entity management (3-5 days), with optional architectural improvements later.
