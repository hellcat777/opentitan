// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

package csrng_env_pkg;
  // dep packages
  import uvm_pkg::*;
  import top_pkg::*;
  import dv_utils_pkg::*;
  import push_pull_agent_pkg::*;
  import csrng_agent_pkg::*;
  import dv_lib_pkg::*;
  import tl_agent_pkg::*;
  import cip_base_pkg::*;
  import csr_utils_pkg::*;
  import csrng_ral_pkg::*;
  import aes_model_dpi_pkg::*;
  import prim_mubi_pkg::*;

  // macro includes
  `include "uvm_macros.svh"
  `include "dv_macros.svh"

  // parameters
  parameter uint     NUM_HW_APPS      = 2;
  parameter string   LIST_OF_ALERTS[] = {"recov_alert","fatal_alert"};
  parameter uint     NUM_ALERTS       = 2;
  parameter uint     KEY_LEN          = 256;
  parameter uint     BLOCK_LEN        = 128;
  parameter uint     CTR_LEN          = 32;
  parameter uint     RSD_CTR_LEN      = 32;

  parameter bit [TL_DW-1:0] [3:0] ZERO_SEED_GENBITS = {32'h0,
                                                       32'h0,
                                                       32'h0,
                                                       32'h4};

  // types
  typedef enum int {
    CmdReqDone = 0,
    EntropyReq = 1,
    HwInstExc  = 2,
    FifoErr    = 3
  } csrng_intr_e;

  // functions

  // package sources
  `include "csrng_env_cfg.sv"
  `include "csrng_env_cov.sv"
  `include "csrng_virtual_sequencer.sv"
  `include "csrng_scoreboard.sv"
  `include "csrng_env.sv"
  `include "csrng_vseq_list.sv"

endpackage
