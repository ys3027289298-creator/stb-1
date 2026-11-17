// png_stream_test.c - test harness for the stb_image PNG streaming decoder.
//
// - generates its own sample PNGs (large / alpha / interlaced / normal)
// - measures peak stb_image allocations, callback counts, decode time
// - verifies pixel-exact consistency with the one-shot decoder
// - verifies invalid chunk sizes are rejected
// - sweeps the in-repo pngsuite for streaming-vs-oneshot consistency
//
// not part of the Makefile; build manually:
//   gcc -I.. -O2 -o png_stream_test png_stream_test.c -lm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

// ---- tracking allocator: measures peak bytes held by stb_image ----------

static size_t trk_cur, trk_peak;

static void *trk_malloc(size_t sz)
{
   size_t *p = (size_t *) malloc(sz + sizeof(size_t));
   if (!p) return NULL;
   p[0] = sz;
   trk_cur += sz;
   if (trk_cur > trk_peak) trk_peak = trk_cur;
   return p + 1;
}

static void trk_free(void *ptr)
{
   size_t *p;
   if (!ptr) return;
   p = ((size_t *) ptr) - 1;
   trk_cur -= p[0];
   free(p);
}

static void *trk_realloc(void *ptr, size_t sz)
{
   size_t *p, *q, old;
   if (!ptr) return trk_malloc(sz);
   p = ((size_t *) ptr) - 1;
   old = p[0]; // read before realloc: the block may move
   q = (size_t *) realloc(p, sz + sizeof(size_t));
   if (!q) return NULL;
   trk_cur -= old;
   trk_cur += sz;
   if (trk_cur > trk_peak) trk_peak = trk_cur;
   q[0] = sz;
   return q + 1;
}

static void trk_reset(void) { trk_cur = 0; trk_peak = 0; }

#define STBI_MALLOC(sz)        trk_malloc(sz)
#define STBI_REALLOC(p,sz)     trk_realloc(p,sz)
#define STBI_FREE(p)           trk_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// ---- small utilities -----------------------------------------------------

static double now_ms(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static stbi_uc *read_file(const char *path, size_t *len)
{
   FILE *f = fopen(path, "rb");
   stbi_uc *buf;
   long n;
   if (!f) return NULL;
   fseek(f, 0, SEEK_END);
   n = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = (stbi_uc *) malloc(n > 0 ? (size_t) n : 1);
   if (buf && fread(buf, 1, (size_t) n, f) != (size_t) n) { free(buf); buf = NULL; }
   fclose(f);
   *len = (size_t) n;
   return buf;
}

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } } while (0)

// ---- self-made sample generators -----------------------------------------

static void make_pixels(stbi_uc *img, int w, int h, int n, unsigned seed)
{
   int x, y;
   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         stbi_uc *p = img + ((size_t) y * w + x) * n;
         seed = seed * 1664525u + 1013904223u;
         p[0] = (stbi_uc) ((x * 255 / w) ^ (seed >> 24));
         if (n > 1) {
            p[1] = (stbi_uc) ((y * 255 / h) ^ (seed >> 16));
            p[2] = (stbi_uc) (((x + y) * 255 / (w + h)) ^ (seed >> 8));
         }
         if (n == 4)
            p[3] = (stbi_uc) ((x * y) % 251);
      }
   }
}

static int write_sample(const char *path, int w, int h, int n)
{
   stbi_uc *img = (stbi_uc *) malloc((size_t) w * h * n);
   int ok;
   make_pixels(img, w, h, n, 12345u + (unsigned) n);
   ok = stbi_write_png(path, w, h, n, img, w * n);
   free(img);
   return ok;
}

// minimal interlaced-PNG writer: Adam7, 8-bit, zlib stream made of stored
// (uncompressed) deflate blocks. CRCs are zero (the decoder ignores them).
static void put_be32(stbi_uc *p, unsigned v)
{
   p[0] = (stbi_uc) (v >> 24); p[1] = (stbi_uc) (v >> 16);
   p[2] = (stbi_uc) (v >> 8);  p[3] = (stbi_uc) v;
}

static unsigned adler32_of(const stbi_uc *data, size_t len)
{
   unsigned a = 1, b = 0;
   size_t i;
   for (i = 0; i < len; ++i) {
      a = (a + data[i]) % 65521;
      b = (b + a) % 65521;
   }
   return (b << 16) | a;
}

static int write_interlaced_sample(const char *path, int w, int h, int n)
{
   static const int xo[7] = { 0,4,0,2,0,1,0 }, yo[7] = { 0,0,4,0,2,0,1 };
   static const int xs[7] = { 8,8,4,4,2,2,1 }, ys[7] = { 8,8,8,4,4,2,2 };
   stbi_uc *img, *raw, *z, *png, *rp, *zp, *pp;
   size_t raw_len = 0, z_len, png_len;
   int pass, x, y;
   FILE *f;

   img = (stbi_uc *) malloc((size_t) w * h * n);
   make_pixels(img, w, h, n, 777u);

   // interleaved pass scanlines, filter byte 0 per row
   for (pass = 0; pass < 7; ++pass) {
      int pw = (w - xo[pass] + xs[pass] - 1) / xs[pass];
      int ph = (h - yo[pass] + ys[pass] - 1) / ys[pass];
      if (pw > 0 && ph > 0)
         raw_len += (size_t) ph * (1 + (size_t) pw * n);
   }
   raw = (stbi_uc *) malloc(raw_len);
   rp = raw;
   for (pass = 0; pass < 7; ++pass) {
      int pw = (w - xo[pass] + xs[pass] - 1) / xs[pass];
      int ph = (h - yo[pass] + ys[pass] - 1) / ys[pass];
      int px, py;
      if (pw <= 0 || ph <= 0) continue;
      for (py = 0; py < ph; ++py) {
         *rp++ = 0; // filter: none
         y = py * ys[pass] + yo[pass];
         for (px = 0; px < pw; ++px) {
            x = px * xs[pass] + xo[pass];
            memcpy(rp, img + ((size_t) y * w + x) * n, (size_t) n);
            rp += n;
         }
      }
   }

   // zlib stream: header + stored blocks + adler32
   z_len = 2 + (raw_len / 65535 + 1) * 5 + raw_len + 4;
   z = zp = (stbi_uc *) malloc(z_len);
   *zp++ = 0x78; *zp++ = 0x01;
   {
      size_t off = 0;
      do {
         size_t blk = raw_len - off;
         if (blk > 65535) blk = 65535;
         *zp++ = (stbi_uc) ((off + blk == raw_len) ? 1 : 0);
         zp[0] = (stbi_uc) blk; zp[1] = (stbi_uc) (blk >> 8);
         zp[2] = (stbi_uc) ~blk; zp[3] = (stbi_uc) (~blk >> 8);
         zp += 4;
         memcpy(zp, raw + off, blk);
         zp += blk;
         off += blk;
      } while (off < raw_len);
   }
   put_be32(zp, adler32_of(raw, raw_len));
   zp += 4;
   z_len = (size_t) (zp - z);

   // png file
   png_len = 8 + (12 + 13) + (12 + z_len) + 12;
   png = pp = (stbi_uc *) malloc(png_len);
   memcpy(pp, "\x89PNG\r\n\x1a\n", 8); pp += 8;
   put_be32(pp, 13); memcpy(pp + 4, "IHDR", 4);
   put_be32(pp + 8, (unsigned) w); put_be32(pp + 12, (unsigned) h);
   pp[16] = 8; pp[17] = (stbi_uc) ((n == 1) ? 0 : (n == 2) ? 4 : (n == 3) ? 2 : 6);
   pp[18] = 0; pp[19] = 0; pp[20] = 1; // interlaced
   memset(pp + 21, 0, 4);
   pp += 25;
   put_be32(pp, (unsigned) z_len); memcpy(pp + 4, "IDAT", 4);
   memcpy(pp + 8, z, z_len);
   memset(pp + 8 + z_len, 0, 4);
   pp += 12 + z_len;
   put_be32(pp, 0); memcpy(pp + 4, "IEND", 4); memset(pp + 8, 0, 4);
   pp += 12;
   png_len = (size_t) (pp - png);

   f = fopen(path, "wb");
   if (!f) { free(img); free(raw); free(z); free(png); return 0; }
   fwrite(png, 1, png_len, f);
   fclose(f);
   free(img); free(raw); free(z); free(png);
   return 1;
}

// ---- decode runners ------------------------------------------------------

typedef struct {
   stbi_uc *acc;        // reassembled image (test-owned)
   size_t acc_cap;
   int calls;
   int ok;
} cb_ctx;

static void row_cb(void *user, int y, const stbi_uc *row, int width, int channels)
{
   cb_ctx *c = (cb_ctx *) user;
   size_t need = (size_t) (y + 1) * width * channels;
   if (y != c->calls) c->ok = 0; // rows must arrive in order 0,1,2,...
   if (need > c->acc_cap) {
      size_t cap = c->acc_cap ? c->acc_cap : 4096;
      while (cap < need) cap *= 2;
      c->acc = (stbi_uc *) realloc(c->acc, cap);
      c->acc_cap = cap;
   }
   memcpy(c->acc + (size_t) y * width * channels, row, (size_t) width * channels);
   c->calls++;
}

typedef struct {
   int ok, w, h, n, calls;
   size_t peak;
   double ms;
   const stbi_uc *pixels; // valid when ok (oneshot: owned; stream keep: stream-owned; else ctx-owned)
   stbi_uc *owned;        // oneshot image to free
   cb_ctx ctx;
} result_t;

static result_t run_oneshot(const char *path, int req)
{
   result_t r;
   double t0;
   memset(&r, 0, sizeof r);
   trk_reset();
   t0 = now_ms();
   r.pixels = stbi_load(path, &r.w, &r.h, &r.n, req);
   r.ms = now_ms() - t0;
   r.peak = trk_peak;
   r.ok = r.pixels != NULL;
   r.owned = (stbi_uc *) r.pixels;
   if (!r.ok) { r.w = r.h = r.n = 0; }
   return r;
}

static result_t run_stream(const stbi_uc *data, size_t len, int chunk, int keep, int req)
{
   result_t r;
   stbi_png_stream *s;
   double t0;
   size_t pos = 0;
   memset(&r, 0, sizeof r);
   r.ctx.ok = 1;

   trk_reset();
   t0 = now_ms();
   s = stbi_png_stream_create(chunk, keep, req, row_cb, &r.ctx);
   if (!s) { r.ms = now_ms() - t0; r.peak = trk_peak; return r; }
   while (pos < len) {
      size_t n = len - pos;
      if (n > (size_t) chunk) n = (size_t) chunk;
      if (!stbi_png_stream_feed(s, data + pos, (int) n)) break;
      pos += n;
   }
   r.ok = stbi_png_stream_finish(s, &r.w, &r.h, &r.n);
   r.ms = now_ms() - t0;
   r.peak = trk_peak;
   r.calls = r.ctx.calls;
   if (r.ok) {
      r.ok = r.ctx.ok && (r.ctx.calls == r.h);
      if (keep) {
         // copy out before the stream is freed; the retained image has the
         // delivered channel count (req_comp if set, else the file's)
         int dn = req ? req : r.n;
         r.owned = (stbi_uc *) malloc((size_t) r.w * r.h * dn);
         memcpy(r.owned, stbi_png_stream_image(s), (size_t) r.w * r.h * dn);
         free(r.ctx.acc);
      } else {
         r.owned = r.ctx.acc; // caller frees
      }
      r.pixels = r.owned;
   } else {
      free(r.ctx.acc);
   }
   stbi_png_stream_free(s);
   return r;
}

// compare streaming (keep=0 and keep=1) against one-shot for one file
static void compare_file(const char *name, const stbi_uc *data, size_t len,
                         const char *path, int chunk, int req, int nch)
{
   result_t one = run_oneshot(path, req);
   result_t s0  = run_stream(data, len, chunk, 0, req);
   result_t s1  = run_stream(data, len, chunk, 1, req);
   size_t bytes = (size_t) one.w * one.h * nch;
   int ok0, ok1;

   CHECK(one.ok, "oneshot decode");
   CHECK(s0.ok && s0.calls == one.h, "stream keep=0 decode/callbacks");
   CHECK(s1.ok && s1.calls == one.h, "stream keep=1 decode/callbacks");
   ok0 = one.ok && s0.ok && s0.w == one.w && s0.h == one.h && s0.n == one.n &&
         s0.pixels && memcmp(s0.pixels, one.pixels, bytes) == 0;
   ok1 = one.ok && s1.ok && s1.pixels && memcmp(s1.pixels, one.pixels, bytes) == 0;
   CHECK(ok0, "stream keep=0 pixel-identical");
   CHECK(ok1, "stream keep=1 pixel-identical");

   printf("%-14s %4dx%-4d n=%d req=%d | peak oneshot %8zu | stream k=0 %8zu | stream k=1 %8zu | cb %4d/%4d | px %s/%s | t %7.2f / %7.2f / %7.2f ms\n",
          name, one.w, one.h, one.n, req,
          one.peak, s0.peak, s1.peak,
          s0.calls, s1.calls,
          ok0 ? "OK" : "DIFF", ok1 ? "OK" : "DIFF",
          one.ms, s0.ms, s1.ms);

   stbi_image_free(one.owned);
   free(s0.owned);
   free(s1.owned);
}

// ---- pngsuite sweep ------------------------------------------------------

static int sweep_total, sweep_both_ok, sweep_both_fail, sweep_mismatch;

static void sweep_one(const char *path)
{
   int w1, h1, n1, w2, h2, n2;
   stbi_uc *img1;
   size_t len;
   stbi_uc *data = read_file(path, &len);
   cb_ctx ctx;
   stbi_png_stream *s;
   size_t pos;
   int ok2;

   if (!data) { printf("  FAIL: cannot read %s\n", path); failures++; return; }
   sweep_total++;

   img1 = stbi_load(path, &w1, &h1, &n1, 4);

   memset(&ctx, 0, sizeof ctx);
   ctx.ok = 1;
   s = stbi_png_stream_create(4096, 0, 4, row_cb, &ctx);
   pos = 0;
   while (pos < len) {
      size_t n = len - pos;
      if (n > 4096) n = 4096;
      if (!stbi_png_stream_feed(s, data + pos, (int) n)) break;
      pos += n;
   }
   ok2 = stbi_png_stream_finish(s, &w2, &h2, &n2);
   if (ok2) ok2 = ctx.ok && ctx.calls == h2;
   stbi_png_stream_free(s);

   if (!img1 && !ok2) {
      sweep_both_fail++;
   } else if (img1 && ok2 && w1 == w2 && h1 == h2 && n1 == n2 &&
              ctx.acc && memcmp(img1, ctx.acc, (size_t) w1 * h1 * 4) == 0) {
      sweep_both_ok++;
   } else {
      sweep_mismatch++;
      printf("  MISMATCH %s: oneshot %s (%dx%d n=%d) vs stream %s (%dx%d n=%d, calls=%d)\n",
             path, img1 ? "ok" : "fail", w1, h1, n1,
             ok2 ? "ok" : "fail", w2, h2, n2, ctx.calls);
      failures++;
   }
   stbi_image_free(img1);
   free(ctx.acc);
   free(data);
}

static void sweep_dir(const char *dir)
{
   DIR *d = opendir(dir);
   struct dirent *e;
   if (!d) return;
   while ((e = readdir(d)) != NULL) {
      char path[1024];
      size_t l = strlen(e->d_name);
      if (l < 4 || strcmp(e->d_name + l - 4, ".png") != 0) continue;
      snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
      sweep_one(path);
   }
   closedir(d);
}

// memory-reader callbacks for stbi_png_stream_load
typedef struct { const stbi_uc *p; size_t left; } mem_reader;

static int mem_read(void *user, char *data, int size)
{
   mem_reader *m = (mem_reader *) user;
   if ((size_t) size > m->left) size = (int) m->left;
   memcpy(data, m->p, (size_t) size);
   m->p += size;
   m->left -= (size_t) size;
   return size;
}

static void mem_skip(void *user, int n) { (void) user; (void) n; }
static int mem_eof(void *user) { return ((mem_reader *) user)->left == 0; }

// ---- main ----------------------------------------------------------------

int main(void)
{
   const char *dir = "stream_samples";
   char path[256];
   size_t len;
   stbi_uc *data;
   int i;

   // invalid chunk sizes must be rejected
   printf("== chunk size validation ==\n");
   CHECK(stbi_png_stream_create(0, 0, 0, row_cb, NULL) == NULL, "chunk_size 0 rejected");
   CHECK(stbi_png_stream_create(-5, 0, 0, row_cb, NULL) == NULL, "chunk_size -5 rejected");
   CHECK(stbi_png_stream_create(7, 0, 0, row_cb, NULL) == NULL, "chunk_size 7 rejected");
   CHECK(stbi_png_stream_create((1 << 30) + 1, 0, 0, row_cb, NULL) == NULL, "chunk_size 2^30+1 rejected");
   {
      stbi_png_stream *s = stbi_png_stream_create(8, 0, 0, row_cb, NULL);
      CHECK(s != NULL, "chunk_size 8 accepted");
      stbi_png_stream_free(s);
      s = stbi_png_stream_create(1 << 30, 0, 0, row_cb, NULL);
      CHECK(s != NULL, "chunk_size 2^30 accepted");
      stbi_png_stream_free(s);
   }
   CHECK(stbi_png_stream_create(64, 0, 5, row_cb, NULL) == NULL, "req_comp 5 rejected");
   printf("  done\n");

   // generate samples
   mkdir(dir, 0755);
   snprintf(path, sizeof path, "%s/large.png", dir);       CHECK(write_sample(path, 1280, 960, 3), "gen large");
   snprintf(path, sizeof path, "%s/alpha.png", dir);       CHECK(write_sample(path, 640, 480, 4), "gen alpha");
   snprintf(path, sizeof path, "%s/interlaced.png", dir);  CHECK(write_interlaced_sample(path, 200, 150, 3), "gen interlaced");
   snprintf(path, sizeof path, "%s/normal.png", dir);      CHECK(write_sample(path, 512, 512, 3), "gen normal");

   printf("== four sample types: peak memory / callbacks / consistency / timing ==\n");
   {
      static const struct { const char *name, *file; int nch; } samples[] = {
         { "large",      "stream_samples/large.png",      3 },
         { "alpha",      "stream_samples/alpha.png",      4 },
         { "interlaced", "stream_samples/interlaced.png", 3 },
         { "normal",     "stream_samples/normal.png",     3 },
      };
      for (i = 0; i < 4; ++i) {
         data = read_file(samples[i].file, &len);
         CHECK(data != NULL, "read sample");
         compare_file(samples[i].name, data, len, samples[i].file, 4096, 0, samples[i].nch);
         free(data);
      }
   }

   printf("== chunk size variations (normal.png, must all be pixel-identical) ==\n");
   {
      static const int chunks[] = { 8, 37, 4096, 1 << 20 };
      data = read_file("stream_samples/normal.png", &len);
      for (i = 0; i < 4; ++i) {
         char name[32];
         snprintf(name, sizeof name, "chunk=%d", chunks[i]);
         compare_file(name, data, len, "stream_samples/normal.png", chunks[i], 0, 3);
      }
      free(data);
   }

   printf("== req_comp conversions (stream vs oneshot) ==\n");
   {
      static const int reqs[] = { 1, 2, 3, 4 };
      data = read_file("stream_samples/normal.png", &len);
      for (i = 0; i < 4; ++i) {
         char name[32];
         snprintf(name, sizeof name, "req=%d", reqs[i]);
         compare_file(name, data, len, "stream_samples/normal.png", 4096, reqs[i], reqs[i]);
      }
      free(data);
      data = read_file("stream_samples/alpha.png", &len);
      compare_file("alpha req=3", data, len, "stream_samples/alpha.png", 4096, 3, 3);
      free(data);
   }

   printf("== pull API (stbi_png_stream_load via callbacks) ==\n");
   {
      size_t flen;
      stbi_uc *fdata = read_file("stream_samples/normal.png", &flen);
      result_t one = run_oneshot("stream_samples/normal.png", 0);
      cb_ctx ctx;
      int w = 0, h = 0, n = 0, ok;
      stbi_io_callbacks io;
      mem_reader mc;
      memset(&ctx, 0, sizeof ctx);
      ctx.ok = 1;
      mc.p = fdata; mc.left = flen;
      io.read = mem_read;
      io.skip = mem_skip;
      io.eof = mem_eof;
      ok = stbi_png_stream_load(&io, &mc, 4096, 0, 0, row_cb, &ctx, &w, &h, &n);
      CHECK(ok, "stbi_png_stream_load ok");
      CHECK(ok && w == one.w && h == one.h && n == one.n, "load dims match");
      CHECK(ok && ctx.calls == h && memcmp(ctx.acc, one.pixels, (size_t) w * h * n) == 0, "load pixels match");
      free(ctx.acc);
      stbi_image_free(one.owned);
      free(fdata);
   }

   printf("== truncated / non-png input ==\n");
   {
      data = read_file("stream_samples/normal.png", &len);
      {
         // feed only the first third, then finish: must fail
         cb_ctx ctx;
         stbi_png_stream *s;
         size_t third = len / 3, pos = 0;
         int ok = 1;
         memset(&ctx, 0, sizeof ctx);
         ctx.ok = 1;
         s = stbi_png_stream_create(4096, 0, 0, row_cb, &ctx);
         while (pos < third) {
            size_t nn = third - pos;
            if (nn > 4096) nn = 4096;
            if (!stbi_png_stream_feed(s, data + pos, (int) nn)) { ok = 0; break; }
            pos += nn;
         }
         if (ok) ok = stbi_png_stream_finish(s, NULL, NULL, NULL);
         CHECK(!ok && stbi_png_stream_error(s) != NULL, "truncated stream fails");
         stbi_png_stream_free(s);
         free(ctx.acc);
      }
      {
         // not a png at all
         static const stbi_uc junk[64] = { 1, 2, 3, 4 };
         stbi_png_stream *s = stbi_png_stream_create(4096, 0, 0, row_cb, NULL);
         int ok = stbi_png_stream_feed(s, junk, sizeof junk);
         CHECK(!ok && stbi_png_stream_error(s) != NULL, "non-png rejected");
         stbi_png_stream_free(s);
      }
      free(data);
   }

   printf("== pngsuite sweep (stream req=4 vs oneshot req=4) ==\n");
   sweep_dir("pngsuite/primary");
   sweep_dir("pngsuite/primary_check");
   sweep_dir("pngsuite/16bit");
   sweep_dir("pngsuite/iphone");
   sweep_dir("pngsuite/corrupt");
   sweep_dir("pngsuite/unused");
   printf("  total %d | both ok %d | both fail %d | mismatch %d\n",
          sweep_total, sweep_both_ok, sweep_both_fail, sweep_mismatch);
   CHECK(sweep_mismatch == 0, "pngsuite sweep mismatches");

   printf("%s\n", failures ? "FAILURES PRESENT" : "ALL OK");
   return failures ? 1 : 0;
}
