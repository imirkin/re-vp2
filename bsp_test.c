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
#include <fcntl.h>
#include <stdio.h>
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


int main() {
  struct nouveau_device *dev;
  struct nouveau_client *client;
  struct nouveau_object *channel;
  struct nouveau_object *bsp;
  struct nouveau_pushbuf *push;
  struct nouveau_bo *sem = NULL;
  struct nv04_fifo nv04_data = { .vram = 0xbeef0201, .gart = 0xbeef0202 };

  int fd, i;

  fd = open("/dev/dri/card0", O_RDWR);
  assert(fd);
  pipe_loader_drm_x_auth(fd);

  assert(!nouveau_device_wrap(fd, 0, &dev));
  assert(!nouveau_client_new(dev, &client));
  assert(!nouveau_object_new(&dev->object, 0, NOUVEAU_FIFO_CHANNEL_CLASS,
                             &nv04_data, sizeof(nv04_data), &channel));
  assert(!nouveau_pushbuf_new(client, channel, 2, 4096, 1, &push));
  assert(!nouveau_object_new(channel, 0xbeef74b0, 0x74b0, NULL, 0, &bsp));
  assert(!nouveau_bo_new(dev, NOUVEAU_BO_VRAM, 0, 0x1000, NULL, &sem));

  printf("bo offset: %lx\n", sem->offset);
  printf("bo handle: %x\n", sem->handle);

  assert(!nouveau_bo_map(sem, NOUVEAU_BO_RD, client));

  printf("bo map: %p\n", sem->map);

  /* Bind the BSP to the fifo */
  BEGIN_NV04(push, 1, 0, 1);
  PUSH_DATA (push, bsp->handle);

  /* Set the DMA channels */
  BEGIN_NV04(push, 1, 0x180, 11);
  for (i = 0; i < 11; i++)
    PUSH_DATA(push, nv04_data.vram);

  /* Set the semaphore */
  BEGIN_NV04(push, 1, 0x610, 3);
  PUSH_DATAh(push, sem->offset);
  PUSH_DATA (push, sem->offset);
  PUSH_DATA (push, 0xabce);

  /* Write abce to the semaphore location */
  BEGIN_NV04(push, 1, 0x304, 1);
  PUSH_DATA (push, 0x101);
  PUSH_KICK (push);

  uint32_t *map = sem->map;
  printf("%x %x %x %x\n", map[0], map[1], map[2], map[3]);
  usleep(10000);
  printf("%x %x %x %x\n", map[0], map[1], map[2], map[3]);
  return 0;
}
