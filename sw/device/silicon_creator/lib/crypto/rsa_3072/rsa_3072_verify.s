/* Copyright lowRISC contributors. */
/* Licensed under the Apache License, Version 2.0, see LICENSE for details. */
/* SPDX-License-Identifier: Apache-2.0 */


.section .text.start

/**
 * OTBN app to run RSA-3072 verification and constant precomputation.
 */
run_rsa_3072:
  la    x2, mode
  lw    x2, 0(x2)

  li    x3, 1
  beq   x2, x3, verify

  li    x3, 2
  beq   x2, x3, compute_rr

  li    x3, 3
  beq   x2, x3, compute_m0_inv

  /* Mode is neither 1 (= verify) nor 2 (= compute_rr) nor 3 (= compute_m0_inv). Fail. */
  unimp

.text

/**
 * Precomputation of Montgomery constant R^2.
 *
 * Expects the modulus (in_mod) to be pre-populated. Result will be stored in
 * in_rr.
 */
compute_rr:
  jal      x1, precomp_rr
  ecall

/**
 * Precomputation of Montgomery constant m0_inv (= -M^-1 mod 2^256).
 *
 * Expects the modulus (in_mod) to be pre-populated. Result will be stored in
 * in_m0_inv.
 */
compute_m0_inv:
  jal      x1, precomp_m0_inv
  ecall

/**
 * RSA-3072 signature verification.
 *
 * Expects the RSA signature (in_buf), constants (in_rr, in_m0inv) and modulus
 * (in_mod) to be pre-populated. Recovered message will be stored in out_buf.
 */
verify:
  /* Run modular exponentiation. */
  jal      x1, modexp_var_3072_f4

  ecall

.data

/* Operation mode (1 = precomp; 2 = verify) */
.globl mode
.balign 4
mode:
  .zero 4

/* Output buffer for the resulting, recovered message. */
.globl out_buf
.balign 32
out_buf:
  .zero 384

/* Input buffer for the modulus. */
.globl in_mod
.balign 32
in_mod:
  .zero 384

/* Input buffer for the signature. */
.globl in_buf
.balign 32
in_buf:
  .zero 384

/* Input buffer for the Montgomery transformation constant R^2. */
.globl in_rr
.balign 32
in_rr:
  .zero 384

/* The Montgomery constant. */
.globl in_m0inv
.balign 32
in_m0inv:
  .zero 32
