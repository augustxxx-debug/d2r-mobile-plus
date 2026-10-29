/* BC3 → ASTC 4x4 的逐块转码（xbg_astc.c）：设备没有 BC、但有 ASTC 时，BC3 贴图上传成 ASTC 4x4（每像素 1 字节），不再软解成 RGBA8（4 字节）。
 * Per-block BC3 -> ASTC 4x4 transcode (xbg_astc.c): on devices without BC but with ASTC, BC3 textures upload as ASTC 4x4 (1 byte per pixel)
 * instead of being decoded to RGBA8 (4 bytes). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 一块 BC3（16 字节）转一块 ASTC 4x4（16 字节）。返回 1 = 块内透明度一样，单平面、8 位端点，颜色与 BC3 软解相差不到 2 级；
 * 2 = 透明度有变化，双平面（第二平面 = 透明度），端点量化到 48 级、两平面各 4 级权重，有损。
 * One BC3 block (16 bytes) to one ASTC 4x4 block (16 bytes). Returns 1 = uniform alpha: single plane, 8-bit endpoints, colour within 2
 * levels of the BC3 decode; 2 = varying alpha: dual plane (plane 2 = alpha), endpoints quantised to 48 levels, 4 weight levels per plane, lossy. */
int xbg_bc3_to_astc(const uint8_t bc3[16], uint8_t astc[16]);
/* 整层：rows×blocks 个 BC3 块（行距 row、层距 image 字节，0 = 紧排）转成紧排的 ASTC 块；返回 0 成功。
 * A whole level: rows x blocks BC3 blocks (row / image byte strides, 0 = tight) to tight ASTC blocks; returns 0 on success. */
int xbg_bc3_to_astc_linear(uint32_t width, uint32_t height, uint32_t depth, const void *source, size_t source_bytes,
                           size_t row, size_t image, void *output, size_t output_bytes);
/* 单测用：解码本编码器产出的这两种 ASTC 块（CEM 12、4x4 网格、2 位权重；单平面 8 位端点或双平面 48 级端点），按 LDR UNORM 出 RGBA8；
 * 其它块返回 -1。srgb 时端点按 sRGB 规则展开（e<<8|0x80），输出仍是 8 位编码值。
 * For tests: decode the two ASTC block kinds this encoder emits (CEM 12, 4x4 grid, 2-bit weights; single plane with 8-bit endpoints or dual
 * plane with 48-level endpoints) to RGBA8 as LDR UNORM; other blocks return -1. With srgb the endpoints expand by the sRGB rule (e<<8|0x80);
 * the output stays 8-bit encoded values. */
int xbg_astc_decode_own(const uint8_t astc[16], int srgb, uint8_t rgba[64]);
/* 48 级端点（1 个三进制位 + 4 位）的反量化，单测与编码共用 | Unquantisation of 48-level endpoints (one trit + 4 bits), shared by tests and encoder */
uint8_t xbg_astc_unquant48(unsigned trit, unsigned bits);
#ifdef __cplusplus
}
#endif
