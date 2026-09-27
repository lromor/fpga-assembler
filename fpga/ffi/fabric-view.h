// Copyright 2025 fpga-assembler authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A C ABI over the assembler's database and FASM facilities, so that
// non-C++ front-ends (the //viewer fabric viewer) resolve a FASM line to
// frame bits through exactly the same code path as //fpga:fpga-as.
//
// Ownership rules, uniformly:
//   * every fpga_view_* pointer returned by an "open"/"eval" call is owned by
//     the caller and released with the matching "_close"/"_free",
//   * every fpga_view_str and every array handed out points into the object
//     it was obtained from and stays valid until that object is released,
//   * an "error" out-parameter is only written on failure and must then be
//     released with fpga_view_string_free.
//
// No function throws: the library is built with -fno-exceptions, and failures
// are reported through return codes and the error out-parameter.

#ifndef FPGA_FFI_FABRIC_VIEW_H
#define FPGA_FFI_FABRIC_VIEW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors fpga::ConfigBusType.  The numeric values are part of the ABI.
typedef enum {
  FPGA_VIEW_BUS_CLB_IO_CLK = 0,
  FPGA_VIEW_BUS_BLOCK_RAM = 1,
  FPGA_VIEW_BUS_CFG_CLB = 2,
} fpga_view_bus;

// Mirrors fpga::PseudoPIPType.
typedef enum {
  FPGA_VIEW_PPIP_ALWAYS = 0,
  FPGA_VIEW_PPIP_DEFAULT = 1,
  FPGA_VIEW_PPIP_HINT = 2,
} fpga_view_ppip_type;

// Why a FASM line produced no frame bits.  Not every such line is a mistake:
// a pseudo pip documents wiring and legitimately configures nothing.
typedef enum {
  FPGA_VIEW_OUTCOME_BITS = 0,        // resolved to at least one frame bit
  FPGA_VIEW_OUTCOME_PSEUDO_PIP = 1,  // a pseudo pip: no bits, by design
  FPGA_VIEW_OUTCOME_ZERO_VALUE = 2,  // every addressed bit was 0
  FPGA_VIEW_OUTCOME_ERROR = 3,       // unknown tile or feature; see `error`
} fpga_view_outcome;

// A borrowed, not necessarily NUL-terminated, UTF-8 slice.
typedef struct {
  const char *data;
  uint32_t size;
} fpga_view_str;

// One of a tile's configuration bit blocks, as tilegrid.json describes it.
// The block occupies `frames` consecutive frame addresses starting at
// `base_address`, and within each frame the `words` words starting at
// `offset` -- that rectangle is the tile's window into the bitstream.
typedef struct {
  fpga_view_bus bus;
  uint64_t base_address;
  uint32_t frames;
  int32_t offset;
  uint32_t words;
  // Set when the tile borrows another tile type's bit definitions.
  // `alias_type` has size 0 when there is no alias.
  fpga_view_str alias_type;
  uint32_t alias_start_offset;
} fpga_view_bits_block;

// A tile of the fabric.  `sites` indexes the snapshot's flat site array.
typedef struct {
  fpga_view_str name;
  fpga_view_str type;
  fpga_view_str clock_region;  // size == 0 when the tile has none
  uint32_t grid_x;
  uint32_t grid_y;
  uint32_t site_first;
  uint32_t site_count;
  uint32_t bits_block_count;
  fpga_view_bits_block bits_blocks[3];
} fpga_view_tile;

// A site instantiated in a tile, e.g. SLICE_X52Y38 of type SLICEM.
typedef struct {
  fpga_view_str name;
  fpga_view_str type;
  uint32_t tile_index;
} fpga_view_site;

// A single configuration bit a feature drives.
typedef struct {
  fpga_view_bus bus;
  uint32_t frame_address;  // absolute frame address in the bitstream
  uint32_t word;           // word within the 101-word frame
  uint32_t index;          // bit within the 32-bit word
  // Position within the tile's own bit window, as tilegrid.json declares it:
  // the frame column, and the bit row counted from the block's `offset`.
  // A tile that borrows an aliased tile type's definitions writes into a
  // window shifted by the alias, so its `word_bit` can fall outside
  // [0, words * 32); `word`/`index` above stay absolute and exact.
  uint32_t word_column;
  int32_t word_bit;
  uint32_t feature_address;  // which [address] of the feature drove this
  uint8_t value;             // 1 to set, 0 for a '!'-prefixed clear
} fpga_view_frame_bit;

// The resolution of one FASM feature assignment.
typedef struct {
  uint32_t line;               // 1-based line in the evaluated text
  fpga_view_str feature;       // full "TILE.SITE.FEATURE" as written
  fpga_view_str tile;          // "CLBLM_R_X33Y38"
  fpga_view_str tile_type;     // "CLBLM_R"
  fpga_view_str tile_feature;  // "SLICEM_X0.ALUT.INIT"
  int32_t tile_index;          // index into the snapshot grid, -1 if unknown
  int32_t start_bit;
  int32_t width;
  uint64_t value_bits;  // the right-hand side, up to 64 bits
  fpga_view_outcome outcome;
  fpga_view_str error;       // size == 0 unless outcome is ERROR
  uint32_t frame_bit_first;  // indexes fpga_view_eval_frame_bits()
  uint32_t frame_bit_count;
} fpga_view_feature;

// A feature the database documents for a tile type, with its bit pattern.
typedef struct {
  fpga_view_str name;  // "CLBLM_R.SLICEM_X0.ALUT.INIT"
  uint32_t address;    // the [address] suffix, 0 when absent
  fpga_view_bus bus;
  uint32_t segbit_first;  // indexes fpga_view_catalog_segbits()
  uint32_t segbit_count;
} fpga_view_catalog_entry;

// One bit of a catalog entry, in tile-local coordinates.
typedef struct {
  uint32_t word_column;
  uint32_t word_bit;
  uint8_t is_set;
} fpga_view_segbit;

// A pseudo pip documented for a tile type.
typedef struct {
  fpga_view_str name;
  fpga_view_ppip_type type;
} fpga_view_ppip;

// Opaque handles.
typedef struct fpga_view_db fpga_view_db;
typedef struct fpga_view_snapshot fpga_view_snapshot;
typedef struct fpga_view_eval fpga_view_eval;
typedef struct fpga_view_catalog fpga_view_catalog;
typedef struct fpga_view_parts fpga_view_parts;

// Identifies a struct of this ABI for fpga_view_abi_sizeof.
typedef enum {
  FPGA_VIEW_ABI_STR = 0,
  FPGA_VIEW_ABI_BITS_BLOCK = 1,
  FPGA_VIEW_ABI_TILE = 2,
  FPGA_VIEW_ABI_SITE = 3,
  FPGA_VIEW_ABI_FRAME_BIT = 4,
  FPGA_VIEW_ABI_FEATURE = 5,
  FPGA_VIEW_ABI_CATALOG_ENTRY = 6,
  FPGA_VIEW_ABI_SEGBIT = 7,
  FPGA_VIEW_ABI_PPIP = 8,
} fpga_view_abi_struct;

// The size of one of this ABI's structs, or 0 for an unknown `which`.
// A binding written in another language asserts its own layout against this
// rather than hard-coding offsets that silently drift.
size_t fpga_view_abi_sizeof(fpga_view_abi_struct which);

// Bumped whenever the meaning of an existing field changes.
uint32_t fpga_view_abi_version(void);

// Releases a string returned through an `error` out-parameter.
void fpga_view_string_free(char *error);

// Lists the parts `db_root` (a family root such as ".../prjxray-db/artix7")
// declares in its mapping/ files.  Returns NULL on failure.
fpga_view_parts *fpga_view_parts_open(const char *db_root, char **error);
void fpga_view_parts_close(fpga_view_parts *parts);
uint32_t fpga_view_parts_count(const fpga_view_parts *parts);
// Fills the part's name/device/fabric/package/speedgrade.  Returns 0 when
// `index` is out of range.
int fpga_view_parts_get(const fpga_view_parts *parts, uint32_t index,
                        fpga_view_str *name, fpga_view_str *device,
                        fpga_view_str *fabric, fpga_view_str *package,
                        fpga_view_str *speedgrade);

// Opens the database for `part` under the family root `db_root`.  This is
// fpga::PartDatabase::Parse; expect it to take a moment and a few hundred MB.
fpga_view_db *fpga_view_db_open(const char *db_root, const char *part,
                                char **error);
void fpga_view_db_close(fpga_view_db *db);

// A flattened, immutable view of the part's tile grid, ordered by tile name.
// Cheap to keep around and safe to read from many threads.
fpga_view_snapshot *fpga_view_db_snapshot(fpga_view_db *db, char **error);
void fpga_view_snapshot_close(fpga_view_snapshot *snapshot);
const fpga_view_tile *fpga_view_snapshot_tiles(
  const fpga_view_snapshot *snapshot, uint32_t *count);
const fpga_view_site *fpga_view_snapshot_sites(
  const fpga_view_snapshot *snapshot, uint32_t *count);
// Grid extent, i.e. one past the largest grid_x/grid_y in the snapshot.
void fpga_view_snapshot_extent(const fpga_view_snapshot *snapshot,
                               uint32_t *width, uint32_t *height);
// Index of `name` in the tile array, or -1 when absent.
int32_t fpga_view_snapshot_find_tile(const fpga_view_snapshot *snapshot,
                                     const char *name);

// Evaluates FASM text through fasm::Parse and fpga::PartDatabase::ConfigBits.
// `text` need not end in a newline.  Returns NULL only when the input could
// not be parsed at all; per-feature failures are reported per feature.
fpga_view_eval *fpga_view_db_eval(fpga_view_db *db,
                                  const fpga_view_snapshot *snapshot,
                                  const char *text, char **error);
void fpga_view_eval_close(fpga_view_eval *eval);
const fpga_view_feature *fpga_view_eval_features(const fpga_view_eval *eval,
                                                 uint32_t *count);
const fpga_view_frame_bit *fpga_view_eval_frame_bits(const fpga_view_eval *eval,
                                                     uint32_t *count);
// Diagnostics fasm::Parse wrote while reading the text, and the severity it
// returned (the fasm::ParseResult value).
fpga_view_str fpga_view_eval_diagnostics(const fpga_view_eval *eval);
uint32_t fpga_view_eval_parse_result(const fpga_view_eval *eval);

// Every feature and pseudo pip the database documents for one tile type.
fpga_view_catalog *fpga_view_db_catalog(fpga_view_db *db, const char *tile_type,
                                        char **error);
void fpga_view_catalog_close(fpga_view_catalog *catalog);
const fpga_view_catalog_entry *fpga_view_catalog_entries(
  const fpga_view_catalog *catalog, uint32_t *count);
const fpga_view_segbit *fpga_view_catalog_segbits(
  const fpga_view_catalog *catalog, uint32_t *count);
const fpga_view_ppip *fpga_view_catalog_ppips(const fpga_view_catalog *catalog,
                                              uint32_t *count);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // FPGA_FFI_FABRIC_VIEW_H
