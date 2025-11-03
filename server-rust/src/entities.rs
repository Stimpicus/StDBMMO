use crate::types::Transform;
use spacetimedb::{ReducerContext, Table};

// Bring player_characters trait into scope so ctx.db.player_characters() is available.
use crate::players::player_characters;

/// Entity table (world objects)
#[spacetimedb::table(name = entities, public)]
#[derive(Debug, Clone)]
pub struct Entity {
    #[primary_key]
    #[auto_inc]
    pub entity_id: u32,
    pub entity_type: String,
    pub transform: Transform,
}

/// Periodic reducer that applies PlayerCharacter.transform -> Entity.transform
#[spacetimedb::reducer]
pub fn move_all_players(ctx: &ReducerContext, _timer: crate::timers::MoveAllPlayersTimer) -> Result<(), String> {
    for pc in ctx.db.player_characters().iter() {
        if let Some(mut entity) = ctx.db.entities().entity_id().find(&pc.entity_id) {
            entity.transform = pc.transform.clone();
            ctx.db.entities().entity_id().update(entity);
        } else {
            continue;
        }
    }
    Ok(())
}