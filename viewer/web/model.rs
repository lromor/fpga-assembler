//! The payloads the server sends, as the renderer needs them.
//!
//! Only the fields the drawing uses are deserialised; the side panel reads
//! the rest of the JSON in JavaScript, where it is text anyway.

use serde::Deserialize;

/// The fabric, as parallel arrays.
#[derive(Debug, Deserialize)]
pub struct Grid {
    pub width: u32,
    pub height: u32,
    pub tile_types: Vec<String>,
    pub names: Vec<String>,
    pub x: Vec<u32>,
    pub y: Vec<u32>,
    pub tile_type: Vec<u32>,
    pub configurable: Vec<u8>,
}

impl Grid {
    /// True when every parallel array is as long as the tile list, which is
    /// the one thing the renderer cannot check as it goes.
    pub fn is_consistent(&self) -> bool {
        let count = self.names.len();
        self.x.len() == count
            && self.y.len() == count
            && self.tile_type.len() == count
            && self.configurable.len() == count
    }

    pub fn len(&self) -> usize {
        self.names.len()
    }
}

/// A configuration bus, as the server names it. A tile can have a block on
/// more than one, and a feature's bits land on exactly one of them.
#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum Bus {
    ClbIoClk,
    BlockRam,
    CfgClb,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum Outcome {
    Bits,
    PseudoPip,
    ZeroValue,
    Error,
}

/// Only the tile-local position is kept: the absolute frame address and
/// word are text for the panel, and the page shows those itself.
#[derive(Clone, Copy, Debug, Deserialize)]
pub struct FrameBit {
    pub bus: Bus,
    pub word_column: u32,
    pub word_bit: i32,
    pub value: bool,
}

#[derive(Clone, Debug, Deserialize)]
pub struct Feature {
    pub tile_index: Option<u32>,
    pub outcome: Outcome,
    pub frame_bits: Vec<FrameBit>,
}

#[derive(Clone, Debug, Deserialize)]
pub struct Evaluation {
    pub features: Vec<Feature>,
}

/// The extent of a tile's bit window; the renderer draws `frames` columns
/// of `words * 32` cells.
#[derive(Clone, Copy, Debug, Deserialize)]
pub struct BitsBlock {
    pub bus: Bus,
    pub frames: u32,
    pub words: u32,
}

#[derive(Clone, Debug, Deserialize)]
pub struct Tile {
    pub bits_blocks: Vec<BitsBlock>,
}

/// What `GET .../tiles/{name}` returns.
#[derive(Clone, Debug, Deserialize)]
pub struct TileDetail {
    pub tile: Tile,
    pub index: u32,
}
