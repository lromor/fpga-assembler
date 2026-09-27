//! The fabric viewer's renderer, compiled to wasm.
//!
//! The page owns the chrome -- the FASM box, the part picker, the panel that
//! spells a feature out -- and this owns the canvas. It is handed the
//! server's JSON and draws two things: the fabric, with the tiles a FASM
//! line touches picked out, and the bit window of the tile in focus, with
//! the individual configuration bits that changed.

mod model;
mod palette;
mod render;
mod scene;

use wasm_bindgen::prelude::*;

use crate::model::{Evaluation, Grid, TileDetail};
use crate::render::{Instance, Pass, Renderer, Viewport};
use crate::scene::{Camera2d, Scene};

/// Installed once so a Rust panic shows up in the browser console as a panic
/// and not as an unreachable-executed trap.
#[wasm_bindgen(start)]
pub fn start() {
    console_error_panic_hook::set_once();
    let _ = console_log::init_with_level(log::Level::Info);
}

/// The categories the fabric is coloured by, as `[{label, color}]`.
#[wasm_bindgen]
pub fn legend() -> Result<JsValue, JsValue> {
    let entries: Vec<_> = palette::legend()
        .into_iter()
        .map(
            |(label, color)| serde_json::json!({ "label": label, "color": palette::to_css(color) }),
        )
        .collect();
    to_json(&entries)
}

fn to_json<T: serde::Serialize>(value: &T) -> Result<JsValue, JsValue> {
    serde_json::to_string(value)
        .map(|json| JsValue::from_str(&json))
        .map_err(|error| JsValue::from_str(&error.to_string()))
}

fn to_js_error(message: impl AsRef<str>) -> JsValue {
    JsValue::from_str(message.as_ref())
}

#[wasm_bindgen]
pub struct Viewer {
    renderer: Renderer,
    scene: Scene,
    camera: Camera2d,
    /// Rebuilt only when the grid changes; it is the large one.
    fabric: Vec<Instance>,
    overlay: Vec<Instance>,
    /// Rebuilt when the focus or the evaluation changes, not per frame: a
    /// tile's bit window can run to a hundred thousand cells.
    inset: Vec<Instance>,
    inset_extent: Option<(f32, f32)>,
    show_inset: bool,
}

/// Creates the viewer on a canvas. Asynchronous because asking the browser
/// for a GPU adapter is.
#[wasm_bindgen]
pub async fn create_viewer(canvas: web_sys::HtmlCanvasElement) -> Result<Viewer, JsValue> {
    let width = canvas.width().max(1);
    let height = canvas.height().max(1);
    let renderer = Renderer::new(canvas, width, height)
        .await
        .map_err(to_js_error)?;
    Ok(Viewer {
        renderer,
        scene: Scene::new(),
        camera: Camera2d {
            center: [0.0, 0.0],
            zoom: 8.0,
        },
        fabric: Vec::new(),
        overlay: Vec::new(),
        inset: Vec::new(),
        inset_extent: None,
        show_inset: false,
    })
}

#[wasm_bindgen]
impl Viewer {
    /// The full-canvas viewport, in physical pixels.
    fn viewport(&self) -> Viewport {
        let (width, height) = self.renderer.size();
        Viewport {
            x: 0.0,
            y: 0.0,
            width: width as f32,
            height: height as f32,
        }
    }

    /// Where the bit window is drawn: a square in the bottom right, sized to
    /// the canvas but never so large that it swallows the fabric.
    fn inset_viewport(&self) -> Viewport {
        let full = self.viewport();
        let side = (full.width.min(full.height) * 0.42).clamp(120.0, 420.0);
        const MARGIN: f32 = 16.0;
        Viewport {
            x: (full.width - side - MARGIN).max(0.0),
            y: (full.height - side - MARGIN).max(0.0),
            width: side.min(full.width),
            height: side.min(full.height),
        }
    }

    /// Loads the fabric. `json` is the body of `GET .../grid`.
    pub fn set_grid(&mut self, json: &str) -> Result<(), JsValue> {
        let grid: Grid = serde_json::from_str(json)
            .map_err(|error| to_js_error(format!("the grid did not parse: {error}")))?;
        if !grid.is_consistent() {
            return Err(to_js_error(
                "the grid's parallel arrays have different lengths",
            ));
        }
        self.scene.set_grid(grid);
        self.fabric.clear();
        self.scene.fabric_instances(&mut self.fabric);
        self.show_inset = false;
        self.rebuild_inset();
        self.fit();
        Ok(())
    }

    /// Loads an evaluation. `json` is the body of `POST .../eval`.
    pub fn set_evaluation(&mut self, json: &str) -> Result<(), JsValue> {
        let evaluation: Evaluation = serde_json::from_str(json)
            .map_err(|error| to_js_error(format!("the evaluation did not parse: {error}")))?;
        self.scene.set_evaluation(&evaluation);
        // Which bits are lit in the window has just changed.
        self.rebuild_inset();
        Ok(())
    }

    pub fn clear_evaluation(&mut self) {
        self.scene.clear_evaluation();
        self.rebuild_inset();
    }

    /// Puts a tile's bit window in the inset. `json` is the body of
    /// `GET .../tiles/{name}`.
    pub fn set_focus_tile(&mut self, json: &str) -> Result<(), JsValue> {
        let detail: TileDetail = serde_json::from_str(json)
            .map_err(|error| to_js_error(format!("the tile did not parse: {error}")))?;
        self.scene.select(Some(detail.index));
        self.show_inset = !detail.tile.bits_blocks.is_empty();
        self.scene.set_focus(Some(detail));
        self.rebuild_inset();
        Ok(())
    }

    pub fn clear_focus(&mut self) {
        self.scene.select(None);
        self.scene.set_focus(None);
        self.show_inset = false;
        self.rebuild_inset();
    }

    /// The canvas was resized; `width` and `height` are physical pixels.
    pub fn resize(&mut self, width: u32, height: u32) {
        self.renderer.resize(width, height);
    }

    /// Frames the whole fabric.
    pub fn fit(&mut self) {
        let Some(grid) = self.scene.grid() else {
            return;
        };
        self.camera = Camera2d::fit(grid.width as f32, grid.height as f32, self.viewport(), 24.0);
    }

    /// Centres on a tile at a readable zoom.
    pub fn focus_on(&mut self, tile_index: u32) {
        let Some(grid) = self.scene.grid() else {
            return;
        };
        let (Some(&x), Some(&y)) = (
            grid.x.get(tile_index as usize),
            grid.y.get(tile_index as usize),
        ) else {
            return;
        };
        self.camera.center = [x as f32 + 0.5, y as f32 + 0.5];
        self.camera.zoom = self.camera.zoom.max(18.0);
    }

    /// Frames every tile the last evaluation touched.
    pub fn frame_touched(&mut self) {
        let Some(grid) = self.scene.grid() else {
            return;
        };
        let touched = self.scene.touched();
        if touched.is_empty() {
            return;
        }
        let (mut min_x, mut min_y) = (f32::MAX, f32::MAX);
        let (mut max_x, mut max_y) = (f32::MIN, f32::MIN);
        for entry in touched {
            let (Some(&x), Some(&y)) = (
                grid.x.get(entry.tile_index as usize),
                grid.y.get(entry.tile_index as usize),
            ) else {
                continue;
            };
            min_x = min_x.min(x as f32);
            min_y = min_y.min(y as f32);
            max_x = max_x.max(x as f32 + 1.0);
            max_y = max_y.max(y as f32 + 1.0);
        }
        if min_x > max_x {
            return;
        }
        // A margin of a few tiles, so a single touched tile does not fill
        // the canvas and lose all its context.
        const CONTEXT: f32 = 6.0;
        let (width, height) = (
            (max_x - min_x) + CONTEXT * 2.0,
            (max_y - min_y) + CONTEXT * 2.0,
        );
        let viewport = self.viewport();
        let zoom_x = (viewport.width - 48.0).max(1.0) / width;
        let zoom_y = (viewport.height - 48.0).max(1.0) / height;
        self.camera = Camera2d {
            center: [(min_x + max_x) / 2.0, (min_y + max_y) / 2.0],
            zoom: zoom_x.min(zoom_y).clamp(0.5, 40.0),
        };
    }

    /// Drags the fabric by a pixel delta.
    pub fn pan(&mut self, dx: f32, dy: f32) {
        self.camera.center[0] -= dx / self.camera.zoom;
        self.camera.center[1] -= dy / self.camera.zoom;
    }

    /// Zooms by `factor`, keeping the world point under (`x`, `y`) put.
    pub fn zoom_at(&mut self, factor: f32, x: f32, y: f32) {
        let viewport = self.viewport();
        let before = self.camera.world_at(viewport, x, y);
        self.camera.zoom = (self.camera.zoom * factor).clamp(0.05, 400.0);
        let after = self.camera.world_at(viewport, x, y);
        self.camera.center[0] += before[0] - after[0];
        self.camera.center[1] += before[1] - after[1];
    }

    /// The index of the tile under a canvas pixel.
    pub fn pick(&self, x: f32, y: f32) -> Option<u32> {
        let world = self.camera.world_at(self.viewport(), x, y);
        self.scene.tile_at(world)
    }

    pub fn tile_name(&self, index: u32) -> Option<String> {
        self.scene.tile_name(index).map(str::to_owned)
    }

    /// Every tile type in the loaded part, with the colour it is drawn in
    /// and the category it belongs to: the page's legend, built from the
    /// same table the renderer uses so the two cannot disagree.
    pub fn tile_types(&self) -> Result<JsValue, JsValue> {
        let Some(grid) = self.scene.grid() else {
            return to_json(&Vec::<u8>::new());
        };
        let colors = self.scene.type_colors();
        let mut counts = vec![0u32; grid.tile_types.len()];
        for &index in &grid.tile_type {
            if let Some(count) = counts.get_mut(index as usize) {
                *count += 1;
            }
        }
        let entries: Vec<_> = grid
            .tile_types
            .iter()
            .enumerate()
            .map(|(index, name)| {
                serde_json::json!({
                    "index": index,
                    "name": name,
                    "kind": palette::kind_of(name).label(),
                    "color": palette::to_css(
                        colors.get(index).copied().unwrap_or([0.5, 0.5, 0.5, 1.0]),
                    ),
                    "count": counts.get(index).copied().unwrap_or(0),
                })
            })
            .collect();
        to_json(&entries)
    }

    /// Draws only this tile type at full strength. Pass no index to show
    /// the whole fabric again.
    pub fn set_type_filter(&mut self, tile_type: Option<u32>) {
        if self.scene.type_filter() == tile_type {
            return;
        }
        self.scene.set_type_filter(tile_type);
        // The dimming is baked into the tile colours rather than layered on
        // top, so the fabric is rebuilt here instead of every frame.
        self.fabric.clear();
        self.scene.fabric_instances(&mut self.fabric);
    }

    /// The name and type of a tile.
    pub fn tile_info(&self, index: u32) -> Result<JsValue, JsValue> {
        let Some(name) = self.scene.tile_name(index) else {
            return Ok(JsValue::NULL);
        };
        let (_, tile_type) = self.scene.tile_type_of(index).unwrap_or((0, ""));
        to_json(&serde_json::json!({ "name": name, "type": tile_type }))
    }

    /// Where to put a label for each visible tile whose cell has room for
    /// one, in physical canvas pixels, and which of its two lines fit.
    ///
    /// `char_px` is the advance width of the label font and `type_ratio`
    /// how much narrower its second line is, both measured by the page.
    pub fn labels(
        &self,
        char_px: f32,
        type_ratio: f32,
        device_pixel_ratio: f32,
    ) -> Result<JsValue, JsValue> {
        // Past a few hundred the labels are unreadable anyway, and this
        // bounds the work done per frame while panning.
        const MAX_LABELS: usize = 400;
        let labels = self.scene.labels(
            &self.camera,
            self.viewport(),
            MAX_LABELS,
            char_px,
            type_ratio,
            device_pixel_ratio,
        );
        let entries: Vec<_> = labels
            .iter()
            .map(|label| {
                serde_json::json!({
                    "x": label.x,
                    "y": label.y,
                    "name": if label.show_name { label.name } else { "" },
                    "type": if label.show_type { label.tile_type } else { "" },
                })
            })
            .collect();
        to_json(&entries)
    }

    /// Rebuilds the bit window's rectangles from the current focus.
    fn rebuild_inset(&mut self) {
        self.inset.clear();
        self.inset_extent = if self.show_inset {
            self.scene.inset_instances(&mut self.inset)
        } else {
            None
        };
    }

    /// Draws a frame. Returns false when the surface had no frame to give,
    /// which happens around a resize; the caller should try again.
    pub fn render(&mut self) -> Result<bool, JsValue> {
        let viewport = self.viewport();
        self.overlay.clear();
        self.scene.overlay_instances(&mut self.overlay);

        let mut passes = vec![
            Pass {
                instances: &self.fabric,
                camera: self.camera.uniform(viewport),
                viewport,
            },
            Pass {
                instances: &self.overlay,
                camera: self.camera.uniform(viewport),
                viewport,
            },
        ];

        let inset_viewport = self.inset_viewport();
        // The window is drawn with a two-cell border, so frame that.
        let inset_camera = self
            .inset_extent
            .map(|(width, height)| Camera2d::fit(width + 4.0, height + 4.0, inset_viewport, 8.0));
        if let Some(mut camera) = inset_camera {
            camera.center = [camera.center[0] - 2.0, camera.center[1] - 2.0];
            passes.push(Pass {
                instances: &self.inset,
                camera: camera.uniform(inset_viewport),
                viewport: inset_viewport,
            });
        }

        self.renderer.render(&passes).map_err(to_js_error)
    }
}
