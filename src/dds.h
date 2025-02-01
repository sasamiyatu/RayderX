#pragma once

#include <windows.h>

#define DDS_MAGIC 0x20534444 // "DDS "

#define DDPF_ALPHAPIXELS 0x1 // Texture contains alpha data; dwRGBAlphaBitMask contains valid data.	
#define DDPF_ALPHA 0x2 //Used in some older DDS files for alpha channel only uncompressed data(dwRGBBitCount contains the alpha channel bitcount; dwABitMask contains valid data)
#define DDPF_FOURCC	0x4 // Texture contains compressed RGB data; dwFourCC contains valid data.	
#define DDPF_RGB 0x40 // Texture contains uncompressed RGB data; dwRGBBitCount and the RGB masks(dwRBitMask, dwGBitMask, dwBBitMask) contain valid data.#	
#define DDPF_YUV 0x200 // Used in some older DDS files for YUV uncompressed data(dwRGBBitCount contains the YUV bit count; dwRBitMask contains the Y mask, dwGBitMask contains the U mask, dwBBitMask contains the V mask)
#define DDPF_LUMINANCE 0x20000 // Used in some older DDS files for single channel color uncompressed data(dwRGBBitCount contains the luminance channel bit count; dwRBitMask contains the channel mask).Can be combined with DDPF_ALPHAPIXELS for a two channel DDS file.

#define DDSD_CAPS 0x1 // Required in every.dds file.
#define DDSD_HEIGHT	0x2 // Required in every.dds file.
#define DDSD_WIDTH 0x4 // Required in every.dds file.
#define DDSD_PITCH 0x8 // Required when pitch is provided for an uncompressed texture.
#define DDSD_PIXELFORMAT 0x1000 // Required in every.dds file.
#define DDSD_MIPMAPCOUNT 0x20000 // Required in a mipmapped texture.
#define DDSD_LINEARSIZE	0x80000 // Required when pitch is provided for a compressed texture.
#define DDSD_DEPTH 0x800000 // Required in a depth texture.


struct DDS_PIXELFORMAT {
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwFourCC;
  DWORD dwRGBBitCount;
  DWORD dwRBitMask;
  DWORD dwGBitMask;
  DWORD dwBBitMask;
  DWORD dwABitMask;
};

typedef struct {
  DWORD           dwSize;
  DWORD           dwFlags;
  DWORD           dwHeight;
  DWORD           dwWidth;
  DWORD           dwPitchOrLinearSize;
  DWORD           dwDepth;
  DWORD           dwMipMapCount;
  DWORD           dwReserved1[11];
  DDS_PIXELFORMAT ddspf;
  DWORD           dwCaps;
  DWORD           dwCaps2;
  DWORD           dwCaps3;
  DWORD           dwCaps4;
  DWORD           dwReserved2;
} DDS_HEADER;