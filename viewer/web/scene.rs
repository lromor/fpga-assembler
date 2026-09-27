//! Turning the database into rectangles: layout, camera and picking.

use std::collections::HashMap;

use crate::model::{Bus, Evaluation, Grid, Outcome, TileDetail};
use crate::palette;
use crate::render::{CameraUniform, Instance, Viewport};

/// Accent colours, kept here so the fabric and the bit inset agree.
const SET_BIT: [f32; 4] = [0.98, 0.72, 0.26, 1.0];
const CLEARED_BIT: [f32; 4] = [0.30, 0.62, 0.95, 1.0];
const SELECTION: [f32; 4] = [0.99, 0.99, 0.99, 1.0];
const CROSSHAIR: [f32; 4] = [0.98, 0.72, 0.26, 0.16];
const INSET_BACKGROUND: [f32; 4] = [0.07, 0.08, 0.10, 0.94];
const INSET_CELL: [f32; 4] = [0.16, 0.18, 0.22, 1.0];
const INSET_FRAME: [f32; 4] = [0.30, 0.33, 0.40, 1.0];

/// A pan-and-zoom camera over a y-down world.
#[derive(Clone, Copy, Debug)]
pub struct Camera2d {
    pub center: [f32; 2],
    /// Pixels per world unit.
    pub zoom: f32,
}

impl Camera2d {
    pub fn uniform(&self, viewport: Viewport) -> CameraUniform {
        let scale_x = 2.0 * self.zoom / viewport.width.max(1.0);
        // Negated: grid y grows downward, clip y grows upward.
        let scale_y = -2.0 * self.zoom / viewport.height.max(1.0);
        CameraUniform {
            scale: [scale_x, scale_y],
            offset: [-self.center[0] * scale_x, -self.center[1] * scale_y],
        }
    }

    /// The world point under a pixel of the viewport.
    pub fn world_at(&self, viewport: Viewport, x: f32, y: f32) -> [f32; 2] {
        [
            self.center[0] + (x - viewport.width / 2.0) / self.zoom,
            self.center[1] + (y - viewport.height / 2.0) / self.zoom,
        ]
    }

    /// Frames a world rectangle, leaving a margin so it does not touch the
    /// edges of the viewport.
    pub fn fit(width: f32, height: f32, viewport: Viewport, margin: f32) -> Self {
        let zoom_x = (viewport.width - margin * 2.0).max(1.0) / width.max(1.0);
        let zoom_y = (viewport.height - margin * 2.0).max(1.0) / height.max(1.0);
        Camera2d {
            center: [width / 2.0, height / 2.0],
            zoom: zoom_x.min(zoom_y).max(0.01),
        }
    }
}

/// One bit an evaluated feature drove, placed in its tile's bit window.
#[derive(Clone, Copy, Debug)]
pub struct TouchedBit {
    pub bus: Bus,
    pub column: u32,
    pub row: i32,
    pub value: bool,
}

/// One tile the evaluation touched.
#[derive(Clone, Debug)]
pub struct Touched {
    pub tile_index: u32,
    pub bits: Vec<TouchedBit>,
}

/// Everything the renderer draws, derived from what the server sent.
/// A tile label the page draws over the canvas, in physical canvas pixels.
///
/// The two lines are reported separately because they fit at different
/// zooms: a tile is legible as `CLBLL_L` well before there is room for
/// `CLBLL_L_X16Y118`.
pub struct Label<'a> {
    pub x: f32,
    pub y: f32,
    pub name: &'a str,
    pub tile_type: &'a str,
    pub show_name: bool,
    pub show_type: bool,
}

pub struct Scene {
    grid: Option<Grid>,
    /// One colour per tile type, not per tile: the page shows the same
    /// table as a legend, so the two cannot disagree.
    type_colors: Vec<[f32; 4]>,
    colors: Vec<[f32; 4]>,
    by_cell: HashMap<(u32, u32), u32>,
    touched: Vec<Touched>,
    selected: Option<u32>,
    focus: Option<TileDetail>,
    /// When set, only tiles of this type are drawn at full strength.
    type_filter: Option<u32>,
}

impl Scene {
    pub fn new() -> Self {
        Scene {
            grid: None,
            type_colors: Vec::new(),
            colors: Vec::new(),
            by_cell: HashMap::new(),
            touched: Vec::new(),
            selected: None,
            focus: None,
            type_filter: None,
        }
    }

    pub fn set_grid(&mut self, grid: Grid) {
        self.type_colors = palette::colors_for_types(&grid.tile_types);
        self.colors = grid
            .tile_type
            .iter()
            .map(|&index| {
                self.type_colors
                    .get(index as usize)
                    .copied()
                    .unwrap_or([0.5, 0.5, 0.5, 1.0])
            })
            .collect();
        self.by_cell = grid
            .x
            .iter()
            .zip(grid.y.iter())
            .enumerate()
            .map(|(index, (&x, &y))| ((x, y), index as u32))
            .collect();
        self.touched.clear();
        self.selected = None;
        self.focus = None;
        self.type_filter = None;
        self.grid = Some(grid);
    }

    /// The colour each tile type is drawn in.
    pub fn type_colors(&self) -> &[[f32; 4]] {
        &self.type_colors
    }

    /// Draws only this tile type at full strength, or all of them.
    pub fn set_type_filter(&mut self, tile_type: Option<u32>) {
        self.type_filter = tile_type;
    }

    pub fn type_filter(&self) -> Option<u32> {
        self.type_filter
    }

    /// The type index and name of a tile.
    pub fn tile_type_of(&self, index: u32) -> Option<(u32, &str)> {
        let grid = self.grid.as_ref()?;
        let type_index = *grid.tile_type.get(index as usize)?;
        Some((
            type_index,
            grid.tile_types.get(type_index as usize)?.as_str(),
        ))
    }

    /// The visible tiles whose cells have room for a label, and which of
    /// the two lines fits.
    ///
    /// A label is included only when the text is narrower than the cell it
    /// names. Gating on zoom alone is not enough: `INT_L_X16Y118` needs
    /// half again the width of `CLBLM_L`, so at any one zoom some names fit
    /// and others would run into the neighbouring tile.
    ///
    /// `char_px` is the advance width of the label font in CSS pixels, and
    /// `type_ratio` how much narrower the second line is. The page measures
    /// both, because it owns the stylesheet.
    pub fn labels(
        &self,
        camera: &Camera2d,
        viewport: Viewport,
        max: usize,
        char_px: f32,
        type_ratio: f32,
        device_pixel_ratio: f32,
    ) -> Vec<Label<'_>> {
        let Some(grid) = &self.grid else {
            return Vec::new();
        };
        // The camera works in physical pixels; the font is measured in CSS
        // pixels, so the cell has to be expressed the same way.
        let cell = camera.zoom / device_pixel_ratio.max(0.1);
        // Room for two stacked lines of the label font.
        const TWO_LINES: f32 = 26.0;
        // Leave a little air so neighbouring labels do not touch.
        let budget = cell * 0.9;
        if char_px <= 0.0 || budget < char_px * 3.0 {
            return Vec::new();
        }

        let top_left = camera.world_at(viewport, 0.0, 0.0);
        let bottom_right = camera.world_at(viewport, viewport.width, viewport.height);

        let mut out = Vec::new();
        for index in 0..grid.len() {
            let (x, y) = (grid.x[index] as f32, grid.y[index] as f32);
            if x + 1.0 < top_left[0]
                || x > bottom_right[0]
                || y + 1.0 < top_left[1]
                || y > bottom_right[1]
            {
                continue;
            }
            let Some(name) = grid.names.get(index) else {
                continue;
            };
            let tile_type = grid
                .tile_type
                .get(index)
                .and_then(|&t| grid.tile_types.get(t as usize))
                .map(String::as_str)
                .unwrap_or("");

            let show_name = name.len() as f32 * char_px <= budget;
            let show_type = !tile_type.is_empty()
                && tile_type.len() as f32 * char_px * type_ratio <= budget
                && (!show_name || cell >= TWO_LINES);
            if !show_name && !show_type {
                continue;
            }
            out.push(Label {
                x: (x + 0.5 - camera.center[0]) * camera.zoom + viewport.width / 2.0,
                y: (y + 0.5 - camera.center[1]) * camera.zoom + viewport.height / 2.0,
                name,
                tile_type,
                show_name,
                show_type,
            });
            if out.len() >= max {
                break;
            }
        }
        out
    }

    pub fn grid(&self) -> Option<&Grid> {
        self.grid.as_ref()
    }

    pub fn tile_name(&self, index: u32) -> Option<&str> {
        self.grid
            .as_ref()?
            .names
            .get(index as usize)
            .map(String::as_str)
    }

    pub fn select(&mut self, index: Option<u32>) {
        self.selected = index;
    }

    pub fn set_focus(&mut self, focus: Option<TileDetail>) {
        self.focus = focus;
    }

    pub fn touched(&self) -> &[Touched] {
        &self.touched
    }

    /// Collects, per tile, the bits the evaluation drove there.
    pub fn set_evaluation(&mut self, evaluation: &Evaluation) {
        let mut by_tile: HashMap<u32, Vec<TouchedBit>> = HashMap::new();
        for feature in &evaluation.features {
            if feature.outcome != Outcome::Bits {
                continue;
            }
            let Some(index) = feature.tile_index else {
                continue;
            };
            let entry = by_tile.entry(index).or_default();
            for bit in &feature.frame_bits {
                entry.push(TouchedBit {
                    bus: bit.bus,
                    column: bit.word_column,
                    row: bit.word_bit,
                    value: bit.value,
                });
            }
        }
        self.touched = by_tile
            .into_iter()
            .map(|(tile_index, bits)| Touched { tile_index, bits })
            .collect();
        self.touched.sort_by_key(|touched| touched.tile_index);
    }

    pub fn clear_evaluation(&mut self) {
        self.touched.clear();
    }

    /// The tile at a world point, if any.
    pub fn tile_at(&self, world: [f32; 2]) -> Option<u32> {
        if world[0] < 0.0 || world[1] < 0.0 {
            return None;
        }
        let cell = (world[0] as u32, world[1] as u32);
        self.by_cell.get(&cell).copied()
    }

    /// The fabric itself: one rectangle per tile.
    pub fn fabric_instances(&self, out: &mut Vec<Instance>) {
        let Some(grid) = &self.grid else {
            return;
        };
        // A gap so the cells read as a grid rather than a wash of colour.
        const GAP: f32 = 0.08;
        for index in 0..grid.len() {
            let mut color = self.colors[index];
            if grid.configurable[index] == 0 {
                // Tiles with no bits of their own recede: they are the
                // scaffolding between the columns that do the work.
                color = [color[0] * 0.55, color[1] * 0.55, color[2] * 0.55, 1.0];
            }
            // Isolating a type pushes every other tile down to a backdrop,
            // which is the quickest way to see where one type actually sits.
            if self
                .type_filter
                .is_some_and(|filter| grid.tile_type[index] != filter)
            {
                color = [color[0] * 0.18, color[1] * 0.18, color[2] * 0.18, 1.0];
            }
            out.push(Instance::new(
                grid.x[index] as f32 + GAP,
                grid.y[index] as f32 + GAP,
                1.0 - GAP * 2.0,
                1.0 - GAP * 2.0,
                color,
            ));
        }
    }

    /// Everything drawn on top of the fabric: the crosshairs that make a
    /// touched tile findable at full-fabric zoom, the rings around the
    /// touched tiles, and the selection.
    pub fn overlay_instances(&self, out: &mut Vec<Instance>) {
        let Some(grid) = &self.grid else {
            return;
        };
        let (width, height) = (grid.width as f32, grid.height as f32);

        for touched in &self.touched {
            let Some(&x) = grid.x.get(touched.tile_index as usize) else {
                continue;
            };
            let Some(&y) = grid.y.get(touched.tile_index as usize) else {
                continue;
            };
            out.push(Instance::new(x as f32, 0.0, 1.0, height, CROSSHAIR));
            out.push(Instance::new(0.0, y as f32, width, 1.0, CROSSHAIR));
        }
        for touched in &self.touched {
            let Some(&x) = grid.x.get(touched.tile_index as usize) else {
                continue;
            };
            let Some(&y) = grid.y.get(touched.tile_index as usize) else {
                continue;
            };
            push_ring(out, x as f32, y as f32, 1.0, 1.0, 0.16, SET_BIT);
        }
        if let Some(selected) = self.selected
            && let (Some(&x), Some(&y)) =
                (grid.x.get(selected as usize), grid.y.get(selected as usize))
        {
            push_ring(out, x as f32, y as f32, 1.0, 1.0, 0.1, SELECTION);
        }
    }

    /// The bit window of the focused tile: one cell per configuration bit,
    /// lit where the evaluation drove it.
    ///
    /// Returns the size of the window in cells, so the caller can frame it.
    pub fn inset_instances(&self, out: &mut Vec<Instance>) -> Option<(f32, f32)> {
        let focus = self.focus.as_ref()?;
        let touched = self
            .touched
            .iter()
            .find(|touched| touched.tile_index == focus.index);
        // A tile can have a block on more than one bus -- a BRAM has both
        // its routing and its contents -- and a feature's bits land on one
        // of them.  Show the block the bits are actually in, or the first
        // one when nothing has been resolved yet.
        let bus = touched
            .and_then(|touched| touched.bits.first())
            .map(|bit| bit.bus);
        let block = bus
            .and_then(|bus| focus.tile.bits_blocks.iter().find(|block| block.bus == bus))
            .or_else(|| focus.tile.bits_blocks.first())?;
        let columns = block.frames.max(1);
        let rows = (block.words * 32).max(1);

        out.push(Instance::new(
            -2.0,
            -2.0,
            columns as f32 + 4.0,
            rows as f32 + 4.0,
            INSET_BACKGROUND,
        ));
        // The window's outline, so its extent is clear even when it is
        // mostly empty.
        push_ring(out, 0.0, 0.0, columns as f32, rows as f32, 0.5, INSET_FRAME);

        // The per-cell grid is what makes the window readable. It is built
        // when the focus changes rather than per frame, so the widest tiles
        // -- a hundred thousand bits -- are affordable; the ceiling is only
        // there so a malformed block cannot ask for an unbounded buffer.
        const MAX_CELLS: u32 = 200_000;
        const GAP: f32 = 0.12;
        if columns.saturating_mul(rows) <= MAX_CELLS {
            for column in 0..columns {
                for row in 0..rows {
                    out.push(Instance::new(
                        column as f32 + GAP,
                        row as f32 + GAP,
                        1.0 - GAP * 2.0,
                        1.0 - GAP * 2.0,
                        INSET_CELL,
                    ));
                }
            }
        }

        // The bits the evaluation drove, drawn over the grid. A cleared bit
        // ("!" in the database) is a different colour from a set one: the
        // difference is the whole point of showing them.
        if let Some(touched) = touched {
            // A lit bit is one cell in a window that can be a hundred and
            // twenty-eight frames wide, which is well under a pixel on
            // screen. The marker grows with the window so that finding the
            // bit does not depend on how large the tile happens to be.
            let marker = (columns.max(rows) as f32 / 40.0).max(1.2);
            let inset = (marker - 1.0) / 2.0;
            for bit in &touched.bits {
                // Bits on the tile's other bus belong to a different window.
                if Some(bit.bus) != bus
                    || bit.column >= columns
                    || bit.row < 0
                    || bit.row as u32 >= rows
                {
                    continue;
                }
                let color = if bit.value { SET_BIT } else { CLEARED_BIT };
                out.push(Instance::new(
                    bit.column as f32 - inset,
                    bit.row as f32 - inset,
                    marker,
                    marker,
                    color,
                ));
            }
        }
        Some((columns as f32, rows as f32))
    }
}

/// Four bars forming the outline of a rectangle, so what is inside stays
/// visible.
fn push_ring(
    out: &mut Vec<Instance>,
    x: f32,
    y: f32,
    width: f32,
    height: f32,
    thickness: f32,
    color: [f32; 4],
) {
    out.push(Instance::new(x, y, width, thickness, color));
    out.push(Instance::new(
        x,
        y + height - thickness,
        width,
        thickness,
        color,
    ));
    out.push(Instance::new(x, y, thickness, height, color));
    out.push(Instance::new(
        x + width - thickness,
        y,
        thickness,
        height,
        color,
    ));
}
