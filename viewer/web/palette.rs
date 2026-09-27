//! Colours for the fabric.
//!
//! Tiles are coloured by what they are, not by a hash of their name: a
//! fabric map is only readable if the columns of logic, interconnect, memory
//! and IO are told apart at a glance.

/// A linear-space RGBA colour.
pub type Color = [f32; 4];

/// The broad kind of a tile, derived from its type name.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Kind {
    Logic,
    Interconnect,
    Memory,
    Dsp,
    Io,
    Clock,
    HardBlock,
    Structure,
}

/// One sRGB byte as the linear float the surface expects.
///
/// A gamma of 2.2 would be exact; squaring is within a shade of it and
/// stays a const fn, so the palette is a compile-time table.
const fn to_linear(channel: u8) -> f32 {
    let value = channel as f32 / 255.0;
    value * value
}

/// An sRGB triple as a linear colour.
const fn srgb(r: u8, g: u8, b: u8) -> Color {
    [to_linear(r), to_linear(g), to_linear(b), 1.0]
}

impl Kind {
    pub fn color(self) -> Color {
        match self {
            Kind::Logic => srgb(90, 134, 232),
            Kind::Interconnect => srgb(58, 82, 122),
            Kind::Memory => srgb(158, 110, 214),
            Kind::Dsp => srgb(222, 152, 72),
            Kind::Io => srgb(214, 186, 84),
            Kind::Clock => srgb(78, 190, 190),
            Kind::HardBlock => srgb(206, 106, 148),
            Kind::Structure => srgb(48, 54, 66),
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Kind::Logic => "logic",
            Kind::Interconnect => "interconnect",
            Kind::Memory => "memory",
            Kind::Dsp => "dsp",
            Kind::Io => "io",
            Kind::Clock => "clock",
            Kind::HardBlock => "hard block",
            Kind::Structure => "structure",
        }
    }
}

/// Classifies a prjxray tile type name.
///
/// The order matters: `BRAM_INT_INTERFACE_L` is interconnect around a BRAM,
/// not memory, and `HCLK_IOI3` is clocking, not IO.
pub fn kind_of(tile_type: &str) -> Kind {
    let name = tile_type;
    if name.contains("INT_INTERFACE") || name.starts_with("INT_") || name == "INT" {
        return Kind::Interconnect;
    }
    if name.starts_with("HCLK")
        || name.starts_with("CLK")
        || name.starts_with("CMT")
        || name.contains("BUFG")
        || name.contains("PMV")
    {
        return Kind::Clock;
    }
    if name.starts_with("CLBL") {
        return Kind::Logic;
    }
    if name.starts_with("BRAM") {
        return Kind::Memory;
    }
    if name.starts_with("DSP") {
        return Kind::Dsp;
    }
    if name.contains("IOB") || name.contains("IOI") || name.contains("IOPAD") {
        return Kind::Io;
    }
    if name.starts_with("GTP")
        || name.starts_with("GTX")
        || name.starts_with("PCIE")
        || name.starts_with("MONITOR")
        || name.contains("XADC")
        || name.starts_with("CFG_CENTER")
    {
        return Kind::HardBlock;
    }
    Kind::Structure
}

pub fn color_of(tile_type: &str) -> Color {
    kind_of(tile_type).color()
}

/// Every kind with its label and colour, for the legend the page draws.
pub fn legend() -> Vec<(&'static str, Color)> {
    [
        Kind::Logic,
        Kind::Interconnect,
        Kind::Memory,
        Kind::Dsp,
        Kind::Io,
        Kind::Clock,
        Kind::HardBlock,
        Kind::Structure,
    ]
    .into_iter()
    .map(|kind| (kind.label(), kind.color()))
    .collect()
}
