/* Recompute EDC/ECC of Mode 2 Form 1 sectors in a raw 2352-byte image.
   Usage: fixsec image.bin lba count [lba count ...] */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static uint8_t ecc_f_lut[256], ecc_b_lut[256];
static uint32_t edc_lut[256];

static void init_luts(void) {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t j = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
    ecc_f_lut[i] = j;
    ecc_b_lut[i ^ j] = i;
    uint32_t edc = i;
    for (j = 0; j < 8; j++) edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001 : 0);
    edc_lut[i] = edc;
  }
}

static uint32_t edc_block(uint32_t edc, const uint8_t *s, size_t n) {
  while (n--) edc = (edc >> 8) ^ edc_lut[(edc ^ *s++) & 0xFF];
  return edc;
}

static void put32(uint8_t *p, uint32_t v) {
  p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void ecc_block(const uint8_t *src, uint32_t major_count, uint32_t minor_count,
                      uint32_t major_mult, uint32_t minor_inc, uint8_t *dest) {
  uint32_t size = major_count * minor_count;
  for (uint32_t major = 0; major < major_count; major++) {
    uint32_t index = (major >> 1) * major_mult + (major & 1);
    uint8_t a = 0, b = 0;
    for (uint32_t minor = 0; minor < minor_count; minor++) {
      uint8_t t = src[index];
      index += minor_inc;
      if (index >= size) index -= size;
      a ^= t; b ^= t;
      a = ecc_f_lut[a];
    }
    a = ecc_b_lut[ecc_f_lut[a] ^ b];
    dest[major] = a;
    dest[major + major_count] = a ^ b;
  }
}

static void ecc_gen(uint8_t *sector, int zeroaddress) {
  uint8_t addr[4];
  if (zeroaddress) { memcpy(addr, sector + 12, 4); memset(sector + 12, 0, 4); }
  ecc_block(sector + 0xC, 86, 24, 2, 86, sector + 0x81C);
  ecc_block(sector + 0xC, 52, 43, 86, 88, sector + 0x8C8);
  if (zeroaddress) memcpy(sector + 12, addr, 4);
}

static void eccedc_gen(uint8_t *sector, int type) {
  switch (type) {
  case 1:
    put32(sector + 0x810, edc_block(0, sector, 0x810));
    memset(sector + 0x814, 0, 8);
    ecc_gen(sector, 0);
    break;
  case 2:
    put32(sector + 0x818, edc_block(0, sector + 0x10, 0x808));
    ecc_gen(sector, 1);
    break;
  case 3:
    put32(sector + 0x92C, edc_block(0, sector + 0x10, 0x91C));
    break;
  }
}

int main(int argc, char **argv) {
  if (argc < 4 || argc % 2) { fprintf(stderr, "usage: fixsec image.bin lba count ...\n"); return 2; }
  FILE *f = fopen(argv[1], "r+b");
  if (!f) { perror("open"); return 1; }
  init_luts();
  uint8_t sector[2352];
  for (int i = 2; i < argc; i += 2) {
    long lba = atol(argv[i]), n = atol(argv[i + 1]);
    for (long s = lba; s < lba + n; s++) {
      fseek(f, s * 2352L, SEEK_SET);
      if (fread(sector, 1, 2352, f) != 2352) { fprintf(stderr, "short read at %ld\n", s); return 1; }
      if (sector[0xF] != 2 || (sector[0x12] & 0x20)) continue; /* only mode 2 form 1 */
      eccedc_gen(sector, 2);
      fseek(f, s * 2352L, SEEK_SET);
      fwrite(sector, 1, 2352, f);
    }
  }
  fclose(f);
  return 0;
}
