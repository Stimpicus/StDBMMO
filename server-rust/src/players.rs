use crate::types::Transform;
use spacetimedb::{spacetimedb_lib::Identity, ReducerContext, Table};

// Bring entities trait into scope because players module inserts entities.
use crate::entities::entities;

/// Player account table (identity is handled by spacetimedb-lib).
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

/// PlayerCharacter table. Do NOT derive SATS Serialize/Deserialize here;
/// the table macro supplies necessary implementations.
#[spacetimedb::table(name = player_characters, public)]
#[spacetimedb::table(name = offline_player_characters)]
#[derive(Debug, Clone)]
pub struct PlayerCharacter {
    #[primary_key]
    pub character_id: u32,
    #[index(btree)]
    pub player_id: u32,
    #[index(btree)]
    pub entity_id: u32,
    pub display_name: String,
    pub transform: Transform,
    pub needs_spawn: bool,
}

/// Spawn helpers and reducers
fn spawn_player_initial_player_character(
    ctx: &ReducerContext,
    player_id: u32,
) -> Result<crate::entities::Entity, String> {
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
) -> Result<crate::entities::Entity, String> {
    let entity = ctx.db.entities().try_insert(crate::entities::Entity {
        entity_id: 0,
        entity_type: String::from("player_pawn"),
        transform: position.clone(),
    })?;

    ctx.db.player_characters().try_insert(PlayerCharacter {
        character_id: 0,
        player_id,
        entity_id: entity.entity_id,
        display_name: String::new(),
        transform: position,
        needs_spawn: true,
    })?;

    Ok(entity)
}

#[spacetimedb::reducer]
pub fn enter_game(ctx: &ReducerContext, name: String) -> Result<(), String> {
    log::info!("Creating player with name {}", name);

    let mut player: Player = ctx
        .db
        .players()
        .identity()
        .find(&ctx.sender)
        .ok_or("Player not found")?;

    let player_id = player.player_id;
    player.display_name = name;
    ctx.db.players().identity().update(player);

    spawn_player_initial_player_character(ctx, player_id)?;

    Ok(())
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
        pc.transform = new_transform.clone();
        ctx.db.player_characters().character_id().update(pc);
    }

    Ok(())
}

#[spacetimedb::reducer]
pub fn player_spawned(ctx: &ReducerContext, character_id: u32) -> Result<(), String> {
    let mut pc = ctx
        .db
        .player_characters()
        .character_id()
        .find(&character_id)
        .ok_or("Character not found")?;
    
    pc.needs_spawn = false;
    ctx.db.player_characters().character_id().update(pc);
    
    Ok(())
}