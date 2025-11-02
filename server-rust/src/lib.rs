use serde::{Deserialize, Serialize};
use std::time::Duration;
use spacetimedb::{spacetimedb_lib::Identity, ReducerContext, ScheduleAt, Table}; // Table trait brings table helper methods into scope

// NOTE: derive path for SpacetimeType uses the crate's exported macro. Adjust path
// if your spacetimedb version exposes it under a different path.
#[derive(Serialize, Deserialize, Debug, Clone, spacetimedb::spacetimedb_lib::SpacetimeType)]
pub struct Transform {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub yaw: f32,
    pub pitch: f32,
    pub roll: f32,
}

// Creates the Config table to store game state metadata
#[spacetimedb::table(name = config, public)]
pub struct Config {
    #[primary_key]
    pub id: u32,
    pub build_version: String,
    pub feature_flags: Option<String>,
    pub global_max_players: Option<u32>,
    pub admin_message: Option<String>,
    pub asset_manifest_url: Option<String>,
    pub last_updated_at: Option<String>,
}

// Player: account / identity metadata
#[spacetimedb::table(name = players, public)]
#[spacetimedb::table(name = offline_players)]
#[derive(Debug, Clone)]
pub struct Player {
    #[primary_key]
    pub identity: Identity,
    #[unique]
    #[auto_inc]
    pub player_id: u32,
    pub display_name: String,
}

// Entity: general world objects (NPCs, items, doors, triggers, etc.)
#[spacetimedb::table(name = entities, public)]
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Entity {
    #[primary_key]
    #[auto_inc]
    pub entity_id: u32,
    pub entity_type: String,
    pub transform: Transform,
}

// PlayerCharacter: persistent avatar state.
#[spacetimedb::table(name = player_characters, public)]
#[spacetimedb::table(name = offline_player_characters)]
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct PlayerCharacter {
    #[primary_key]
    pub character_id: u32,
    #[index(btree)]
    pub player_id: u32,
    // Reference to the in-world entity row
    #[index(btree)]
    pub entity_id: u32,
    pub display_name: String,
    pub transform: Transform,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct CharacterStats {
    pub strength: u32,
    pub intelligence: u64,
    pub dexterity: f32,
    pub health: f32,
    pub stamina: f32,
    pub mana: f32,
}

// Creates the MoveAllPlayersTimer table for scheduling player movement updates
#[spacetimedb::table(name = move_all_players_timer, scheduled(move_all_players))]
pub struct MoveAllPlayersTimer {
    #[primary_key]
    #[auto_inc]
    pub scheduled_id: u64,
    pub scheduled_at: spacetimedb::ScheduleAt,
}

// Init reducer called when the SpacetimeDB is first created or when the database is cleared
#[spacetimedb::reducer(init)]
pub fn init(ctx: &ReducerContext) -> Result<(), String> {
    log::info!("Initializing...");
    ctx.db.move_all_players_timer().try_insert(MoveAllPlayersTimer {
        scheduled_id: 0,
        scheduled_at: ScheduleAt::Interval(Duration::from_millis(50).into()),
    })?;
    Ok(())
}

// Client_connected reducer called every time a client connects to the host
#[spacetimedb::reducer(client_connected)]
pub fn connect(ctx: &ReducerContext) -> Result<(), String> {
    if let Some(player) = ctx.db.offline_players().identity().find(&ctx.sender) {
        ctx.db.players().try_insert(player.clone())?;
        ctx.db.offline_players().identity().delete(&player.identity);
        // Restore existing player pawn from offline table
        for pc in ctx.db.offline_player_characters().player_id().filter(&player.player_id) {
            ctx.db.offline_player_characters().character_id().delete(pc.character_id);
            ctx.db.player_characters().try_insert(pc.clone())?;
        }
    } else {
        ctx.db.players().try_insert(Player {
            identity: ctx.sender.clone(),
            player_id: 0,
            display_name: String::new(),
        })?;
    }
    Ok(())
}

// Disconnect reducer: move player and their character(s) to offline tables
#[spacetimedb::reducer(client_disconnected)]
pub fn disconnect(ctx: &ReducerContext) -> Result<(), String> {
    // Find the player by identity (sender)
    let player = ctx
        .db
        .players()
        .identity()
        .find(&ctx.sender)
        .ok_or("Player not found")?;
    let player_id = player.player_id;

    // Move player to offline_players table and remove from live players table
    ctx.db.offline_players().try_insert(player.clone())?;
    ctx.db.players().identity().delete(&ctx.sender);

    // Move any playerCharacter pawns from the world into offline tables
    for pc in ctx.db.player_characters().player_id().filter(&player_id) {
        // Find the associated entity in the entities table; assume it exists
        let _entity = ctx
            .db
            .entities()
            .entity_id()
            .find(&pc.entity_id)
            .ok_or("Entity not found")?;

        // Delete the entity from the live entities table
        ctx.db.entities().entity_id().delete(&pc.entity_id);

        // Move the playerCharacter into the offline_player_characters table
        ctx.db.offline_player_characters().try_insert(pc.clone())?;

        // Remove the playerCharacter from the live player_characters table
        ctx.db.player_characters().entity_id().delete(&pc.entity_id);
    }

    Ok(())
}

// Reducer to create/update player and spawn initial character
#[spacetimedb::reducer]
pub fn enter_game(ctx: &ReducerContext, name: String) -> Result<(), String> {
    log::info!("Creating player with name {}", name);

    // Find the player record by identity
    let mut player: Player = ctx
        .db
        .players()
        .identity()
        .find(&ctx.sender)
        .ok_or("Player not found")?;

    let player_id = player.player_id;
    player.display_name = name;

    // Update the player record
    ctx.db.players().identity().update(player);

    // Spawn initial PlayerCharacter for this player
    spawn_player_initial_player_character(ctx, player_id)?;

    Ok(())
}

fn spawn_player_initial_player_character(
    ctx: &ReducerContext,
    player_id: u32,
) -> Result<Entity, String> {
    let position = Transform {
        x: 0.0,
        y: 0.0,
        z: 100.0,
        yaw: 0.0,
        pitch: 0.0,
        roll: 0.0,
    };
    spawn_player_character_at(ctx, player_id, position)
}

fn spawn_player_character_at(
    ctx: &ReducerContext,
    player_id: u32,
    position: Transform,
) -> Result<Entity, String> {
    // Insert a new Entity (the pawn) into entities table
    let entity = ctx.db.entities().try_insert(Entity {
        entity_id: 0,
        entity_type: String::from("player_pawn"),
        transform: position.clone(),
    })?;

    // Insert the PlayerCharacter that references the entity
    ctx.db.player_characters().try_insert(PlayerCharacter {
        character_id: 0,
        player_id,
        entity_id: entity.entity_id,
        display_name: String::new(),
        transform: position,
    })?;

    Ok(entity)
}

#[spacetimedb::reducer]
pub fn respawn(ctx: &ReducerContext) -> Result<(), String> {
    let player = ctx
        .db
        .players()
        .identity()
        .find(&ctx.sender)
        .ok_or("No such player found")?;

    spawn_player_initial_player_character(ctx, player.player_id)?;

    Ok(())
}

#[spacetimedb::reducer]
pub fn update_player_input(ctx: &ReducerContext, new_transform: Transform) -> Result<(), String> {
    let player = ctx
        .db
        .players()
        .identity()
        .find(&ctx.sender)
        .ok_or("Player not found")?;

    for mut pc in ctx.db.player_characters().player_id().filter(&player.player_id) {
        // Apply the incoming transform as the new authoritative transform for the character.
        pc.transform = new_transform.clone();

        // Use the primary-key accessor to update the whole row by character_id
        ctx.db.player_characters().character_id().update(pc);
    }

    Ok(())
}

#[spacetimedb::reducer]
pub fn move_all_players(ctx: &ReducerContext, _timer: MoveAllPlayersTimer) -> Result<(), String> {
    // For this design we apply each PlayerCharacter.transform to the associated Entity.
    for pc in ctx.db.player_characters().iter() {
        if let Some(mut entity) = ctx.db.entities().entity_id().find(&pc.entity_id) {
            entity.transform = pc.transform.clone();
            ctx.db.entities().entity_id().update(entity);
        } else {
            // Entity missing — skip (can log if desired).
            continue;
        }
    }

    Ok(())
}