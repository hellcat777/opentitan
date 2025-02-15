// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

class sram_ctrl_env_cfg #(parameter int AddrWidth = 10)
    extends cip_base_env_cfg #(.RAL_T(sram_ctrl_regs_reg_block));

  `uvm_object_param_utils_begin(sram_ctrl_env_cfg#(AddrWidth))
  `uvm_object_utils_end

  `uvm_object_new

  string sram_ral_name = "sram_ctrl_prim_reg_block";

  // ext component cfgs
  rand push_pull_agent_cfg#(.DeviceDataWidth(KDI_DATA_SIZE)) m_kdi_cfg;

  // ext interfaces
  virtual clk_rst_if clk_rst_vif_sram_ctrl_prim_reg_block;
  virtual clk_rst_if otp_clk_rst_vif;
  virtual sram_ctrl_lc_if lc_vif;
  virtual sram_ctrl_exec_if exec_vif;
  mem_bkdr_util mem_bkdr_util_h;

  // Represent the lower and upper bounds of the SRAM memory region
  bit [TL_AW-1:0] sram_start_addr, sram_end_addr;

  // otp clk freq
  rand uint otp_freq_mhz;

  constraint otp_freq_mhz_c {
    `DV_COMMON_CLK_CONSTRAINT(otp_freq_mhz)
  }
  virtual function void initialize(bit [31:0] csr_base_addr = '1);
    list_of_alerts = sram_ctrl_env_pkg::LIST_OF_ALERTS;
    tl_intg_alert_name = "fatal_error";

    // Set up second RAL model for SRAM memory and associated collateral
    ral_model_names.push_back(sram_ral_name);

    // both RAL models use same clock frequency
    clk_freqs_mhz[sram_ral_name] = clk_freq_mhz;

    super.initialize(csr_base_addr);

    // SRAM is a single, contiguous, address range
    sram_start_addr = ral_models[sram_ral_name].mapped_addr_ranges[0].start_addr;
    sram_end_addr   = ral_models[sram_ral_name].mapped_addr_ranges[0].end_addr;

    `uvm_info(`gfn, $sformatf("sram_start_addr: 0x%0x", sram_start_addr), UVM_HIGH)
    `uvm_info(`gfn, $sformatf("sram_end_addr: 0x%0x", sram_end_addr), UVM_HIGH)

    // Build KDI cfg object and configure
    m_kdi_cfg = push_pull_agent_cfg#(.DeviceDataWidth(KDI_DATA_SIZE))::type_id::create("m_kdi_cfg");
    m_kdi_cfg.agent_type = push_pull_agent_pkg::PullAgent;
    m_kdi_cfg.if_mode = dv_utils_pkg::Device;

    // CDC synchronization between OTP and SRAM clock domains requires that the scrambling seed data
    // should be held for at least a few cycles before it can be safely latched by the SRAM domain.
    // Easy way to do this is just to force the push_pull_agent to hold the data until the next key
    // request is sent out.
    m_kdi_cfg.hold_d_data_until_next_req = 1'b1;

    // KDI interface will never need zero delay mode.
    // As per SRAM spec, KDI process will generally take around 800 CPU cyclesj
    m_kdi_cfg.zero_delays.rand_mode(0);

    `uvm_info(`gfn, $sformatf("ral_model_names: %0p", ral_model_names), UVM_LOW)
  endfunction

  // Override the default implementation in dv_base_env_cfg.
  //
  // This is required for the SRAM environment for reuse at the chip level as 2 different
  // parameterizations of the design and testbench exist, as a result the custom RAL model for the
  // SRAM memory primitive must also be explicitly parameterized.
  //
  // We cannot instantiate parameterized UVM objects/components using the standard factory
  // mechanisms, so a custom instantiation method is required here.
  //
  // Note that the SRAM only has 2 RAL models, one is the "default" CSR model,
  // and the other is the custom model to represent the memory primitive.
  virtual function dv_base_reg_block create_ral_by_name(string name);
    if (name == RAL_T::type_name) begin
      return super.create_ral_by_name(name);
    end else if (name == sram_ral_name) begin
      return sram_ctrl_prim_reg_block#(AddrWidth)::type_id::create(sram_ral_name);
    end else begin
      `uvm_error(`gfn, $sformatf("%0s is an illegal RAL model name", name))
    end
  endfunction

endclass
