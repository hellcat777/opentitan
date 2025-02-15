// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

class edn_genbits_test extends edn_base_test;

  `uvm_component_utils(edn_genbits_test)
  `uvm_component_new

  function void configure_env();
    super.configure_env();

    cfg.boot_req_mode_pct = 100;
    // TODO: auto_req_mode

    `DV_CHECK_RANDOMIZE_FATAL(cfg)
    `uvm_info(`gfn, $sformatf("%s", cfg.convert2string()), UVM_HIGH)
  endfunction
endclass : edn_genbits_test
