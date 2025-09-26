#ifndef __APPLE__
#include "qcom_decoder.h"

#include <assert.h>
#include "third_party/linux/include/v4l2-controls.h"
#include <linux/videodev2.h>

#include "common/swaglog.h"
#include "common/util.h"

// echo "0xFFFF" > /sys/kernel/debug/msm_vidc/debug_level

static void copyBuffer(VisionBuf *src_buf, VisionBuf *dst_buf) {
  // Copy Y plane with proper stride handling
  for (int i = 0; i < src_buf->height; i++) {
    memcpy((uint8_t*)dst_buf->y + i * dst_buf->stride, 
           (uint8_t*)src_buf->y + i * src_buf->stride, 
           src_buf->width);
  }
  // Copy UV plane with proper stride handling
  for (int i = 0; i < src_buf->height / 2; i++) {
    memcpy((uint8_t*)dst_buf->uv + i * dst_buf->stride, 
           (uint8_t*)src_buf->uv + i * src_buf->stride, 
           src_buf->width);
  }
}

static void request_buffers(int fd, v4l2_buf_type buf_type, unsigned int count) {
  struct v4l2_requestbuffers reqbuf = {
    .count = count,
    .type = buf_type,
    .memory = V4L2_MEMORY_USERPTR
  };
  int ret = util::safe_ioctl(fd, VIDIOC_REQBUFS, &reqbuf, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_REQBUFS failed: %d", ret);
  }
}

MsmVidc::~MsmVidc() {
  if (fd > 0) {
    v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    util::safe_ioctl(fd, VIDIOC_STREAMOFF, &out_type, nullptr);
    v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    util::safe_ioctl(fd, VIDIOC_STREAMOFF, &cap_type, nullptr);
    close(fd);
  }
}

bool MsmVidc::init(const char* dev, size_t width, size_t height, uint64_t codec) {
  LOG("Initializing msm_vidc device %s", dev);
  this->w = width;
  this->h = height;
  this->fd = open(dev, O_RDWR, 0);
  if (fd < 0) {
    LOGE("failed to open video device %s", dev);
    return false;
  }
  
  // Query device capabilities first
  struct v4l2_capability cap;
  if (util::safe_ioctl(fd, VIDIOC_QUERYCAP, &cap, nullptr) < 0) {
    LOGE("VIDIOC_QUERYCAP failed");
    close(fd);
    return false;
  }
  
  subscribeEvents();
  v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  setPlaneFormat(out_type, V4L2_PIX_FMT_HEVC);
  setFPS(FPS);
  request_buffers(fd, out_type, OUTPUT_BUFFER_COUNT);
  
  // Don't start output stream immediately, wait for capture setup
  // util::safe_ioctl(fd, VIDIOC_STREAMON, &out_type, nullptr);
  
  restartCapture();
  
  // Now start output stream after capture is ready
  int ret = util::safe_ioctl(fd, VIDIOC_STREAMON, &out_type, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_STREAMON OUTPUT failed: %d", ret);
    return false;
  }
  
  setupPolling();

  this->initialized = true;
  return true;
}

VisionBuf* MsmVidc::decodeFrame(AVPacket *pkt, VisionBuf *buf) {
  assert(initialized && (pkt != nullptr) && (buf != nullptr));

  this->frame_ready = false;
  this->current_output_buf = buf;
  bool sent_packet = false;

  while (!this->frame_ready) {
    if (!sent_packet) {
      int buf_index = getBufferUnlocked();
      if (buf_index >= 0) {
        assert(buf_index < out_buf_cnt);
        sendPacket(buf_index, pkt);
        sent_packet = true;
      }
    }

    if (poll(pfd, nfds, 100) < 0) {  // 100ms timeout
      if (errno == EINTR) {
        continue;
      }
      LOGE("poll() error: %d", errno);
      return nullptr;
    }

    if (VisionBuf* result = processEvents()) {
      return result;
    }
  }

  return buf;
}

VisionBuf* MsmVidc::processEvents() {
  for (int idx = 0; idx < nfds; idx++) {
    short revents = pfd[idx].revents;
    if (!revents) continue;

    if (idx == ev[EV_VIDEO]) {
      if (revents & (POLLIN | POLLRDNORM)) {
        VisionBuf *result = handleCapture();
        if (result == this->current_output_buf) {
          this->frame_ready = true;
        }
      }
      if (revents & (POLLOUT | POLLWRNORM)) {
        handleOutput();
      }
      if (revents & POLLPRI) {
        handleEvent();
      }
    } else {
      LOGE("Unexpected event on fd %d", pfd[idx].fd);
    }
  }
  return nullptr;
}

VisionBuf* MsmVidc::handleCapture() {
  struct v4l2_buffer buf = {0};
  struct v4l2_plane planes[1] = {0};
  buf.type          = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  buf.memory        = V4L2_MEMORY_USERPTR;
  buf.m.planes      = planes;
  buf.length        = 1;
  
  int ret = util::safe_ioctl(this->fd, VIDIOC_DQBUF, &buf, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_DQBUF CAPTURE failed: %d", ret);
    return nullptr;
  }

  if (this->reconfigure_pending || buf.m.planes[0].bytesused == 0) {
    return nullptr;
  }

  copyBuffer(&cap_bufs[buf.index], this->current_output_buf);
  queueCaptureBuffer(buf.index);
  return this->current_output_buf;
}

bool MsmVidc::subscribeEvents() {
  for (uint32_t event : subscriptions) {
    struct v4l2_event_subscription sub = { .type = event};
    int ret = util::safe_ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub, nullptr);
    if (ret < 0) {
      LOGE("VIDIOC_SUBSCRIBE_EVENT failed for event 0x%x", event);
    }
  }
  return true;
}

bool MsmVidc::setPlaneFormat(enum v4l2_buf_type type, uint32_t fourcc) {
  struct v4l2_format fmt = {.type = type};
  struct v4l2_pix_format_mplane *pix = &fmt.fmt.pix_mp;
  *pix = {
    .width = (__u32)this->w,
    .height = (__u32)this->h,
    .pixelformat = fourcc
  };
  
  int ret = util::safe_ioctl(fd, VIDIOC_S_FMT, &fmt, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_S_FMT failed for type %d", type);
    return false;
  }
  
  if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
    this->out_buf_size = pix->plane_fmt[0].sizeimage;
    int ion_size = this->out_buf_size * OUTPUT_BUFFER_COUNT;
    this->out_buf.allocate(ion_size);
    for (int i = 0; i < OUTPUT_BUFFER_COUNT; i++) {
      this->out_buf_off[i] = i * this->out_buf_size;
      this->out_buf_addr[i] = (char *)this->out_buf.addr + this->out_buf_off[i];
      this->out_buf_flag[i] = false;
    }
    LOGD("Set output buffer size to %d, count %d, addr %p", this->out_buf_size, OUTPUT_BUFFER_COUNT, this->out_buf.addr);
  } else if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
    request_buffers(this->fd, type, CAPTURE_BUFFER_COUNT);
    
    // Re-get format to see what driver actually set
    ret = util::safe_ioctl(fd, VIDIOC_G_FMT, &fmt, nullptr);
    if (ret < 0) {
      LOGE("VIDIOC_G_FMT failed");
      return false;
    }
    
    const __u32 y_size    = pix->plane_fmt[0].sizeimage;
    const __u32 y_stride  = pix->plane_fmt[0].bytesperline;
    
    // Ensure stride is at least width for proper alignment
    __u32 actual_stride = std::max(y_stride, pix->width);
    
    for (int i = 0; i < CAPTURE_BUFFER_COUNT; i++) {
      size_t uv_offset = (size_t)actual_stride * pix->height;
      size_t required = uv_offset + (actual_stride * pix->height / 2);
      size_t alloc_size = std::max<size_t>(y_size, required);
      this->cap_bufs[i].allocate(alloc_size);
      this->cap_bufs[i].init_yuv(pix->width, pix->height, actual_stride, uv_offset);
    }
    LOGD("Set capture buffer size to %d, count %d, stride %d, addr %p",
      pix->plane_fmt[0].sizeimage, CAPTURE_BUFFER_COUNT, actual_stride, this->cap_bufs[0].addr);
  }
  return true;
}

bool MsmVidc::setFPS(uint32_t fps) {
  struct v4l2_streamparm streamparam = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
    .parm.output.timeperframe = {1, fps}
  };
  int ret = util::safe_ioctl(fd, VIDIOC_S_PARM, &streamparam, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_S_PARM failed");
  }
  return ret >= 0;
}

bool MsmVidc::restartCapture() {
  // stop if already initialized
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  if (this->initialized) {
    LOGD("Restarting capture, flushing buffers...");
    util::safe_ioctl(this->fd, VIDIOC_STREAMOFF, &type, nullptr);
    struct v4l2_requestbuffers reqbuf = {.type = type, .memory = V4L2_MEMORY_USERPTR};
    util::safe_ioctl(this->fd, VIDIOC_REQBUFS, &reqbuf, nullptr);
    for (size_t i = 0; i < CAPTURE_BUFFER_COUNT; ++i) {
      this->cap_bufs[i].free();
      this->cap_buf_flag[i] = false;
      cap_bufs[i].~VisionBuf();
      new (&cap_bufs[i]) VisionBuf();
    }
  }
  
  // setup, start and queue capture buffers
  setDBP();
  setPlaneFormat(type, V4L2_PIX_FMT_NV12);
  
  int ret = util::safe_ioctl(this->fd, VIDIOC_STREAMON, &type, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_STREAMON CAPTURE failed: %d", ret);
    return false;
  }
  
  for (size_t i = 0; i < CAPTURE_BUFFER_COUNT; ++i) {
    queueCaptureBuffer(i);
  }

  return true;
}

bool MsmVidc::queueCaptureBuffer(int i) {
  struct v4l2_buffer buf = {0};
  struct v4l2_plane planes[1] = {0};

  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  buf.memory = V4L2_MEMORY_USERPTR;
  buf.index = i;
  buf.m.planes = planes;
  buf.length = 1;
  
  planes[0].m.userptr     = (unsigned long)this->cap_bufs[i].addr;
  planes[0].length        = this->cap_bufs[i].len;
  planes[0].reserved[0]   = this->cap_bufs[i].fd;
  planes[0].reserved[1]   = 0;
  planes[0].bytesused     = this->cap_bufs[i].len;
  planes[0].data_offset   = 0;
  
  int ret = util::safe_ioctl(this->fd, VIDIOC_QBUF, &buf, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_QBUF CAPTURE failed: %d", ret);
    return false;
  }
  this->cap_buf_flag[i] = true;
  return true;
}

bool MsmVidc::queueOutputBuffer(int i, size_t size) {
  struct v4l2_buffer buf = {0};
  struct v4l2_plane planes[1] = {0};

  buf.type                = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  buf.memory              = V4L2_MEMORY_USERPTR;
  buf.index               = i;
  buf.m.planes            = planes;
  buf.length              = 1;
  
  planes[0].m.userptr     = (unsigned long)this->out_buf_off[i];
  planes[0].length        = this->out_buf_size;
  planes[0].reserved[0]   = this->out_buf.fd;
  planes[0].reserved[1]   = 0;
  planes[0].bytesused     = size;
  planes[0].data_offset   = 0;
  
  // Ensure alignment
  assert((this->out_buf_off[i] & 0xfff) == 0);
  assert(this->out_buf_size % 4096 == 0);

  int ret = util::safe_ioctl(this->fd, VIDIOC_QBUF, &buf, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_QBUF OUTPUT failed: %d", ret);
    return false;
  }
  this->out_buf_flag[i] = true;
  return true;
}

bool MsmVidc::setDBP() {
  struct v4l2_ext_control control[2] = {0};
  struct v4l2_ext_controls controls = {0};
  control[0].id           = V4L2_CID_MPEG_VIDC_VIDEO_STREAM_OUTPUT_MODE;
  control[0].value        = 1;
  control[1].id           = V4L2_CID_MPEG_VIDC_VIDEO_DPB_COLOR_FORMAT;
  control[1].value        = 0;
  controls.count          = 2;
  controls.ctrl_class     = V4L2_CTRL_CLASS_MPEG;
  controls.controls       = control;
  int ret = util::safe_ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_S_EXT_CTRLS failed");
  }
  return ret >= 0;
}

bool MsmVidc::setupPolling() {
  pfd[EV_VIDEO] = {fd, POLLIN | POLLOUT | POLLWRNORM | POLLRDNORM | POLLPRI, 0};
  ev[EV_VIDEO] = EV_VIDEO;
  nfds = 1;
  return true;
}

bool MsmVidc::sendPacket(int buf_index, AVPacket *pkt) {
  assert(buf_index >= 0 && buf_index < out_buf_cnt);
  assert(pkt != nullptr && pkt->data != nullptr && pkt->size > 0);
  
  memset(this->out_buf_addr[buf_index], 0, this->out_buf_size);
  uint8_t * data = (uint8_t *)this->out_buf_addr[buf_index];
  memcpy(data, pkt->data, pkt->size);
  return queueOutputBuffer(buf_index, pkt->size);
}

int MsmVidc::getBufferUnlocked() {
  for (int i = 0; i < this->out_buf_cnt; i++) {
    if (!out_buf_flag[i]) {
      return i;
    }
  }
  return -1;
}

bool MsmVidc::handleOutput() {
  struct v4l2_buffer buf = {0};
  struct v4l2_plane planes[1];
  buf.type      = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  buf.memory    = V4L2_MEMORY_USERPTR;
  buf.m.planes  = planes;
  buf.length    = 1;
  
  int ret = util::safe_ioctl(this->fd, VIDIOC_DQBUF, &buf, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_DQBUF OUTPUT failed: %d", ret);
    return false;
  }
  this->out_buf_flag[buf.index] = false;
  return true;
}

bool MsmVidc::handleEvent() {
  struct v4l2_event event = {0};
  int ret = util::safe_ioctl(this->fd, VIDIOC_DQEVENT, &event, nullptr);
  if (ret < 0) {
    LOGE("VIDIOC_DQEVENT failed: %d", ret);
    return false;
  }
  
  switch (event.type) {
    case V4L2_EVENT_MSM_VIDC_PORT_SETTINGS_CHANGED_INSUFFICIENT: {
      unsigned int *ptr     = (unsigned int *)event.u.data;
      unsigned int height   = ptr[0];
      unsigned int width    = ptr[1];
      this->w               = width;
      this->h               = height;
      LOGD("Port Reconfig received insufficient, new size %ux%u, flushing capture bufs...", width, height);
      struct v4l2_decoder_cmd dec;
      dec.flags = V4L2_QCOM_CMD_FLUSH_CAPTURE;
      dec.cmd = V4L2_QCOM_CMD_FLUSH;
      util::safe_ioctl(this->fd, VIDIOC_DECODER_CMD, &dec, nullptr);
      this->reconfigure_pending = true;
      LOGD("Waiting for flush done event to reconfigure capture queue");
      break;
    }

    case V4L2_EVENT_MSM_VIDC_FLUSH_DONE: {
      unsigned int *ptr   = (unsigned int *)event.u.data;
      unsigned int flags  = ptr[0];
      if (flags & V4L2_QCOM_CMD_FLUSH_CAPTURE) {
        if (this->reconfigure_pending) {
          this->restartCapture();
          this->reconfigure_pending = false;
        }
      }
      break;
    }
    default:
      break;
  }
  return true;
}

#endif
