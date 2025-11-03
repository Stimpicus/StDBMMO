use spacetimedb_sats::{Serialize, Deserialize}; // SATS derives (crate name in Cargo.toml: spacetimedb-sats)
use spacetimedb::SpacetimeType;

/// Transform must implement SpacetimeType to be used as table columns and reducer args.
/// Do NOT also derive SATS Serialize/Deserialize here — SpacetimeType provides the required impls.
#[derive(Debug, Clone, SpacetimeType)]
pub struct Transform {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub yaw: f32,
    pub pitch: f32,
    pub roll: f32,
}

/// CharacterStats is a helper structure (not a table column in this layout).
/// If you need to serialize it with SATS, derive SATS here.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CharacterStats {
    pub strength: u32,
    pub intelligence: u64,
    pub dexterity: f32,
    pub health: f32,
    pub stamina: f32,
    pub mana: f32,
}