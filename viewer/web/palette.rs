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

/// Colours for a whole set of tile types.
///
/// A category colour alone is not enough to read the fabric: a part has a
/// dozen kinds of interconnect and half a dozen kinds of CLB, and drawn in
/// one flat blue they are a wall. Each type gets its own shade of its
/// category's colour, spread evenly across the types that share the
/// category, so the family is still legible at a glance and the individual
/// columns are still told apart.
///
/// The spread is by sorted name rather than by order of appearance, so a
/// type keeps its shade no matter what order the grid lists tiles in.
pub fn colors_for_types(tile_types: &[String]) -> Vec<Color> {
    // Position of each type within its category, by name.
    let mut by_kind: std::collections::HashMap<&str, Vec<(&str, usize)>> =
        std::collections::HashMap::new();
    for (index, name) in tile_types.iter().enumerate() {
        by_kind
            .entry(kind_of(name).label())
            .or_default()
            .push((name.as_str(), index));
    }

    let mut colors = vec![[0.5, 0.5, 0.5, 1.0]; tile_types.len()];
    for group in by_kind.values_mut() {
        group.sort_unstable();
        let total = group.len();
        for (position, &(name, index)) in group.iter().enumerate() {
            let base = kind_of(name).color();
            // Evenly spaced brightness, centred on the category colour. A
            // lone type in its category keeps that colour exactly.
            let factor = if total <= 1 {
                1.0
            } else {
                let t = position as f32 / (total - 1) as f32;
                0.62 + t * 0.76
            };
            colors[index] = [
                (base[0] * factor).clamp(0.0, 1.0),
                (base[1] * factor).clamp(0.0, 1.0),
                (base[2] * factor).clamp(0.0, 1.0),
                1.0,
            ];
        }
    }
    colors
}

/// A linear colour as the `#rrggbb` the page needs for a swatch.
pub fn to_css(color: Color) -> String {
    let channel = |value: f32| (value.sqrt() * 255.0).round().clamp(0.0, 255.0) as u8;
    format!(
        "#{:02x}{:02x}{:02x}",
        channel(color[0]),
        channel(color[1]),
        channel(color[2])
    )
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
