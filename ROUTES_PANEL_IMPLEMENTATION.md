# Ford Openpilot Routes Panel - Hardware Video Decoder Implementation

## Overview

This implementation provides a complete hardware-accelerated video playback solution for the Ford Openpilot routes panel, featuring:

- **Hardware-accelerated video decoding** using existing ffmpeg libraries
- **Modern UI design** matching SidebarBP styling
- **Route management** with pagination and on-demand loading
- **macOS-specific optimizations** for VideoToolbox hardware acceleration

## Key Features

### 1. Hardware Video Decoder (`bp_hardware_video_decoder.h/.cc`)

- **Platform-specific hardware acceleration**:
  - macOS: VideoToolbox (AV_HWDEVICE_TYPE_VIDEOTOOLBOX)
  - Linux: CUDA (AV_HWDEVICE_TYPE_CUDA)
- **Thread-safe decoding** with proper frame synchronization
- **Automatic fallback** to software decoding if hardware fails
- **Memory-efficient** frame processing with hardware-to-CPU transfer

### 2. Modern Routes Panel UI

- **SidebarBP-inspired styling** with gradient backgrounds and modern cards
- **Route grouping by date** with elegant date headers
- **Larger thumbnails** (320x180) for better visual appeal
- **Card-based layout** with hover effects and smooth transitions
- **Action buttons** for play, video selection, and deletion

### 3. Pagination and Performance

- **On-demand loading** with configurable page size (default: 10 routes)
- **Efficient thumbnail generation** with async processing
- **Memory management** with proper cleanup and caching
- **Smooth scrolling** with optimized rendering

### 4. Route Management

- **Video selection menu** with multiple camera options
- **Route deletion** with confirmation dialogs
- **File type indicators** (Video, RLog, QLog)
- **Route statistics** (segments, size, duration, distance)

## Technical Implementation

### Hardware Decoder Architecture

```cpp
class BPHardwareVideoDecoder : public QObject {
    // FFmpeg contexts
    AVFormatContext *m_formatContext;
    AVCodecContext *m_codecContext;
    AVBufferRef *m_hwDeviceContext;

    // Hardware acceleration
    AVPixelFormat m_hwPixelFormat;
    static constexpr AVHWDeviceType HW_DEVICE_TYPE;

    // Threading
    QThread *m_decodeThread;
    QAtomicInt m_isPlaying;
    QAtomicInt m_shouldStop;
};
```

### Modern UI Components

```cpp
struct RouteInfo {
    QString baseName;
    QString timestamp;
    QDate date;                    // For grouping
    QString thumbnailPath;         // Cached thumbnail
    // ... other fields
};

// Route grouping and pagination
QHash<QDate, QVector<RouteInfo>> routesByDate;
int currentPage = 0;
int routesPerPage = 10;
```

### Styling System

The implementation uses a comprehensive CSS-like styling system:

```css
QWidget.route-card {
    background-color: #242424;
    border-radius: 15px;
    border: 2px solid transparent;
}
QWidget.route-card:hover {
    border-color: #404040;
    background-color: #2a2a2a;
}
```

## Build Configuration

### FFmpeg Integration

The build system automatically links the appropriate ffmpeg libraries:

```python
# In SConscript
base_libs = [common, messaging, visionipc, transformations,
             'm', 'OpenCL', 'ssl', 'crypto', 'pthread',
             'avformat', 'avcodec', 'avutil', 'x264'] + qt_env["LIBS"]
```

### Platform Detection

```cpp
#ifdef __APPLE__
  static constexpr AVHWDeviceType HW_DEVICE_TYPE = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#else
  static constexpr AVHWDeviceType HW_DEVICE_TYPE = AV_HWDEVICE_TYPE_CUDA;
#endif
```

## Usage Examples

### Basic Video Playback

```cpp
// Initialize decoder
BPHardwareVideoDecoder *decoder = new BPHardwareVideoDecoder(this);

// Initialize with video file
if (decoder->initialize("/path/to/video.hevc")) {
    decoder->play();
}

// Connect to signals
connect(decoder, &BPHardwareVideoDecoder::positionChanged,
        this, &MyClass::updatePosition);
```

### Route Management

```cpp
// Create modern route widget
void BPRoutesPanel::createModernRouteWidget(const RouteInfo &route) {
    auto routeCard = new QWidget();
    routeCard->setProperty("class", "route-card");

    // Add thumbnail, info, and action buttons
    // ...
}
```

## Performance Optimizations

### 1. Hardware Acceleration
- **VideoToolbox on macOS** for optimal performance
- **Automatic fallback** to software decoding
- **Efficient memory management** with hardware frame contexts

### 2. UI Performance
- **Pagination** reduces initial load time
- **Async thumbnail generation** prevents UI blocking
- **Efficient caching** with route info cache
- **Lazy loading** of route details

### 3. Memory Management
- **Proper cleanup** of FFmpeg resources
- **Thread-safe operations** with mutexes
- **Automatic garbage collection** of UI elements

## Error Handling

### Hardware Decoder Errors
```cpp
void BPHardwareVideoDecoder::onErrorOccurred(const QString &error) {
    qWarning() << "Video decoder error:" << error;
    // Automatic fallback to software decoding
}
```

### UI Error Handling
```cpp
if (!decoder->initialize(videoPath)) {
    emit errorOccurred("Failed to initialize hardware decoder");
    return false;
}
```

## Testing

A test utility is provided (`bp_hardware_video_test.cc`) to verify hardware decoder functionality:

```bash
# Compile test
g++ -o test_decoder bp_hardware_video_test.cc bp_hardware_video_decoder.cc \
    -I/path/to/ffmpeg/include -L/path/to/ffmpeg/lib \
    -lavcodec -lavformat -lavutil -lx264 -lQt5Core -lQt5Widgets

# Run test
./test_decoder /path/to/test/video.hevc
```

## Future Enhancements

### Planned Features
1. **Route replay integration** with `tools/replay`
2. **Advanced video filters** and effects
3. **Route comparison** and analysis tools
4. **Export functionality** for route data
5. **Real-time streaming** support

### Performance Improvements
1. **GPU-accelerated thumbnail generation**
2. **Parallel route processing**
3. **Advanced caching strategies**
4. **Memory pool optimization**

## Troubleshooting

### Common Issues

1. **Hardware decoder not available**
   - Check VideoToolbox/CUDA installation
   - Verify ffmpeg hardware support
   - Check system requirements

2. **Video playback issues**
   - Verify video file format (HEVC/H.264)
   - Check file permissions
   - Test with software decoder fallback

3. **UI performance issues**
   - Reduce `routesPerPage` value
   - Clear thumbnail cache
   - Check available memory

### Debug Information

Enable debug logging:
```cpp
av_log_set_level(AV_LOG_DEBUG);
```

Check hardware device availability:
```cpp
AVHWDeviceType *deviceTypes;
int count = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE);
```

## Conclusion

This implementation provides a robust, hardware-accelerated video playback solution for the Ford Openpilot routes panel. The modern UI design, efficient pagination, and comprehensive error handling make it suitable for production use while maintaining excellent performance on macOS systems.

The modular architecture allows for easy extension and customization, while the hardware acceleration ensures smooth video playback even with high-resolution content.
