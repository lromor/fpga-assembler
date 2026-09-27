//! Raw declarations of the C ABI in `fpga/ffi/fabric-view.h`.
//!
//! Hand-written rather than generated, so the safety contract of every call
//! is written down next to it. `abi::assert_layout_matches` checks the struct
//! sizes against the C++ side at run time, which catches the one mistake a
//! hand-written binding can silently make.

#![allow(non_camel_case_types)]
// This module mirrors the C header in full, including the constants and
// entry points the rest of the viewer happens not to call yet.
#![allow(dead_code)]

use std::os::raw::{c_char, c_int};

pub const BUS_CLB_IO_CLK: u32 = 0;
pub const BUS_BLOCK_RAM: u32 = 1;
pub const BUS_CFG_CLB: u32 = 2;

pub const PPIP_ALWAYS: u32 = 0;
pub const PPIP_DEFAULT: u32 = 1;
pub const PPIP_HINT: u32 = 2;

pub const OUTCOME_BITS: u32 = 0;
pub const OUTCOME_PSEUDO_PIP: u32 = 1;
pub const OUTCOME_ZERO_VALUE: u32 = 2;
pub const OUTCOME_ERROR: u32 = 3;

/// Identifies a struct for [`fpga_view_abi_sizeof`].
pub const ABI_STR: u32 = 0;
pub const ABI_BITS_BLOCK: u32 = 1;
pub const ABI_TILE: u32 = 2;
pub const ABI_SITE: u32 = 3;
pub const ABI_FRAME_BIT: u32 = 4;
pub const ABI_FEATURE: u32 = 5;
pub const ABI_CATALOG_ENTRY: u32 = 6;
pub const ABI_SEGBIT: u32 = 7;
pub const ABI_PPIP: u32 = 8;

/// A borrowed UTF-8 slice owned by the object it came from.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct fpga_view_str {
    pub data: *const c_char,
    pub size: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct fpga_view_bits_block {
    pub bus: u32,
    pub base_address: u64,
    pub frames: u32,
    pub offset: i32,
    pub words: u32,
    pub alias_type: fpga_view_str,
    pub alias_start_offset: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct fpga_view_tile {
    pub name: fpga_view_str,
    pub type_: fpga_view_str,
    pub clock_region: fpga_view_str,
    pub grid_x: u32,
    pub grid_y: u32,
    pub site_first: u32,
    pub site_count: u32,
    pub bits_block_count: u32,
    pub bits_blocks: [fpga_view_bits_block; 3],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct fpga_view_site {
    pub name: fpga_view_str,
    pub type_: fpga_view_str,
    pub tile_index: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct fpga_view_frame_bit {
    pub bus: u32,
    pub frame_address: u32,
    pub word: u32,
    pub index: u32,
    pub word_column: u32,
    pub word_bit: i32,
    pub feature_address: u32,
    pub value: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct fpga_view_feature {
    pub line: u32,
    pub feature: fpga_view_str,
    pub tile: fpga_view_str,
    pub tile_type: fpga_view_str,
    pub tile_feature: fpga_view_str,
    pub tile_index: i32,
    pub start_bit: i32,
    pub width: i32,
    pub value_bits: u64,
    pub outcome: u32,
    pub error: fpga_view_str,
    pub frame_bit_first: u32,
    pub frame_bit_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct fpga_view_catalog_entry {
    pub name: fpga_view_str,
    pub address: u32,
    pub bus: u32,
    pub segbit_first: u32,
    pub segbit_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct fpga_view_segbit {
    pub word_column: u32,
    pub word_bit: u32,
    pub is_set: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct fpga_view_ppip {
    pub name: fpga_view_str,
    pub type_: u32,
}

#[repr(C)]
pub struct fpga_view_db {
    _private: [u8; 0],
}
#[repr(C)]
pub struct fpga_view_snapshot {
    _private: [u8; 0],
}
#[repr(C)]
pub struct fpga_view_eval {
    _private: [u8; 0],
}
#[repr(C)]
pub struct fpga_view_catalog {
    _private: [u8; 0],
}
#[repr(C)]
pub struct fpga_view_parts {
    _private: [u8; 0],
}

unsafe extern "C" {
    pub fn fpga_view_abi_sizeof(which: u32) -> usize;
    pub fn fpga_view_abi_version() -> u32;
    pub fn fpga_view_string_free(error: *mut c_char);

    pub fn fpga_view_parts_open(
        db_root: *const c_char,
        error: *mut *mut c_char,
    ) -> *mut fpga_view_parts;
    pub fn fpga_view_parts_close(parts: *mut fpga_view_parts);
    pub fn fpga_view_parts_count(parts: *const fpga_view_parts) -> u32;
    pub fn fpga_view_parts_get(
        parts: *const fpga_view_parts,
        index: u32,
        name: *mut fpga_view_str,
        device: *mut fpga_view_str,
        fabric: *mut fpga_view_str,
        package: *mut fpga_view_str,
        speedgrade: *mut fpga_view_str,
    ) -> c_int;

    pub fn fpga_view_db_open(
        db_root: *const c_char,
        part: *const c_char,
        error: *mut *mut c_char,
    ) -> *mut fpga_view_db;
    pub fn fpga_view_db_close(db: *mut fpga_view_db);

    pub fn fpga_view_db_snapshot(
        db: *mut fpga_view_db,
        error: *mut *mut c_char,
    ) -> *mut fpga_view_snapshot;
    pub fn fpga_view_snapshot_close(snapshot: *mut fpga_view_snapshot);
    pub fn fpga_view_snapshot_tiles(
        snapshot: *const fpga_view_snapshot,
        count: *mut u32,
    ) -> *const fpga_view_tile;
    pub fn fpga_view_snapshot_sites(
        snapshot: *const fpga_view_snapshot,
        count: *mut u32,
    ) -> *const fpga_view_site;
    pub fn fpga_view_snapshot_extent(
        snapshot: *const fpga_view_snapshot,
        width: *mut u32,
        height: *mut u32,
    );
    pub fn fpga_view_snapshot_find_tile(
        snapshot: *const fpga_view_snapshot,
        name: *const c_char,
    ) -> i32;

    pub fn fpga_view_db_eval(
        db: *mut fpga_view_db,
        snapshot: *const fpga_view_snapshot,
        text: *const c_char,
        error: *mut *mut c_char,
    ) -> *mut fpga_view_eval;
    pub fn fpga_view_eval_close(eval: *mut fpga_view_eval);
    pub fn fpga_view_eval_features(
        eval: *const fpga_view_eval,
        count: *mut u32,
    ) -> *const fpga_view_feature;
    pub fn fpga_view_eval_frame_bits(
        eval: *const fpga_view_eval,
        count: *mut u32,
    ) -> *const fpga_view_frame_bit;
    pub fn fpga_view_eval_diagnostics(eval: *const fpga_view_eval) -> fpga_view_str;
    pub fn fpga_view_eval_parse_result(eval: *const fpga_view_eval) -> u32;

    pub fn fpga_view_db_catalog(
        db: *mut fpga_view_db,
        tile_type: *const c_char,
        error: *mut *mut c_char,
    ) -> *mut fpga_view_catalog;
    pub fn fpga_view_catalog_close(catalog: *mut fpga_view_catalog);
    pub fn fpga_view_catalog_entries(
        catalog: *const fpga_view_catalog,
        count: *mut u32,
    ) -> *const fpga_view_catalog_entry;
    pub fn fpga_view_catalog_segbits(
        catalog: *const fpga_view_catalog,
        count: *mut u32,
    ) -> *const fpga_view_segbit;
    pub fn fpga_view_catalog_ppips(
        catalog: *const fpga_view_catalog,
        count: *mut u32,
    ) -> *const fpga_view_ppip;
}
