use crate::timers::default_move_all_players_interval;
use spacetimedb::{ReducerContext, Table};

// Bring the generated table helper traits into scope so ctx.db.<table>() helpers exist.
use crate::players::players;
use crate::players::offline_players;
use crate::players::player_characters;
use crate::players::offline_player_characters;
use crate::entities::entities;
use crate::timers::move_all_players_timer;

// Import spawn helper from players module
use crate::players::spawn_player_initial_player_character;

/// init reducer: create timer row
#[spacetimedb::reducer(init)]
pub fn init(ctx: &ReducerContext) -> Result<(), String> {
    log::info!("Initializing...");
    ctx.db
        .move_all_players_timer()
        .try_insert(default_move_all_players_interval())?;
    Ok(())
}

/// client_connected: restore from offline or create player row
#[spacetimedb::reducer(client_connected)]
pub fn connect(ctx: &ReducerContext) -> Result<(), String> {
    if let Some(player) = ctx.db.offline_players().identity().find(&ctx.sender) {
        ctx.db.players().try_insert(player.clone())?;
        ctx.db.offline_players().identity().delete(&player.identity);

        // restore offline characters for this player
        for pc in ctx.db.offline_player_characters().player_id().filter(&player.player_id) {
            ctx.db
                .offline_player_characters()
                .character_id()
                .delete(pc.character_id);
            ctx.db.player_characters().try_insert(pc.clone())?;
        }
    } else {
        // create a new blank player row
        let player = ctx.db.players().try_insert(crate::players::Player {
            identity: ctx.sender.clone(),
            player_id: 0,
            display_name: String::new(),
        })?;

        // create initial PlayerCharacter if no existing characters
        let existing_chars: Vec<_> = ctx.db.player_characters().player_id().filter(&player.player_id).collect();
        if existing_chars.is_empty() {
            spawn_player_initial_player_character(ctx, player.player_id)?;
        }
    }
    Ok(())
}

/// client_disconnected: move player + characters to offline tables
#[spacetimedb::reducer(client_disconnected)]
pub fn disconnect(ctx: &ReducerContext) -> Result<(), String> {
    let player = ctx
        .db
        .players()
        .identity()
        .find(&ctx.sender)
        .ok_or("Player not found")?;
    let player_id = player.player_id;

    ctx.db.offline_players().try_insert(player.clone())?;
    ctx.db.players().identity().delete(&ctx.sender);

    for pc in ctx.db.player_characters().player_id().filter(&player_id) {
        // ensure entity exists (or skip)
        let _entity = ctx
            .db
            .entities()
            .entity_id()
            .find(&pc.entity_id)
            .ok_or("Entity not found")?;

        // delete live entity, move character to offline
        ctx.db.entities().entity_id().delete(&pc.entity_id);
        ctx.db.offline_player_characters().try_insert(pc.clone())?;
        ctx.db.player_characters().character_id().delete(pc.character_id);
    }

    Ok(())
}