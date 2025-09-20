#include "system/loggerd/decoder/v4l_decoder.h"

#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
#include <iomanip>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>

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
    .memory = V4L2_MEMORY_MMAP,
    .m = { .planes = &plane, },
    .length = 1,
  };
  
  if (ioctl(fd, VIDIOC_DQBUF, &v4l_buf) < 0) {
    if (errno != EAGAIN) {
      LOGE("VIDIOC_DQBUF failed: %s", strerror(errno));
    }
    return;
  }

  if (index) *index = v4l_buf.index;
  if (bytesused) *bytesused = v4l_buf.m.planes[0].bytesused;
  if (flags) *flags = v4l_buf.flags;
  if (timestamp) *timestamp = v4l_buf.timestamp;
}

static void queue_buffer(int fd, v4l2_buf_type buf_type, unsigned int index,
                        unsigned int length=0, unsigned int bytesused=0, 
                        struct timeval timestamp={}) {
  v4l2_plane plane = {
    .length = length,
    .bytesused = bytesused,
  };

  v4l2_buffer v4l_buf = {
    .type = buf_type,
    .index = index,
    .memory = V4L2_MEMORY_MMAP,
    .m = { .planes = &plane, },
    .length = 1,
    .flags = static_cast<__u32>((timestamp.tv_sec || timestamp.tv_usec) ? V4L2_BUF_FLAG_TIMESTAMP_COPY : 0),
    .timestamp = timestamp
  };
  
  if (ioctl(fd, VIDIOC_QBUF, &v4l_buf) < 0) {
    LOGE("VIDIOC_QBUF failed: %s", strerror(errno));
  }
}

static void request_buffers(int fd, v4l2_buf_type buf_type, unsigned int count) {
  struct v4l2_requestbuffers reqbuf = {
    .type = buf_type,
    .memory = V4L2_MEMORY_MMAP,
    .count = count
  };
  if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
    LOGE("VIDIOC_REQBUFS failed for type %d, count %d: %s", buf_type, count, strerror(errno));
  } else {
    std::cout << "V4L Decoder: VIDIOC_REQBUFS succeeded for type " << buf_type << ", requested " << count << ", allocated " << reqbuf.count << std::endl;
  }
}

void V4LDecoder::decode_handler(V4LDecoder *d) {
  util::set_thread_name("v4l-decode");

  struct pollfd pfd;
  pfd.fd = d->fd;
  pfd.events = POLLIN | POLLOUT;
  bool exit = false;

  while (!exit && d->is_open) {
    int rc = poll(&pfd, 1, 100);
    if (rc < 0) {
      if (errno != EINTR) {
        LOGE("decoder poll failed (%d - %s)", rc, strerror(errno));
      }
      continue;
    } else if (rc == 0) {
      continue;
    }

    // Handle decoded frames
    if (pfd.revents & POLLIN) {
      unsigned int bytesused, flags, index;
      struct timeval timestamp;
      dequeue_buffer(d->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, &index,
                     &bytesused, &flags, &timestamp);

      if (!(flags & V4L2_QCOM_BUF_FLAG_EOS) && bytesused > 0) {
        if (d->frame_callback && index < BUF_OUT_COUNT) {
          uint8_t *nv12_y = (uint8_t*)d->buf_out[index];
          uint8_t *nv12_uv = nv12_y + d->stride * d->out_height;

          uint64_t frame_ts = 0;
          if (!d->timestamp_queue.empty()) {
            frame_ts = d->timestamp_queue.front();
            d->timestamp_queue.pop();
          }

          DecodedFrame frame = {
            .y = nv12_y,
            .u = nv12_uv,      // NV12 interleaved UV
            .v = nv12_uv + 1,  // V starts at offset 1
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
      } else if (flags & V4L2_QCOM_BUF_FLAG_EOS) {
        exit = true;
      }

      // Requeue buffer
      queue_buffer(d->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, index, d->frame_size, 0);
    }

    // Handle input buffer availability
    if (pfd.revents & POLLOUT) {
      unsigned int index;
      dequeue_buffer(d->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, &index);
      if (index < BUF_IN_COUNT) {
        d->free_buf_in.push(index);
      }
    }
  }
}

V4LDecoder::V4LDecoder(int width, int height, FrameCallback callback, bool is_h265)
    : VideoDecoder(width, height, callback), is_hevc(is_h265) {

  // Try different device paths - decoder is index0
  const char* device_paths[] = {
    "/dev/v4l/by-path/platform-aa00000.qcom_vidc-video-index0",
    "/dev/video32",
    "/dev/video/decoder",
    "/dev/video0"
  };
  
  for (const char* path : device_paths) {
    fd = HANDLE_EINTR(open(path, O_RDWR | O_NONBLOCK));
    if (fd >= 0) {
      std::cout << "V4L Decoder: Opened decoder device: " << path << std::endl;
      break;
    }
  }
  
  if (fd < 0) {
    LOGE("Failed to open any video decode device");
    return;
  }

  struct v4l2_capability cap;
  if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
    LOGE("VIDIOC_QUERYCAP failed: %s", strerror(errno));
    close(fd);
    fd = -1;
    return;
  }
  std::cout << "V4L Decoder: Device driver=" << cap.driver << ", card=" << cap.card << std::endl;

  // Verify we have the right decoder device
  if (strcmp((const char *)cap.driver, "msm_vidc_driver") != 0) {
    LOGE("Wrong driver: expected msm_vidc_driver, got %s", cap.driver);
    close(fd);
    fd = -1;
    return;
  }

  // Set decoded output format first (like encoder does)
  struct v4l2_format fmt_out = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
    .fmt = {
      .pix_mp = {
        .width = (unsigned int)width,
        .height = (unsigned int)height,
        .pixelformat = V4L2_PIX_FMT_NV12,
        .field = V4L2_FIELD_ANY,
        .colorspace = V4L2_COLORSPACE_470_SYSTEM_BG,
        .num_planes = 1,
      }
    }
  };

  if (ioctl(fd, VIDIOC_S_FMT, &fmt_out) < 0) {
    LOGE("VIDIOC_S_FMT output failed: %s", strerror(errno));
    close(fd);
    fd = -1;
    return;
  }
  std::cout << "V4L Decoder: Output format set: " << width << "x" << height << ", pixelformat=0x" << std::hex << fmt_out.fmt.pix_mp.pixelformat << std::dec << std::endl;

  // Set compressed input format
  struct v4l2_format fmt_in = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
    .fmt = {
      .pix_mp = {
        .width = (unsigned int)width,
        .height = (unsigned int)height,
        .pixelformat = is_hevc ? V4L2_PIX_FMT_HEVC : V4L2_PIX_FMT_H264,
        .field = V4L2_FIELD_ANY,
        .colorspace = V4L2_COLORSPACE_DEFAULT,
        .num_planes = 1,
      }
    }
  };

  if (ioctl(fd, VIDIOC_S_FMT, &fmt_in) < 0) {
    LOGE("VIDIOC_S_FMT input failed: %s", strerror(errno));
    close(fd);
    fd = -1;
    return;
  }
  std::cout << "V4L Decoder: Input format set: " << width << "x" << height << ", pixelformat=0x" << std::hex << fmt_in.fmt.pix_mp.pixelformat << std::dec << std::endl;
  compressed_buf_size = fmt_in.fmt.pix_mp.plane_fmt[0].sizeimage;
  if (compressed_buf_size == 0) {
    compressed_buf_size = 2 * 1024 * 1024;  // Default 2MB
  }

  stride = fmt_out.fmt.pix_mp.plane_fmt[0].bytesperline;
  frame_size = fmt_out.fmt.pix_mp.plane_fmt[0].sizeimage;
  out_width = width;
  out_height = height;

  std::cout << "V4L Decoder: Decoder configured - compressed: " << compressed_buf_size << " bytes, frame: " << frame_size << " bytes, stride: " << stride << std::endl;
  std::cout << "V4L Decoder: Format details - out_width: " << out_width << ", out_height: " << out_height << std::endl;

  // Set decoder controls (only those that are supported for decoder)
  struct v4l2_control ctrls[] = {
    { .id = V4L2_CID_MPEG_VIDC_VIDEO_OUTPUT_ORDER, .value = V4L2_MPEG_VIDC_VIDEO_OUTPUT_ORDER_DISPLAY },
  };
  for (auto ctrl : ctrls) {
    int ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
    if (ret < 0) {
      std::cout << "V4L Decoder: Failed to set control " << ctrl.id << ": " << strerror(errno) << std::endl;
    } else {
      std::cout << "V4L Decoder: Successfully set control " << ctrl.id << std::endl;
    }
  }

  // Request buffers
  request_buffers(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, BUF_IN_COUNT);
  request_buffers(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, BUF_OUT_COUNT);

  // Map buffers
  for (int i = 0; i < BUF_IN_COUNT; i++) {
    v4l2_plane plane = {0};
    v4l2_buffer buf = {
      .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
      .memory = V4L2_MEMORY_MMAP,
      .index = (unsigned int)i,
      .m = { .planes = &plane },
      .length = 1
    };
    
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) >= 0) {
      buf_in[i] = mmap(NULL, compressed_buf_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, buf.m.planes[0].m.mem_offset);
      if (buf_in[i] == MAP_FAILED) {
        LOGE("Failed to mmap input buffer %d", i);
        buf_in[i] = nullptr;
      } else {
        free_buf_in.push(i);
      }
    }
  }

  for (int i = 0; i < BUF_OUT_COUNT; i++) {
    v4l2_plane plane = {0};
    v4l2_buffer buf = {
      .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
      .memory = V4L2_MEMORY_MMAP,
      .index = (unsigned int)i,
      .m = { .planes = &plane },
      .length = 1
    };
    
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) >= 0) {
      buf_out[i] = mmap(NULL, frame_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, buf.m.planes[0].m.mem_offset);
      if (buf_out[i] == MAP_FAILED) {
        LOGE("Failed to mmap output buffer %d", i);
        buf_out[i] = nullptr;
      }
    }
  }
}

void V4LDecoder::decoder_open() {
  if (fd < 0) {
    LOGE("V4L decoder_open: invalid fd");
    return;
  }
  if (is_open) {
    return;
  }

  // Queue all output buffers first
  for (int i = 0; i < BUF_OUT_COUNT; i++) {
    queue_buffer(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, i, frame_size, 0);
  }

  // Start streaming
  v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  if (ioctl(fd, VIDIOC_STREAMON, &buf_type) < 0) {
    LOGE("VIDIOC_STREAMON output failed: %s", strerror(errno));
    return;
  }

  buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  if (ioctl(fd, VIDIOC_STREAMON, &buf_type) < 0) {
    LOGE("VIDIOC_STREAMON capture failed: %s", strerror(errno));
    buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ioctl(fd, VIDIOC_STREAMOFF, &buf_type);
    return;
  }

  is_open = true;
  frame_count = 0;

  // Start decode thread
  decode_thread = std::thread(V4LDecoder::decode_handler, this);
  std::cout << "V4L Decoder: Decoder opened successfully" << std::endl;
}

int V4LDecoder::decode_frame(uint8_t* data, int len, uint64_t timestamp_us, bool keyframe) {
  if (!is_open || !data || len <= 0) return -1;

  // Get free input buffer
  int buf_idx = free_buf_in.pop();
  if (buf_idx < 0 || buf_idx >= BUF_IN_COUNT) {
    LOGE("No free input buffers");
    return -1;
  }

  // Copy data to mmap'd buffer
  int copy_len = std::min(len, compressed_buf_size);
  if (buf_in[buf_idx]) {
    memcpy(buf_in[buf_idx], data, copy_len);
  }

  // Store timestamp
  timestamp_queue.push(timestamp_us);

  // Queue buffer
  struct timeval tv = {
    .tv_sec = (long)(timestamp_us / 1000000),
    .tv_usec = (long)(timestamp_us % 1000000)
  };

  queue_buffer(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, buf_idx, 
               compressed_buf_size, copy_len, tv);

  return 0;
}

void V4LDecoder::flush() {
  if (!is_open) return;

  struct v4l2_decoder_cmd cmd = { .cmd = V4L2_DEC_CMD_STOP };
  ioctl(fd, VIDIOC_DECODER_CMD, &cmd);
}

void V4LDecoder::decoder_close() {
  if (!is_open) return;

  is_open = false;
  flush();

  if (decode_thread.joinable()) {
    decode_thread.join();
  }

  // Stop streaming
  v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  ioctl(fd, VIDIOC_STREAMOFF, &buf_type);
  buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  ioctl(fd, VIDIOC_STREAMOFF, &buf_type);

  // Clear queues
  while (!timestamp_queue.empty()) timestamp_queue.pop();
  while (!free_buf_in.empty()) free_buf_in.pop();
}

V4LDecoder::~V4LDecoder() {
  decoder_close();

  // Unmap buffers
  for (int i = 0; i < BUF_IN_COUNT; i++) {
    if (buf_in[i] && buf_in[i] != MAP_FAILED) {
      munmap(buf_in[i], compressed_buf_size);
    }
  }
  for (int i = 0; i < BUF_OUT_COUNT; i++) {
    if (buf_out[i] && buf_out[i] != MAP_FAILED) {
      munmap(buf_out[i], frame_size);
    }
  }

  if (fd >= 0) {
    request_buffers(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 0);
    request_buffers(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 0);
    close(fd);
  }
}
