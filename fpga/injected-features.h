#ifndef FPGA_INJECTED_FEATURES_H
#define FPGA_INJECTED_FEATURES_H

#include <cstdint>
#include <string>
#include <vector>

#include "fpga/database.h"

namespace fpga {

// A feature as the assembler carries it around: the line of the FASM input it
// was parsed from, its full tile.site.feature name, and the bit range it sets.
struct FasmFeature {
  int64_t line;
  std::string name;
  int start_bit;
  int width;
  uint64_t bits;
};

// Appends the configuration the reference implementation injects around the
// design's own features to `features`: the opt-in PUDC_B pullup, the STEPDOWN
// bank fill, the HP bank glue, the GFAN tie root and the BUFRCLK channel
// markers, in the order the reference implementation applies them.
//
// Every rule probes the database for the feature it is about to inject: the
// markers only exist in databases that have been annotated with them.
void InjectConfigurationFeatures(PartDatabase &db, bool emit_pudc_b_pullup,
                                 std::vector<FasmFeature> &features);

}  // namespace fpga

#endif  // FPGA_INJECTED_FEATURES_H
