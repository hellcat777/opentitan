// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/silicon_creator/lib/drivers/flash_ctrl.h"

#include <assert.h>

#include "sw/device/lib/base/bitfield.h"
#include "sw/device/lib/base/hardened.h"
#include "sw/device/silicon_creator/lib/base/abs_mmio.h"
#include "sw/device/silicon_creator/lib/base/sec_mmio.h"
#include "sw/device/silicon_creator/lib/error.h"

#include "flash_ctrl_regs.h"  // Generated.
#include "hw/top_earlgrey/sw/autogen/top_earlgrey.h"

// Values of `flash_ctrl_partition_t` constants must be distinct from each
// other, and `kFlashCtrlRegionInfo* >> 1` should give the correct
// CONTROL.INFO_SEL value.
static_assert(kFlashCtrlPartitionData == 0u,
              "Incorrect enum value for kFlashCtrlRegionData");
static_assert(kFlashCtrlPartitionInfo0 >> 1 == 0,
              "Incorrect enum value for kFlashCtrlRegionInfo0");
static_assert(kFlashCtrlPartitionInfo1 >> 1 == 1,
              "Incorrect enum value for kFlashCtrlRegionInfo1");
static_assert(kFlashCtrlPartitionInfo2 >> 1 == 2,
              "Incorrect enum value for kFlashCtrlRegionInfo2");

enum {
  /**
   * Base address of the flash_ctrl registers.
   */
  kBase = TOP_EARLGREY_FLASH_CTRL_CORE_BASE_ADDR,
  /**
   * Base address of the flash memory.
   */
  kMemBase = TOP_EARLGREY_FLASH_CTRL_MEM_BASE_ADDR,
};

/**
 * Flash transaction parameters.
 */
typedef struct transaction_params {
  /**
   * Start address of a flash transaction.
   *
   * Must be the full byte address. For read and write operations flash
   * controller will truncate to the closest 32-bit word aligned address. For
   * page erases, the controller will truncate to the closest lower page aligned
   * address. For bank erases, the controller will truncate to the closest lower
   * bank aligned address.
   */
  uint32_t addr;
  /**
   * Operation type.
   *
   * Must be set to one of FLASH_CTRL_CONTROL_OP_VALUE_*.
   */
  uint32_t op_type;
  /**
   * Whether to erase a bank or a single page.
   *
   * Only applies to erase operations.
   */
  flash_ctrl_erase_type_t erase_type;
  /**
   * Partition to operate on.
   */
  flash_ctrl_partition_t partition;
  /**
   * Number of 32-bit words.
   *
   * Only applies to read and write operations.
   */
  uint32_t word_count;
} transaction_params_t;

/**
 * Starts a flash transaction.
 *
 * @param params Transaction parameters, see `transaction_params_t`.
 * @return The result of the operation.
 */
static void transaction_start(transaction_params_t params) {
  // Set the address.
  abs_mmio_write32(kBase + FLASH_CTRL_ADDR_REG_OFFSET, params.addr);
  // Configure flash_ctrl and start the transaction.
  const bool is_info =
      bitfield_bit32_read(params.partition, FLASH_CTRL_PARTITION_BIT_IS_INFO);
  const uint32_t info_type = bitfield_field32_read(
      params.partition, FLASH_CTRL_PARTITION_FIELD_INFO_TYPE);
  const bool bank_erase = params.erase_type == kFlashCtrlEraseTypeBank;
  uint32_t reg = bitfield_bit32_write(0, FLASH_CTRL_CONTROL_START_BIT, true);
  reg =
      bitfield_field32_write(reg, FLASH_CTRL_CONTROL_OP_FIELD, params.op_type);
  reg =
      bitfield_bit32_write(reg, FLASH_CTRL_CONTROL_PARTITION_SEL_BIT, is_info);
  reg =
      bitfield_field32_write(reg, FLASH_CTRL_CONTROL_INFO_SEL_FIELD, info_type);
  reg = bitfield_bit32_write(reg, FLASH_CTRL_CONTROL_ERASE_SEL_BIT, bank_erase);
  // TODO(#3353): Remove -1 when flash_ctrl is updated.
  reg = bitfield_field32_write(reg, FLASH_CTRL_CONTROL_NUM_FIELD,
                               params.word_count - 1);
  abs_mmio_write32(kBase + FLASH_CTRL_CONTROL_REG_OFFSET, reg);
}

/**
 * Copies `word_count` words from the read FIFO to the given buffer.
 *
 * Large reads may create back pressure.
 *
 * @param word_count Number of words to read from the FIFO.
 * @param[out] data Output buffer.
 */
static void fifo_read(size_t word_count, uint32_t *data) {
  for (size_t i = 0; i < word_count; ++i) {
    data[i] = abs_mmio_read32(kBase + FLASH_CTRL_RD_FIFO_REG_OFFSET);
  }
}

/**
 * Copies `word_count` words from the given buffer to the program FIFO.
 *
 * Large writes may create back pressure.
 *
 * @param word_count Number of words to write to the FIFO.
 * @param data Input buffer.
 */
static void fifo_write(size_t word_count, const uint32_t *data) {
  for (size_t i = 0; i < word_count; ++i) {
    abs_mmio_write32(kBase + FLASH_CTRL_PROG_FIFO_REG_OFFSET, data[i]);
  }
}

/**
 * Blocks until the current flash transaction is complete.
 *
 * @param error Error code to return in case of a flash controller error.
 * @return The result of the operation.
 */
static rom_error_t wait_for_done(rom_error_t error) {
  uint32_t op_status;
  do {
    op_status = abs_mmio_read32(kBase + FLASH_CTRL_OP_STATUS_REG_OFFSET);
  } while (!bitfield_bit32_read(op_status, FLASH_CTRL_OP_STATUS_DONE_BIT));
  abs_mmio_write32(kBase + FLASH_CTRL_OP_STATUS_REG_OFFSET, 0u);

  if (bitfield_bit32_read(op_status, FLASH_CTRL_OP_STATUS_ERR_BIT)) {
    return error;
  }
  return kErrorOk;
}

/**
 * Writes data to the given partition.
 *
 * @param addr Full byte address to write to.
 * @param partition The partition to write to.
 * @param word_count Number of bus words to write.
 * @param data Data to write.
 * @param error Error code to return in case of a flash controller error.
 * @return Result of the operation.
 */
static rom_error_t write(uint32_t addr, flash_ctrl_partition_t partition,
                         uint32_t word_count, const uint32_t *data,
                         rom_error_t error) {
  enum {
    kWindowWordCount =
        FLASH_CTRL_PARAM_REG_BUS_PGM_RES_BYTES / sizeof(uint32_t),
  };

  // Find the number of words that can be written in the first window.
  uint32_t window_word_count =
      kWindowWordCount - ((addr / sizeof(uint32_t)) % kWindowWordCount);
  while (word_count > 0) {
    // Program operations can't cross window boundaries.
    window_word_count =
        word_count < window_word_count ? word_count : window_word_count;

    transaction_start((transaction_params_t){
        .addr = addr,
        .op_type = FLASH_CTRL_CONTROL_OP_VALUE_PROG,
        .partition = partition,
        .word_count = window_word_count,
        // Does not apply to program transactions.
        .erase_type = kFlashCtrlEraseTypePage,
    });

    fifo_write(window_word_count, data);
    RETURN_IF_ERROR(wait_for_done(error));

    addr += window_word_count * sizeof(uint32_t);
    data += window_word_count;
    word_count -= window_word_count;
    window_word_count = kWindowWordCount;
  }

  return kErrorOk;
}

/**
 * Returns the base address of an information page.
 *
 * @param info_page An information page.
 * @return Base address of the given page.
 */
static uint32_t info_page_addr(flash_ctrl_info_page_t info_page) {
  const uint32_t bank_index =
      bitfield_bit32_read(info_page, FLASH_CTRL_INFO_PAGE_BIT_BANK);
  const uint32_t page_index =
      bitfield_field32_read(info_page, FLASH_CTRL_INFO_PAGE_FIELD_INDEX);
  return kMemBase + bank_index * FLASH_CTRL_PARAM_BYTES_PER_BANK +
         page_index * FLASH_CTRL_PARAM_BYTES_PER_PAGE;
}

void flash_ctrl_init(void) {
  // Initialize the flash controller.
  abs_mmio_write32(kBase + FLASH_CTRL_INIT_REG_OFFSET,
                   bitfield_bit32_write(0u, FLASH_CTRL_INIT_VAL_BIT, true));
}

void flash_ctrl_status_get(flash_ctrl_status_t *status) {
  // Read flash controller status.
  uint32_t fc_status = abs_mmio_read32(kBase + FLASH_CTRL_STATUS_REG_OFFSET);

  // Extract flash controller status bits.
  status->rd_full =
      bitfield_bit32_read(fc_status, FLASH_CTRL_STATUS_RD_FULL_BIT);
  status->rd_empty =
      bitfield_bit32_read(fc_status, FLASH_CTRL_STATUS_RD_EMPTY_BIT);
  status->prog_full =
      bitfield_bit32_read(fc_status, FLASH_CTRL_STATUS_PROG_FULL_BIT);
  status->prog_empty =
      bitfield_bit32_read(fc_status, FLASH_CTRL_STATUS_PROG_EMPTY_BIT);
  status->init_wip =
      bitfield_bit32_read(fc_status, FLASH_CTRL_STATUS_INIT_WIP_BIT);
}

rom_error_t flash_ctrl_data_read(uint32_t addr, uint32_t word_count,
                                 uint32_t *data) {
  transaction_start((transaction_params_t){
      .addr = addr,
      .op_type = FLASH_CTRL_CONTROL_OP_VALUE_READ,
      .partition = kFlashCtrlPartitionData,
      .word_count = word_count,
      // Does not apply to read transactions.
      .erase_type = kFlashCtrlEraseTypePage,
  });
  fifo_read(word_count, data);
  return wait_for_done(kErrorFlashCtrlDataRead);
}

rom_error_t flash_ctrl_info_read(flash_ctrl_info_page_t info_page,
                                 uint32_t offset, uint32_t word_count,
                                 uint32_t *data) {
  const uint32_t addr = info_page_addr(info_page) + offset;
  const flash_ctrl_partition_t partition =
      bitfield_field32_read(info_page, FLASH_CTRL_INFO_PAGE_FIELD_PARTITION);
  transaction_start((transaction_params_t){
      .addr = addr,
      .op_type = FLASH_CTRL_CONTROL_OP_VALUE_READ,
      .partition = partition,
      .word_count = word_count,
      // Does not apply to read transactions.
      .erase_type = kFlashCtrlEraseTypePage,
  });
  fifo_read(word_count, data);
  return wait_for_done(kErrorFlashCtrlInfoRead);
}

rom_error_t flash_ctrl_data_write(uint32_t addr, uint32_t word_count,
                                  const uint32_t *data) {
  return write(addr, kFlashCtrlPartitionData, word_count, data,
               kErrorFlashCtrlDataWrite);
}

rom_error_t flash_ctrl_info_write(flash_ctrl_info_page_t info_page,
                                  uint32_t offset, uint32_t word_count,
                                  const uint32_t *data) {
  const uint32_t addr = info_page_addr(info_page) + offset;
  const flash_ctrl_partition_t partition =
      bitfield_field32_read(info_page, FLASH_CTRL_INFO_PAGE_FIELD_PARTITION);
  return write(addr, partition, word_count, data, kErrorFlashCtrlInfoWrite);
}

rom_error_t flash_ctrl_data_erase(uint32_t addr,
                                  flash_ctrl_erase_type_t erase_type) {
  transaction_start((transaction_params_t){
      .addr = addr,
      .op_type = FLASH_CTRL_CONTROL_OP_VALUE_ERASE,
      .erase_type = erase_type,
      .partition = kFlashCtrlPartitionData,
      // Does not apply to erase transactions.
      .word_count = 1,
  });
  return wait_for_done(kErrorFlashCtrlDataErase);
}

rom_error_t flash_ctrl_info_erase(flash_ctrl_info_page_t info_page,
                                  flash_ctrl_erase_type_t erase_type) {
  const uint32_t addr = info_page_addr(info_page);
  const flash_ctrl_partition_t partition =
      bitfield_field32_read(info_page, FLASH_CTRL_INFO_PAGE_FIELD_PARTITION);
  transaction_start((transaction_params_t){
      .addr = addr,
      .op_type = FLASH_CTRL_CONTROL_OP_VALUE_ERASE,
      .erase_type = erase_type,
      .partition = partition,
      // Does not apply to erase transactions.
      .word_count = 1,
  });
  return wait_for_done(kErrorFlashCtrlInfoErase);
}

void flash_ctrl_exec_set(flash_ctrl_exec_t enable) {
  // Enable or disable flash execution.
  abs_mmio_write32(kBase + FLASH_CTRL_EXEC_REG_OFFSET, (uint32_t)enable);
}

/**
 * A struct for storing config and config write-enable register addresses of an
 * info page.
 */
typedef struct info_cfg_regs {
  /**
   * Config write-enable register address.
   */
  uint32_t cfg_wen_addr;
  /**
   * Config register address.
   */
  uint32_t cfg_addr;
} info_cfg_regs_t;

/**
 * Returns config and config write-enable register addresses of an info page.
 *
 * Note: This function only supports info pages of type 0.
 *
 * @param info_page An info page.
 * @return Config and config write-enable register addresses of the info page.
 */
static info_cfg_regs_t info_cfg_regs(flash_ctrl_info_page_t info_page) {
  // For each bank and info page type, there are N config regwen registers
  // followed by N config registers, where N is the number pages available for
  // the info page type. These "blocks" of registers are mapped to a contiguous
  // address space by bank number starting with config regwen registers for page
  // 0-9, type 0, bank 0, followed by config registers for page 0-9, type 0,
  // bank 0, and so on.
  enum {
    kBankOffset = FLASH_CTRL_BANK1_INFO0_PAGE_CFG_SHADOWED_0_REG_OFFSET -
                  FLASH_CTRL_BANK0_INFO0_PAGE_CFG_SHADOWED_0_REG_OFFSET,
    kPageOffset = sizeof(uint32_t),
  };
  const uint32_t bank_index =
      bitfield_bit32_read(info_page, FLASH_CTRL_INFO_PAGE_BIT_BANK);
  const uint32_t page_index =
      bitfield_field32_read(info_page, FLASH_CTRL_INFO_PAGE_FIELD_INDEX);
  const uint32_t pre_addr =
      kBase + bank_index * kBankOffset + page_index * kPageOffset;
  return (info_cfg_regs_t){
      .cfg_wen_addr = pre_addr + FLASH_CTRL_BANK0_INFO0_REGWEN_0_REG_OFFSET,
      .cfg_addr =
          pre_addr + FLASH_CTRL_BANK0_INFO0_PAGE_CFG_SHADOWED_0_REG_OFFSET,
  };
}

void flash_ctrl_info_mp_set(flash_ctrl_info_page_t info_page,
                            flash_ctrl_mp_t perms) {
  const uint32_t addr = info_cfg_regs(info_page).cfg_addr;
  // Read first to preserve ECC, scrambling, and high endurance bits.
  uint32_t reg = sec_mmio_read32(addr);
  reg = bitfield_bit32_write(
      reg, FLASH_CTRL_BANK0_INFO0_PAGE_CFG_SHADOWED_0_EN_0_BIT, true);
  reg = bitfield_bit32_write(
      reg, FLASH_CTRL_BANK0_INFO0_PAGE_CFG_SHADOWED_0_RD_EN_0_BIT,
      perms.read == kHardenedBoolTrue);
  reg = bitfield_bit32_write(
      reg, FLASH_CTRL_BANK0_INFO0_PAGE_CFG_SHADOWED_0_PROG_EN_0_BIT,
      perms.write == kHardenedBoolTrue);
  reg = bitfield_bit32_write(
      reg, FLASH_CTRL_BANK0_INFO0_PAGE_CFG_SHADOWED_0_ERASE_EN_0_BIT,
      perms.erase == kHardenedBoolTrue);
  sec_mmio_write32_shadowed(addr, reg);
}
