/* pnglibconf.h - minimal read-only build for Switch icon decoding */
/* libpng version 1.6.48 */

#ifndef PNGLCONF_H
#define PNGLCONF_H

/* options */
#define PNG_16BIT_SUPPORTED                  /* needed by READ_16BIT */
#define PNG_ALIGNED_MEMORY_SUPPORTED
#define PNG_BENIGN_ERRORS_SUPPORTED          /* tolerate minor malformations */
#define PNG_BENIGN_READ_ERRORS_SUPPORTED
#define PNG_EASY_ACCESS_SUPPORTED            /* png_get_image_width/height etc. */
//#define PNG_ERROR_TEXT_SUPPORTED
#define PNG_FIXED_POINT_SUPPORTED
#define PNG_INFO_IMAGE_SUPPORTED
#define PNG_POINTER_INDEXING_SUPPORTED
#define PNG_READ_16BIT_SUPPORTED             /* handle 16-bit input channels */
#define PNG_READ_COMPOSITE_NODIV_SUPPORTED   /* internal compositing optimisation */
#define PNG_READ_EXPAND_SUPPORTED            /* palette→RGB, tRNS→alpha, gray expand */
#define PNG_READ_FILLER_SUPPORTED            /* png_set_filler: add alpha to RGB */
#define PNG_READ_GRAY_TO_RGB_SUPPORTED       /* png_set_gray_to_rgb */
#define PNG_READ_INT_FUNCTIONS_SUPPORTED
#define PNG_READ_INTERLACING_SUPPORTED       /* auto-enabled by READ; required for spec */
#define PNG_READ_STRIP_16_TO_8_SUPPORTED     /* png_set_strip_16 */
#define PNG_READ_SUPPORTED
#define PNG_READ_TRANSFORMS_SUPPORTED        /* png_read_update_info */
#define PNG_READ_tRNS_SUPPORTED              /* transparency chunk */
#define PNG_SEQUENTIAL_READ_SUPPORTED        /* png_read_row */
#define PNG_SETJMP_SUPPORTED
#define PNG_SET_OPTION_SUPPORTED             /* auto-enabled by READ */
#define PNG_tRNS_SUPPORTED
//#define PNG_WARNINGS_SUPPORTED

#define PNG_NO_CONSOLE_IO
#define PNG_NO_STDIO

/* end of options */

/* settings */
#define PNG_API_RULE 0
#define PNG_DEFAULT_READ_MACROS 1
#define PNG_GAMMA_THRESHOLD_FIXED 5000
#define PNG_IDAT_READ_SIZE PNG_ZBUF_SIZE
#define PNG_INFLATE_BUF_SIZE 1024
#define PNG_LINKAGE_API extern
#define PNG_LINKAGE_CALLBACK extern
#define PNG_LINKAGE_DATA extern
#define PNG_LINKAGE_FUNCTION extern
#define PNG_MAX_GAMMA_8 11
#define PNG_QUANTIZE_BLUE_BITS 5
#define PNG_QUANTIZE_GREEN_BITS 5
#define PNG_QUANTIZE_RED_BITS 5
#define PNG_TEXT_Z_DEFAULT_COMPRESSION (-1)
#define PNG_TEXT_Z_DEFAULT_STRATEGY 0
#define PNG_USER_CHUNK_CACHE_MAX 1000
#define PNG_USER_CHUNK_MALLOC_MAX 8000000
#define PNG_USER_HEIGHT_MAX 1000000
#define PNG_USER_WIDTH_MAX 1000000
#define PNG_ZBUF_SIZE 8192
#define PNG_ZLIB_VERNUM 0
#define PNG_Z_DEFAULT_COMPRESSION (-1)
#define PNG_Z_DEFAULT_NOFILTER_STRATEGY 0
#define PNG_Z_DEFAULT_STRATEGY 1
#define PNG_sCAL_PRECISION 5
#define PNG_sRGB_PROFILE_CHECKS 2
/* end of settings */

#endif /* PNGLCONF_H */
