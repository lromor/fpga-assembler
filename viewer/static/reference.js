// Reference material for a Xilinx 7-series fabric.
//
// The text is written for this viewer. It is not copied. Project X-Ray is
// the source of the knowledge, and the SOURCES entry gives it credit.
// Every number was checked against the Project X-Ray documentation and
// against the code in this repository. The frame address comes from
// fpga/xilinx/arch-xc7-frame.h. The feature lookup comes from
// fpga/database.cc.
//
// The scope is Xilinx 7-series. That is what Project X-Ray documents and
// what this assembler supports. There is no general FPGA theory here.
//
// The prose follows ASD-STE100 Simplified Technical English.

/** A block is one of:
 *   {p: "..."}                      a paragraph
 *   {list: ["...", ...]}            a bulleted list
 *   {code: "..."}                   a preformatted block
 *   {table: {head: [...], rows: [[...]]}}
 *   {links: [[label, href], ...]}
 *  The renderer inserts all text as text. It never inserts markup.
 */

export const PIPELINE = {
  id: "pipeline",
  title: "How a FASM line becomes bits",
  body: [
    {
      p:
        "A FASM line names one feature of one tile. It also gives the bits " +
        "to set. The tool does not calculate the result. It reads the " +
        "answer from the database.",
    },
    { code: "CLBLM_R_X33Y38.SLICEM_X0.ALUT.INIT[63:32]=32'b…100" },
    {
      list: [
        "Split the line at the first dot. The left part is the tile name, " +
          "CLBLM_R_X33Y38. The rest is the feature, SLICEM_X0.ALUT.INIT.",
        "Read the value. Only a bit that is 1 selects something. This " +
          "value has bit 34 set, so the line selects address 34 of the " +
          "feature. A line that assigns zero selects no bit.",
        "Find the tile in tilegrid.json. The file gives the tile type, " +
          "CLBLM_R. It also gives the bit block of the tile.",
        "Find the feature in the segbits file of the tile type, " +
          "segbits_clblm_r.db. Look under CLBLM_R.SLICEM_X0.ALUT.INIT[34]. " +
          "The entry lists bit positions inside the tile.",
        "Put each position in the bitstream. Add the frame number to the " +
          "base address of the bit block. Count the bit from the offset of " +
          "the bit block, times 32.",
      ],
    },
    {
      p:
        "The segbits entry for this line is 34_06. The bit block starts at " +
        "frame 0x00401080, at word 77. The bit goes to frame 0x004010a2, " +
        "word 77, bit 6. This viewer shows that result. The fpga-as tool " +
        "reads the same answer to make a bitstream.",
    },
    {
      p:
        "A complete build does more. The fpga-as tool also adds the " +
        "configuration that the original Python tool puts around a " +
        "design. It writes all frames of a bit block that changes. This " +
        "viewer shows only the lines that you type.",
    },
  ],
};

export const TERMS = [
  {
    id: "tile",
    term: "Tile",
    body: [
      {
        p:
          "A tile is one cell of the fabric. A 7-series chip is a grid of " +
          "tiles. Each tile holds one kind of hardware. Each tile has a " +
          "type, for example CLBLM_R, INT_L or BRAM_L.",
      },
      {
        p:
          "A FASM line names a tile before the first dot. The database " +
          "describes each tile type one time. All tiles of that type share " +
          "the description. For this reason the bit positions in a segbits " +
          "file are local to the tile.",
      },
    ],
    see: ["tile-type", "coordinates", "bit-block"],
  },
  {
    id: "tile-type",
    term: "Tile type",
    body: [
      {
        p:
          "A tile type is the kind of a tile. All tiles of one type have " +
          "the same features. They also have the same bit layout. The " +
          "database keeps one segbits file and one ppips file for each type.",
      },
      {
        p:
          "The end of the name, _L or _R, gives the side of the " +
          "interconnect that the tile is on. The two are different types, " +
          "and they have different files.",
      },
    ],
    see: ["segbits", "interconnect"],
  },
  {
    id: "site",
    term: "Site",
    body: [
      {
        p:
          "A site is a place in a tile where the tools put logic. A CLB " +
          "tile holds two slices. The CLBLL_L and CLBLL_R tiles hold two " +
          "SLICEL sites. The CLBLM_L and CLBLM_R tiles hold one SLICEL " +
          "site and one SLICEM site.",
      },
      {
        p:
          "A SLICEM site can also work as a small RAM or as a shift " +
          "register. A SLICEL site cannot.",
      },
      {
        p:
          "A site has its own name, for example SLICE_X52Y38. The numbers " +
          "in a site name are not the numbers in the tile name.",
      },
    ],
    see: ["coordinates", "lut"],
  },
  {
    id: "interconnect",
    term: "Interconnect tile",
    aka: ["INT_L", "INT_R", "switch box"],
    body: [
      {
        p:
          "An interconnect tile is the switch box of the fabric. Each " +
          "logic tile, memory tile and IO tile connects to one. Most of " +
          "the configuration of a design is interconnect. It selects which " +
          "wire drives which wire.",
      },
      {
        p:
          "An interconnect tile adds 26 frames to its column. A CLB behind " +
          "it adds 12 more frames. A column of CLBs is therefore 36 frames " +
          "wide.",
      },
    ],
    see: ["pip", "column"],
  },
  {
    id: "pip",
    term: "PIP",
    aka: ["programmable interconnect point"],
    body: [
      {
        p:
          "A PIP is a connection between two wires in a tile. The " +
          "configuration turns the connection on or off. To route a signal " +
          "in the bitstream is to turn PIPs on.",
      },
      {
        p:
          "The database keeps a group of bits for each destination wire. " +
          "In that group, each source wire that can drive the destination " +
          "has its own pattern. For this reason the name gives the " +
          "destination first.",
      },
      { code: "INT_L.BYP_ALT0.FAN_BOUNCE2   routes FAN_BOUNCE2 to BYP_ALT0" },
      {
        p:
          "All bits of the pattern are important. A ! in front of a bit " +
          "means that the bit must be 0. Do not set the bits of two " +
          "sources at the same time. That is not a valid configuration.",
      },
    ],
    see: ["pseudo-pip", "segbits", "interconnect"],
  },
  {
    id: "pseudo-pip",
    term: "Pseudo PIP",
    aka: ["ppip"],
    body: [
      {
        p:
          "Vivado shows a pseudo PIP as a PIP. A pseudo PIP has no " +
          "configuration bits. The database lists these connections in the " +
          "ppips file of the tile type. A tool then knows that the missing " +
          "bits are correct, and not a hole in the database.",
      },
      { p: "Project X-Ray records three kinds. They are not the same:" },
      {
        table: {
          head: ["Kind", "Meaning"],
          rows: [
            ["always", "A permanent connection between two wires."],
            [
              "default",
              "The driver of the net when nothing else drives it. These " +
                "connect to VCC_WIRE.",
            ],
            [
              "hint",
              "Tells the router that two slice outputs carry the same value.",
            ],
          ],
        },
      },
      {
        p:
          "A FASM line that names a pseudo PIP is correct. It changes no " +
          "bit. This viewer reports a pseudo PIP, and not an error.",
      },
    ],
    see: ["pip", "segbits"],
  },
  {
    id: "frame",
    term: "Frame",
    body: [
      {
        p:
          "A frame is the unit that holds configuration data. One frame " +
          "has 101 words of 32 bits. A 32-bit frame address selects the " +
          "frame. The word at index 50 holds an error correction code.",
      },
      {
        p:
          "A frame is a thin vertical slice of a column. It is not one " +
          "tile. Of the 101 words, 100 go to the 50 tiles of the column, " +
          "two words for each tile. One word goes to the horizontal clock " +
          "row.",
      },
      {
        p:
          "A tile therefore uses a few words of many frames. Those words " +
          "are the bit block of the tile.",
      },
    ],
    see: ["word", "frame-address", "column", "bit-block"],
  },
  {
    id: "word",
    term: "Word",
    body: [
      {
        p:
          "A word is 32 bits in big-endian order. Frames count their " +
          "content in words. A bit address is a word index in the frame, " +
          "plus a bit index in the word.",
      },
    ],
    see: ["frame"],
  },
  {
    id: "frame-address",
    term: "Frame address",
    body: [
      {
        p:
          "A frame address has 32 bits. It is not a flat number. The bits " +
          "are fields, and the fields give the place of the frame in the " +
          "chip.",
      },
      {
        table: {
          head: ["Bits", "Field", "Meaning"],
          rows: [
            ["31:26", "reserved", "not used"],
            ["25:23", "bus", "the configuration bus"],
            ["22", "half", "the top half or the bottom half of the chip"],
            ["21:17", "row", "the horizontal clock row in that half"],
            ["16:7", "column", "the column in that row"],
            ["6:0", "minor", "the frame in that column"],
          ],
        },
      },
      {
        p:
          "The base address of a column has the last seven bits set to 0. " +
          "The frames of a tile start at the base address of its bit block.",
      },
    ],
    see: ["bus", "column", "frame"],
  },
  {
    id: "bus",
    term: "Configuration bus",
    body: [
      {
        p:
          "A tile connects to one of three configuration buses. Bits 25:23 " +
          "of the frame address select the bus. A tile can use more than " +
          "one bus. A BRAM keeps its configuration on one bus and its " +
          "content on another bus.",
      },
      {
        table: {
          head: ["Value", "Name", "Holds"],
          rows: [
            [
              "0",
              "CLB_IO_CLK",
              "the configuration of logic, IO, clocks and interconnect",
            ],
            ["1", "BLOCK_RAM", "the content of a block RAM"],
            [
              "2",
              "CFG_CLB",
              "documented, but not yet seen in a 7-series bitstream",
            ],
          ],
        },
      },
      {
        p:
          "The database uses the same split. The segbits_bram_l.db file " +
          "holds the configuration of the BRAM. The " +
          "segbits_bram_l.block_ram.db file holds its content.",
      },
    ],
    see: ["frame-address", "segbits", "bit-block"],
  },
  {
    id: "column",
    term: "Column",
    body: [
      {
        p:
          "A column is a vertical run of 50 tiles. One set of frames " +
          "configures the column. The number of frames depends on the " +
          "content of the column. Interconnect adds 26 frames, and a CLB " +
          "behind it adds 12 more. A logic column has 36 frames.",
      },
    ],
    see: ["frame", "segment", "interconnect"],
  },
  {
    id: "segment",
    term: "Segment",
    body: [
      {
        p:
          "A segment is all configuration bits of one horizontal slice of " +
          "a column. It is a range of frames and a range of words. A " +
          "segment of a logic column is 36 frames by 2 words. The segbits " +
          "files take their name from the segment.",
      },
    ],
    see: ["column", "segbits", "bit-block"],
  },
  {
    id: "bit-block",
    term: "Bit block",
    body: [
      {
        p:
          "A bit block is the part of the bitstream that belongs to one " +
          "tile. The square in the corner of the fabric draws it. The " +
          "square has one column for each frame and one row for each bit.",
      },
      { p: "The tilegrid.json file gives four numbers for each bus:" },
      {
        table: {
          head: ["Name", "Meaning"],
          rows: [
            ["baseaddr", "the address of the first frame of the block"],
            ["frames", "how many frames the block uses"],
            ["offset", "the index of its first word in each frame"],
            ["words", "how many words of each frame the block uses"],
          ],
        },
      },
      {
        p:
          "A segbits position has the form <frame>_<bit>. Read it against " +
          "the bit block. Add the frame number to baseaddr. Count the bit " +
          "from offset times 32.",
      },
    ],
    see: ["frame", "segbits", "bus"],
  },
  {
    id: "segbits",
    term: "segbits file",
    body: [
      {
        p:
          "A segbits file maps a feature to the bits that make it work. " +
          "The database keeps one file for each tile type. Each line gives " +
          "a feature name and its bit positions. The positions are local " +
          "to the tile.",
      },
      {
        code:
          "CLBLM_R.SLICEM_X0.ALUT.INIT[34] 34_06\n" +
          "INT_L.BYP_ALT0.FAN_BOUNCE2 21_07 !22_07 23_07 24_07 25_07",
      },
      {
        p:
          "A position has the form <frame>_<bit>. A ! in front means that " +
          "the bit must be 0. The number in brackets after a feature name " +
          "is the address. Features that hold an array use the address, " +
          "for example the INIT of a LUT.",
      },
      {
        p:
          "A tile type that uses a second bus has a second file, for " +
          "example segbits_bram_l.block_ram.db.",
      },
    ],
    see: ["bit-block", "pip", "bus"],
  },
  {
    id: "lut",
    term: "LUT and INIT",
    body: [
      {
        p:
          "A LUT makes logic from its inputs. It stores the truth table of " +
          "that logic. A 7-series LUT has six inputs, so the table has 64 " +
          "bits. The feature that holds the table is INIT.",
      },
      {
        p:
          "Each bit of INIT is a separate feature address. A FASM line " +
          "that assigns a range of INIT does one lookup for each bit that " +
          "is 1.",
      },
      {
        p:
          "The tools change the order of the LUT inputs in INIT. They do " +
          "not change the routing. For this reason some connections in a " +
          "CLB have no bits.",
      },
    ],
    see: ["site", "segbits", "pseudo-pip"],
  },
  {
    id: "clock-region",
    term: "Clock region",
    aka: ["horizontal clock row", "HROW", "half"],
    body: [
      {
        p:
          "The chip has a top half and a bottom half. Each half has rows. " +
          "A row is 50 CLBs high. A horizontal clock row is at the center " +
          "of the row. The half field and the row field of the frame " +
          "address hold this structure.",
      },
      {
        p:
          "The tilegrid.json file gives the region of a tile, for example " +
          "X1Y0. The region belongs to the place of the tile. It is not " +
          "part of the tile name.",
      },
    ],
    see: ["frame-address", "column"],
  },
  {
    id: "coordinates",
    term: "The three sets of coordinates",
    body: [
      {
        p:
          "A tile has three sets of numbers, and they do not agree. This " +
          "is the most common cause of confusion.",
      },
      {
        table: {
          head: ["Set", "Example", "Counted"],
          rows: [
            [
              "grid",
              "84, 116",
              "the place on the chip. Each tile has its own. This viewer " +
                "draws it.",
            ],
            [
              "tile name",
              "CLBLM_R_X33Y38",
              "inside the tile type. Other types use the same numbers.",
            ],
            [
              "site name",
              "SLICE_X52Y38",
              "inside the site type, across the whole chip.",
            ],
          ],
        },
      },
      {
        p:
          "On the xc7a50t chip, the name X0Y0 belongs to five tiles. Two " +
          "of them are INT_L_X0Y0 and LIOB33_SING_X0Y0. Each one is at a " +
          "different place on the chip.",
      },
      {
        p:
          "The CLBLM_R_X33Y38 tile holds the SLICE_X52Y38 site. The Y " +
          "numbers agree. The X numbers do not agree.",
      },
    ],
    see: ["tile", "site"],
  },
  {
    id: "fasm",
    term: "FASM",
    body: [
      {
        p:
          "FASM is the text format that this tool reads. The name is short " +
          "for FPGA Assembly. Each line names one feature. The name starts " +
          "with the tile. A bit range and a value can follow.",
      },
      {
        code:
          "CLBLM_R_X33Y38.SLICEM_X0.WA7USED\n" +
          "CLBLM_R_X33Y38.SLICEM_X0.ALUT.INIT[63:32]=32'b…100",
      },
      {
        p:
          "A line with no value sets the feature. A line with a value does " +
          "one lookup for each bit that is 1. The count starts at the low " +
          "end of the range.",
      },
    ],
    see: ["pipeline", "segbits"],
  },
];

export const SOURCES = {
  id: "sources",
  title: "Sources",
  body: [
    {
      p:
        "Project X-Ray documented the 7-series bitstream format. It also " +
        "makes the database that this tool reads. The text here is our " +
        "own. We checked every number two times. We checked it against the " +
        "Project X-Ray documentation, and against the code in this " +
        "repository.",
    },
    {
      links: [
        ["Project X-Ray", "https://github.com/f4pga/prjxray"],
        [
          "Architecture: configuration",
          "https://f4pga.readthedocs.io/projects/prjxray/en/latest/architecture/configuration.html",
        ],
        [
          "Architecture: interconnect",
          "https://f4pga.readthedocs.io/projects/prjxray/en/latest/architecture/interconnect.html",
        ],
        [
          "Database: segbits",
          "https://f4pga.readthedocs.io/projects/prjxray/en/latest/dev_database/common/segbits.html",
        ],
        [
          "Database: ppips",
          "https://f4pga.readthedocs.io/projects/prjxray/en/latest/dev_database/common/ppips.html",
        ],
        [
          "Database: tilegrid",
          "https://f4pga.readthedocs.io/projects/prjxray/en/latest/dev_database/part_specific/tilegrid.html",
        ],
        ["FASM specification", "https://fasm.readthedocs.io/"],
      ],
    },
    {
      p:
        "Project X-Ray uses the ISC license. This repository keeps code " +
        "from Project X-Ray in fpga/xilinx/ under that license.",
    },
  ],
};
