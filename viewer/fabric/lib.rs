//! A safe, owned view of the assembler's database.
//!
//! Everything here goes through the C ABI in `fpga/ffi/fabric-view.h`, so a
//! FASM line resolves to frame bits through the very code path `fpga-as`
//! uses. The C++ objects are read once into owned Rust values: the server
//! holds them across await points and serialises them, and nothing in the
//! rest of the viewer has to reason about the lifetime of a C pointer.

mod sys;

use std::ffi::{CStr, CString};
use std::fmt;
use std::os::raw::c_char;
use std::path::Path;
use std::slice;

use serde::Serialize;

/// A configuration bus of the bitstream.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Bus {
    ClbIoClk,
    BlockRam,
    CfgClb,
}

impl Bus {
    fn from_raw(raw: u32) -> Self {
        match raw {
            sys::BUS_BLOCK_RAM => Bus::BlockRam,
            sys::BUS_CFG_CLB => Bus::CfgClb,
            _ => Bus::ClbIoClk,
        }
    }
}

/// How a pseudo pip is documented.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PseudoPipKind {
    Always,
    Default,
    Hint,
}

impl PseudoPipKind {
    fn from_raw(raw: u32) -> Self {
        match raw {
            sys::PPIP_ALWAYS => PseudoPipKind::Always,
            sys::PPIP_DEFAULT => PseudoPipKind::Default,
            _ => PseudoPipKind::Hint,
        }
    }
}

/// What resolving one FASM assignment produced.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Outcome {
    /// At least one frame bit; the interesting case.
    Bits,
    /// A pseudo pip: it documents wiring and configures nothing.
    PseudoPip,
    /// Every addressed bit of the assignment was zero.
    ZeroValue,
    /// The tile or the feature is not in the database.
    Error,
}

impl Outcome {
    fn from_raw(raw: u32) -> Self {
        match raw {
            sys::OUTCOME_PSEUDO_PIP => Outcome::PseudoPip,
            sys::OUTCOME_ZERO_VALUE => Outcome::ZeroValue,
            sys::OUTCOME_ERROR => Outcome::Error,
            _ => Outcome::Bits,
        }
    }
}

/// The severity `fasm::Parse` returned, in increasing order.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ParseSeverity {
    Success,
    Info,
    NonCritical,
    Skipped,
    UserAbort,
    Error,
}

impl ParseSeverity {
    fn from_raw(raw: u32) -> Self {
        match raw {
            1 => ParseSeverity::Info,
            2 => ParseSeverity::NonCritical,
            3 => ParseSeverity::Skipped,
            4 => ParseSeverity::UserAbort,
            5 => ParseSeverity::Error,
            _ => ParseSeverity::Success,
        }
    }
}

/// A tile's window into the bitstream: `frames` frame addresses starting at
/// `base_address`, and within each the `words` words starting at `offset`.
#[derive(Clone, Debug, Serialize)]
pub struct BitsBlock {
    pub bus: Bus,
    pub base_address: u64,
    pub frames: u32,
    pub offset: i32,
    pub words: u32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub alias: Option<BitsAlias>,
}

/// Set when a tile borrows another tile type's bit definitions.
#[derive(Clone, Debug, Serialize)]
pub struct BitsAlias {
    pub tile_type: String,
    pub start_offset: u32,
}

#[derive(Clone, Debug, Serialize)]
pub struct Tile {
    pub name: String,
    pub tile_type: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub clock_region: Option<String>,
    pub grid_x: u32,
    pub grid_y: u32,
    pub site_first: u32,
    pub site_count: u32,
    pub bits_blocks: Vec<BitsBlock>,
}

#[derive(Clone, Debug, Serialize)]
pub struct Site {
    pub name: String,
    pub site_type: String,
    pub tile_index: u32,
}

/// The whole fabric, flattened. Tiles are ordered by name, so an index is
/// stable for as long as the database is.
#[derive(Clone, Debug, Serialize)]
pub struct Snapshot {
    pub tiles: Vec<Tile>,
    pub sites: Vec<Site>,
    pub width: u32,
    pub height: u32,
}

impl Snapshot {
    /// The index of a tile by name, or `None`.
    pub fn find(&self, name: &str) -> Option<usize> {
        self.tiles
            .binary_search_by(|tile| tile.name.as_str().cmp(name))
            .ok()
    }
}

/// One configuration bit a feature drives.
#[derive(Clone, Copy, Debug, Serialize)]
pub struct FrameBit {
    pub bus: Bus,
    pub frame_address: u32,
    pub word: u32,
    pub index: u32,
    pub word_column: u32,
    pub word_bit: i32,
    pub feature_address: u32,
    /// `true` to set the bit, `false` for a `!`-prefixed clear.
    pub value: bool,
}

/// The resolution of one FASM assignment.
#[derive(Clone, Debug, Serialize)]
pub struct Feature {
    pub line: u32,
    pub feature: String,
    pub tile: String,
    pub tile_type: String,
    pub tile_feature: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tile_index: Option<u32>,
    pub start_bit: i32,
    pub width: i32,
    /// The right-hand side of the assignment, as `0x...`.
    ///
    /// A string, not a number: a 64-bit INIT does not survive a round trip
    /// through a JSON number, and losing the top bits of a LUT is exactly
    /// the kind of quiet wrongness this viewer exists to rule out.
    #[serde(serialize_with = "serialize_hex")]
    pub value_bits: u64,
    pub outcome: Outcome,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    pub frame_bits: Vec<FrameBit>,
}

/// Everything one run of the evaluator produced.
#[derive(Clone, Debug, Serialize)]
pub struct Evaluation {
    pub features: Vec<Feature>,
    pub diagnostics: String,
    pub severity: ParseSeverity,
}

/// A feature the database documents for a tile type.
#[derive(Clone, Debug, Serialize)]
pub struct CatalogEntry {
    pub name: String,
    pub address: u32,
    pub bus: Bus,
    pub segbits: Vec<Segbit>,
}

/// One bit of a catalog entry, in tile-local coordinates.
#[derive(Clone, Copy, Debug, Serialize)]
pub struct Segbit {
    pub word_column: u32,
    pub word_bit: u32,
    pub is_set: bool,
}

#[derive(Clone, Debug, Serialize)]
pub struct PseudoPip {
    pub name: String,
    pub kind: PseudoPipKind,
}

#[derive(Clone, Debug, Serialize)]
pub struct Catalog {
    pub tile_type: String,
    pub entries: Vec<CatalogEntry>,
    pub pseudo_pips: Vec<PseudoPip>,
}

#[derive(Clone, Debug, Serialize)]
pub struct PartInfo {
    pub name: String,
    pub device: String,
    pub fabric: String,
    pub package: String,
    pub speedgrade: String,
}

fn serialize_hex<S: serde::Serializer>(
    value: &u64,
    serializer: S,
) -> std::result::Result<S::Ok, S::Error> {
    serializer.serialize_str(&format!("{value:#x}"))
}

/// A failure reported by the C++ side, or a bad argument on the way in.
#[derive(Clone, Debug)]
pub struct Error(String);

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

impl std::error::Error for Error {}

impl Error {
    fn new(message: impl Into<String>) -> Self {
        Error(message.into())
    }
}

type Result<T> = std::result::Result<T, Error>;

/// Reads a borrowed C string into an owned `String`, lossily: the database is
/// ASCII in practice, and a mangled byte is not worth failing a request over.
///
/// # Safety
/// `text` must be a slice the owning C++ object still keeps alive.
unsafe fn to_string(text: sys::fpga_view_str) -> String {
    if text.data.is_null() || text.size == 0 {
        return String::new();
    }
    let bytes = unsafe { slice::from_raw_parts(text.data.cast::<u8>(), text.size as usize) };
    String::from_utf8_lossy(bytes).into_owned()
}

/// The same, but an empty string becomes `None`.
unsafe fn to_option_string(text: sys::fpga_view_str) -> Option<String> {
    let value = unsafe { to_string(text) };
    if value.is_empty() { None } else { Some(value) }
}

/// Takes ownership of an error the C++ side allocated and frees it.
///
/// # Safety
/// `error` must be the out-parameter of a call that just failed.
unsafe fn take_error(error: *mut c_char, fallback: &str) -> Error {
    if error.is_null() {
        return Error::new(fallback);
    }
    let message = unsafe { CStr::from_ptr(error) }
        .to_string_lossy()
        .into_owned();
    unsafe { sys::fpga_view_string_free(error) };
    Error::new(message)
}

fn to_c_string(value: &str) -> Result<CString> {
    CString::new(value).map_err(|_| Error::new("the value contains a NUL byte"))
}

fn path_to_c_string(path: &Path) -> Result<CString> {
    let text = path
        .to_str()
        .ok_or_else(|| Error::new("the path is not valid UTF-8"))?;
    to_c_string(text)
}

/// Checks that this binding's structs are laid out the way the C++ side lays
/// them out. Called once per handle, so a mismatch surfaces as an error on
/// the first request instead of as garbage in the renderer.
fn assert_layout_matches() -> Result<()> {
    let expected: [(u32, usize, &str); 9] = [
        (sys::ABI_STR, size_of::<sys::fpga_view_str>(), "str"),
        (
            sys::ABI_BITS_BLOCK,
            size_of::<sys::fpga_view_bits_block>(),
            "bits_block",
        ),
        (sys::ABI_TILE, size_of::<sys::fpga_view_tile>(), "tile"),
        (sys::ABI_SITE, size_of::<sys::fpga_view_site>(), "site"),
        (
            sys::ABI_FRAME_BIT,
            size_of::<sys::fpga_view_frame_bit>(),
            "frame_bit",
        ),
        (
            sys::ABI_FEATURE,
            size_of::<sys::fpga_view_feature>(),
            "feature",
        ),
        (
            sys::ABI_CATALOG_ENTRY,
            size_of::<sys::fpga_view_catalog_entry>(),
            "catalog_entry",
        ),
        (
            sys::ABI_SEGBIT,
            size_of::<sys::fpga_view_segbit>(),
            "segbit",
        ),
        (sys::ABI_PPIP, size_of::<sys::fpga_view_ppip>(), "ppip"),
    ];
    for (which, rust_size, name) in expected {
        let c_size = unsafe { sys::fpga_view_abi_sizeof(which) };
        if c_size != rust_size {
            return Err(Error::new(format!(
                "fabric-view ABI mismatch: C++ sizeof({name}) is {c_size}, \
                 this binding assumes {rust_size}"
            )));
        }
    }
    Ok(())
}

/// The parts a family root declares.
pub fn parts(db_root: &Path) -> Result<Vec<PartInfo>> {
    assert_layout_matches()?;
    let root = path_to_c_string(db_root)?;
    let mut error: *mut c_char = std::ptr::null_mut();
    let handle = unsafe { sys::fpga_view_parts_open(root.as_ptr(), &mut error) };
    if handle.is_null() {
        return Err(unsafe { take_error(error, "could not read the part mapping") });
    }
    let count = unsafe { sys::fpga_view_parts_count(handle) };
    let mut out = Vec::with_capacity(count as usize);
    for index in 0..count {
        let mut name = sys::fpga_view_str {
            data: std::ptr::null(),
            size: 0,
        };
        let (mut device, mut fabric, mut package, mut speedgrade) = (name, name, name, name);
        let ok = unsafe {
            sys::fpga_view_parts_get(
                handle,
                index,
                &mut name,
                &mut device,
                &mut fabric,
                &mut package,
                &mut speedgrade,
            )
        };
        if ok == 0 {
            continue;
        }
        out.push(unsafe {
            PartInfo {
                name: to_string(name),
                device: to_string(device),
                fabric: to_string(fabric),
                package: to_string(package),
                speedgrade: to_string(speedgrade),
            }
        });
    }
    unsafe { sys::fpga_view_parts_close(handle) };
    Ok(out)
}

/// An open part database.
///
/// The C++ side guards every entry point with a mutex, because resolving a
/// feature fills a segbits cache and so mutates the database. That is what
/// makes this `Sync`.
pub struct Database {
    handle: *mut sys::fpga_view_db,
    /// Built once when the database opens: the evaluator needs it to name
    /// the tile a feature lands on, and reading the grid is not cheap.
    snapshot: *mut sys::fpga_view_snapshot,
}

impl fmt::Debug for Database {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("Database")
    }
}

// SAFETY: the handle is only ever used through the C ABI, whose entry points
// take an internal mutex before touching the database.
unsafe impl Send for Database {}
unsafe impl Sync for Database {}

impl Drop for Database {
    fn drop(&mut self) {
        // The snapshot borrows from the database, so it goes first.
        unsafe { sys::fpga_view_snapshot_close(self.snapshot) };
        unsafe { sys::fpga_view_db_close(self.handle) };
    }
}

impl Database {
    /// Opens `part` under a family root such as `.../prjxray-db/artix7`.
    /// Parsing the grid and the mapping takes a moment and a good deal of
    /// memory, so callers open one and keep it.
    pub fn open(db_root: &Path, part: &str) -> Result<Self> {
        assert_layout_matches()?;
        let root = path_to_c_string(db_root)?;
        let part = to_c_string(part)?;
        let mut error: *mut c_char = std::ptr::null_mut();
        let handle = unsafe { sys::fpga_view_db_open(root.as_ptr(), part.as_ptr(), &mut error) };
        if handle.is_null() {
            return Err(unsafe { take_error(error, "could not open the database") });
        }
        let snapshot = unsafe { sys::fpga_view_db_snapshot(handle, &mut error) };
        if snapshot.is_null() {
            unsafe { sys::fpga_view_db_close(handle) };
            return Err(unsafe { take_error(error, "could not read the tile grid") });
        }
        Ok(Database { handle, snapshot })
    }

    /// Reads the whole tile grid into owned values.
    pub fn snapshot(&self) -> Snapshot {
        unsafe { read_snapshot(self.snapshot) }
    }

    /// Resolves FASM text the way the assembler would.
    pub fn eval(&self, text: &str) -> Result<Evaluation> {
        let text = to_c_string(text)?;
        let mut error: *mut c_char = std::ptr::null_mut();
        let handle = unsafe {
            sys::fpga_view_db_eval(self.handle, self.snapshot, text.as_ptr(), &mut error)
        };
        if handle.is_null() {
            return Err(unsafe { take_error(error, "could not evaluate the input") });
        }
        let evaluation = unsafe { read_evaluation(handle) };
        unsafe { sys::fpga_view_eval_close(handle) };
        Ok(evaluation)
    }

    /// Every feature and pseudo pip documented for one tile type.
    pub fn catalog(&self, tile_type: &str) -> Result<Catalog> {
        let name = to_c_string(tile_type)?;
        let mut error: *mut c_char = std::ptr::null_mut();
        let handle = unsafe { sys::fpga_view_db_catalog(self.handle, name.as_ptr(), &mut error) };
        if handle.is_null() {
            return Err(unsafe { take_error(error, "no database for that tile type") });
        }
        let catalog = unsafe { read_catalog(tile_type, handle) };
        unsafe { sys::fpga_view_catalog_close(handle) };
        Ok(catalog)
    }
}

/// # Safety
/// `handle` must be a live snapshot.
unsafe fn read_snapshot(handle: *mut sys::fpga_view_snapshot) -> Snapshot {
    let mut tile_count = 0u32;
    let tiles_ptr = unsafe { sys::fpga_view_snapshot_tiles(handle, &mut tile_count) };
    let mut site_count = 0u32;
    let sites_ptr = unsafe { sys::fpga_view_snapshot_sites(handle, &mut site_count) };
    let (mut width, mut height) = (0u32, 0u32);
    unsafe { sys::fpga_view_snapshot_extent(handle, &mut width, &mut height) };

    let raw_tiles = if tiles_ptr.is_null() {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(tiles_ptr, tile_count as usize) }
    };
    let raw_sites = if sites_ptr.is_null() {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(sites_ptr, site_count as usize) }
    };

    let tiles = raw_tiles
        .iter()
        .map(|tile| {
            let blocks = tile.bits_blocks[..(tile.bits_block_count as usize).min(3)]
                .iter()
                .map(|block| BitsBlock {
                    bus: Bus::from_raw(block.bus),
                    base_address: block.base_address,
                    frames: block.frames,
                    offset: block.offset,
                    words: block.words,
                    alias: unsafe { to_option_string(block.alias_type) }.map(|tile_type| {
                        BitsAlias {
                            tile_type,
                            start_offset: block.alias_start_offset,
                        }
                    }),
                })
                .collect();
            Tile {
                name: unsafe { to_string(tile.name) },
                tile_type: unsafe { to_string(tile.type_) },
                clock_region: unsafe { to_option_string(tile.clock_region) },
                grid_x: tile.grid_x,
                grid_y: tile.grid_y,
                site_first: tile.site_first,
                site_count: tile.site_count,
                bits_blocks: blocks,
            }
        })
        .collect();

    let sites = raw_sites
        .iter()
        .map(|site| Site {
            name: unsafe { to_string(site.name) },
            site_type: unsafe { to_string(site.type_) },
            tile_index: site.tile_index,
        })
        .collect();

    Snapshot {
        tiles,
        sites,
        width,
        height,
    }
}

/// # Safety
/// `handle` must be a live evaluation.
unsafe fn read_evaluation(handle: *mut sys::fpga_view_eval) -> Evaluation {
    let mut feature_count = 0u32;
    let features_ptr = unsafe { sys::fpga_view_eval_features(handle, &mut feature_count) };
    let mut bit_count = 0u32;
    let bits_ptr = unsafe { sys::fpga_view_eval_frame_bits(handle, &mut bit_count) };

    let raw_features = if features_ptr.is_null() {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(features_ptr, feature_count as usize) }
    };
    let raw_bits = if bits_ptr.is_null() {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(bits_ptr, bit_count as usize) }
    };

    let features = raw_features
        .iter()
        .map(|feature| {
            let first = feature.frame_bit_first as usize;
            let last = first.saturating_add(feature.frame_bit_count as usize);
            let frame_bits = raw_bits
                .get(first..last.min(raw_bits.len()))
                .unwrap_or(&[])
                .iter()
                .map(|bit| FrameBit {
                    bus: Bus::from_raw(bit.bus),
                    frame_address: bit.frame_address,
                    word: bit.word,
                    index: bit.index,
                    word_column: bit.word_column,
                    word_bit: bit.word_bit,
                    feature_address: bit.feature_address,
                    value: bit.value != 0,
                })
                .collect();
            Feature {
                line: feature.line,
                feature: unsafe { to_string(feature.feature) },
                tile: unsafe { to_string(feature.tile) },
                tile_type: unsafe { to_string(feature.tile_type) },
                tile_feature: unsafe { to_string(feature.tile_feature) },
                tile_index: u32::try_from(feature.tile_index).ok(),
                start_bit: feature.start_bit,
                width: feature.width,
                value_bits: feature.value_bits,
                outcome: Outcome::from_raw(feature.outcome),
                error: unsafe { to_option_string(feature.error) },
                frame_bits,
            }
        })
        .collect();

    Evaluation {
        features,
        diagnostics: unsafe { to_string(sys::fpga_view_eval_diagnostics(handle)) },
        severity: ParseSeverity::from_raw(unsafe { sys::fpga_view_eval_parse_result(handle) }),
    }
}

/// # Safety
/// `handle` must be a live catalog.
unsafe fn read_catalog(tile_type: &str, handle: *mut sys::fpga_view_catalog) -> Catalog {
    let mut entry_count = 0u32;
    let entries_ptr = unsafe { sys::fpga_view_catalog_entries(handle, &mut entry_count) };
    let mut segbit_count = 0u32;
    let segbits_ptr = unsafe { sys::fpga_view_catalog_segbits(handle, &mut segbit_count) };
    let mut ppip_count = 0u32;
    let ppips_ptr = unsafe { sys::fpga_view_catalog_ppips(handle, &mut ppip_count) };

    let raw_entries = if entries_ptr.is_null() {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(entries_ptr, entry_count as usize) }
    };
    let raw_segbits = if segbits_ptr.is_null() {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(segbits_ptr, segbit_count as usize) }
    };
    let raw_ppips = if ppips_ptr.is_null() {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(ppips_ptr, ppip_count as usize) }
    };

    let entries = raw_entries
        .iter()
        .map(|entry| {
            let first = entry.segbit_first as usize;
            let last = first.saturating_add(entry.segbit_count as usize);
            CatalogEntry {
                name: unsafe { to_string(entry.name) },
                address: entry.address,
                bus: Bus::from_raw(entry.bus),
                segbits: raw_segbits
                    .get(first..last.min(raw_segbits.len()))
                    .unwrap_or(&[])
                    .iter()
                    .map(|segbit| Segbit {
                        word_column: segbit.word_column,
                        word_bit: segbit.word_bit,
                        is_set: segbit.is_set != 0,
                    })
                    .collect(),
            }
        })
        .collect();

    let pseudo_pips = raw_ppips
        .iter()
        .map(|ppip| PseudoPip {
            name: unsafe { to_string(ppip.name) },
            kind: PseudoPipKind::from_raw(ppip.type_),
        })
        .collect();

    Catalog {
        tile_type: tile_type.to_string(),
        entries,
        pseudo_pips,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The miniature database `//fpga/ffi:testdata` carries, so these tests
    /// assert against bits that are written down rather than fuzzed.
    const DB_ROOT: &str = "fpga/ffi/testdata/artix7";
    const PART: &str = "xc7tiny-test-1";

    fn open() -> Database {
        Database::open(Path::new(DB_ROOT), PART).expect("the test database opens")
    }

    #[test]
    fn the_binding_agrees_with_the_cxx_layout() {
        assert_layout_matches().expect("the struct layouts match");
    }

    #[test]
    fn parts_are_read_from_the_mapping() {
        let parts = parts(Path::new(DB_ROOT)).expect("the mapping is readable");
        assert_eq!(parts.len(), 1);
        assert_eq!(parts[0].name, PART);
        assert_eq!(parts[0].fabric, "xc7tiny");
    }

    #[test]
    fn opening_a_missing_database_reports_the_cxx_error() {
        let error = Database::open(Path::new("/nonexistent/database"), PART)
            .expect_err("a missing database fails");
        assert!(!error.to_string().is_empty());
    }

    #[test]
    fn the_snapshot_carries_the_grid() {
        let snapshot = open().snapshot();
        assert_eq!(snapshot.tiles.len(), 4);
        assert_eq!((snapshot.width, snapshot.height), (3, 2));
        let index = snapshot.find("CLBLM_R_X1Y0").expect("the tile is present");
        let tile = &snapshot.tiles[index];
        assert_eq!(tile.tile_type, "CLBLM_R");
        assert_eq!(tile.clock_region.as_deref(), Some("X0Y0"));
        assert_eq!(tile.bits_blocks.len(), 1);
        assert_eq!(tile.bits_blocks[0].base_address, 0x0040_1080);
        assert_eq!(tile.bits_blocks[0].offset, 77);
        assert!(tile.bits_blocks[0].alias.is_none());
        assert_eq!(snapshot.find("NOPE_X0Y0"), None);

        // The sites of a tile are the slice its `site_first`/`site_count`
        // name, so the renderer can label a tile without a second lookup.
        let sites =
            &snapshot.sites[tile.site_first as usize..(tile.site_first + tile.site_count) as usize];
        assert_eq!(sites.len(), 2);
        assert_eq!(sites[0].name, "SLICE_X0Y0");
        assert_eq!(sites[0].site_type, "SLICEM");
    }

    #[test]
    fn a_lut_init_bit_resolves_to_its_frame_address() {
        let evaluation = open()
            .eval("CLBLM_R_X1Y0.SLICEM_X0.ALUT.INIT[34]=1")
            .expect("the line evaluates");
        assert_eq!(evaluation.severity, ParseSeverity::Success);
        assert_eq!(evaluation.features.len(), 1);
        let feature = &evaluation.features[0];
        assert_eq!(feature.outcome, Outcome::Bits);
        assert_eq!(feature.tile, "CLBLM_R_X1Y0");
        assert_eq!(feature.tile_feature, "SLICEM_X0.ALUT.INIT");
        assert_eq!(feature.tile_index, Some(1));
        assert_eq!(feature.frame_bits.len(), 1);
        let bit = feature.frame_bits[0];
        assert_eq!(bit.frame_address, 0x0040_1080 + 34);
        assert_eq!(bit.word, 77);
        assert_eq!(bit.index, 6);
        assert_eq!(bit.word_column, 34);
        assert_eq!(bit.word_bit, 6);
        assert!(bit.value);
    }

    #[test]
    fn a_tiles_bit_blocks_come_back_in_bus_order() {
        let snapshot = open().snapshot();
        let index = snapshot.find("BRAM_L_X0Y1").expect("the tile is present");
        let buses: Vec<Bus> = snapshot.tiles[index]
            .bits_blocks
            .iter()
            .map(|block| block.bus)
            .collect();
        assert_eq!(buses, vec![Bus::ClbIoClk, Bus::BlockRam]);
    }

    #[test]
    fn a_bram_content_bit_lands_on_the_block_ram_bus() {
        let evaluation = open()
            .eval("BRAM_L_X0Y1.RAMB18_Y0.INIT_00[2:0]=3'b100")
            .expect("the line evaluates");
        let bits = &evaluation.features[0].frame_bits;
        assert_eq!(bits.len(), 1);
        assert_eq!(bits[0].bus, Bus::BlockRam);
        assert_eq!(bits[0].frame_address, 0x00c0_0000);
        assert_eq!(bits[0].word_bit, 5);
    }

    #[test]
    fn a_cleared_bit_is_distinguishable_from_a_set_one() {
        let evaluation = open()
            .eval("CLBLM_R_X1Y0.SLICEM_X0.WA7USED")
            .expect("the line evaluates");
        let bits = &evaluation.features[0].frame_bits;
        assert_eq!(bits.len(), 2);
        assert!(bits[0].value);
        assert!(!bits[1].value);
    }

    #[test]
    fn a_pseudo_pip_is_not_an_error() {
        let evaluation = open()
            .eval("CLBLM_R_X1Y0.SLICEM_X0.CLKINV.CLK")
            .expect("the line evaluates");
        assert_eq!(evaluation.features[0].outcome, Outcome::PseudoPip);
        assert!(evaluation.features[0].frame_bits.is_empty());
        assert!(evaluation.features[0].error.is_none());
    }

    #[test]
    fn an_unknown_feature_does_not_sink_the_rest_of_the_input() {
        let evaluation = open()
            .eval(
                "CLBLM_R_X1Y0.SLICEM_X0.NOT_A_FEATURE\n\
                 CLBLM_R_X1Y0.SLICEM_X0.ALUT.INIT[34]=1\n",
            )
            .expect("the input evaluates");
        assert_eq!(evaluation.features.len(), 2);
        assert_eq!(evaluation.features[0].outcome, Outcome::Error);
        assert!(evaluation.features[0].error.is_some());
        assert_eq!(evaluation.features[1].outcome, Outcome::Bits);
    }

    #[test]
    fn the_catalog_lists_the_tile_types_features() {
        let catalog = open().catalog("CLBLM_R").expect("the tile type is known");
        assert_eq!(catalog.entries.len(), 4);
        assert_eq!(catalog.entries[0].name, "CLBLM_R.SLICEL_X1.AFF.ZINI");
        assert_eq!(catalog.entries[1].address, 34);
        let wa7used = catalog
            .entries
            .iter()
            .find(|entry| entry.name.ends_with("WA7USED"))
            .expect("the feature is in the catalog");
        assert_eq!(wa7used.segbits.len(), 2);
        assert!(wa7used.segbits[0].is_set);
        assert!(!wa7used.segbits[1].is_set);
        assert_eq!(catalog.pseudo_pips.len(), 1);
        assert_eq!(catalog.pseudo_pips[0].kind, PseudoPipKind::Default);
    }

    #[test]
    fn an_unknown_tile_type_has_no_catalog() {
        assert!(open().catalog("NOT_A_TILE_TYPE").is_err());
    }
}
