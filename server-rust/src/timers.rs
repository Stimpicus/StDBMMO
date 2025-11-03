use std::time::Duration;
use spacetimedb::ScheduleAt;

/// Timer table for moving all players.
/// Use a crate-qualified path for the scheduled reducer so the macro can resolve it
/// during expansion regardless of module compile ordering.
#[spacetimedb::table(name = move_all_players_timer, scheduled(crate::entities::move_all_players))]
pub struct MoveAllPlayersTimer {
    #[primary_key]
    #[auto_inc]
    pub scheduled_id: u64,
    pub scheduled_at: spacetimedb::ScheduleAt,
}

/// Helper to initialize the timer row.
pub fn default_move_all_players_interval() -> MoveAllPlayersTimer {
    MoveAllPlayersTimer {
        scheduled_id: 0,
        scheduled_at: ScheduleAt::Interval(Duration::from_millis(50).into()),
    }
}