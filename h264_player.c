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

#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>

VdpGetProcAddress *vdp_get_proc_address;

VdpDecoderCreate *vdp_decoder_create;
VdpDecoderDestroy *vdp_decoder_destroy;
VdpDecoderRender *vdp_decoder_render;

VdpVideoSurfaceCreate *vdp_video_surface_create;
VdpVideoSurfaceDestroy *vdp_video_surface_destroy;
VdpVideoSurfaceGetBitsYCbCr *vdp_video_surface_get_bits_ycbcr;

VdpOutputSurfaceCreate *vdp_output_surface_create;
VdpOutputSurfaceDestroy *vdp_output_surface_destroy;
VdpOutputSurfaceGetBitsNative *vdp_output_surface_get_bits_native;

VdpVideoMixerCreate *vdp_video_mixer_create;
VdpVideoMixerDestroy *vdp_video_mixer_destroy;
VdpVideoMixerRender *vdp_video_mixer_render;

VdpPresentationQueueCreate *vdp_presentation_queue_create;
VdpPresentationQueueDestroy *vdp_presentation_queue_destroy;
VdpPresentationQueueDisplay *vdp_presentation_queue_display;
VdpPresentationQueueBlockUntilSurfaceIdle *vdp_presentation_queue_block_until_surface_idle;
VdpPresentationQueueGetTime *vdp_presentation_queue_get_time;
VdpPresentationQueueTargetCreateX11 *vdp_presentation_queue_target_create_x11;

int read_bit(void *addr, int *bit_offset) {
  int offt = *bit_offset;
  addr += offt / 8;
  offt %= 8;
  *bit_offset = *bit_offset + 1;
  return ((*(char *)addr) >> (7 - offt)) & 1;
}

uint64_t read_bits(void *addr, int *bit_offset, int n) {
  int i;
  uint64_t ret = 0;
  for (i = 0; i < n; i++) {
    ret <<= 1;
    ret |= read_bit(addr, bit_offset);
  }
  return ret;
}

uint64_t ue(void *addr, int *bit_offset) {
  int leadingZeroBits = -1;
  int b;
  for (b = 0; !b; leadingZeroBits++) {
    b = read_bit(addr, bit_offset);
  }
  int ret = (1 << leadingZeroBits) - 1 + read_bits(addr, bit_offset, leadingZeroBits);
  return ret;
}

int64_t se(void *addr, int *bit_offset) {
  int codeNum = ue(addr, bit_offset);
  return ((codeNum % 2 == 1) ? 1 : -1) * (codeNum / 2 + codeNum % 2);
}

void mark(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
void mark(const char *fmt, ...) {
  va_list ap;
  char buf[100] = {0};
  va_start(ap, fmt);
  int len = vsnprintf(buf, 99, fmt, ap);
  va_end(ap);

  int fd = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY);
  if (fd == -1) return;

  assert(len == write(fd, buf, len));
  close(fd);
}

int main(int argc, char **argv) {
  int width = 1280, height = 544;
  Display *display = XOpenDisplay(NULL);
  /*
  Window root = XDefaultRootWindow(display);
  Window window = XCreateSimpleWindow(
      display, root, 0, 0, 1280, 544, 0, 0, 0);
  XSelectInput(display, window, ExposureMask | KeyPressMask);
  XMapWindow(display, window);
  XSync(display, 0);
  */

  VdpDevice dev;

  mark("vdp_device_create_x11\n");
  int ret = vdp_device_create_x11(display, 0, &dev, &vdp_get_proc_address);
  assert(ret == VDP_STATUS_OK);

#define get(id, func) \
  ret = vdp_get_proc_address(dev, id, (void **)&func); \
  assert(ret == VDP_STATUS_OK);

  get(VDP_FUNC_ID_DECODER_CREATE, vdp_decoder_create);
  get(VDP_FUNC_ID_DECODER_DESTROY, vdp_decoder_destroy);
  get(VDP_FUNC_ID_DECODER_RENDER, vdp_decoder_render);

  get(VDP_FUNC_ID_VIDEO_MIXER_CREATE, vdp_video_mixer_create);
  get(VDP_FUNC_ID_VIDEO_MIXER_DESTROY, vdp_video_mixer_destroy);
  get(VDP_FUNC_ID_VIDEO_MIXER_RENDER, vdp_video_mixer_render);

  get(VDP_FUNC_ID_VIDEO_SURFACE_CREATE, vdp_video_surface_create);
  get(VDP_FUNC_ID_VIDEO_SURFACE_DESTROY, vdp_video_surface_destroy);
  get(VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR, vdp_video_surface_get_bits_ycbcr);

  get(VDP_FUNC_ID_OUTPUT_SURFACE_CREATE, vdp_output_surface_create);
  get(VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY, vdp_output_surface_destroy);
  get(VDP_FUNC_ID_OUTPUT_SURFACE_GET_BITS_NATIVE, vdp_output_surface_get_bits_native);

  get(VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE, vdp_presentation_queue_create);
  get(VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY, vdp_presentation_queue_destroy);
  get(VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY, vdp_presentation_queue_display);
  get(VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_CREATE_X11, vdp_presentation_queue_target_create_x11);
  get(VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE, vdp_presentation_queue_block_until_surface_idle);
  get(VDP_FUNC_ID_PRESENTATION_QUEUE_GET_TIME, vdp_presentation_queue_get_time);

#undef get

  VdpDecoder dec;
  VdpVideoSurface video[16];
  VdpOutputSurface output;
  VdpPresentationQueue queue;
  VdpPresentationQueueTarget target;
  VdpVideoMixer mixer;

  VdpVideoMixerFeature mixer_features[] = {
  };
  VdpVideoMixerParameter mixer_params[] = {
    VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH,
    VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
    VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE
  };
  int zero = 0;
  const void *mixer_param_vals[] = {
    &width,
    &height,
    &zero
  };

  mark("vdp_decoder_create\n");
  ret = vdp_decoder_create(dev, VDP_DECODER_PROFILE_H264_MAIN, 1280, 544, 6, &dec);
  assert(ret == VDP_STATUS_OK);

  int i;
  for (i = 0; i < 1; i++) {
    mark("vdp_video_surface_create: %d\n", i);
    ret = vdp_video_surface_create(dev, VDP_CHROMA_TYPE_420, 1280, 544, &video[i]);
    assert(ret == VDP_STATUS_OK);
    mark(" <-- %d\n", video[i]);
  }


  mark("vdp_output_surface_create\n");
  ret = vdp_output_surface_create(dev, VDP_RGBA_FORMAT_B8G8R8A8, 1280, 544, &output);
  assert(ret == VDP_STATUS_OK);

/*
  mark("vdp_presentation_queue_target_create_x11\n");
  ret = vdp_presentation_queue_target_create_x11(dev, window, &target);
  assert(ret == VDP_STATUS_OK);

  mark("vdp_presentation_queue_create\n");
  ret = vdp_presentation_queue_create(dev, target, &queue);
  assert(ret == VDP_STATUS_OK);
*/
  mark("vdp_video_mixer_create\n");
  ret = vdp_video_mixer_create(dev, sizeof(mixer_features)/sizeof(mixer_features[0]), mixer_features, sizeof(mixer_params)/sizeof(mixer_params[0]), mixer_params, mixer_param_vals, &mixer);
  assert(ret == VDP_STATUS_OK);


  assert(argc > 1);
  int fd = open(argv[1], O_RDONLY);
  struct stat statbuf;
  assert(fstat(fd, &statbuf) == 0);
  void *addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
  void *orig_addr = addr;

  mark("mmap file addr: 0x%p size: 0x%lx\n", addr, statbuf.st_size);

  //printf("mmap'd file of size: %ld\n", statbuf.st_size);

  VdpPictureInfoH264 info = {
    .slice_count = 1,
    .field_order_cnt = { 65536, 65536 },
    .is_reference = 1,
    .frame_num = -1,
    .field_pic_flag = 0,
    .bottom_field_flag = 0,
    .num_ref_frames = 6,
    .mb_adaptive_frame_field_flag = 0,
    .constrained_intra_pred_flag = 0,
    .weighted_pred_flag = 0,
    .weighted_bipred_idc = 0,
    .frame_mbs_only_flag = 1,
    .transform_8x8_mode_flag = 0,
    .chroma_qp_index_offset = 0,
    .second_chroma_qp_index_offset = 0,
    .pic_init_qp_minus26 = 0,
    .num_ref_idx_l0_active_minus1 = 0,
    .num_ref_idx_l1_active_minus1 = 0,
    .log2_max_frame_num_minus4 = 5,
    .pic_order_cnt_type = 0,
    .log2_max_pic_order_cnt_lsb_minus4 = 6,
    .delta_pic_order_always_zero_flag = 0,
    .direct_8x8_inference_flag = 1,
    .entropy_coding_mode_flag = 1,
    .pic_order_present_flag = 0,
    .deblocking_filter_control_present_flag = 1,
    .redundant_pic_cnt_present_flag = 0,
  };
  int j;
  for (j = 0; j < 6; ++j) {
    int k;

    for (k = 0; k < 16; ++k)
      info.scaling_lists_4x4[j][k] = 16;
  }

  for (j = 0; j < 2; ++j) {
    int k;

    for (k = 0; k < 64; ++k)
      info.scaling_lists_8x8[j][k] = 16;
  }

  for (j = 0; j < 16; ++j)
    info.referenceFrames[j].surface = VDP_INVALID_HANDLE;


  mark("vdp_presentation_queue_get_time\n");
  /*VdpTime t;
  ret = vdp_presentation_queue_get_time(queue, &t);
  assert(ret == VDP_STATUS_OK);
  */
  //fprintf(stderr, "Start time: %ld\n", t);

  int vframe = 0;

  while ((addr - orig_addr) < statbuf.st_size) {
    int size = ntohl(*(int *)addr);
    addr += 4;
    int nal_type = (*(char *)addr) & 0x1F;
    int nal_ref_idc = (*(char *)addr) >> 5;
    if (nal_type != 1 && nal_type != 5) {
      //fprintf(stderr, "Skipping NAL type %d, size: %d\n", nal_type, size);
      addr += size;
      continue;
    }
    //fprintf(stderr, "Processing NAL type %d, ref_idc: %d, size: %d\n", nal_type, nal_ref_idc, size);

    int bit_offset = 8;
    ue(addr, &bit_offset);
    int slice_type = ue(addr, &bit_offset);
    mark("nal_type: %d, ref_idc: %d, size: %d, slice_type: %d\n", nal_type, nal_ref_idc, size, slice_type);
    //fprintf(stderr, "Slice type: %d\n", slice_type);
    ue(addr, &bit_offset);
    info.frame_num = read_bits(addr, &bit_offset, info.log2_max_frame_num_minus4 + 4);
    if (nal_type == 5) {
      ue(addr, &bit_offset);
      info.frame_num = 0;
      for (j = 0; j < 16; ++j)
        info.referenceFrames[j].surface = VDP_INVALID_HANDLE;
    }

    uint32_t poc_lsb = read_bits(addr, &bit_offset, info.log2_max_pic_order_cnt_lsb_minus4 + 4);
    info.field_order_cnt[0] = (1 << 16) + poc_lsb;
    info.field_order_cnt[1] = (1 << 16) + poc_lsb;

    info.is_reference = nal_ref_idc != 0;

    VdpBitstreamBuffer buffer[2];
    static const char header[3] = {0, 0, 1};
    buffer[0].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
    buffer[0].bitstream = header;
    buffer[0].bitstream_bytes = sizeof(header);
    buffer[1].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
    buffer[1].bitstream = addr;
    buffer[1].bitstream_bytes = size;
    mark("vdp_decoder_render: %d\n", video[vframe]);
    ret = vdp_decoder_render(dec, video[vframe], (void*)&info, 2, buffer);
    assert(ret == VDP_STATUS_OK);

    mark("vdp_video_mixer_render\n");
    ret = vdp_video_mixer_render(
        mixer,
        VDP_INVALID_HANDLE, NULL,
        VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME,
        0, NULL,
        video[vframe],
        0, NULL,
        NULL,
        output,
        NULL,
        NULL,
        0, NULL);
    assert(ret == VDP_STATUS_OK);
/*
    t += 1000000000ULL / 24;
    mark("vdp_presentation_queue_display\n");
    ret = vdp_presentation_queue_display(queue, output, 1280, 544, t);
    assert(ret == VDP_STATUS_OK);
    */

    addr += size;

    uint32_t pitches[1] = {1280 * 4/*, 640, 640*/};
    uint8_t *data[1];
    for (i = 0; i < 1; i++) {
      data[i] = malloc(1280 * 544 * 4);// (i ? 4 : 1));
      assert(data[i]);
    }
    ret = vdp_output_surface_get_bits_native(output, NULL, (void **)data, pitches);
    assert(ret == VDP_STATUS_OK);

    /* for (i = 0; i < 1280 * 544; i++) { */
    /*   uint32_t *pos = data[0]; */
    /*   pos += i; */
    /*   write(1, (void *)pos, 3); */
    /* } */

    if (info.is_reference) {
      for (j = 5; j > 0; --j)
        memcpy(&info.referenceFrames[j], &info.referenceFrames[j-1], sizeof(info.referenceFrames[0]));
      info.referenceFrames[0].surface = video[vframe];
      memcpy(info.referenceFrames[0].field_order_cnt, info.field_order_cnt, 2 * sizeof(uint32_t));
      info.referenceFrames[0].frame_idx = info.frame_num;
      info.referenceFrames[0].top_is_reference = 1;
      info.referenceFrames[0].bottom_is_reference = 1;
    }
    vframe = (vframe + 1) % 16;
    break;
  }

  return 0;
}
