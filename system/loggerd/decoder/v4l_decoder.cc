#include <cassert>
#include <string>
#include <sys/ioctl.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

#include "system/loggerd/decoder/v4l_decoder.h"
#include "common/util.h"
#include "common/timing.h"
#include "common/swaglog.h"

#include "third_party/libyuv/include/libyuv.h"
#include "third_party/linux/include/msm_media_info.h"
#include "third_party/linux/include/v4l2-controls.h"
#include <linux/videodev2.h>

#define V4L2_QCOM_BUF_FLAG_CODECCONFIG 0x00020000
#define V4L2_QCOM_BUF_FLAG_EOS 0x02000000

static void dequeue_buffer(int fd, v4l2_buf_type buf_type, unsigned int *index=NULL, 
                          unsigned int *bytesused=NULL, unsigned int *flags=NULL, 
                          struct timeval *timestamp=NULL) {
  v4l2_plane plane = {0};
  v4l2_buffer v4l_buf = {
    .type = buf_type,
    .memory = V4L2_MEMORY_USERPTR,
    .m = { .planes = &plane, },
    .length = 1,
  };
  util::safe_ioctl(fd, VIDIOC_DQBUF, &v4l_buf, "VIDIOC_DQBUF failed");

  if (index) *index = v4l_buf.index;
  if (bytesused) *bytesused = v4l_buf.m.planes[0].bytesused;
  if (flags) *flags = v4l_buf.flags;
  if (timestamp) *timestamp = v4l_buf.timestamp;
  assert(v4l_buf.m.planes[0].data_offset == 0);
}

static void queue_buffer(int fd, v4l2_buf_type buf_type, unsigned int index, 
                        uint8_t *data, int len, struct timeval timestamp={}) {
  v4l2_plane plane = {
    .length = (unsigned int)len,
    .m = { .userptr = (unsigned long)data, },
    .bytesused = (uint32_t)len,
  };

  v4l2_buffer v4l_buf = {
    .type = buf_type,
    .index = index,
    .memory = V4L2_MEMORY_USERPTR,
    .m = { .planes = &plane, },
    .length = 1,
    .flags = V4L2_BUF_FLAG_TIMESTAMP_COPY,
    .timestamp = timestamp
  };
  util::safe_ioctl(fd, VIDIOC_QBUF, &v4l_buf, "VIDIOC_QBUF failed");
}

static void request_buffers(int fd, v4l2_buf_type buf_type, unsigned int count) {
  struct v4l2_requestbuffers reqbuf = {
    .type = buf_type,
    .memory = V4L2_MEMORY_USERPTR,
    .count = count
  };
  util::safe_ioctl(fd, VIDIOC_REQBUFS, &reqbuf, "VIDIOC_REQBUFS failed");
}

void V4LDecoder::decode_handler(V4LDecoder *d) {
  util::set_thread_name("v4l-decode");

  // POLLIN is decoded output, POLLOUT is compressed input ready
  struct pollfd pfd;
  pfd.events = POLLIN | POLLOUT;
  pfd.fd = d->fd;
  bool exit = false;

  while (!exit && d->is_open) {
    int rc = poll(&pfd, 1, 1000);
    if (rc < 0) {
      if (errno != EINTR) {
        LOGE("decoder poll failed (%d - %d)", rc, errno);
      }
      continue;
    } else if (rc == 0) {
      continue; // timeout is normal when no data
    }

    // Handle decoded frames (output)
    if (pfd.revents & POLLIN) {
      unsigned int bytesused, flags, index;
      struct timeval timestamp;
      dequeue_buffer(d->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, &index, 
                     &bytesused, &flags, &timestamp);
      
      d->buf_out[index].sync(VISIONBUF_SYNC_FROM_DEVICE);
      
      // Process decoded frame if not EOS
      if (!(flags & V4L2_QCOM_BUF_FLAG_EOS)) {
        if (bytesused > 0 && d->frame_callback) {
          // Convert NV12 to YUV420P for callback
          uint8_t *nv12_y = (uint8_t*)d->buf_out[index].addr;
          uint8_t *nv12_uv = nv12_y + d->stride * d->out_height;
          
          // Get timestamp from queue
          uint64_t frame_ts = 0;
          if (!d->timestamp_queue.empty()) {
            frame_ts = d->timestamp_queue.front();
            d->timestamp_queue.pop();
          }
          
          DecodedFrame frame = {
            .y = nv12_y,
            .u = nv12_uv,  // Note: NV12 interleaved UV
            .v = nv12_uv + 1,
            .width = d->out_width,
            .height = d->out_height,
            .stride_y = d->stride,
            .stride_uv = d->stride,
            .timestamp_us = frame_ts,
            .frame_id = d->frame_count++,
            .keyframe = (flags & V4L2_BUF_FLAG_KEYFRAME) != 0
          };
          
          d->frame_callback(frame);
        }
      } else {
        exit = true;
      }
      
      // Requeue buffer
      queue_buffer(d->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, index,
                   (uint8_t*)d->buf_out[index].addr, d->buf_out[index].len);
    }

    // Handle input buffer availability
    if (pfd.revents & POLLOUT) {
      unsigned int index;
      dequeue_buffer(d->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, &index);
      d->free_buf_in.push(index);
    }
  }
}

V4LDecoder::V4LDecoder(int width, int height, FrameCallback callback, bool is_h265)
    : VideoDecoder(width, height, callback), is_hevc(is_h265) {
  
  fd = HANDLE_EINTR(open("/dev/v4l/by-path/platform-aa00000.qcom_vidc-video-index0", O_RDWR|O_NONBLOCK));
  if (fd < 0) {
    LOGE("Failed to open video decode device");
    return;
  }

  struct v4l2_capability cap;
  util::safe_ioctl(fd, VIDIOC_QUERYCAP, &cap, "VIDIOC_QUERYCAP failed");
  LOGD("opened decoder device %s %s = %d", cap.driver, cap.card, fd);
  
  // Set output format (compressed stream)
  struct v4l2_format fmt_in = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
    .fmt = {
      .pix_mp = {
        .width = (unsigned int)width,
        .height = (unsigned int)height,
        .pixelformat = is_hevc ? V4L2_PIX_FMT_HEVC : V4L2_PIX_FMT_H264,
        .field = V4L2_FIELD_ANY,
        .colorspace = V4L2_COLORSPACE_DEFAULT,
      }
    }
  };
  util::safe_ioctl(fd, VIDIOC_S_FMT, &fmt_in, "VIDIOC_S_FMT input failed");
  compressed_buf_size = fmt_in.fmt.pix_mp.plane_fmt[0].sizeimage;

  // Set capture format (decoded frames)
  struct v4l2_format fmt_out = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
    .fmt = {
      .pix_mp = {
        .width = (unsigned int)width,
        .height = (unsigned int)height,
        .pixelformat = V4L2_PIX_FMT_NV12,
        .field = V4L2_FIELD_ANY,
        .colorspace = V4L2_COLORSPACE_REC709,
      }
    }
  };
  util::safe_ioctl(fd, VIDIOC_S_FMT, &fmt_out, "VIDIOC_S_FMT output failed");
  
  stride = fmt_out.fmt.pix_mp.plane_fmt[0].bytesperline;
  int frame_size = fmt_out.fmt.pix_mp.plane_fmt[0].sizeimage;

  LOGD("Decoder buffers - compressed: %d, decoded frame: %d, stride: %d", 
       compressed_buf_size, frame_size, stride);

  // Request buffers
  request_buffers(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, BUF_IN_COUNT);
  request_buffers(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, BUF_OUT_COUNT);

  // Allocate input buffers for compressed data
  for (int i = 0; i < BUF_IN_COUNT; i++) {
    buf_in[i].allocate(compressed_buf_size);
    free_buf_in.push(i);
  }

  // Allocate output buffers for decoded frames
  for (int i = 0; i < BUF_OUT_COUNT; i++) {
    buf_out[i].allocate(frame_size);
  }
}

void V4LDecoder::decoder_open() {
  if (!fd || is_open) return;

  // Start streaming
  v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  util::safe_ioctl(fd, VIDIOC_STREAMON, &buf_type, "VIDIOC_STREAMON input failed");
  
  buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  util::safe_ioctl(fd, VIDIOC_STREAMON, &buf_type, "VIDIOC_STREAMON output failed");

  // Queue output buffers
  for (int i = 0; i < BUF_OUT_COUNT; i++) {
    queue_buffer(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, i,
                 (uint8_t*)buf_out[i].addr, buf_out[i].len);
  }

  is_open = true;
  frame_count = 0;
  
  // Start decode thread
  decode_thread = std::thread(V4LDecoder::decode_handler, this);
}

int V4LDecoder::decode_frame(uint8_t* data, int len, uint64_t timestamp_us, bool keyframe) {
  if (!is_open || !data || len <= 0) return -1;

  // Get free input buffer
  int buf_idx = free_buf_in.pop();
  if (buf_idx < 0) {
    LOGE("No free input buffers");
    return -1;
  }

  // Copy compressed data to buffer
  int copy_len = std::min(len, compressed_buf_size);
  memcpy(buf_in[buf_idx].addr, data, copy_len);
  buf_in[buf_idx].sync(VISIONBUF_SYNC_TO_DEVICE);

  // Store timestamp for later association
  timestamp_queue.push(timestamp_us);

  // Queue buffer for decoding
  struct timeval tv = {
    .tv_sec = (long)(timestamp_us / 1000000),
    .tv_usec = (long)(timestamp_us % 1000000)
  };
  
  queue_buffer(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, buf_idx,
               (uint8_t*)buf_in[buf_idx].addr, copy_len, tv);
  
  return 0;
}

void V4LDecoder::flush() {
  if (!is_open) return;
  
  // Send EOS to flush decoder
  struct v4l2_decoder_cmd cmd = { .cmd = V4L2_DEC_CMD_STOP };
  util::safe_ioctl(fd, VIDIOC_DECODER_CMD, &cmd, "VIDIOC_DECODER_CMD stop failed");
}

void V4LDecoder::decoder_close() {
  if (!is_open) return;
  
  is_open = false;
  flush();
  
  // Wait for thread to finish
  if (decode_thread.joinable()) {
    decode_thread.join();
  }

  // Stop streaming
  v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  util::safe_ioctl(fd, VIDIOC_STREAMOFF, &buf_type, "VIDIOC_STREAMOFF input");
  
  buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  util::safe_ioctl(fd, VIDIOC_STREAMOFF, &buf_type, "VIDIOC_STREAMOFF output");

  // Clear buffers
  while (!timestamp_queue.empty()) timestamp_queue.pop();
  while (!free_buf_in.empty()) free_buf_in.pop();
}

V4LDecoder::~V4LDecoder() {
  decoder_close();
  
  if (fd >= 0) {
    request_buffers(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 0);
    request_buffers(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 0);
    close(fd);
  }

  for (int i = 0; i < BUF_IN_COUNT; i++) {
    buf_in[i].free();
  }
  for (int i = 0; i < BUF_OUT_COUNT; i++) {
    buf_out[i].free();
  }
}
