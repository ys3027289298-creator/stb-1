// Anti-aliased downsampling tests and benchmarks for the stbir_downsample_* API.
//
// Self-contained: synthetic 42x42 RGB source, no external files.
// Build:  cc -I.. -O2 downsample_test.c -lm -o downsample_test
//      or c++ -I.. -O2 downsample_test.c -lm -o downsample_test
//
// Exit code is non-zero if any check fails.

#if defined(__linux__) || defined(__APPLE__)
#define _POSIX_C_SOURCE 199309L
#endif

//------------------------------------------------------------
// instrumented allocator: tracks live and peak bytes
// (macros must be defined before the implementation include)
//------------------------------------------------------------
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static size_t g_live_bytes = 0;
static size_t g_peak_bytes = 0;
static size_t g_alloc_count = 0;

static void * tracked_malloc( size_t size, void * user_data )
{
  (void) user_data;
  // allocate with an 8-byte header storing the block size
  unsigned char * block = (unsigned char *) malloc( size + sizeof(size_t) );
  if ( !block ) return NULL;
  memcpy( block, &size, sizeof(size_t) );
  g_live_bytes += size;
  if ( g_live_bytes > g_peak_bytes ) g_peak_bytes = g_live_bytes;
  ++g_alloc_count;
  return block + sizeof(size_t);
}

static void tracked_free( void * ptr, void * user_data )
{
  (void) user_data;
  if ( !ptr ) return;
  unsigned char * block = (unsigned char *) ptr - sizeof(size_t);
  size_t size;
  memcpy( &size, block, sizeof(size_t) );
  g_live_bytes -= size;
  free( block );
}

static void mem_reset( void ) { g_live_bytes = 0; g_peak_bytes = 0; g_alloc_count = 0; }

#define STBIR_MALLOC tracked_malloc
#define STBIR_FREE   tracked_free

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

//------------------------------------------------------------
// small deterministic 42x42 RGB source: gradients, checkerboard
// (hard high-frequency content) and solid bars
//------------------------------------------------------------
#define SRC_W 42
#define SRC_H 42
#define CH    3

static void make_source( unsigned char img[ SRC_H * SRC_W * CH ] )
{
  for ( int y = 0; y < SRC_H; ++y ) {
    for ( int x = 0; x < SRC_W; ++x ) {
      unsigned char * p = img + ( y * SRC_W + x ) * CH;
      int checker = ( ( x / 3 ) + ( y / 3 ) ) & 1;       // 3px checkerboard
      p[0] = (unsigned char) ( ( x * 255 ) / ( SRC_W - 1 ) );
      p[1] = (unsigned char) ( ( y * 255 ) / ( SRC_H - 1 ) );
      p[2] = (unsigned char) ( checker ? 230 : 20 );
    }
  }
}

static float srgb_to_lin( float c )
{
  return c <= 0.04045f ? c / 12.92f : (float) pow( ( c + 0.055f ) / 1.055f, 2.4 );
}
static float lin_to_srgb( float c )
{
  return c <= 0.0031308f ? c * 12.92f : 1.055f * (float) pow( c, 1.0 / 2.4 ) - 0.055f;
}

//------------------------------------------------------------
// reference: exact 2D area-weighted box average in linear light,
// computed directly (not separably) in double precision
//------------------------------------------------------------
static void reference_box_linear( const unsigned char * src, int sw, int sh,
                                  unsigned char * dst, int dw, int dh )
{
  double sx = (double) sw / dw;
  double sy = (double) sh / dh;
  for ( int oy = 0; oy < dh; ++oy ) {
    double fy0 = ( oy + 0.5 ) * sy - sy * 0.5;
    double fy1 = fy0 + sy;
    for ( int ox = 0; ox < dw; ++ox ) {
      double fx0 = ( ox + 0.5 ) * sx - sx * 0.5;
      double fx1 = fx0 + sx;
      for ( int c = 0; c < CH; ++c ) {
        double acc = 0, wsum = 0;
        int x0 = (int) floor( fx0 ), x1 = (int) ceil( fx1 );
        int y0 = (int) floor( fy0 ), y1 = (int) ceil( fy1 );
        if ( x0 < 0 ) x0 = 0;
        if ( x1 > sw ) x1 = sw;
        if ( y0 < 0 ) y0 = 0;
        if ( y1 > sh ) y1 = sh;
        for ( int iy = y0; iy < y1; ++iy ) {
          double wy = ( fy1 < iy + 1 ? fy1 : iy + 1 ) - ( fy0 > iy ? fy0 : iy );
          for ( int ix = x0; ix < x1; ++ix ) {
            double wx = ( fx1 < ix + 1 ? fx1 : ix + 1 ) - ( fx0 > ix ? fx0 : ix );
            double w = wx * wy;
            float v = src[ ( iy * sw + ix ) * CH + c ] / 255.0f;
            acc += w * srgb_to_lin( v );
            wsum += w;
          }
        }
        float g = lin_to_srgb( (float) ( acc / wsum ) );
        if ( g < 0 ) g = 0;
        if ( g > 1 ) g = 1;
        dst[ ( oy * dw + ox ) * CH + c ] = (unsigned char) ( g * 255.0f + 0.5f );
      }
    }
  }
}

//------------------------------------------------------------
// baselines implemented in the test: nearest neighbor and
// bilinear (point-sampled), both gamma-space, matching what a
// naive thumbnailer produces
//------------------------------------------------------------
static void nearest( const unsigned char * src, int sw, int sh,
                     unsigned char * dst, int dw, int dh )
{
  for ( int oy = 0; oy < dh; ++oy ) {
    int iy = ( oy * sh ) / dh;
    if ( iy >= sh ) iy = sh - 1;
    for ( int ox = 0; ox < dw; ++ox ) {
      int ix = ( ox * sw ) / dw;
      if ( ix >= sw ) ix = sw - 1;
      for ( int c = 0; c < CH; ++c )
        dst[ ( oy * dw + ox ) * CH + c ] = src[ ( iy * sw + ix ) * CH + c ];
    }
  }
}

static void bilinear( const unsigned char * src, int sw, int sh,
                      unsigned char * dst, int dw, int dh )
{
  for ( int oy = 0; oy < dh; ++oy ) {
    double fy = ( ( oy + 0.5 ) * sh ) / dh - 0.5;
    int y0 = (int) floor( fy ); double ty = fy - y0;
    int ya = y0, yb = y0 + 1;
    if ( ya < 0 ) ya = 0;
    if ( yb >= sh ) yb = sh - 1;
    for ( int ox = 0; ox < dw; ++ox ) {
      double fx = ( ( ox + 0.5 ) * sw ) / dw - 0.5;
      int x0 = (int) floor( fx ); double tx = fx - x0;
      int xa = x0, xb = x0 + 1;
      if ( xa < 0 ) xa = 0;
      if ( xb >= sw ) xb = sw - 1;
      for ( int c = 0; c < CH; ++c ) {
        double v00 = src[ ( ya * sw + xa ) * CH + c ];
        double v01 = src[ ( ya * sw + xb ) * CH + c ];
        double v10 = src[ ( yb * sw + xa ) * CH + c ];
        double v11 = src[ ( yb * sw + xb ) * CH + c ];
        double top = v00 * ( 1 - tx ) + v01 * tx;
        double bot = v10 * ( 1 - tx ) + v11 * tx;
        double v = top * ( 1 - ty ) + bot * ty;
        if ( v < 0 ) v = 0;
        if ( v > 255 ) v = 255;
        dst[ ( oy * dw + ox ) * CH + c ] = (unsigned char) ( v + 0.5 );
      }
    }
  }
}

//------------------------------------------------------------
// metrics: PSNR against the reference and signed mean
// luminance bias (positive = washed out / too bright)
//------------------------------------------------------------
static double psnr_db( const unsigned char * a, const unsigned char * b, int n )
{
  double se = 0;
  for ( int i = 0; i < n; ++i ) {
    double d = ( a[i] - b[i] ) / 255.0;
    se += d * d;
  }
  if ( se == 0 ) return 99.0;
  return 10.0 * log10( (double) n / se );
}

static double luminance_bias( const unsigned char * img, const unsigned char * ref, int n )
{
  // signed mean difference in linear-light luminance, reported in 8-bit levels
  double sum = 0;
  for ( int i = 0; i < n; ++i ) {
    double li = srgb_to_lin( img[i] / 255.0f );
    double lr = srgb_to_lin( ref[i] / 255.0f );
    sum += ( li - lr ) * 255.0;
  }
  return sum / n;
}

//------------------------------------------------------------
// test harness
//------------------------------------------------------------
static int g_failures = 0;
#define CHECK( cond ) do { if ( !( cond ) ) { printf( "  FAIL: %s (line %d)\n", #cond, __LINE__ ); ++g_failures; } } while ( 0 )

typedef struct {
  const char * name;
  int          kernel;
  int          color_mode;
} method_t;

int main( void )
{
  static unsigned char src[ SRC_H * SRC_W * CH ];
  make_source( src );

  //----------------------------------------------------------
  // 1. parameter validation: illegal arguments must be rejected
  //----------------------------------------------------------
  printf( "== parameter validation ==\n" );

  stbir_downsample_config cfg;
  CHECK( stbir_downsample_config_init( &cfg, STBIR_DS_BOX, STBIR_DS_LINEAR ) == 1 );
  CHECK( stbir_downsample_config_init( NULL, STBIR_DS_BOX, STBIR_DS_LINEAR ) == 0 );
  CHECK( stbir_downsample_config_init( &cfg, 99, STBIR_DS_LINEAR ) == 0 );
  CHECK( stbir_downsample_config_init( &cfg, -1, STBIR_DS_LINEAR ) == 0 );
  CHECK( stbir_downsample_config_init( &cfg, STBIR_DS_BOX, 99 ) == 0 );
  CHECK( stbir_downsample_config_init( &cfg, STBIR_DS_BOX, -1 ) == 0 );

  unsigned char small_dst[ 64 ];
  stbir_downsample_config good = { STBIR_DS_BOX, STBIR_DS_GAMMA };

  CHECK( stbir_downsample_uint8( NULL, SRC_W, SRC_H, 0, small_dst, 21, 21, 0, CH, &good ) == NULL ); // null input
  CHECK( stbir_downsample_uint8( src, 0, SRC_H, 0, small_dst, 21, 21, 0, CH, &good ) == NULL );      // bad dims
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, small_dst, 21, 21, 0, 0, &good ) == NULL );   // 0 channels
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, small_dst, 21, 21, 0, 5, &good ) == NULL );   // too many channels
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, small_dst, SRC_W, 21, 0, CH, &good ) == NULL ); // not downsampling
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, small_dst, 21, SRC_H, 0, CH, &good ) == NULL );
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, small_dst, SRC_W + 1, 21, 0, CH, &good ) == NULL ); // upsample
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 5, small_dst, 21, 21, 0, CH, &good ) == NULL );  // stride too small
  stbir_downsample_config bad1 = { 99, STBIR_DS_LINEAR };
  stbir_downsample_config bad2 = { STBIR_DS_BOX, 99 };
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, small_dst, 21, 21, 0, CH, &bad1 ) == NULL ); // bad kernel
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, small_dst, 21, 21, 0, CH, &bad2 ) == NULL ); // bad color mode
  CHECK( stbir_downsample_scratch_size_bytes( SRC_W, SRC_H, 21, 21, 0 ) == 0 );
  CHECK( stbir_downsample_scratch_size_bytes( SRC_W, SRC_H, 21, 21, CH ) > 0 );

  // float entry validation
  CHECK( stbir_downsample_float( NULL, SRC_W, SRC_H, 0, NULL, 21, 21, 0, CH, &good ) == NULL );
  CHECK( stbir_downsample_float( (const float*) src, SRC_W, SRC_H, 0, NULL, 21, 21, 0, 9, &good ) == NULL );

  //----------------------------------------------------------
  // 2. algorithm correctness
  //----------------------------------------------------------
  printf( "== correctness ==\n" );

  // box + linear must match the independent double-precision 2D reference
  const int tw = 20, th = 20;
  unsigned char * ref = (unsigned char*) malloc( (size_t) tw * th * CH );
  unsigned char * out = (unsigned char*) malloc( (size_t) tw * th * CH );
  reference_box_linear( src, SRC_W, SRC_H, ref, tw, th );

  stbir_downsample_config box_lin = { STBIR_DS_BOX, STBIR_DS_LINEAR };
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, out, tw, th, 0, CH, &box_lin ) != NULL );
  int mismatches = 0, maxdiff = 0;
  for ( int i = 0; i < tw * th * CH; ++i ) {
    int d = abs( (int) out[i] - (int) ref[i] );
    if ( d > 1 ) ++mismatches;
    if ( d > maxdiff ) maxdiff = d;
  }
  printf( "  box+linear vs exact 2D reference: max diff %d, pixels beyond +-1: %d\n", maxdiff, mismatches );
  CHECK( mismatches == 0 );

  // weights must sum to 1: uniform image downsample must return the same value
  unsigned char uniform[ SRC_H * SRC_W * CH ], udst[ 4 * 4 * CH ];
  memset( uniform, 177, sizeof(uniform) );
  stbir_downsample_config tri_g = { STBIR_DS_TRIANGLE, STBIR_DS_GAMMA };
  CHECK( stbir_downsample_uint8( uniform, SRC_W, SRC_H, 0, udst, 4, 4, 0, CH, &tri_g ) != NULL );
  int uniform_ok = 1;
  for ( int i = 0; i < 4 * 4 * CH; ++i ) if ( abs( (int) udst[i] - 177 ) > 1 ) uniform_ok = 0;
  CHECK( uniform_ok );

  // gamma-space averaging darkens mean linear-light luminance versus the
  // energy-preserving linear average (Jensen's inequality: E[f(x)] < f(E[x]))
  stbir_downsample_config box_g = { STBIR_DS_BOX, STBIR_DS_GAMMA };
  unsigned char lin_out[ 21 * 21 * CH ], gam_out[ 21 * 21 * CH ];
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, lin_out, 21, 21, 0, CH, &box_lin ) != NULL );
  CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, gam_out, 21, 21, 0, CH, &box_g ) != NULL );
  double lin_mean = 0, gam_mean = 0;
  for ( int i = 0; i < 21 * 21 * CH; ++i ) {
    lin_mean += srgb_to_lin( lin_out[i] / 255.0f );
    gam_mean += srgb_to_lin( gam_out[i] / 255.0f );
  }
  printf( "  mean linear-light: gamma-avg %.4f vs linear-avg %.4f (gamma avg must be darker)\n",
          gam_mean / ( 21 * 21 * CH ), lin_mean / ( 21 * 21 * CH ) );
  CHECK( gam_mean < lin_mean );

  // NULL config uses documented defaults and succeeds; result must be freed
  unsigned char * defaulted = stbir_downsample_uint8( src, SRC_W, SRC_H, 0, NULL, 21, 21, 0, CH, NULL );
  CHECK( defaulted != NULL );
  STBIR_FREE( defaulted, 0 );
  CHECK( g_live_bytes == 0 );   // every allocation was released

  // float path round-trip: downsampled floats stay in [0,1], same shape as u8 path
  float fsrc[ SRC_H * SRC_W * CH ], * fout;
  for ( int i = 0; i < SRC_H * SRC_W * CH; ++i ) fsrc[i] = src[i] / 255.0f;
  mem_reset();
  fout = stbir_downsample_float( fsrc, SRC_W, SRC_H, 0, NULL, 7, 7, 0, CH, &box_lin );
  CHECK( fout != NULL );
  int range_ok = 1;
  for ( int i = 0; i < 7 * 7 * CH; ++i ) if ( fout[i] < 0 || fout[i] > 1 ) range_ok = 0;
  CHECK( range_ok );
  STBIR_FREE( fout, 0 );
  CHECK( g_live_bytes == 0 );

  // explicit strides honored: padded source and destination must match the packed result
  {
    const int dw = 13, dh = 11, pad = 7;
    unsigned char packed[ 13 * 11 * CH ];
    unsigned char src_padded[ ( SRC_W * CH + pad ) * SRC_H ];
    unsigned char padded[ ( dw * CH + pad ) * dh ];
    for ( int y = 0; y < SRC_H; ++y )
      memcpy( src_padded + y * ( SRC_W * CH + pad ), src + y * SRC_W * CH, SRC_W * CH );
    CHECK( stbir_downsample_uint8( src_padded, SRC_W, SRC_H, SRC_W * CH + pad, padded, dw, dh, dw * CH + pad, CH, &tri_g ) != NULL );
    CHECK( stbir_downsample_uint8( src, SRC_W, SRC_H, 0, packed, dw, dh, 0, CH, &tri_g ) != NULL );
    int stride_ok = 1;
    for ( int y = 0; y < dh; ++y )
      for ( int x = 0; x < dw * CH; ++x )
        if ( padded[ y * ( dw * CH + pad ) + x ] != packed[ y * dw * CH + x ] ) stride_ok = 0;
    CHECK( stride_ok );
  }

  // all supported channel counts (1..4), both data types, both color modes
  for ( int ch = 1; ch <= 4; ++ch ) {
    size_t in_n = (size_t) SRC_W * SRC_H * ch;
    size_t out_n = (size_t) 8 * 8 * ch;
    unsigned char * msrc = (unsigned char*) malloc( in_n );
    unsigned char * mdst = (unsigned char*) malloc( out_n );
    for ( size_t i = 0; i < in_n; ++i ) msrc[i] = (unsigned char) ( ( i * 53 + ch * 7 ) & 255 );
    stbir_downsample_config mc = { ch & 1 ? STBIR_DS_BOX : STBIR_DS_TRIANGLE,
                                   ch & 2 ? STBIR_DS_LINEAR : STBIR_DS_GAMMA };
    CHECK( stbir_downsample_uint8( msrc, SRC_W, SRC_H, 0, mdst, 8, 8, 0, ch, &mc ) != NULL );
    free( msrc ); free( mdst );

    float * fmsrc = (float*) malloc( in_n * sizeof(float) );
    for ( size_t i = 0; i < in_n; ++i ) fmsrc[i] = (float) ( ( i % 251 ) ) / 251.0f;
    float * fmdst = stbir_downsample_float( fmsrc, SRC_W, SRC_H, 0, NULL, 8, 8, 0, ch, &mc );
    CHECK( fmdst != NULL );
    int ok = 1;
    for ( size_t i = 0; i < out_n; ++i ) if ( fmdst[i] < 0 || fmdst[i] > 1 ) ok = 0;
    CHECK( ok );
    STBIR_FREE( fmdst, 0 );
    free( fmsrc );
  }
  CHECK( g_live_bytes == 0 );

  //----------------------------------------------------------
  // 3. benchmark table: four shrink ratios x six methods
  //----------------------------------------------------------
  const struct { int w, h; const char * tag; } ratios[] = {
    { 21, 21, "2x   (42->21)" },
    { 14, 14, "3x   (42->14)" },
    {  6,  6, "7x   (42->6)"  },
    { 13, 11, "non-integer (~3.2x / 3.8x)" },
  };

  method_t methods[] = {
    { "nearest",          -1, -1 },
    { "bilinear",         -1, -1 },
    { "box/gamma",        STBIR_DS_BOX,      STBIR_DS_GAMMA  },
    { "box/linear",       STBIR_DS_BOX,      STBIR_DS_LINEAR },
    { "triangle/gamma",   STBIR_DS_TRIANGLE, STBIR_DS_GAMMA  },
    { "triangle/linear",  STBIR_DS_TRIANGLE, STBIR_DS_LINEAR },
  };

  const int ITERS = 200;
  printf( "\n== quality / speed / memory (PSNR vs exact linear box reference, higher is better) ==\n" );

  for ( unsigned r = 0; r < sizeof(ratios)/sizeof(ratios[0]); ++r ) {
    int dw = ratios[r].w, dh = ratios[r].h;
    size_t npix = (size_t) dw * dh * CH;
    unsigned char * rref = (unsigned char*) malloc( npix );
    unsigned char * dst  = (unsigned char*) malloc( npix );
    reference_box_linear( src, SRC_W, SRC_H, rref, dw, dh );

    printf( "\nratio %s, output %dx%d:\n", ratios[r].tag, dw, dh );
    printf( "  %-16s %8s %12s %10s %14s\n", "method", "PSNR dB", "lum bias", "time ms", "peak mem bytes" );

    for ( unsigned m = 0; m < sizeof(methods)/sizeof(methods[0]); ++m ) {
      mem_reset();
      struct timespec t0, t1;
      clock_gettime( CLOCK_MONOTONIC, &t0 );
      for ( int it = 0; it < ITERS; ++it ) {
        if ( methods[m].kernel < 0 ) {
          if ( m == 0 ) nearest( src, SRC_W, SRC_H, dst, dw, dh );
          else          bilinear( src, SRC_W, SRC_H, dst, dw, dh );
        } else {
          stbir_downsample_config mc = { methods[m].kernel, methods[m].color_mode };
          stbir_downsample_uint8( src, SRC_W, SRC_H, 0, dst, dw, dh, 0, CH, &mc );
        }
      }
      clock_gettime( CLOCK_MONOTONIC, &t1 );
      double ms = ( t1.tv_sec - t0.tv_sec ) * 1000.0 + ( t1.tv_nsec - t0.tv_nsec ) / 1.0e6;
      double psnr = psnr_db( dst, rref, (int) npix );
      double bias = luminance_bias( dst, rref, (int) npix );
      size_t peak = methods[m].kernel < 0 ? 0 : g_peak_bytes;
      printf( "  %-16s %8.2f %+12.3f %10.3f %14zu\n", methods[m].name, psnr, bias, ms, peak );
    }
    free( rref );
    free( dst );
  }

  //----------------------------------------------------------
  // 4. existing API output unchanged: determinism smoke check
  //----------------------------------------------------------
  printf( "\n== existing API stability (determinism checksum) ==\n" );
  unsigned char * a = stbir_resize_uint8_srgb( src, SRC_W, SRC_H, 0, NULL, 21, 21, 0, STBIR_RGB );
  unsigned char * b = stbir_resize_uint8_srgb( src, SRC_W, SRC_H, 0, NULL, 21, 21, 0, STBIR_RGB );
  CHECK( a != NULL && b != NULL && memcmp( a, b, 21 * 21 * CH ) == 0 );
  uint32_t checksum = 0;
  for ( int i = 0; i < 21 * 21 * CH; ++i ) checksum = checksum * 131u + a[i];
  printf( "  stbir_resize_uint8_srgb 42->21 checksum: %u\n", (unsigned) checksum );
  STBIR_FREE( a, 0 );
  STBIR_FREE( b, 0 );
  CHECK( g_live_bytes == 0 );

  free( ref );
  free( out );

  printf( "\n%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED" );
  return g_failures ? 1 : 0;
}
