/*
 * Copyright (C) 2013 Ilia Mirkin <imirkin@alum.mit.edu>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xcb/dri2.h>


#include "nv50/nv50_context.h"

#undef NDEBUG
#include <assert.h>

/* From pipe_loader_drm.c in mesa */
static void
pipe_loader_drm_x_auth(int fd)
{
   /* Try authenticate with the X server to give us access to devices that X
    * is running on. */
   xcb_connection_t *xcb_conn;
   const xcb_setup_t *xcb_setup;
   xcb_screen_iterator_t s;
   xcb_dri2_connect_cookie_t connect_cookie;
   xcb_dri2_connect_reply_t *connect;
   drm_magic_t magic;
   xcb_dri2_authenticate_cookie_t authenticate_cookie;
   xcb_dri2_authenticate_reply_t *authenticate;

   xcb_conn = xcb_connect(NULL,  NULL);

   if(!xcb_conn)
      return;

   xcb_setup = xcb_get_setup(xcb_conn);

  if (!xcb_setup)
    goto disconnect;

   s = xcb_setup_roots_iterator(xcb_setup);
   connect_cookie = xcb_dri2_connect_unchecked(xcb_conn, s.data->root,
                                               XCB_DRI2_DRIVER_TYPE_DRI);
   connect = xcb_dri2_connect_reply(xcb_conn, connect_cookie, NULL);

   if (!connect || connect->driver_name_length
                   + connect->device_name_length == 0) {

      goto disconnect;
   }

   if (drmGetMagic(fd, &magic))
      goto disconnect;

   authenticate_cookie = xcb_dri2_authenticate_unchecked(xcb_conn,
                                                         s.data->root,
                                                         magic);
   authenticate = xcb_dri2_authenticate_reply(xcb_conn,
                                              authenticate_cookie,
                                              NULL);
   FREE(authenticate);

disconnect:
   xcb_disconnect(xcb_conn);
}

static struct nouveau_bufctx *bufctx;

static struct nouveau_bo *
new_bo_and_map(struct nouveau_device *dev,
               struct nouveau_client *client, long size) {
  struct nouveau_bo *ret;
  assert(!nouveau_bo_new(dev, NOUVEAU_BO_VRAM, 0x1000, size, NULL, &ret));
  if (client)
    assert(!nouveau_bo_map(ret, NOUVEAU_BO_RDWR, client));
  fprintf(stderr, "returning map: %llx\n", ret->offset);
  nouveau_bufctx_refn(bufctx, 0, ret, NOUVEAU_BO_VRAM | NOUVEAU_BO_RDWR);
  return ret;
}

static struct nouveau_bo *
new_bo_and_map_tile(struct nouveau_device *dev,
               struct nouveau_client *client, long size) {
  struct nouveau_bo *ret;
  union nouveau_bo_config cfg;

  cfg.nv50.tile_mode = 0x20;
  cfg.nv50.memtype = 0x70;
  assert(!nouveau_bo_new(dev, NOUVEAU_BO_VRAM, 0x1000, size, &cfg, &ret));
  if (client)
    assert(!nouveau_bo_map(ret, NOUVEAU_BO_RDWR, client));
  fprintf(stderr, "returning map: %llx\n", ret->offset);
  nouveau_bufctx_refn(bufctx, 0, ret, NOUVEAU_BO_VRAM | NOUVEAU_BO_RDWR);
  return ret;
}

static struct nouveau_bo *
new_bo_and_map_gart(struct nouveau_device *dev,
                    struct nouveau_client *client, long size) {
  struct nouveau_bo *ret;
  assert(!nouveau_bo_new(dev, NOUVEAU_BO_GART, 0x1000, size, NULL, &ret));
  if (client)
    assert(!nouveau_bo_map(ret, NOUVEAU_BO_RDWR, client));
  fprintf(stderr, "returning gart map: %llx\n", ret->offset);
  nouveau_bufctx_refn(bufctx, 0, ret, NOUVEAU_BO_GART | NOUVEAU_BO_RDWR);
  return ret;
}

static void
load_bsp_fw(struct nouveau_bo *fw) {
  int fd = open("/lib/firmware/nouveau/nv84_bsp-h264", O_RDONLY);
  struct stat statbuf;
  void *addr;
  assert(fd);
  assert(fstat(fd, &statbuf) == 0);
  addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
  assert(addr);

  memcpy(fw->map, addr, statbuf.st_size);
  memset(fw->map + statbuf.st_size, 0, fw->size - statbuf.st_size);

  munmap(addr, statbuf.st_size);
  close(fd);
}

static void
clear_3d(struct nouveau_pushbuf *push, uint64_t offset,
         uint16_t w, uint16_t h, int scale, int tile_mode, uint32_t color) {
  int i;

  BEGIN_NV04(push, 3, 0x200, 4);
  PUSH_DATAh(push, offset);
  PUSH_DATA (push, offset);
  PUSH_DATA (push, 0xd5); /* RGBA8_UNORM - some of the 0's use BGRA8, but whatever, it's all 0's... */
  PUSH_DATA (push, tile_mode); /* tile mode */
  BEGIN_NV04(push, 3, 0xff4, 2);
  PUSH_DATA (push, (uint32_t)w << 16);
  PUSH_DATA (push, (uint32_t)h << 16);
  BEGIN_NV04(push, 3, 0x1240, 2);
  PUSH_DATA (push, (scale == 1 ? 0 : 0x80000000) | scale * w);
  PUSH_DATA (push, h);
  BEGIN_NV04(push, 3, 0x143c, 1);
  PUSH_DATA (push, 0);
  BEGIN_NV04(push, 3, 0xd80, 4);
  for (i = 0; i < 4; i++)
    PUSH_DATA(push, color);
  BEGIN_NV04(push, 3, 0x19d0, 1);
  PUSH_DATA (push, 0x3c);
  PUSH_KICK (push);
}

static void
copy_to_linear(struct nouveau_pushbuf *push, uint64_t from, uint64_t to,
               int width, int height, int lines, int dest_pitch) {
  BEGIN_NV04(push, 4, 0x200, 4);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, 0x20 /* tiling mode */);
  PUSH_DATA (push, width);
  PUSH_DATA (push, height);

  BEGIN_NV04(push, 4, 0x218, 2);
  PUSH_DATA (push, 0 << 16); /* y offset */
  PUSH_DATA (push, 1);

  BEGIN_NV04(push, 4, 0x238, 2);
  PUSH_DATAh(push, from);
  PUSH_DATAh(push, to);

  BEGIN_NV04(push, 4, 0x30c, 8);
  PUSH_DATA (push, from);
  PUSH_DATA (push, to);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, dest_pitch);
  PUSH_DATA (push, width);
  PUSH_DATA (push, lines);
  PUSH_DATA (push, 0x101);
  PUSH_DATA (push, 0);
}

static void
copy_buffer(struct nouveau_pushbuf *push, struct nouveau_bo *from, struct nouveau_bo *to) {
  /*
  copy_to_linear(push, from->offset, to->offset + 0 * 0x7d00, 1280, 272, 25, 0);
  copy_to_linear(push, from->offset, to->offset + 1 * 0x7d00, 1280, 272, 25, 25);
  copy_to_linear(push, from->offset, to->offset + 2 * 0x7d00, 1280, 272, 25, 50);
  copy_to_linear(push, from->offset, to->offset + 3 * 0x7d00, 1280, 272, 25, 75);
  copy_to_linear(push, from->offset, to->offset + 4 * 0x7d00, 1280, 272, 25, 100);
  copy_to_linear(push, from->offset, to->offset + 5 * 0x7d00, 1280, 272, 25, 125);
  copy_to_linear(push, from->offset, to->offset + 6 * 0x7d00, 1280, 272, 25, 150);
  copy_to_linear(push, from->offset, to->offset + 7 * 0x7d00, 1280, 272, 25, 175);
  copy_to_linear(push, from->offset, to->offset + 8 * 0x7d00, 1280, 272, 25, 200);
  copy_to_linear(push, from->offset, to->offset + 9 * 0x7d00, 1280, 272, 25, 225);
  copy_to_linear(push, from->offset, to->offset + 10 * 0x7d00, 1280, 272, 22, 250);
  */
  copy_to_linear(push, from->offset, to->offset, 1280, 272, 272, 1280 * 2);

  /*
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 0 * 0x7d00, 1280, 272, 25, 0);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 1 * 0x7d00, 1280, 272, 25, 25);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 2 * 0x7d00, 1280, 272, 25, 50);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 3 * 0x7d00, 1280, 272, 25, 75);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 4 * 0x7d00, 1280, 272, 25, 100);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 5 * 0x7d00, 1280, 272, 25, 125);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 6 * 0x7d00, 1280, 272, 25, 150);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 7 * 0x7d00, 1280, 272, 25, 175);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 8 * 0x7d00, 1280, 272, 25, 200);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 9 * 0x7d00, 1280, 272, 25, 225);
  copy_to_linear(push, from->offset + 0x55000, to->offset + 0x55000 + 10 * 0x7d00, 1280, 272, 22, 250);
  */
  copy_to_linear(push, from->offset + 0x55000, to->offset + /*0x55000*/ 1280, 1280, 272, 272, 1280 * 2);

  /*
  copy_to_linear(push, from->offset + 0xaa000, to->offset + 0xaa000 + 0 * 0x7d00,
                 1280, 136, 25, 0);
  copy_to_linear(push, from->offset + 0xaa000, to->offset + 0xaa000 + 1 * 0x7d00,
                 1280, 136, 25, 25);
  copy_to_linear(push, from->offset + 0xaa000, to->offset + 0xaa000 + 2 * 0x7d00,
                 1280, 136, 25, 50);
  copy_to_linear(push, from->offset + 0xaa000, to->offset + 0xaa000 + 3 * 0x7d00,
                 1280, 136, 25, 75);
  copy_to_linear(push, from->offset + 0xaa000, to->offset + 0xaa000 + 4 * 0x7d00,
                 1280, 136, 25, 100);
  copy_to_linear(push, from->offset + 0xaa000, to->offset + 0xaa000 + 5 * 0x7d00,
                 1280, 136, 11, 125);
  */
  copy_to_linear(push, from->offset + 0xaa000, to->offset + 0xaa000, 1280, 136, 136, 1280 * 2);

  /* Round up number of lines to 16, so 2d000 offset on source. */
  /*
  copy_to_linear(push, from->offset + 0xaa000 + 0x2d000, to->offset + 0xaa000 + 0x2a800 + 0 * 0x7d00,
                 1280, 136, 25, 0);
  copy_to_linear(push, from->offset + 0xaa000 + 0x2d000, to->offset + 0xaa000 + 0x2a800 + 1 * 0x7d00,
                 1280, 136, 25, 25);
  copy_to_linear(push, from->offset + 0xaa000 + 0x2d000, to->offset + 0xaa000 + 0x2a800 + 2 * 0x7d00,
                 1280, 136, 25, 50);
  copy_to_linear(push, from->offset + 0xaa000 + 0x2d000, to->offset + 0xaa000 + 0x2a800 + 3 * 0x7d00,
                 1280, 136, 25, 75);
  copy_to_linear(push, from->offset + 0xaa000 + 0x2d000, to->offset + 0xaa000 + 0x2a800 + 4 * 0x7d00,
                 1280, 136, 25, 100);
  copy_to_linear(push, from->offset + 0xaa000 + 0x2d000, to->offset + 0xaa000 + 0x2a800 + 5 * 0x7d00,
                 1280, 136, 11, 125);
  */
  copy_to_linear(push, from->offset + 0xaa000 + 0x2d000, to->offset + 0xaa000 + /*0x2a800*/ + 1280, 1280, 136, 136, 1280 * 2);

  PUSH_KICK(push);
}

static void
load_vp_fw(struct nouveau_bo *fw) {
  int fd;
  struct stat statbuf;
  void *addr;

  assert((fd = open("/lib/firmware/nouveau/nv84_vp-h264-1", O_RDONLY)));
  assert(fstat(fd, &statbuf) == 0);
  assert(statbuf.st_size < 0x1f400);
  assert((addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)));

  memcpy(fw->map, addr, statbuf.st_size);

  munmap(addr, statbuf.st_size);
  close(fd);

  assert((fd = open("/lib/firmware/nouveau/nv84_vp-h264-2", O_RDONLY)));
  assert(fstat(fd, &statbuf) == 0);
  assert((addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)));

  memcpy(fw->map + 0x1f400, addr, statbuf.st_size);

  munmap(addr, statbuf.st_size);
  close(fd);
}

static void
load_bitstream(struct nouveau_bo *data) {
  int fd;
  struct stat statbuf;
  void *addr;
  uint32_t arr[0x530 / 4] = {0};
  uint32_t arr2[0x44 / 4] = {0};

  assert((fd = open("frame_nal", O_RDONLY)));
  assert(fstat(fd, &statbuf) == 0);
  assert((addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)));

  arr[0x0   / 4 + 0] = 0x1;
  arr[0x120 / 4 + 2] = 0x5;
  arr[0x130 / 4 + 0] = 0x6;
  arr[0x130 / 4 + 2] = 0x6;
  arr[0x130 / 4 + 3] = 0x4f;
  arr[0x140 / 4 + 0] = 0x21;
  arr[0x140 / 4 + 1] = 0x1;
  arr[0x140 / 4 + 3] = 0x1;
  arr[0x150 / 4 + 0] = 0x1;
  arr[0x1e0 / 4 + 1] = 0x1;
  arr[0x320 / 4 + 0] = 0x10000;
  arr[0x320 / 4 + 1] = 0x10000;
  arr[0x320 / 4 + 2] = 0x10000;

  arr2[1] = statbuf.st_size + 3 + 16;

  uint32_t end[2] = {0x0b010000, 0};

  memcpy(data->map, arr, sizeof(arr));
  memcpy(data->map + 0x600, arr2, sizeof(arr2));
  uint8_t *map = data->map;
  map[0x700] = 0;
  map[0x701] = 0;
  map[0x702] = 1;
  memcpy(data->map + 0x703, addr, statbuf.st_size);
  memcpy(data->map + 0x703 + statbuf.st_size, end, sizeof(end));
  memcpy(data->map + 0x703 + statbuf.st_size + sizeof(end), end, sizeof(end));
}

static void
init_vp_params(struct nouveau_bo *data, struct nouveau_bo *frames[]) {
  uint32_t *map = data->map;
  int i;

  for (i = 0; i < 0xe0 / 4; i++)
    map[i] = 0x10101010;
  map[0xe0 / 4] = 0x500; /* width */
  map[0xe4 / 4] = 0x220; /* height */
  for (i = 0; i < 16; i++)
    *((uint64_t *)data->map + 0xe8 / 8 + i) = frames[0]->offset;

  for (i = 0; i < 16; i++)
    *((uint64_t *)data->map + 0x168 / 8 + i) = frames[1]->offset;

  map[0x1e8 / 4] = 0;
  map[0x1ec / 4] = 0;
  map[0x1f0 / 4] = 0x500;
  map[0x1f4 / 4] = 0x500;
  map[0x1f8 / 4] = 0x500;
  map[0x1fc / 4] = 0x220;
  map[0x200 / 4] = 0x220;
  map[0x204 / 4] = 0x220;
  map[0x208 / 4] = 0;
  map[0x20c / 4] = 0;
  map[0x210 / 4] = 0x3231564e; /* ??? */
  map[0x214 / 4] = 0;
  map[0x400 / 4] = 0x500;
  map[0x404 / 4] = 0x220;
  map[0x408 / 4] = 0xaa0; /* width * height / 8 ? */
  map[0x40c / 4] = 0x500;
  map[0x410 / 4] = 0x500;
  map[0x414 / 4] = 0x500;
  map[0x418 / 4] = 0x220;
  map[0x41c / 4] = 0x220;
  map[0x420 / 4] = 0x220;
  map[0x424 / 4] = 0;
  map[0x428 / 4] = 0;
  map[0x42c / 4] = 0;
  map[0x430 / 4] = 0;
  map[0x434 / 4] = 1;
}

int main() {
  struct nouveau_device *dev;
  struct nouveau_client *client;
  struct nouveau_object *channel;
  struct nouveau_object *bsp, *vp, *threed, *m2mf, *sync;
  struct nouveau_pushbuf *push;
  struct nouveau_bo *bsp_sem, *bsp_fw, *bsp_scratch, *bitstream, *mbring, *vpring;
  struct nouveau_bo *vp_sem, *vp_fw, *vp_scratch, *vp_params, *frames[2];
  struct nouveau_bo *d3_fpvp, *d3_cb_def, *d3_tsc_tic;
  struct nouveau_bo *output;

  struct nv04_fifo nv04_data = { .vram = 0xbeef0201, .gart = 0xbeef0202 };

  int fd, i;

  fd = open("/dev/dri/card0", O_RDWR);
  assert(fd);
  pipe_loader_drm_x_auth(fd);

  assert(!nouveau_device_wrap(fd, 0, &dev));
  assert(!nouveau_client_new(dev, &client));
  assert(!nouveau_object_new(&dev->object, 0, NOUVEAU_FIFO_CHANNEL_CLASS,
                             &nv04_data, sizeof(nv04_data), &channel));
  assert(!nouveau_pushbuf_new(client, channel, 2, 0x2000, 1, &push));

  assert(!nouveau_object_new(channel, 0xbeef74b0, 0x74b0, NULL, 0, &bsp));
  assert(!nouveau_object_new(channel, 0xbeef7476, 0x7476, NULL, 0, &vp));
  assert(!nouveau_object_new(channel, 0xbeef8297, 0x8297, NULL, 0, &threed));
  assert(!nouveau_object_new(channel, 0xbeef5039, 0x5039, NULL, 0, &m2mf));

  assert(!nouveau_object_new(channel, 0xbeef0301, NOUVEAU_NOTIFIER_CLASS,
                             &(struct nv04_notify){ .length = 32 },
                             sizeof(struct nv04_notify), &sync));

  assert(!nouveau_bufctx_new(client, 1, &bufctx));
  nouveau_pushbuf_bufctx(push, bufctx);


  bsp_sem = new_bo_and_map(dev, client, 0x1000);
  bsp_fw = new_bo_and_map(dev, client, 0xd9d0);
  bsp_scratch = new_bo_and_map(dev, client, 0x40000);
  bitstream = new_bo_and_map(dev, client, 0x1ffe00);
  mbring = new_bo_and_map(dev, NULL, 0x1d5800);
  vpring = new_bo_and_map(dev, NULL, 0x9ee200);

  vp_sem = new_bo_and_map(dev, client, 0x1000);
  vp_fw = new_bo_and_map(dev, client, 0x3b3fc);
  vp_scratch = new_bo_and_map(dev, client, 0x40000);
  vp_params = new_bo_and_map(dev, client, 0x2000);

  d3_fpvp = new_bo_and_map(dev, NULL, 0x8f00);
  d3_cb_def = new_bo_and_map(dev, NULL, 0x1000);
  d3_tsc_tic = new_bo_and_map(dev, NULL, 0x2000);

  for (i = 0; i < 2; i++) {
    frames[i] = new_bo_and_map_tile(dev, client, 0x104000);
  }

  output = new_bo_and_map_gart(dev, client, 0x104000);

  *(uint64_t *)bsp_sem->map = ~0;
  *(uint64_t *)vp_sem->map = ~0;

  /* Setup DMA for the SEMAPHORE logic */
  BEGIN_NV04(push, 0, 0x60, 1);
  PUSH_DATA (push, nv04_data.vram);

  /* Bind the BSP to the fifo */
  BEGIN_NV04(push, 1, 0, 1);
  PUSH_DATA (push, bsp->handle);

  /* Bind the VP to the fifo */
  BEGIN_NV04(push, 2, 0, 1);
  PUSH_DATA (push, vp->handle);

  /* Bind the 3D to the fifo */
  BEGIN_NV04(push, 3, 0, 1);
  PUSH_DATA (push, threed->handle);

  /* Bind the M2MF to the fifo */
  BEGIN_NV04(push, 4, 0, 1);
  PUSH_DATA (push, m2mf->handle);

  /* Set the DMA channels */
  BEGIN_NV04(push, 1, 0x180, 11);
  for (i = 0; i < 11; i++)
    PUSH_DATA(push, nv04_data.vram);

  BEGIN_NV04(push, 1, 0x1b8, 1);
  PUSH_DATA (push, nv04_data.vram);

  BEGIN_NV04(push, 2, 0x180, 11);
  for (i = 0; i < 11; i++)
    PUSH_DATA(push, nv04_data.vram);

  BEGIN_NV04(push, 2, 0x1b8, 1);
  PUSH_DATA (push, nv04_data.vram);

  BEGIN_NV04(push, 3, 0x180, 1);
  PUSH_DATA (push, sync->handle);
  BEGIN_NV04(push, 3, 0x188, 2);
  for (i = 0; i < 2; i++)
    PUSH_DATA (push, nv04_data.vram);
  BEGIN_NV04(push, 3, 0x198, 6);
  for (i = 0; i < 6; i++)
    PUSH_DATA (push, nv04_data.vram);

  BEGIN_NV04(push, 3, 0x1c0, 8);
  for (i = 0; i < 8; i++)
    PUSH_DATA (push, nv04_data.vram);

  BEGIN_NV04(push, 4, 0x180, 3);
  PUSH_DATA (push, sync->handle);
  for (i = 0; i < 2; i++)
    PUSH_DATA (push, nv04_data.gart);

  /* Initialize 3D FP/VP/whatever */
  BEGIN_NV04(push, 3, 0xfa4, 2);
  PUSH_DATAh(push, d3_fpvp->offset);
  PUSH_DATA (push, d3_fpvp->offset);

  BEGIN_NV04(push, 3, 0xf7c, 2);
  PUSH_DATAh(push, d3_fpvp->offset);
  PUSH_DATA (push, d3_fpvp->offset);

  BEGIN_NV04(push, 3, 0x1290, 1);
  PUSH_DATA (push, 0xfff);
  BEGIN_NV04(push, 3, 0x1988, 1);
  PUSH_DATA (push, 0x240424);
  BEGIN_NV04(push, 3, 0x1298, 1);
  PUSH_DATA (push, 0x4);
  BEGIN_NV04(push, 3, 0x140c, 1);
  PUSH_DATA (push, 0x0);
  BEGIN_NV04(push, 3, 0x16ac, 2);
  PUSH_DATA (push, 0x24);
  PUSH_DATA (push, 0x0);
  BEGIN_NV04(push, 3, 0x129c, 1);
  PUSH_DATA (push, 0x20);
  BEGIN_NV04(push, 3, 0x1650, 2);
  PUSH_DATA (push, ~0);
  PUSH_DATA (push, ~0);
  BEGIN_NV04(push, 3, 0x16b0, 1);
  PUSH_DATA (push, 0x24);
  BEGIN_NV04(push, 3, 0x16bc, 1);
  PUSH_DATA (push, 0x03020100);
  BEGIN_NV04(push, 3, 0x1540, 2);
  PUSH_DATA (push, ~0);
  PUSH_DATA (push, ~0);
  BEGIN_NV04(push, 3, 0x1280, 3);
  PUSH_DATAh(push, d3_cb_def->offset);
  PUSH_DATA (push, d3_cb_def->offset);
  PUSH_DATA (push, 0x100);
  BEGIN_NV04(push, 3, 0x1694, 1);
  PUSH_DATA (push, 0x131);
  BEGIN_NV04(push, 3, 0x1280, 3);
  PUSH_DATAh(push, d3_cb_def->offset + 0x400);
  PUSH_DATA (push, d3_cb_def->offset + 0x400);
  PUSH_DATA (push, 0x100);
  BEGIN_NV04(push, 3, 0x1694, 1);
  PUSH_DATA (push, 0x1031);
  BEGIN_NV04(push, 3, 0xa00, 6);
  for (i = 0; i < 3; i++)
    PUSH_DATA(push, 0x3f800000);
  for (i = 0; i < 3; i++)
    PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0xc00, 4);
  PUSH_DATA (push, 0x20000000);
  PUSH_DATA (push, 0x20000000);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, 0x3f800000);
  BEGIN_NV04(push, 3, 0xdac, 3);
  PUSH_DATA(push, 0x1b02);
  PUSH_DATA(push, 0x1b02);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0xdc0, 3);
  PUSH_DATA(push, 0);
  PUSH_DATA(push, 0);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0xdf8, 2);
  PUSH_DATA(push, 0);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0xe00, 1);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0x1234, 1);
  PUSH_DATA(push, 1);
  BEGIN_NV04(push, 3, 0x12cc, 3);
  PUSH_DATA(push, 0);
  PUSH_DATA(push, 3);
  PUSH_DATA(push, 2);
  BEGIN_NV04(push, 3, 0x12e8, 2);
  PUSH_DATA(push, 0);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0x1308, 1);
  PUSH_DATA(push, 1);
  BEGIN_NV04(push, 3, 0x133c, 1);
  PUSH_DATA(push, 1);
  BEGIN_NV04(push, 3, 0x13bc, 1);
  PUSH_DATA(push, 0x44);
  /*
  BEGIN_NV04(push, 3, 0x1528, 1);
  PUSH_DATA(push, 0);
  */
  BEGIN_NV04(push, 3, 0x1534, 1);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0x155c, 3);
  PUSH_DATAh(push, d3_tsc_tic->offset + 0x1000);
  PUSH_DATA (push, d3_tsc_tic->offset + 0x1000);
  PUSH_DATA (push, 0x80);
  BEGIN_NV04(push, 3, 0x1574, 3);
  PUSH_DATAh(push, d3_tsc_tic->offset);
  PUSH_DATA (push, d3_tsc_tic->offset);
  PUSH_DATA (push, 0x80);
  BEGIN_NV04(push, 3, 0x15b4, 2);
  PUSH_DATA(push, 0);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0x168c, 1);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0x1924, 1);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0x192c, 1);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0x194c, 1);
  PUSH_DATA(push, 0);
  BEGIN_NV04(push, 3, 0x1a00, 1);
  PUSH_DATA(push, 0x1111);
  BEGIN_NV04(push, 3, 0x121c, 1);
  PUSH_DATA(push, 1);
  BEGIN_NV04(push, 3, 0x1538, 1);
  PUSH_DATA(push, 0);

  /* Clear stuff on mbring/vpring */
  clear_3d(push, mbring->offset + 0xaa000,
           64, 4760, 4, 0, 0);
  clear_3d(push, vpring->offset + 0x4f6100,
           1024, 1, 4, 0, 0);
  clear_3d(push, vpring->offset + 0x9ed200,
           1024, 1, 4, 0, 0);

  /* Write semaphore */
  BEGIN_NV04(push, 3, 0x1b00, 4);
  PUSH_DATAh(push, bsp_sem->offset);
  PUSH_DATA (push, bsp_sem->offset);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, 0xf010); /* write + ? */

  /* Load BSP firmware/scratch buf */
  load_bsp_fw(bsp_fw);
  BEGIN_NV04(push, 1, 0x600, 3);
  PUSH_DATAh(push, bsp_fw->offset);
  PUSH_DATA (push, bsp_fw->offset);
  PUSH_DATA (push, bsp_fw->size);

  BEGIN_NV04(push, 1, 0x628, 2);
  PUSH_DATA (push, bsp_scratch->offset >> 8);
  PUSH_DATA (push, bsp_scratch->size);
  PUSH_KICK (push);

  /* Load VP firmware/scratch buf */

  load_vp_fw(vp_fw);
  BEGIN_NV04(push, 2, 0x600, 3);
  PUSH_DATAh(push, vp_fw->offset);
  PUSH_DATA (push, vp_fw->offset);
  PUSH_DATA (push, vp_fw->size);

  BEGIN_NV04(push, 2, 0x628, 2);
  PUSH_DATA (push, vp_scratch->offset >> 8);
  PUSH_DATA (push, vp_scratch->size);
  PUSH_KICK (push);

  load_bitstream(bitstream);
  init_vp_params(vp_params, frames);

  /* Clear frames */
/*
  clear_3d(push, frames[1]->offset,
           320, 544, 1, 0x20, 0);
  clear_3d(push, frames[1]->offset + 0xaa000,
           320, 272, 1, 0x20, 0x3f000000);
  clear_3d(push, frames[0]->offset,
           320, 272, 1, 0x20, 0);
  clear_3d(push, frames[0]->offset + 0x55000,
           320, 272, 1, 0x20, 0);
  clear_3d(push, frames[0]->offset + 0xaa000,
           320, 136, 1, 0x20, 0x3f000000);
  clear_3d(push, frames[0]->offset + 0xaa000 + 0x2d000, // 1280 * 0x90
           320, 136, 1, 0x20, 0x3f000000);
*/

  memset(frames[0]->map, 0xff, frames[0]->size);
  memset(frames[1]->map, 0xff, frames[1]->size);

  /* Wait for the mbring/vpring clearing */
  BEGIN_NV04(push, 1, 0x10, 4);
  PUSH_DATAh(push, bsp_sem->offset);
  PUSH_DATA (push, bsp_sem->offset);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, 1); /* wait for sem == 0 */
  PUSH_KICK (push);

  /* Kick off the BSP */
  BEGIN_NV04(push, 1, 0x400, 20);
  PUSH_DATA (push, bitstream->offset >> 8);
  PUSH_DATA (push, (bitstream->offset >> 8) + 7);
  PUSH_DATA (push, 0xFF800); /* length? seems high. perhaps max buffer? */
  PUSH_DATA (push, (bitstream->offset >> 8) + 6);
  PUSH_DATA (push, 1);
  PUSH_DATA (push, mbring->offset >> 8);
  PUSH_DATA (push, 0xaa000); /* width * height? */
  PUSH_DATA (push, (mbring->offset >> 8) + 0xaa0);
  PUSH_DATA (push, vpring->offset >> 8);
  PUSH_DATA (push, 0x4f7100); /* half the vpring size? */
  PUSH_DATA (push, 0x3fe000);
  PUSH_DATA (push, 0xd8300);
  PUSH_DATA (push, 0x0);
  PUSH_DATA (push, 0x3fe000);
  PUSH_DATA (push, 0x4d6300);
  PUSH_DATA (push, 0x1fe00);
  PUSH_DATA (push, (vpring->offset >> 8) + 0x4f61); /* 0x4d63 + 0x1fe */
  PUSH_DATA (push, 0x654321);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, 0x100008);

  BEGIN_NV04(push, 1, 0x620, 2);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, 0);

  BEGIN_NV04(push, 1, 0x300, 1);
  PUSH_DATA (push, 0);

  /* Set the semaphore */
  BEGIN_NV04(push, 1, 0x610, 3);
  PUSH_DATAh(push, bsp_sem->offset);
  PUSH_DATA (push, bsp_sem->offset);
  PUSH_DATA (push, 1);

  /* Write 1 to the semaphore location */
  BEGIN_NV04(push, 1, 0x304, 1);
  PUSH_DATA (push, 0x101);
  PUSH_KICK (push);

  /* Wait for the semaphore to get written */
  BEGIN_NV04(push, 2, 0x10, 4);
  PUSH_DATAh(push, bsp_sem->offset);
  PUSH_DATA (push, bsp_sem->offset);
  PUSH_DATA (push, 1);
  PUSH_DATA (push, 1); /* wait for sem == 1 */
  PUSH_KICK (push);

  /* VP step 1 */
  BEGIN_NV04(push, 2, 0x400, 15);
  PUSH_DATA (push, 1);
  PUSH_DATA (push, 0xaa0); /* related to aa000 above? */
  PUSH_DATA (push, 0x3987654);
  PUSH_DATA (push, 0x55001);
  PUSH_DATA (push, vp_params->offset >> 8);
  PUSH_DATA (push, (vpring->offset >> 8) + 0x3fe0);
  PUSH_DATA (push, 0xd8300);
  PUSH_DATA (push, vpring->offset >> 8);
  PUSH_DATA (push, 0xff800); /* related to ff800 above? */
  PUSH_DATA (push, (mbring->offset >> 8) + 0x1d38);
  PUSH_DATA (push, (vpring->offset >> 8) + 0x4f61);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, 0x100008);
  PUSH_DATA (push, frames[0]->offset >> 8);
  PUSH_DATA (push, 0);

  BEGIN_NV04(push, 2, 0x620, 2);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, 0);

  BEGIN_NV04(push, 2, 0x300, 1);
  PUSH_DATA (push, 0);
  PUSH_KICK (push);

  /* VP step 2 */
  BEGIN_NV04(push, 2, 0x400, 5);
  PUSH_DATA (push, 0x54530201);
  PUSH_DATA (push, (vp_params->offset >> 8) + 0x4);
  PUSH_DATA (push, (vpring->offset >> 8) + 0x4d63);
  PUSH_DATA (push, frames[0]->offset >> 8);
  PUSH_DATA (push, frames[0]->offset >> 8);
  BEGIN_NV04(push, 2, 0x414, 1);
  PUSH_DATA (push, frames[1]->offset >> 8);

  BEGIN_NV04(push, 2, 0x620, 2);
  PUSH_DATA (push, 0);
  PUSH_DATA (push, 0x1f400); /* offset for second firmware */

  BEGIN_NV04(push, 2, 0x300, 1);
  PUSH_DATA (push, 0);
  PUSH_KICK (push);

  /* Set the semaphore */
  BEGIN_NV04(push, 2, 0x610, 3);
  PUSH_DATAh(push, vp_sem->offset);
  PUSH_DATA (push, vp_sem->offset);
  PUSH_DATA (push, 3);

  /* Write to the semaphore location, intr */
  BEGIN_NV04(push, 2, 0x304, 1);
  PUSH_DATA (push, 0x101);
  PUSH_KICK (push);

  /* Set the semaphore */
  BEGIN_NV04(push, 2, 0x610, 3);
  PUSH_DATAh(push, vp_sem->offset);
  PUSH_DATA (push, vp_sem->offset);
  PUSH_DATA (push, 3);

  /* Write to the semaphore location */
  BEGIN_NV04(push, 2, 0x304, 1);
  PUSH_DATA (push, 1);
  PUSH_KICK (push);

  /* Wait for the semaphore to get written */
  BEGIN_NV04(push, 4, 0x10, 4);
  PUSH_DATAh(push, vp_sem->offset);
  PUSH_DATA (push, vp_sem->offset);
  PUSH_DATA (push, 3);
  PUSH_DATA (push, 1); /* wait for sem == 3 */
  PUSH_KICK (push);

  copy_buffer(push, frames[0], output);

  fprintf(stderr, "%x\n", *(uint32_t *)vp_sem->map);

  sleep(1);

  fprintf(stderr, "%x\n", *(uint32_t *)vp_sem->map);

  write(1, output->map, 0xaa000);
  for (i = 0; i < 0x55000; i += 2) {
    write(1, output->map + 0xaa000 + i, 1);
  }
  for (i = 0; i < 0x55000; i += 2) {
    write(1, output->map + 0xaa000 + i + 1, 1);
  }

  return 0;
}
