Based on our conversation, here's a comprehensive summary of your route panel design requirements:

## 📋 **Route Panel Design Requirements Summary**

### **🎯 Core Functionality Requirements**

#### **1. Hardware-Accelerated Video Playback**
- **Replace QMediaPlayer** with custom hardware decoder for better performance
- **Use existing FFmpeg libraries** (`libavcodec.a`, `libavformat.a`, `libavutil.a`, `libx264.a`)
- **Support VideoToolbox** hardware acceleration on macOS
- **Maintain Qt5 compatibility** and follow existing bluepilot UI patterns

#### **2. Modern UI Overhaul**
- **Match SidebarBP styling** throughout the interface
- **Card-based layout** for route display
- **Group routes by day** with proper date headers
- **Larger thumbnails** for better visual appeal
- **On-demand loading** as user scrolls (pagination)

### **📱 QCOM2 Device Optimization**

#### **Screen Orientation & Sizing**
- **Portrait mode optimization** for QCOM2 devices (1080x2160 resolution)
- **Touch-friendly interface** with larger buttons and touch targets
- **Proper screen margins** and centering for portrait orientation
- **Modal sizing** optimized for 6" touch screen

### **🎬 Video Modal Design**

#### **Modal Layout Structure**
- **Popup modal** for selected route viewer (make it fullscreen with a close button in the top right and make sure to rotate for qcom2)
- **Horizontal split layout**: Video player on left, camera stack on right (show the video types as buttons that change whats shown in the video player)
- **Default camera**: Show `fcamera.hevc` by default when opening modal
- **Camera selection stack** on the right side of video player
- **Delete button placement** at bottom of modal (not in route list) leave room for other future buttons

#### **Camera Stack Panel**
- **Fixed width panel** (200px) on the right side
- **Camera selection buttons**: Front Camera, Driver Camera, Wide Camera
- **SidebarBP styling** for all buttons and panels
- **Touch-optimized** button sizes and spacing

### **👆 User Interaction Design**

#### **Route Row Interaction**
- **Entire route row clickable** (not just individual buttons)
- **Tap anywhere on route card** to open video modal
- **Default behavior**: Open modal with front camera video (stopped)

#### **Automatic Loading**
- **Initial routes load** automatically when panel is shown
- **Scroll-based loading**: More routes load automatically when scrolling near bottom
- **Remove manual "Load More" button** in favor of automatic loading
- **Support both mouse wheel and touch scrolling**

### **🎨 Visual Design Requirements**

#### **SidebarBP Styling Consistency**
- **Color scheme**: `#2196F3` (blue), `#F44336` (red), `#242424` (cards), `#1a1a1a` (background)
- **Typography**: InterFont with proper weights (600 for buttons, DemiBold for headers)
- **Border radius**: 12px for cards, 20px for main modal
- **Spacing**: 30px margins, 20px gaps between elements
- **Gradients**: Background gradients matching SidebarBP

#### **Route Display**
- **Date grouping** with proper day names (not "EEEE" formatting issues)
- **Route information**: Timestamp, ID, duration, segments, size, trip miles
- **File type indicators**: Video, RLog, QLog availability (show as badges)
- **Thumbnail size**: 320x180px for better visibility

### **🔧 Technical Implementation**

#### **Event Handling**
- **Event filters** for route card clicks and scroll detection
- **QVariant support** for storing route information on widgets
- **Touch event conversion** to mouse events for compatibility
- **Keyboard shortcuts** in modal (Space, Escape, Arrow keys, F for fullscreen)

#### **Performance**
- **Background loading** of routes using QtConcurrent
- **Thumbnail generation** with proper caching
- **Hardware acceleration** for video decoding
- **Efficient pagination** with on-demand loading

### **📋 Summary of Key Design Principles**

1. **Touch-First Design**: Optimized for 6" QCOM2 touch screen
2. **SidebarBP Consistency**: Visual and interaction patterns match existing UI
3. **Intuitive Navigation**: Tap route → open modal → view video
4. **Automatic Loading**: Seamless scrolling experience without manual pagination
5. **Hardware Performance**: Leverage existing FFmpeg libraries for optimal video playback
6. **Modal-Centric**: Route details and video playback in dedicated modal interface
7. **Camera Flexibility**: Easy switching between different camera views
8. **Clean Organization**: Routes grouped by date with clear visual hierarchy

This design creates a **modern, touch-optimized route management interface** that feels native to the SidebarBP ecosystem while providing powerful video playback capabilities optimized for QCOM2 hardware.
