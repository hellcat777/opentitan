// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

package otbn_env_pkg;
  // dep packages
  import uvm_pkg::*;
  import top_pkg::*;
  import dv_utils_pkg::*;
  import dv_lib_pkg::*;
  import tl_agent_pkg::*;
  import cip_base_pkg::*;
  import otbn_model_pkg::*;
  import otbn_model_agent_pkg::*;
  import otbn_memutil_pkg::*;

  // autogenerated RAL model
  import otbn_reg_pkg::*;
  import otbn_ral_pkg::*;

  import otbn_pkg::flags_t;
  import bus_params_pkg::BUS_AW, bus_params_pkg::BUS_DW, bus_params_pkg::BUS_DBW;
  import top_pkg::TL_AIW;

  // macro includes
  `include "uvm_macros.svh"
  `include "dv_macros.svh"

  // Imports for the functions defined in otbn_test_helpers.cc. There are documentation comments
  // explaining what the functions do there.
  import "DPI-C" function chandle OtbnTestHelperMake(string path);
  import "DPI-C" function void OtbnTestHelperFree(chandle helper);
  import "DPI-C" function int OtbnTestHelperCountFilesInDir(chandle helper);
  import "DPI-C" function string OtbnTestHelperGetFilePath(chandle helper, int index);

  // parameters
  parameter string LIST_OF_ALERTS[] = {"fatal", "recov"};
  parameter uint NUM_ALERTS = otbn_reg_pkg::NumAlerts;

  // typedefs
  typedef virtual pins_if #(1) idle_vif;
  typedef logic [TL_AIW-1:0]   tl_source_t;

  // Expected data for a pending read (see exp_read_values in otbn_scoreboard.sv)
  typedef struct packed {
    bit                upd;
    logic              chk;
    logic [BUS_DW-1:0] val;
  } otbn_exp_read_data_t;

  // Used for coverage in otbn_env_cov.sv (where we need to convert string mnemonics to a packed
  // integral type)
  parameter int unsigned MNEM_STR_LEN = 16;
  typedef bit [MNEM_STR_LEN*8-1:0] mnem_str_t;

  // A very simple wrapper around a word that has been loaded from the input binary and needs
  // storing to OTBN's IMEM or DMEM.
  typedef struct packed {
    // Is this destined for IMEM?
    bit           for_imem;
    // The (word) offset within the destination memory
    bit [21:0]    offset;
    // The data to be loaded
    bit [31:0]    data;

  } otbn_loaded_word;

  typedef enum {
    StackEmpty,
    StackPartial,
    StackFull
  } stack_fullness_e;

  typedef struct packed {
    logic pop_a;
    logic pop_b;
    logic push;
  } call_stack_flags_t;

  typedef enum {
    OperationalStateIdle,
    OperationalStateBusy,
    OperationalStateLocked
  } operational_state_e;

  typedef enum {
    AccessSoftwareRead,
    AccessSoftwareWrite
  } access_e;

  // package sources
  `include "otbn_env_cfg.sv"
  `include "otbn_trace_item.sv"
  `include "otbn_env_cov.sv"
  `include "otbn_trace_monitor.sv"
  `include "otbn_virtual_sequencer.sv"
  `include "otbn_scoreboard.sv"
  `include "otbn_env.sv"

  `include "otbn_vseq_list.sv"

endpackage
