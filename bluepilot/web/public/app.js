// BluePilot Routes - Web Application
// Complete rewrite matching old Qt panel behavior

class BluePilotRoutes {
  constructor() {
    this.API_BASE = window.location.origin;
    this.routes = [];
    this.routesHash = null; // Track routes state to prevent unnecessary re-renders
    this.currentRoute = null;
    this.currentSegment = 0;
    this.currentCamera = "front"; // Prefer front camera by default
    this.player = null; // h265web.js player instance
    this.hls = null; // HLS.js instance
    this.hevcSupported = false; // Will be set during init
    this.webglSupported = false; // Will be set during init
    this.isSafari = false; // Safari detection for playback strategy
    this.isFirefox = false; // Firefox detection for limited functionality warning
    this.retryCount = 0; // Track retry attempts for network errors
    this.maxRetries = 3; // Maximum retry attempts
    this.retryDelay = 1000; // Initial retry delay in ms
    this.isRetrying = false; // Flag to prevent multiple simultaneous retries
    this.lastPlaybackTime = 0; // Store playback time for camera switching sync

    // WebSocket support
    this.websocket = null;
    this.websocketSupported = false;
    this.websocketConnected = false;
    this.useWebSocket = false; // Will be determined during init
    this.isRetrying = false; // Flag to prevent multiple simultaneous retries

    // Fallback polling
    this.routesPollingInterval = null;
    this.statusPollingInterval = null;

    // FFmpeg Debug Mode
    this.debugMode = false;

    // Route download tracking
    this.routeDownloadHistory = [];
    this.routeDownloadTokens = new Set();
    this.routeDownloadTokenQueue = [];

    // View navigation
    this.currentView = "landing"; // 'landing', 'routes', 'system', 'params'
    this.paramsEditMode = false;
    this.allParams = {};

    // Params sorting state
    this.paramsSortColumn = "key"; // Default sort by parameter name
    this.paramsSortDirection = "asc"; // 'asc' or 'desc'

    this.init();
  }

  async init() {
    console.log("=".repeat(60));
    console.log("BluePilot Web App - Version: 2025-11-03-v2");
    console.log("Looking for parameter Type and Last Modified columns?");
    console.log(
      'Check for "Code version: 2025-11-03-v2" when viewing Parameters'
    );
    console.log("=".repeat(60));

    this.cacheElements();
    this.attachEventListeners();
    this.setupTimelineListeners();

    // Detect browser capabilities once at startup
    await this.detectBrowserCapabilities();

    // Initialize WebSocket support
    await this.initWebSocket();

    // Load initial data for landing page
    this.loadRoutes();
    this.loadInitialLandingPageData();

    // Setup fallback polling (will be disabled if WebSocket works)
    this.setupFallbackPolling();
  }

  async loadInitialLandingPageData() {
    // Load params count for landing page
    try {
      const response = await fetch(`${this.API_BASE}/api/params`);
      const data = await response.json();
      if (data.success) {
        this.allParams = data.params;
        this.$landingParamsCount.textContent = `${
          Object.keys(this.allParams).length
        } parameters`;
      }
    } catch (error) {
      console.error("Error loading initial params count:", error);
      this.$landingParamsCount.textContent = "Error loading";
    }
  }

  async detectBrowserCapabilities() {
    this.isSafari = this.detectSafari();
    this.isFirefox = this.detectFirefox();
    console.log("Safari browser detected:", this.isSafari);
    console.log("Firefox browser detected:", this.isFirefox);

    // Check for native HEVC support
    this.hevcSupported = await this.checkNativeHEVCSupport();

    // Check for WebGL support (for h265-web-player fallback)
    this.webglSupported = this.checkWebGLSupport();

    const playerAvailable =
      typeof Player !== "undefined" &&
      typeof Player.prototype.init === "function";

    console.log("=== Browser Capabilities Detected ===");
    console.log("HLS.js loaded:", typeof Hls !== "undefined");
    if (typeof Hls !== "undefined") {
      console.log("HLS.js version:", Hls.version || "unknown");
      console.log("HLS.js supported:", Hls.isSupported());
      if (!Hls.isSupported()) {
        console.warn("⚠️ HLS.js is loaded but not supported on this browser");
        console.warn("MediaSource API supported:", "MediaSource" in window);
        console.warn(
          "This may happen on older Safari versions or HTTP (non-HTTPS) contexts"
        );
      }
    } else {
      console.error(
        "❌ HLS.js NOT loaded! Check CDN connection or browser console for script loading errors"
      );
    }
    console.log("Native HEVC support:", this.hevcSupported);
    console.log("WebGL support:", this.webglSupported);
    console.log("h265-web-player available:", playerAvailable);
    console.log("Can play HEVC videos:", this.canPlayHEVC());

    // Show Firefox warning if detected and HEVC is not supported
    this.initFirefoxWarning();
  }

  detectSafari() {
    if (typeof navigator === "undefined") {
      return false;
    }
    const ua = navigator.userAgent;
    return /safari/i.test(ua) && !/chrome|chromium|crios|android/i.test(ua);
  }

  detectFirefox() {
    if (typeof navigator === "undefined") {
      return false;
    }
    const ua = navigator.userAgent;
    return /firefox/i.test(ua);
  }

  canPlayHEVC() {
    // Server-side remuxing is now implemented!
    // Raw HEVC files are converted to MP4 containers on-the-fly
    // All browsers with native HEVC support can play these

    return this.hevcSupported; // Use native browser HEVC support

    // Note: h265-web-player fallback is disabled for now
    // If needed, uncomment below to enable WebGL fallback:
    // const playerAvailable = typeof Player !== "undefined" &&
    //                        typeof Player.prototype.init === "function";
    // return this.hevcSupported || (this.webglSupported && playerAvailable);
  }

  /**
   * Robust native HLS detection (Safari iOS/macOS, and any future native HLS)
   * @returns {boolean} True if browser supports native HLS playback
   */
  canUseNativeHLS() {
    const v = document.createElement("video");
    return !!(
      v.canPlayType("application/vnd.apple.mpegurl") ||
      v.canPlayType("application/x-mpegURL")
    );
  }

  /**
   * Ensure a <video> element replaces the canvas path when doing native playback
   * @returns {HTMLVideoElement} The video element
   */
  ensureVideoElement() {
    if (this.$videoElement && this.$videoElement.tagName === "VIDEO") {
      return this.$videoElement;
    }

    // Remove canvas if present
    if (this.$videoCanvas && this.$videoCanvas.parentNode) {
      const videoWrapper = this.$videoCanvas.parentNode;

      // Create video element
      const video = document.createElement("video");
      video.id = "route-video";
      video.setAttribute("playsinline", ""); // iOS inline playback
      video.setAttribute("muted", ""); // allow programmatic play
      video.setAttribute("preload", "metadata"); // Load first frame only (thumbnail)
      video.controls = true;
      video.className = "video-element";
      video.style.width = "100%";
      video.style.height = "100%";
      video.style.backgroundColor = "#000";

      // Replace canvas with video
      videoWrapper.replaceChild(video, this.$videoCanvas);
      this.$videoElement = video;

      // Attach event listeners
      this.attachVideoListeners();
    }

    return this.$videoElement;
  }

  /**
   * iOS-safe, reliable seek helper
   * Pause-then-seek can prevent stalls across discontinuities on iOS
   * @param {number} sec - Target time in seconds
   */
  async seekToSeconds(sec) {
    if (!this.$videoElement) return;

    const v = this.$videoElement;

    // Pause first to prevent stalls on iOS
    try {
      v.pause();
    } catch (e) {
      console.warn("Pause before seek failed:", e);
    }

    // Wait for 'seeked' or 'canplay' event
    const once = (el, ev) =>
      new Promise((res) => {
        const handler = () => {
          res();
          el.removeEventListener(ev, handler);
        };
        el.addEventListener(ev, handler, { once: true });
      });

    v.currentTime = Math.max(0, sec);

    // Some iOS builds signal 'canplay' but not 'seeked' reliably after big jumps
    try {
      await Promise.race([
        once(v, "seeked"),
        once(v, "canplay"),
        new Promise((res) => setTimeout(res, 1000)), // Timeout after 1s
      ]);
    } catch (e) {
      console.warn("Seek event wait failed:", e);
    }

    // Resume playback
    try {
      await v.play();
    } catch (e) {
      // Ignore if user gesture required
      console.warn("Play after seek failed (may require user gesture):", e);
    }
  }

  /**
   * Attach all necessary event listeners to the video element
   * Keeps video event handling consistent across different playback paths
   */
  attachVideoListeners() {
    if (!this.$videoElement) return;

    // Prevent duplicate listeners
    const v = this.$videoElement;

    v.addEventListener("loadstart", () => {
      console.log("Video loadstart event");
      this.showVideoLoading();
    });

    v.addEventListener("loadedmetadata", () => {
      console.log("Video metadata loaded:", {
        duration: v.duration,
        videoWidth: v.videoWidth,
        videoHeight: v.videoHeight,
      });
    });

    v.addEventListener("canplay", () => {
      console.log("Video can play");

      // Sync to stored playback time when switching cameras
      if (this.lastPlaybackTime > 0) {
        console.log(`Seeking to synced time: ${this.lastPlaybackTime}s`);
        v.currentTime = this.lastPlaybackTime;
        this.lastPlaybackTime = 0; // Reset after use
      }

      this.hideVideoLoading();
    });

    v.addEventListener("playing", () => {
      console.log("Video is playing");
      this.hideVideoLoading();
    });

    v.addEventListener("ended", () => {
      console.log("Full route playback completed");
      this.showReplayOverlay();
    });

    v.addEventListener("timeupdate", () => {
      // Update timeline position indicator
      this.updateTimelinePosition();

      // Track which segment we're in during HLS playback for auto-reloading logs/cereal
      if (this.hls && v) {
        const currentTime = v.currentTime;
        const calculatedSegment = Math.floor(currentTime / 60); // Each segment is 60 seconds

        // If we've moved to a different segment, update and reload data
        if (
          calculatedSegment !== this.currentSegment &&
          calculatedSegment < this.currentRoute.totalSegments
        ) {
          this.currentSegment = calculatedSegment;

          // Update segment info display
          this.$segmentInfo.textContent = `Segment ${
            calculatedSegment + 1
          } of ${
            this.currentRoute.totalSegments
          } - ${this.currentCamera.toUpperCase()}`;

          // Auto-reload logs and cereal data if they were previously loaded
          if (this.currentLogMessages && this.currentLogMessages.length > 0) {
            this.loadLogs();
          }
          if (this.currentCerealData && this.currentCerealData.length > 0) {
            this.loadCerealData();
          }
        }
      }
    });

    v.addEventListener("error", (e) => this.handleVideoError(e));
  }

  /**
   * Convert UTC timestamp to browser's local timezone and format as time
   * @param {string} utcTimestamp - ISO format UTC timestamp (e.g., "2024-09-18T14:30:00" or "2024-09-18T14:30:00+00:00")
   * @returns {string} - Formatted time in 12-hour format (e.g., "2:30 PM")
   */
  formatLocalTime(utcTimestamp) {
    if (!utcTimestamp) return "";
    try {
      // If timestamp doesn't have timezone info, treat it as UTC by appending 'Z'
      let timestamp = utcTimestamp;
      if (!timestamp.includes("+") && !timestamp.endsWith("Z")) {
        timestamp = timestamp + "Z";
      }
      const date = new Date(timestamp);
      return date.toLocaleTimeString("en-US", {
        hour: "numeric",
        minute: "2-digit",
        hour12: true,
      });
    } catch (e) {
      console.error("Error formatting time:", e);
      return "";
    }
  }

  /**
   * Convert UTC timestamp to browser's local timezone and format as date
   * @param {string} utcTimestamp - ISO format UTC timestamp (e.g., "2024-09-18T14:30:00" or "2024-09-18T14:30:00+00:00")
   * @returns {string} - Formatted date (e.g., "Thursday - September 18th, 2024")
   */
  formatLocalDate(utcTimestamp) {
    if (!utcTimestamp) return "";
    try {
      // If timestamp doesn't have timezone info, treat it as UTC by appending 'Z'
      let timestamp = utcTimestamp;
      if (!timestamp.includes("+") && !timestamp.endsWith("Z")) {
        timestamp = timestamp + "Z";
      }
      const date = new Date(timestamp);
      const dayName = date.toLocaleDateString("en-US", { weekday: "long" });
      const monthName = date.toLocaleDateString("en-US", { month: "long" });
      const day = date.getDate();
      const year = date.getFullYear();

      // Add ordinal suffix
      let suffix = "th";
      if (day % 10 === 1 && day !== 11) suffix = "st";
      else if (day % 10 === 2 && day !== 12) suffix = "nd";
      else if (day % 10 === 3 && day !== 13) suffix = "rd";

      return `${dayName} - ${monthName} ${day}${suffix}, ${year}`;
    } catch (e) {
      console.error("Error formatting date:", e);
      return "";
    }
  }

  /**
   * Calculate elapsed time from UTC timestamp to now
   * @param {string} utcTimestamp - ISO format UTC timestamp
   * @returns {string} - Elapsed time string (e.g., "2 hours ago")
   */
  formatElapsedTime(utcTimestamp) {
    if (!utcTimestamp) return "";
    try {
      // If timestamp doesn't have timezone info, treat it as UTC by appending 'Z'
      let timestamp = utcTimestamp;
      if (!timestamp.includes("+") && !timestamp.endsWith("Z")) {
        timestamp = timestamp + "Z";
      }
      const date = new Date(timestamp);
      const now = new Date();
      const seconds = Math.floor((now - date) / 1000);

      if (seconds < 60) return "just now";

      const minutes = Math.floor(seconds / 60);
      if (minutes < 60)
        return `${minutes} minute${minutes !== 1 ? "s" : ""} ago`;

      const hours = Math.floor(minutes / 60);
      if (hours < 24) return `${hours} hour${hours !== 1 ? "s" : ""} ago`;

      const days = Math.floor(hours / 24);
      if (days < 7) return `${days} day${days !== 1 ? "s" : ""} ago`;

      const weeks = Math.floor(days / 7);
      return `${weeks} week${weeks !== 1 ? "s" : ""} ago`;
    } catch (e) {
      console.error("Error formatting elapsed time:", e);
      return "";
    }
  }

  cacheElements() {
    // Containers
    this.$loading = document.getElementById("loading");
    this.$error = document.getElementById("error");
    this.$empty = document.getElementById("empty");
    this.$routesContainer = document.getElementById("routes-container");
    this.$routesContent = document.getElementById("routes-content");

    // Storage Bar (New tethered design)
    this.$storageBar = document.getElementById("storage-bar");
    this.$storageText = document.getElementById("storage-text");
    this.$storageWarning = document.getElementById("storage-warning");
    this.$storagePreservedBar = document.getElementById(
      "storage-preserved-bar"
    );
    this.$storageRoutesBar = document.getElementById("storage-routes-bar");
    this.$storageCacheBar = document.getElementById("storage-cache-bar");
    this.$storageThresholdMarker = document.getElementById(
      "storage-threshold-marker"
    );
    this.$storagePreservedText = document.getElementById(
      "storage-preserved-text"
    );
    this.$storageRoutesText = document.getElementById("storage-routes-text");
    this.$storageCacheText = document.getElementById("storage-cache-text");

    // Storage Bar Buttons
    this.$storageClearCacheBtn = document.getElementById(
      "storage-clear-cache-btn"
    );
    this.$storageRefreshBtn = document.getElementById("storage-refresh-btn");

    // Legacy disk viz container (for backward compatibility)
    this.$diskVizContainer = this.$storageBar; // Point to new storage bar
    this.$diskVizStatsText = this.$storageText;
    this.$diskPreservedBar = this.$storagePreservedBar;
    this.$diskRoutesBar = this.$storageRoutesBar;
    this.$diskCacheBar = this.$storageCacheBar;
    this.$diskThresholdMarker = this.$storageThresholdMarker;
    this.$diskWarning = this.$storageWarning;
    this.$diskWarningText = null; // No separate warning text element
    this.$diskPreservedValue = this.$storagePreservedText;
    this.$diskRoutesValue = this.$storageRoutesText;
    this.$diskCacheValue = this.$storageCacheText;

    // Status overlay
    this.$statusOverlay = document.getElementById("status-overlay");
    this.$statusOverlayTitle = document.getElementById("status-overlay-title");
    this.$statusOverlayMessage = document.getElementById(
      "status-overlay-message"
    );
    this.$statusOverlayDetails = document.getElementById(
      "status-overlay-details"
    );
    this.$statusOverlayRetry = document.getElementById("status-overlay-retry");
    this.$statusOverlaySpinner = document.getElementById(
      "status-overlay-spinner"
    );
    this.$detailConnection = document.getElementById("detail-connection");
    this.$detailRateLimit = document.getElementById("detail-rate-limit");
    this.$detailLastUpdate = document.getElementById("detail-last-update");

    // Cellular warning
    this.$cellularWarning = document.getElementById("cellular-warning");
    this.$cellularWarningDetails = document.getElementById(
      "cellular-warning-details"
    );
    this.$cellularWarningClose = document.getElementById(
      "cellular-warning-close"
    );

    // Firefox warning
    this.$firefoxWarning = document.getElementById("firefox-warning");
    this.$firefoxWarningClose = document.getElementById(
      "firefox-warning-close"
    );

    // Header
    this.$deviceStatus = document.getElementById("device-status");
    this.$statusText = document.getElementById("status-text");
    this.$websocketIcon = document.getElementById("websocket-icon");

    // Header action buttons
    this.$headerMetricsBtn = document.getElementById("header-metrics-btn");
    this.$headerClearCacheBtn = document.getElementById(
      "header-clear-cache-btn"
    );
    this.$headerRefreshBtn = document.getElementById("header-refresh-btn");

    // Legacy header elements (removed from UI, kept for backwards compatibility)
    this.$routeCount = null; // Removed from header
    this.$totalSize = null; // Removed from header
    this.$metricsBtn = this.$headerMetricsBtn; // Point to new button
    this.$importBackupBtn = document.getElementById("import-backup-btn");
    this.$clearCacheBtn = this.$headerClearCacheBtn; // Point to new button
    this.$refreshBtn = this.$headerRefreshBtn; // Point to new button
    this.$retryBtn = document.getElementById("retry-btn");

    // Metrics modal
    this.$metricsModal = document.getElementById("metrics-modal");
    this.$closeMetricsBtn = document.getElementById("close-metrics-btn");
    this.$refreshMetricsBtn = document.getElementById("refresh-metrics-btn");

    // Parameter Value Viewer modal
    this.$paramValueModal = document.getElementById("param-value-modal");
    this.$closeParamValueBtn = document.getElementById("close-param-value-btn");
    this.$copyParamValueBtn = document.getElementById("copy-param-value-btn");
    this.$paramValueKey = document.getElementById("param-value-key");
    this.$paramValueType = document.getElementById("param-value-type");
    this.$paramValueCategory = document.getElementById("param-value-category");
    this.$paramValueText = document.getElementById("param-value-text");

    // Export & Backup modal
    this.$exportBackupModal = document.getElementById("export-backup-modal");
    this.$closeExportModalBtn = document.getElementById(
      "close-export-modal-btn"
    );
    this.$exportRouteName = document.getElementById("export-route-name");
    this.$exportRouteSegments = document.getElementById(
      "export-route-segments"
    );
    this.$exportRouteSize = document.getElementById("export-route-size");
    this.$exportRouteDuration = document.getElementById(
      "export-route-duration"
    );
    this.$cameraCheckboxes = document.querySelectorAll(".camera-checkbox");
    this.$downloadSelectedVideosBtn = document.getElementById(
      "download-selected-videos-btn"
    );
    this.$downloadAllVideosBtn = document.getElementById(
      "download-all-videos-btn"
    );
    this.$videoExportStatus = document.getElementById("video-export-status");
    this.$backupRouteBtn = document.getElementById("backup-route-btn");
    this.$backupSizeEstimate = document.getElementById("backup-size-estimate");
    this.$backupStatus = document.getElementById("backup-status");

    // Camera size displays
    this.$frontSize = document.getElementById("front-size");
    this.$wideSize = document.getElementById("wide-size");
    this.$driverSize = document.getElementById("driver-size");
    this.$lqSize = document.getElementById("lq-size");
    this.$allVideosSize = document.getElementById("all-videos-size");

    // Import backup modal
    this.$importBackupModal = document.getElementById("import-backup-modal");
    this.$closeImportModalBtn = document.getElementById(
      "close-import-modal-btn"
    );
    this.$selectImportFileBtn = document.getElementById(
      "select-import-file-btn"
    );
    this.$importFileInput = document.getElementById("import-file-input");
    this.$importFileName = document.getElementById("import-file-name");
    this.$importRouteBtn = document.getElementById("import-route-btn");
    this.$importStatus = document.getElementById("import-status");

    // Confirmation modal
    this.$confirmModal = document.getElementById("confirm-modal");
    this.$confirmModalTitle = document.getElementById("confirm-modal-title");
    this.$confirmModalMessage = document.getElementById(
      "confirm-modal-message"
    );
    this.$confirmModalCancelBtn = document.getElementById(
      "confirm-modal-cancel-btn"
    );
    this.$confirmModalConfirmBtn = document.getElementById(
      "confirm-modal-confirm-btn"
    );

    // Video player
    this.$videoPlayer = document.getElementById("video-player");
    this.$videoTitle = document.getElementById("video-title");
    this.$videoRouteId = document.getElementById("video-route-id");
    this.$playerRouteRange = document.getElementById("player-route-range");
    this.$videoCanvas = document.getElementById("video-canvas");
    this.$videoLoading = document.getElementById("video-loading");
    this.$videoReplay = document.getElementById("video-replay");
    this.$replayBtn = document.getElementById("replay-btn");
    this.$playerBackBtn = document.getElementById("player-back-btn");
    this.$segmentInfo = document.getElementById("segment-info");
    this.$cameraButtons = document.querySelectorAll(".camera-btn");

    // Player sidebar elements
    this.$playerStarBtn = document.getElementById("player-star-btn");
    this.$playerStarText = document.getElementById("player-star-text");
    this.$playerDeleteBtn = document.getElementById("player-delete-btn");

    // Stats elements
    this.$statDuration = document.getElementById("stat-duration");
    this.$statSegments = document.getElementById("stat-segments");
    this.$statSize = document.getElementById("stat-size");
    this.$statDistance = document.getElementById("stat-distance");
    this.$statAvgSpeed = document.getElementById("stat-avg-speed");
    this.$statTopSpeed = document.getElementById("stat-top-speed");
    this.$statOpEngaged = document.getElementById("stat-op-engaged");
    this.$statDisengagements = document.getElementById("stat-disengagements");
    this.$statAlerts = document.getElementById("stat-alerts");
    this.$statLaneChanges = document.getElementById("stat-lane-changes");
    this.$statLocationStart = document.getElementById("stat-location-start");
    this.$statLocationEnd = document.getElementById("stat-location-end");

    // Vehicle info panel elements
    this.$vehiclePanel = document.getElementById("vehicle-panel");
    this.$vehiclePanelHeader = document.getElementById("vehicle-panel-header");
    this.$vehiclePanelContent = document.getElementById(
      "vehicle-panel-content"
    );
    this.$vehiclePlatform = document.getElementById("vehicle-platform");
    this.$vehicleVin = document.getElementById("vehicle-vin");
    this.$vehicleBrand = document.getElementById("vehicle-brand");
    this.$vehicleSource = document.getElementById("vehicle-source");
    this.$firmwareTableBody = document.getElementById("firmware-table-body");

    // Timeline elements
    this.$routeTimeline = document.getElementById("route-timeline");
    this.$timelineCanvas = document.getElementById("timeline-canvas");
    this.$timelineTooltip = document.getElementById("timeline-tooltip");
    this.$timelinePositionIndicator = document.getElementById(
      "timeline-position-indicator"
    );
    this.$timelinePositionTime = document.getElementById(
      "timeline-position-time"
    );
    this.$timelineHoverIndicator = document.getElementById(
      "timeline-hover-indicator"
    );

    // Debug canvas element
    console.log("Canvas element found:", !!this.$videoCanvas);
    console.log("Canvas element:", this.$videoCanvas);
    if (this.$videoCanvas) {
      console.log(
        "Canvas width:",
        this.$videoCanvas.width,
        "height:",
        this.$videoCanvas.height
      );
      console.log(
        "Canvas style:",
        this.$videoCanvas.style.width,
        "x",
        this.$videoCanvas.style.height
      );
    }

    // Validate canvas element exists
    if (!this.$videoCanvas) {
      console.error("Canvas element not found! Video playback will not work.");
      console.error("Available elements with ID:");
      const allElements = document.querySelectorAll("[id]");
      allElements.forEach((el) => console.error("  -", el.id));
    }

    // Log panel elements
    this.$logPanel = document.getElementById("log-panel");
    this.$logPanelHeader = document.getElementById("log-panel-header");
    this.$logPanelContent = document.getElementById("log-panel-content");
    this.$logTypeButtons = document.querySelectorAll(".log-type-btn");
    this.$logLevelFilter = document.getElementById("log-level-filter");
    this.$logSyncCheckbox = document.getElementById("log-sync-checkbox");
    this.$logSearchInput = document.getElementById("log-search-input");
    this.$loadLogsBtn = document.getElementById("load-logs-btn");
    this.$stopLogsBtn = document.getElementById("stop-logs-btn");
    this.$reloadLogsBtn = document.getElementById("reload-logs-btn");
    this.$logViewerContainer = document.getElementById("log-viewer-container");
    this.$logMessages = document.getElementById("log-messages");
    this.$logLoading = document.getElementById("log-loading");
    this.$logStatus = document.getElementById("log-status");
    this.$logCount = document.getElementById("log-count");

    // Cereal panel elements
    this.$cerealPanel = document.getElementById("cereal-panel");
    this.$cerealPanelHeader = document.getElementById("cereal-panel-header");
    this.$cerealPanelContent = document.getElementById("cereal-panel-content");
    this.$loadCerealBtn = document.getElementById("load-cereal-btn");
    this.$stopCerealBtn = document.getElementById("stop-cereal-btn");
    this.$reloadCerealBtn = document.getElementById("reload-cereal-btn");
    this.$cerealMessageSelect = document.getElementById(
      "cereal-message-select"
    );
    this.$cerealSyncCheckbox = document.getElementById("cereal-sync-checkbox");
    this.$cerealViewerContainer = document.getElementById(
      "cereal-viewer-container"
    );
    this.$cerealDataTable = document.getElementById("cereal-data-table");
    this.$cerealDataBody = document.getElementById("cereal-data-body");
    this.$cerealLoading = document.getElementById("cereal-loading");
    this.$cerealLastUpdate = document.getElementById("cereal-last-update");
    this.$cerealMessageCount = document.getElementById("cereal-message-count");

    // FFmpeg Debug Panel elements
    this.$toggleDebugBtn = document.getElementById("toggle-debug-btn");
    this.$ffmpegDebugPanel = document.getElementById("ffmpeg-debug-panel");
    this.$ffmpegDebugContent = document.getElementById("ffmpeg-debug-content");
    this.$ffmpegDebugMessages = document.getElementById(
      "ffmpeg-debug-messages"
    );
    this.$clearDebugBtn = document.getElementById("clear-debug-btn");
    this.$debugAutoScroll = document.getElementById("debug-auto-scroll");

    // View containers
    this.$landingPage = document.getElementById("landing-page");
    this.$paramsManager = document.getElementById("params-manager");
    this.$headerTitle = document.getElementById("header-title");

    // Landing page cards
    this.$homeBtn = document.getElementById("home-btn");
    this.$cardRoutes = document.getElementById("card-routes");
    this.$cardParams = document.getElementById("card-params");
    this.$landingRouteCount = document.getElementById("landing-route-count");
    this.$landingTotalSize = document.getElementById("landing-total-size");
    this.$landingParamsCount = document.getElementById("landing-params-count");

    // Params manager
    this.$paramsSearch = document.getElementById("params-search");
    this.$paramsEditToggle = document.getElementById("params-edit-toggle");
    this.$paramsContent = document.querySelector(".params-content");

    // Routes status bar (now integrated into storage bar)
    this.$routesRouteCount = document.getElementById("routes-route-count");
    this.$routesTotalSize = document.getElementById("routes-total-size");
    this.$routesDiskCompact = document.getElementById("routes-disk-compact");
    this.$routesImportBtn = document.getElementById("routes-import-btn");
    this.$routesClearCacheBtn = document.getElementById(
      "routes-clear-cache-btn"
    );
    this.$routesRefreshBtn = document.getElementById("routes-refresh-btn");
  }

  attachEventListeners() {
    // Legacy header buttons (optional, may not exist)
    if (this.$metricsBtn) {
      this.$metricsBtn.addEventListener("click", () => this.openMetrics());
    }
    this.$closeMetricsBtn.addEventListener("click", () => this.closeMetrics());
    this.$refreshMetricsBtn.addEventListener("click", () => this.loadMetrics());

    // Close metrics modal when clicking backdrop
    this.$metricsModal.addEventListener("click", (e) => {
      if (e.target === this.$metricsModal) {
        this.closeMetrics();
      }
    });

    // Parameter Value Viewer modal handlers
    this.$closeParamValueBtn.addEventListener("click", () => this.closeParamValueModal());
    this.$copyParamValueBtn.addEventListener("click", () => this.copyParamValue());

    // Close param value modal when clicking backdrop
    this.$paramValueModal.addEventListener("click", (e) => {
      if (e.target === this.$paramValueModal) {
        this.closeParamValueModal();
      }
    });

    // Export & Backup modal handlers
    this.$closeExportModalBtn.addEventListener("click", () =>
      this.closeExportBackupModal()
    );
    this.$exportBackupModal.addEventListener("click", (e) => {
      if (e.target === this.$exportBackupModal) {
        this.closeExportBackupModal();
      }
    });

    // Import Backup modal handlers
    this.$closeImportModalBtn.addEventListener("click", () =>
      this.closeImportBackupModal()
    );
    this.$importBackupModal.addEventListener("click", (e) => {
      if (e.target === this.$importBackupModal) {
        this.closeImportBackupModal();
      }
    });

    // Camera checkbox handlers
    this.$cameraCheckboxes.forEach((checkbox) => {
      checkbox.addEventListener("change", () =>
        this.updateSelectedVideosButton()
      );
    });

    // Video download handlers
    this.$downloadSelectedVideosBtn.addEventListener("click", () =>
      this.downloadSelectedVideos()
    );
    this.$downloadAllVideosBtn.addEventListener("click", () =>
      this.downloadAllVideos()
    );

    // Backup handlers
    this.$backupRouteBtn.addEventListener("click", () =>
      this.backupCurrentRoute()
    );

    // Import handlers
    this.$selectImportFileBtn.addEventListener("click", () =>
      this.$importFileInput.click()
    );
    this.$importFileInput.addEventListener("change", (e) =>
      this.handleImportFileSelected(e)
    );
    this.$importRouteBtn.addEventListener("click", () =>
      this.importRouteBackup()
    );

    // Cellular warning close
    this.$cellularWarningClose.addEventListener("click", () => {
      this.$cellularWarning.classList.add("hidden");
      // Store dismissal in session storage
      sessionStorage.setItem("cellularWarningDismissed", "true");
    });

    // Firefox warning close
    if (this.$firefoxWarningClose) {
      this.$firefoxWarningClose.addEventListener("click", () => {
        this.$firefoxWarning.classList.add("hidden");
        // Store dismissal in session storage
        sessionStorage.setItem("firefoxWarningDismissed", "true");
      });
    }

    if (this.$importBackupBtn) {
      this.$importBackupBtn.addEventListener("click", () =>
        this.openImportBackupModal()
      );
    }

    // View navigation listeners
    this.$homeBtn.addEventListener("click", () =>
      this.navigateToView("landing")
    );
    this.$cardRoutes.addEventListener("click", () =>
      this.navigateToView("routes")
    );
    this.$cardParams.addEventListener("click", () =>
      this.navigateToView("params")
    );

    // Params manager listeners
    this.$paramsEditToggle.addEventListener("change", (e) => {
      this.paramsEditMode = e.target.checked;
      this.renderParamsTable();
    });
    this.$paramsSearch.addEventListener("input", (e) => {
      this.searchParams(e.target.value);
    });

    // Routes status bar button listeners (now in storage bar)
    if (this.$routesImportBtn) {
      this.$routesImportBtn.addEventListener("click", () =>
        this.openImportBackupModal()
      );
    }
    if (this.$routesClearCacheBtn) {
      this.$routesClearCacheBtn.addEventListener("click", () =>
        this.clearCache()
      );
    }
    if (this.$routesRefreshBtn) {
      this.$routesRefreshBtn.addEventListener("click", () => this.loadRoutes());
    }

    // Header button listeners
    if (this.$headerMetricsBtn) {
      this.$headerMetricsBtn.addEventListener("click", () =>
        this.openMetrics()
      );
    }
    if (this.$headerClearCacheBtn) {
      this.$headerClearCacheBtn.addEventListener("click", () =>
        this.clearCache()
      );
    }
    if (this.$headerRefreshBtn) {
      this.$headerRefreshBtn.addEventListener("click", () => this.loadRoutes());
    }

    // Storage bar button listeners
    if (this.$storageClearCacheBtn) {
      this.$storageClearCacheBtn.addEventListener("click", () =>
        this.clearCache()
      );
    }
    if (this.$storageRefreshBtn) {
      this.$storageRefreshBtn.addEventListener("click", () =>
        this.loadRoutes()
      );
    }

    // Keep old button references for backwards compatibility (if they exist)
    if (
      this.$clearCacheBtn &&
      this.$clearCacheBtn !== this.$headerClearCacheBtn
    ) {
      this.$clearCacheBtn.addEventListener("click", () => this.clearCache());
    }
    if (this.$refreshBtn) {
      this.$refreshBtn.addEventListener("click", () => this.loadRoutes());
    }
    this.$retryBtn.addEventListener("click", () => this.loadRoutes());
    if (this.$playerBackBtn) {
      this.$playerBackBtn.addEventListener("click", () => this.closeVideo());
    }
    this.$replayBtn.addEventListener("click", () => this.replayRoute());

    // FFmpeg Debug Panel
    this.$toggleDebugBtn.addEventListener("click", () =>
      this.toggleDebugPanel()
    );
    this.$clearDebugBtn.addEventListener("click", () => this.clearDebugLogs());

    // Monitor connection status periodically
    this.startConnectionMonitoring();

    // Camera switching
    this.$cameraButtons.forEach((btn) => {
      btn.addEventListener("click", () => {
        const camera = btn.dataset.camera;
        this.switchCamera(camera);
      });
    });

    // Player sidebar actions
    this.$playerStarBtn.addEventListener("click", () => {
      if (this.currentRoute) {
        this.toggleStarFromPlayer();
      }
    });

    this.$playerDeleteBtn.addEventListener("click", () => {
      if (this.currentRoute) {
        this.deleteRoute(this.currentRoute);
      }
    });

    // Download buttons
    this.$playerDownloadRouteBtn = document.getElementById(
      "player-download-route-btn"
    );
    this.$routeDownloadStatus = document.getElementById(
      "route-download-status"
    );
    this.$routeDownloadHistory = document.getElementById(
      "route-download-history"
    );
    this.renderRouteDownloadHistory();

    this.$playerDownloadRouteBtn.addEventListener("click", () => {
      if (this.currentRoute) {
        this.openExportBackupModal();
      }
    });

    // Panel toggle handlers
    this.$logPanelHeader.addEventListener("click", (e) => {
      // Don't toggle if clicking the Load button
      if (!e.target.closest(".panel-action-btn")) {
        this.togglePanel(this.$logPanelContent);
      }
    });

    this.$cerealPanelHeader.addEventListener("click", (e) => {
      // Don't toggle if clicking the Load button
      if (!e.target.closest(".panel-action-btn")) {
        this.togglePanel(this.$cerealPanelContent);
      }
    });

    this.$vehiclePanelHeader.addEventListener("click", () => {
      this.togglePanel(this.$vehiclePanelContent);
    });

    // Log viewer controls
    this.$logTypeButtons.forEach((btn) => {
      btn.addEventListener("click", () => {
        this.$logTypeButtons.forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
      });
    });

    this.$loadLogsBtn.addEventListener("click", () => {
      this.togglePanel(this.$logPanelContent, true); // Force open
      this.loadLogs();
    });

    this.$stopLogsBtn.addEventListener("click", () => {
      this.stopLogs();
    });

    this.$reloadLogsBtn.addEventListener("click", () => {
      this.loadLogs();
    });

    // Allow Enter key in search to trigger load
    this.$logSearchInput.addEventListener("keypress", (e) => {
      if (e.key === "Enter") {
        this.loadLogs();
      }
    });

    // Cereal viewer controls
    this.$loadCerealBtn.addEventListener("click", () => {
      this.togglePanel(this.$cerealPanelContent, true); // Force open
      this.loadCerealData();
    });

    this.$stopCerealBtn.addEventListener("click", () => {
      this.stopCereal();
    });

    this.$reloadCerealBtn.addEventListener("click", () => {
      this.loadCerealData();
    });

    this.$cerealMessageSelect.addEventListener("change", () => {
      if (this.currentCerealData) {
        this.loadCerealData();
      }
    });

    // Keyboard shortcuts
    document.addEventListener("keydown", (e) => this.handleKeyboard(e));
  }

  togglePanel(panelContent, forceOpen = false) {
    if (forceOpen) {
      panelContent.classList.remove("hidden");
    } else {
      panelContent.classList.toggle("hidden");
    }
  }

  showLoading() {
    // Only show loading if we're on the routes view
    if (this.currentView !== "routes") {
      return;
    }
    this.$loading.classList.remove("hidden");
    this.$error.classList.add("hidden");
    this.$empty.classList.add("hidden");
    // Clear only the routes content area, not the entire container
    if (this.$routesContent) {
      const routeCards = this.$routesContent.querySelectorAll(".route-card");
      routeCards.forEach((card) => card.remove());
    }
  }

  hideLoading() {
    this.$loading.classList.add("hidden");
  }

  showError(message) {
    // Only show error if we're on the routes view
    if (this.currentView !== "routes") {
      console.error("Routes error (not on routes view):", message);
      return;
    }
    this.$error.classList.remove("hidden");
    this.$error.querySelector(".error-message").textContent = message;
    this.$loading.classList.add("hidden");
    this.$empty.classList.add("hidden");
  }

  showEmpty() {
    // Only show empty state if we're on the routes view
    if (this.currentView !== "routes") {
      return;
    }
    this.$empty.classList.remove("hidden");
    this.$loading.classList.add("hidden");
    this.$error.classList.add("hidden");
  }

  async clearCache() {
    // Confirm before clearing cache
    const confirmed = await this.showConfirm(
      "Clear Cache",
      "Clear all cached data? This will remove cached videos, thumbnails, GPS metrics, drive stats, and fingerprints. Route files will NOT be deleted.",
      "Clear Cache",
      "Cancel"
    );

    if (!confirmed) {
      return;
    }

    try {
      // Disable button and show loading state
      this.$clearCacheBtn.disabled = true;
      this.$clearCacheBtn.style.opacity = "0.5";

      const response = await fetch(`${this.API_BASE}/api/clear-cache`, {
        method: "POST",
      });

      if (!response.ok) {
        throw new Error(`Failed to clear cache: ${response.statusText}`);
      }

      const data = await response.json();

      if (data.success) {
        const { cleared } = data;
        const totalCleared =
          cleared.remux_cache +
          cleared.thumbnails +
          cleared.gps_metrics +
          cleared.gps_coordinates +
          (cleared.drive_stats || 0) +
          (cleared.fingerprints || 0);

        await this.showAlert(
          "Cache Cleared",
          `Cache cleared successfully!\n\n` +
            `• Remuxed videos: ${cleared.remux_cache}\n` +
            `• Thumbnails: ${cleared.thumbnails}\n` +
            `• GPS metrics: ${cleared.gps_metrics}\n` +
            `• GPS coordinates: ${cleared.gps_coordinates}\n` +
            `• Drive stats: ${cleared.drive_stats || 0}\n` +
            `• Fingerprints: ${cleared.fingerprints || 0}\n\n` +
            `Total items cleared: ${totalCleared}`
        );

        // Reload routes to refresh thumbnails and stats
        this.loadRoutes();
      } else {
        throw new Error(data.error || "Failed to clear cache");
      }
    } catch (error) {
      console.error("Error clearing cache:", error);
      await this.showAlert("Error", `Error clearing cache: ${error.message}`);
    } finally {
      // Re-enable button
      this.$clearCacheBtn.disabled = false;
      this.$clearCacheBtn.style.opacity = "1";
    }
  }

  // Compute a simple hash of routes to detect changes
  computeRoutesHash(routes) {
    // Create a string representation of key route data
    const routeKeys = routes
      .map((r) => `${r.baseName}:${r.timestamp}:${r.isStarred}:${r.duration}`)
      .join("|");

    // Simple hash function
    let hash = 0;
    for (let i = 0; i < routeKeys.length; i++) {
      const char = routeKeys.charCodeAt(i);
      hash = (hash << 5) - hash + char;
      hash = hash & hash; // Convert to 32-bit integer
    }
    return hash;
  }

  async loadRoutes() {
    try {
      this.showLoading();

      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 10000);

      const response = await fetch(`${this.API_BASE}/api/routes`, {
        signal: controller.signal,
      });

      clearTimeout(timeoutId);

      if (!response.ok) {
        if (response.status === 0) {
          throw new Error(
            "Cannot connect to server. Please check if the server is running."
          );
        } else {
          throw new Error(
            `Server error: ${response.status} ${response.statusText}`
          );
        }
      }

      const data = await response.json();

      if (!data.success) {
        throw new Error(data.error || "Failed to load routes");
      }

      // Debug: Log first route to see data structure
      console.log("=== DEBUG: Routes API Response ===");
      // console.log("routes data:", data.routes);
      console.log("Total routes:", data.routes.length);
      if (data.routes.length > 0) {
        console.log("First route sample:", data.routes[0]);
        console.log("First route driveStats:", data.routes[0].driveStats);

        // Debug: Check all routes for driveStats
        const routesWithStats = data.routes.filter((r) => r.driveStats).length;
        const routesWithoutStats = data.routes.length - routesWithStats;
        console.log(
          `Routes with driveStats: ${routesWithStats}/${data.routes.length}`
        );
        console.log(
          `Routes without driveStats: ${routesWithoutStats}/${data.routes.length}`
        );

        // Sample a few routes
        // console.log("=== Sample of first 3 routes ===");
        // data.routes.slice(0, 3).forEach((route, idx) => {
        //   console.log(`Route ${idx + 1}: ${route.baseName}`);
        //   console.log(`  - driveStats:`, route.driveStats);
        //   console.log(`  - hasGpsData:`, route.hasGpsData);
        //   console.log(`  - mileage:`, route.mileage);
        // });
      }

      const newRoutes = data.routes.map((route) => ({
        ...route,
        isStarred:
          route.isStarred ?? route.isPreserved ?? route.is_preserved ?? false,
      }));

      // Compute hash of new routes
      const newHash = this.computeRoutesHash(newRoutes);

      // Only update and re-render if routes have actually changed
      const routesChanged = this.routesHash !== newHash;

      this.routes = newRoutes;
      this.routesHash = newHash;
      this.hideLoading();

      if (this.routes.length === 0) {
        this.showEmpty();
        return;
      }

      // Only re-render DOM if routes actually changed
      if (routesChanged) {
        console.log("Routes changed, re-rendering...");
        this.renderRoutes();
      } else {
        console.log("Routes unchanged, skipping re-render");
      }

      this.updateStats();
      this.updateDeviceStatus();

      // Trigger background geocoding for routes with GPS data (only if routes changed)
      if (routesChanged) {
        setTimeout(() => this.startBackgroundGeocoding(), 1000);
      }
    } catch (error) {
      console.error("Error loading routes:", error);

      let errorMessage = "Failed to load routes. Please try again.";

      if (error.name === "AbortError") {
        errorMessage =
          "Request timed out. The server may be overloaded or unavailable.";
        this.updateDeviceStatus("offline");
      } else if (error.message.includes("Cannot connect to server")) {
        errorMessage =
          error.message + " Make sure the BluePilot server is running.";
        this.updateDeviceStatus("offline");
      } else if (error.message.includes("Server error")) {
        errorMessage = error.message;
        this.updateDeviceStatus("error");
      } else {
        errorMessage = error.message || errorMessage;
        this.updateDeviceStatus("error");
      }

      this.showError(errorMessage);
    }
  }

  async updateDeviceStatus(forceStatus = null) {
    if (forceStatus) {
      // Use forced status (for error states)
      this.setDeviceStatusUI(forceStatus);
      return;
    }

    try {
      // Try to fetch device status from backend
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 3000);

      const response = await fetch(`${this.API_BASE}/api/status`, {
        signal: controller.signal,
      });

      clearTimeout(timeoutId);

      if (response.ok) {
        const data = await response.json();
        // Backend should return {onroad: true/false, online: true}
        if (data.onroad) {
          this.setDeviceStatusUI("onroad");
        } else {
          this.setDeviceStatusUI("online");
        }
      } else {
        this.setDeviceStatusUI("online"); // Server responding but no status endpoint
      }
    } catch (error) {
      // If status check fails, assume offline
      this.setDeviceStatusUI("offline");
    }
  }

  setDeviceStatusUI(status) {
    // Remove all status classes
    this.$deviceStatus.classList.remove("online", "onroad", "offline", "error");

    // Add current status class
    this.$deviceStatus.classList.add(status);

    // Update status text
    const statusTexts = {
      online: "Online",
      onroad: "Onroad",
      offline: "Offline",
      error: "Error",
    };

    this.$statusText.textContent = statusTexts[status] || "Unknown";

    // Show/hide status overlay based on status
    this.updateStatusOverlay(status);
  }

  async updateStatusOverlay(status) {
    // Show full-page overlay for onroad or offline states
    if (status === "onroad") {
      await this.showStatusOverlay("onroad");
    } else if (status === "offline") {
      await this.showStatusOverlay("offline");
    } else {
      this.hideStatusOverlay();
    }
  }

  async showStatusOverlay(type) {
    // Show overlay with appropriate message
    this.$statusOverlay.classList.remove(
      "hidden",
      "status-onroad",
      "status-offline",
      "status-reconnecting"
    );
    this.$statusOverlay.classList.add(`status-${type}`);

    // Fetch detailed status
    let detailedStatus = null;
    try {
      const response = await fetch(`${this.API_BASE}/api/status/detailed`, {
        signal: AbortSignal.timeout(3000),
      });
      if (response.ok) {
        detailedStatus = await response.json();
        // Update cellular warning based on detailed status
        this.updateCellularWarning(detailedStatus);
      }
    } catch (e) {
      console.warn("Could not fetch detailed status:", e);
    }

    if (type === "onroad") {
      this.$statusOverlayTitle.textContent = "Driving Mode";
      this.$statusOverlayMessage.textContent =
        "The device is currently driving. All web interface interactions are disabled for safety. Park the vehicle to access routes and videos.";

      // Show details
      this.$statusOverlayDetails.classList.remove("hidden");
      this.$statusOverlayRetry.classList.add("hidden");
      this.$statusOverlaySpinner.classList.add("hidden");

      if (detailedStatus) {
        this.$detailConnection.textContent =
          detailedStatus.network?.connection_type?.toUpperCase() || "Unknown";
        this.$detailConnection.className = "detail-value warning";

        this.$detailRateLimit.textContent = `${
          detailedStatus.rate_limit?.requests_per_minute || 6
        } req/min (Restricted)`;
        this.$detailRateLimit.className = "detail-value warning";
      } else {
        this.$detailConnection.textContent = "Unknown";
        this.$detailRateLimit.textContent = "Restricted";
      }

      this.$detailLastUpdate.textContent = new Date().toLocaleTimeString();
    } else if (type === "offline") {
      this.$statusOverlayTitle.textContent = "Server Offline";
      this.$statusOverlayMessage.textContent =
        "Cannot connect to the BluePilot server. Please check your network connection or verify the server is running.";

      // Show retry button
      this.$statusOverlayDetails.classList.add("hidden");
      this.$statusOverlayRetry.classList.remove("hidden");
      this.$statusOverlaySpinner.classList.add("hidden");

      // Add retry handler
      this.$statusOverlayRetry.onclick = () => this.retryConnection();
    }
  }

  hideStatusOverlay() {
    this.$statusOverlay.classList.add("hidden");
  }

  async retryConnection() {
    // Show reconnecting state
    this.$statusOverlay.classList.remove("status-offline");
    this.$statusOverlay.classList.add("status-reconnecting");
    this.$statusOverlayTitle.textContent = "Reconnecting...";
    this.$statusOverlayMessage.textContent =
      "Attempting to reconnect to the server.";
    this.$statusOverlayRetry.classList.add("hidden");
    this.$statusOverlaySpinner.classList.remove("hidden");

    // Try to reconnect
    await new Promise((resolve) => setTimeout(resolve, 1500));

    try {
      await this.loadRoutes();
      this.hideStatusOverlay();
    } catch (e) {
      // Still offline, show offline state again
      this.showStatusOverlay("offline");
    }
  }

  updateCellularWarning(detailedStatus) {
    // Check if user dismissed the warning this session
    const dismissed =
      sessionStorage.getItem("cellularWarningDismissed") === "true";

    if (!detailedStatus || !detailedStatus.cellular_access) {
      this.$cellularWarning.classList.add("hidden");
      return;
    }

    const cellularStatus = detailedStatus.cellular_access;

    // Show warning if cellular access is enabled and not dismissed
    if (cellularStatus.active && !dismissed) {
      this.$cellularWarning.classList.remove("hidden");

      // Update details text
      const remaining = cellularStatus.time_remaining_minutes;
      const connectionType =
        detailedStatus.network?.connection_type || "unknown";

      let detailText = `Server accessible over cellular network`;

      if (remaining > 0) {
        if (remaining > 60) {
          const hours = Math.floor(remaining / 60);
          const mins = remaining % 60;
          detailText += ` • Auto-disables in ${hours}h ${mins}m`;
        } else {
          detailText += ` • Auto-disables in ${remaining} minutes`;
        }
      }

      if (connectionType === "cellular") {
        detailText += ` • Currently connected via cellular`;
      }

      this.$cellularWarningDetails.textContent = detailText;
    } else {
      this.$cellularWarning.classList.add("hidden");
    }
  }

  updateStats() {
    const routeCountText = `${this.routes.length} route${
      this.routes.length !== 1 ? "s" : ""
    }`;

    // Calculate total size
    const totalBytes = this.routes.reduce(
      (sum, route) => sum + (route.sizeBytes || 0),
      0
    );
    const totalGB = (totalBytes / (1024 * 1024 * 1024)).toFixed(1);
    const totalSizeText = `${totalGB} GB`;

    // Update header (if elements exist - legacy)
    if (this.$routeCount) {
      this.$routeCount.textContent = routeCountText;
    }
    if (this.$totalSize) {
      this.$totalSize.textContent = totalSizeText;
    }

    // Update routes status bar with same info
    if (this.$routesRouteCount) {
      this.$routesRouteCount.textContent = routeCountText;
    }
    if (this.$routesTotalSize) {
      this.$routesTotalSize.textContent = totalSizeText;
    }

    // Update landing page routes card
    if (this.$landingRouteCount) {
      this.$landingRouteCount.textContent = routeCountText;
    }
    if (this.$landingTotalSize) {
      this.$landingTotalSize.textContent = totalSizeText;
    }

    // Update disk visualization
    this.updateDiskVisualization();
  }

  async updateDiskVisualization() {
    try {
      const response = await fetch(`${this.API_BASE}/api/disk-analysis`);
      const data = await response.json();

      if (!data.success) {
        console.error("Failed to load disk analysis");
        return;
      }

      const { disk, cache } = data;

      // Show the disk info container only if on routes view
      if (this.currentView === "routes") {
        this.$diskVizContainer.classList.remove("hidden");
      }

      // Update compact disk info text
      this.$diskVizStatsText.textContent = `${disk.formatted.used} / ${disk.formatted.total} (${disk.formatted.free} free)`;
      if (this.$diskPreservedValue) {
        this.$diskPreservedValue.textContent = disk.formatted.preserved;
      }
      if (this.$diskRoutesValue) {
        this.$diskRoutesValue.textContent = disk.formatted.non_preserved;
      }
      if (this.$diskCacheValue && cache) {
        this.$diskCacheValue.textContent = cache.formatted.total;
      }

      // Calculate percentages for gauge bar segments (only show USED space)
      const preservedPercent = (disk.preserved_bytes / disk.total_bytes) * 100;
      const routesPercent = (disk.non_preserved_bytes / disk.total_bytes) * 100;
      const cachePercent = cache
        ? (cache.total_bytes / disk.total_bytes) * 100
        : 0;
      // Free space is just the empty part of the gauge - no need to render it

      // Update gauge bar widths
      this.$diskPreservedBar.style.width = `${preservedPercent}%`;
      this.$diskRoutesBar.style.width = `${routesPercent}%`;
      if (this.$diskCacheBar) {
        this.$diskCacheBar.style.width = `${cachePercent}%`;
      }

      // Update compact disk bar in routes status bar
      if (this.$routesDiskCompact) {
        const totalUsedPercent =
          preservedPercent + routesPercent + cachePercent;
        const diskBarFill =
          this.$routesDiskCompact.querySelector(".disk-bar-fill");
        if (diskBarFill) {
          diskBarFill.style.width = `${totalUsedPercent.toFixed(1)}%`;

          // Update color based on warning level
          diskBarFill.classList.remove("warning", "critical");
          if (disk.warning_level === "critical") {
            diskBarFill.classList.add("critical");
          } else if (
            disk.warning_level === "low" ||
            disk.warning_level === "medium"
          ) {
            diskBarFill.classList.add("warning");
          }
        }

        // Update tooltip/title with disk info
        this.$routesDiskCompact.title = `${disk.formatted.used} / ${disk.formatted.total} (${disk.formatted.free} free)`;
      }

      // Position threshold marker (shows critical point where deletion starts)
      const thresholdPercent =
        (disk.deletion_threshold_bytes / disk.total_bytes) * 100;
      const thresholdPosition = 100 - thresholdPercent;
      this.$diskThresholdMarker.style.left = `${thresholdPosition}%`;

      // Show/hide warning icon
      if (this.$diskWarning) {
        this.$diskWarning.classList.remove("warning", "critical", "hidden");

        if (
          disk.warning_level === "critical" ||
          disk.warning_level === "low" ||
          disk.warning_level === "medium"
        ) {
          this.$diskWarning.classList.remove("hidden");
          // Update title for tooltip
          this.$diskWarning.title = `Low storage: ${disk.formatted.free} free`;
        } else {
          this.$diskWarning.classList.add("hidden");
        }
      }

      // Legacy warning text element (if exists)
      if (this.$diskWarningText) {
        if (disk.warning_level === "critical") {
          this.$diskWarningText.textContent = `Low space: ${disk.formatted.free} free`;
        } else if (
          disk.warning_level === "low" ||
          disk.warning_level === "medium"
        ) {
          this.$diskWarningText.textContent = `${disk.formatted.free} free`;
        }
      }
    } catch (error) {
      console.error("Error updating disk info:", error);
      // Hide disk info on error
      this.$diskVizContainer.classList.add("hidden");
    }
  }

  async startBackgroundGeocoding() {
    // Geocode each route individually in the background
    // This spreads out the API calls and respects rate limits
    const routesWithGps = this.routes.filter((r) => r.hasGpsData);

    if (routesWithGps.length === 0) return;

    // Track which routes we've already tried to geocode in this session
    if (!this.geocodedRoutes) {
      this.geocodedRoutes = new Set();
    }

    // Filter out routes we've already checked
    const routesToGeocode = routesWithGps.filter(
      (r) => !this.geocodedRoutes.has(r.baseName)
    );

    if (routesToGeocode.length === 0) {
      console.log("All routes already geocoded in this session");
      return;
    }

    console.log(
      `Starting background geocoding for ${routesToGeocode.length} routes...`
    );

    // Geocode routes one at a time with rate limiting
    for (let i = 0; i < routesToGeocode.length; i++) {
      const route = routesToGeocode[i];

      try {
        const response = await fetch(
          `${this.API_BASE}/api/geocode/${route.baseName}`
        );
        const data = await response.json();

        // Mark this route as geocoded (even if result is null)
        this.geocodedRoutes.add(route.baseName);

        if (data.success) {
          // Update this route with location data
          const routeIndex = this.routes.findIndex(
            (r) => r.baseName === route.baseName
          );
          if (routeIndex !== -1) {
            this.routes[routeIndex].startLocation = data.startLocation;
            this.routes[routeIndex].endLocation = data.endLocation;

            // Re-render just this route's card
            this.updateRouteCard(this.routes[routeIndex]);
          }
        }
      } catch (error) {
        // Mark as tried even if failed, to avoid retrying immediately
        this.geocodedRoutes.add(route.baseName);

        // Silent failure for individual routes
        console.debug(
          `Geocoding failed for ${route.baseName} (offline?)`,
          error
        );
      }

      // Rate limit: wait 1.2 seconds between requests (Nominatim requires 1 req/sec)
      if (i < routesToGeocode.length - 1) {
        await new Promise((resolve) => setTimeout(resolve, 1200));
      }
    }

    console.log("Background geocoding complete");
  }

  updateRouteCard(route) {
    // Find and update the specific route card with new location data
    const cards = document.querySelectorAll(".route-card");
    for (const card of cards) {
      // Check if this card matches the route (by looking for its base name in click handler)
      if (card.dataset && card.dataset.baseName === route.baseName) {
        // Find the location subtitle element
        const locationEl = card.querySelector(".route-location");
        if (locationEl) {
          const start = route.startLocation || "N/A";
          const end = route.endLocation || "N/A";
          if (start === end) {
            locationEl.textContent = start;
          } else {
            locationEl.textContent = `${start} → ${end}`;
          }
        }
        break;
      }
    }
  }

  renderRoutes() {
    // Clear only the route cards, not the loading/error/empty states
    if (this.$routesContent) {
      const existingRoutes =
        this.$routesContent.querySelectorAll(".date-group");
      existingRoutes.forEach((group) => group.remove());
    }

    // Group routes by date (convert UTC timestamp to local date)
    const grouped = {};
    for (const route of this.routes) {
      // Convert UTC timestamp to local date for grouping
      const dateKey = route.timestamp
        ? this.formatLocalDate(route.timestamp)
        : "Unknown";
      if (!grouped[dateKey]) {
        grouped[dateKey] = [];
      }
      grouped[dateKey].push(route);
    }

    // Render each date group
    for (const [date, routes] of Object.entries(grouped)) {
      const dateGroup = this.createDateGroup(date, routes);
      this.$routesContent.appendChild(dateGroup);
    }
  }

  createDateGroup(date, routes) {
    const group = document.createElement("div");
    group.className = "date-group";

    const header = document.createElement("div");
    header.className = "date-header";
    header.textContent = date;

    group.appendChild(header);

    // Create grid container for cards
    const cardsContainer = document.createElement("div");
    cardsContainer.className = "date-group-cards";

    for (const route of routes) {
      const card = this.createRouteCard(route);
      cardsContainer.appendChild(card);
    }

    group.appendChild(cardsContainer);

    return group;
  }

  createRouteCard(route) {
    const card = document.createElement("div");
    card.className = "route-card";
    card.dataset.baseName = route.baseName; // For finding card later during geocoding

    // Debug: Log drive stats for this route
    // console.log("route:", route);
    console.log(`Route ${route.baseName} - driveStats:`, route.driveStats);
    if (route.driveStats) {
      console.log(`  opEngagedPercent: ${route.driveStats.opEngagedPercent}`);
      console.log(`  alertCount: ${route.driveStats.alertCount}`);
      console.log(
        `  disengagementCount: ${route.driveStats.disengagementCount}`
      );
    }

    // Convert UTC timestamps to browser's local timezone
    const displayTime = route.timestamp
      ? this.formatLocalTime(route.timestamp)
      : "";

    // Calculate end time by adding duration (stored in route.duration format like "1h 30m" or "45m")
    let displayEndTime = "";
    if (route.timestamp && route.duration) {
      try {
        // Append 'Z' to treat timestamp as UTC (same as formatLocalTime does)
        let timestamp = route.timestamp;
        if (!timestamp.includes("+") && !timestamp.endsWith("Z")) {
          timestamp = timestamp + "Z";
        }
        const startDate = new Date(timestamp);

        // Parse duration string (e.g., "1h 30m" or "45m")
        const durationMatch = route.duration.match(/(?:(\d+)h\s*)?(?:(\d+)m)?/);
        if (durationMatch) {
          const hours = parseInt(durationMatch[1] || 0);
          const minutes = parseInt(durationMatch[2] || 0);
          const totalMinutes = hours * 60 + minutes;
          const endDate = new Date(
            startDate.getTime() + totalMinutes * 60 * 1000
          );
          displayEndTime = endDate.toLocaleTimeString("en-US", {
            hour: "numeric",
            minute: "2-digit",
            hour12: true,
          });
        }
      } catch (e) {
        console.error("Error calculating end time:", e);
      }
    }

    const timeRange = displayEndTime
      ? `${displayTime} - ${displayEndTime}`
      : displayTime;

    // Preserved badge
    const preservedBadge = route.isStarred
      ? `
      <div class="preserved-badge">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="currentColor" stroke="currentColor" stroke-width="2">
          <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2"/>
        </svg>
        Preserved
      </div>
    `
      : "";

    // Format location subtitle (start -> end)
    let locationHTML = "";
    if (route.startLocation || route.endLocation) {
      const start = route.startLocation || "N/A";
      const end = route.endLocation || "N/A";
      if (start === end) {
        locationHTML = `
          <div class="route-location">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"/>
              <circle cx="12" cy="10" r="3"/>
            </svg>
            ${start}
          </div>`;
      } else {
        locationHTML = `
          <div class="route-location">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"/>
              <circle cx="12" cy="10" r="3"/>
            </svg>
            ${start} → ${end}
          </div>`;
      }
    }

    // Format fingerprint information (simple display for route card)
    let fingerprintHTML = "";
    if (route.fingerprint && route.fingerprint.carFingerprint) {
      fingerprintHTML = `
        <div class="route-fingerprint">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <rect x="2" y="7" width="20" height="14" rx="2" ry="2"/>
            <path d="M16 21V5a2 2 0 0 0-2-2h-4a2 2 0 0 0-2 2v16"/>
          </svg>
          ${route.fingerprint.carFingerprint}
        </div>`;
    }

    const preserveButtonClass = route.isStarred
      ? "route-preserve-btn active"
      : "route-preserve-btn";
    const preserveButtonLabel = route.isStarred ? "Preserved" : "Preserve";
    const preserveIconFill = route.isStarred ? "currentColor" : "none";

    // Processing banner - show if drive stats are not yet available
    const processingBanner = !route.driveStats
      ? `
      <div class="route-processing-banner">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <circle cx="12" cy="12" r="10"/>
          <path d="M12 6v6l4 2"/>
        </svg>
        <span class="banner-text">Processing drive statistics...</span>
      </div>
    `
      : "";

    card.innerHTML = `
      ${preservedBadge}
      ${processingBanner}
      <div class="route-thumbnail">
        <img src="${this.API_BASE}/api/thumbnail/${route.baseName}"
             onerror="this.style.display='none'">
        <div class="play-overlay">
          <svg width="48" height="48" viewBox="0 0 24 24" fill="#2196f3" stroke="#2196f3" stroke-width="2">
            <polygon points="5 3 19 12 5 21 5 3"/>
          </svg>
        </div>
      </div>
      <div class="route-info">
        <div class="route-info-header">
          <div class="route-time-range">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <circle cx="12" cy="12" r="10"/>
              <polyline points="12 6 12 12 16 14"/>
            </svg>
            ${timeRange || route.baseName}
          </div>
          <button
            type="button"
            class="${preserveButtonClass}"
            aria-pressed="${route.isStarred}"
            title="${preserveButtonLabel} this route"
          >
            <svg width="14" height="14" viewBox="0 0 24 24" fill="${preserveIconFill}" stroke="currentColor" stroke-width="2">
              <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2"/>
            </svg>
            <span>${preserveButtonLabel}</span>
          </button>
        </div>
        ${locationHTML}
        ${fingerprintHTML}
        <div class="route-stats-grid">
          <div class="route-stat">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>
            </svg>
            ${route.duration}
          </div>
          <div class="route-stat">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"/><circle cx="12" cy="10" r="3"/>
            </svg>
            ${route.mileage || route.distance || "--"}
          </div>
          ${
            route.avgSpeed
              ? `
            <div class="route-stat">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <path d="M12 2v4"/><path d="m6.8 15-3.5 2"/><path d="m20.7 7-3.5 2"/>
                <path d="M6.8 9 3.3 7"/><path d="m20.7 17-3.5-2"/><path d="M18 18.7a9 9 0 1 0-12 0"/>
              </svg>
              ${route.avgSpeed} avg
            </div>
          `
              : ""
          }
          ${
            route.topSpeed
              ? `
            <div class="route-stat">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/>
              </svg>
              ${route.topSpeed} top
            </div>
          `
              : ""
          }
          ${
            route.driveStats && route.driveStats.opEngagedPercent !== undefined
              ? `
            <div class="route-stat route-stat-engagement">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <circle cx="12" cy="12" r="10"/>
                <path d="M12 6v6l4 2"/>
              </svg>
              ${route.driveStats.opEngagedPercent.toFixed(0)}% engaged
            </div>
          `
              : ""
          }
          ${
            route.driveStats &&
            route.driveStats.alertCount !== undefined &&
            route.driveStats.alertCount > 0
              ? `
            <div class="route-stat route-stat-alerts">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/>
                <line x1="12" y1="9" x2="12" y2="13"/>
                <line x1="12" y1="17" x2="12.01" y2="17"/>
              </svg>
              ${route.driveStats.alertCount} alerts
            </div>
          `
              : ""
          }
          <div class="route-stat">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/>
            </svg>
            ${
              route.totalSegments ||
              (Array.isArray(route.segments)
                ? route.segments.length
                : route.segments)
            } seg
          </div>
          <div class="route-stat">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <line x1="22" y1="12" x2="2" y2="12"/>
              <path d="M5.45 5.11L2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11z"/>
            </svg>
            ${route.size}
          </div>
        </div>
      </div>
    `;

    const preserveBtn = card.querySelector(".route-preserve-btn");
    if (preserveBtn) {
      preserveBtn.addEventListener("click", (event) => {
        event.stopPropagation();
        this.toggleStar(route);
      });
    }

    // Make entire card clickable
    card.addEventListener("click", () => {
      this.playRoute(route);
    });

    return card;
  }

  createRiskBadge(route) {
    if (!route.deletionRisk) return "";

    const risk = route.deletionRisk;
    const level = risk.level;

    // Risk level emoji and text
    const riskConfig = {
      safe: { icon: "🟢", text: "Safe" },
      low: { icon: "🟡", text: "Low Risk" },
      medium: { icon: "🟠", text: "Medium Risk" },
      high: { icon: "🔴", text: "High Risk" },
      critical: { icon: "🚨", text: "Critical Risk" },
    };

    const config = riskConfig[level] || riskConfig.safe;

    // Build risk details
    let details = "";
    if (risk.rank) {
      details = `Rank ${risk.rank}/${risk.totalInQueue}`;
      if (risk.segmentsAtRisk > 0) {
        details += ` • ${risk.segmentsAtRisk}/${risk.totalSegments} segments at risk`;
      }
    } else if (risk.segmentsProtected === risk.totalSegments) {
      details = "All segments protected";
    }

    // Incomplete route warning
    let incompleteWarning = "";
    if (risk.isIncomplete) {
      const missing = risk.firstSegmentNumber;
      incompleteWarning = `<span class="route-incomplete-badge">⚠️ Incomplete: missing segments 0-${
        missing - 1
      }</span>`;
    }

    return `
      <div class="route-risk-badge ${level}">
        <span class="risk-icon"></span>
        <span>${config.text}</span>
      </div>
      ${
        details
          ? `<div class="route-risk-details">${details} ${incompleteWarning}</div>`
          : ""
      }
    `;
  }

  async playRoute(route) {
    try {
      // Load route details
      const response = await fetch(
        `${this.API_BASE}/api/routes/${route.baseName}`
      );
      const data = await response.json();

      if (!data.success) {
        throw new Error(data.error || "Failed to load route details");
      }

      const mergedRoute = {
        ...(route || {}),
        ...data,
      };
      if (
        mergedRoute.isStarred === undefined ||
        mergedRoute.isStarred === null
      ) {
        mergedRoute.isStarred =
          data.isStarred ??
          data.isPreserved ??
          data.is_preserved ??
          route?.isStarred ??
          route?.isPreserved ??
          false;
      }

      this.currentRoute = mergedRoute;
      this.currentSegment = 0;

      // Determine best camera to start with based on HEVC support
      const firstSegmentVideos = data.segments[0]?.videos || {};
      const availableCameras = Object.keys(firstSegmentVideos);

      // Define camera priority based on HEVC support
      let cameraPriority;
      if (this.canPlayHEVC()) {
        // HEVC supported: prioritize HEVC cameras (front -> wide -> driver -> lq)
        cameraPriority = ["front", "wide", "driver", "lq"];
        console.log("HEVC playback available - prioritizing HEVC cameras");
      } else {
        // No HEVC support: only use LQ camera
        cameraPriority = ["lq"];
        console.log("HEVC not supported - using LQ camera only");
      }

      // Select first available camera from priority list
      this.currentCamera = null;
      for (const camera of cameraPriority) {
        if (availableCameras.includes(camera)) {
          this.currentCamera = camera;
          break;
        }
      }

      if (!this.currentCamera) {
        throw new Error("No playable video cameras available for this route");
      }

      this.openVideo();
      this.updateCameraButtons();
      this.loadSegment(0);
    } catch (error) {
      console.error("Error playing route:", error);
      await this.showAlert(
        "Playback Error",
        `Failed to play route: ${error.message}`
      );
    }
  }

  openVideo() {
    this.$videoPlayer.classList.remove("hidden");
    this.hideReplayOverlay();

    // Set title: Display date and time range on first line (convert UTC to local time)
    const displayDate = this.currentRoute.timestamp
      ? this.formatLocalDate(this.currentRoute.timestamp)
      : "";
    const displayTime = this.currentRoute.timestamp
      ? this.formatLocalTime(this.currentRoute.timestamp)
      : "";

    // Calculate end time from start time + duration
    let displayEndTime = "";
    if (this.currentRoute.timestamp && this.currentRoute.duration) {
      try {
        // Append 'Z' to treat timestamp as UTC (same as formatLocalTime does)
        let timestamp = this.currentRoute.timestamp;
        if (!timestamp.includes("+") && !timestamp.endsWith("Z")) {
          timestamp = timestamp + "Z";
        }
        const startDate = new Date(timestamp);

        const durationMatch = this.currentRoute.duration.match(
          /(?:(\d+)h\s*)?(?:(\d+)m)?/
        );
        if (durationMatch) {
          const hours = parseInt(durationMatch[1] || 0);
          const minutes = parseInt(durationMatch[2] || 0);
          const totalMinutes = hours * 60 + minutes;
          const endDate = new Date(
            startDate.getTime() + totalMinutes * 60 * 1000
          );
          displayEndTime = endDate.toLocaleTimeString("en-US", {
            hour: "numeric",
            minute: "2-digit",
            hour12: true,
          });
        }
      } catch (e) {
        console.error("Error calculating end time:", e);
      }
    }

    const timeRange = displayEndTime
      ? `${displayTime} - ${displayEndTime}`
      : displayTime;
    if (this.$playerRouteRange) {
      this.$playerRouteRange.textContent =
        timeRange || this.currentRoute.displayTime || "--";
    }
    this.$videoTitle.textContent =
      displayDate + (displayDate && timeRange ? " - " : "") + timeRange;

    // Set route ID on second line
    this.$videoRouteId.textContent = this.currentRoute.baseName;

    // Populate stats
    this.updatePlayerStats();

    // Render timeline
    this.renderTimeline();

    // Update star button text
    this.updatePlayerStarButton();

    document.body.style.overflow = "hidden";
  }

  updatePlayerStats() {
    if (!this.currentRoute) return;

    // Duration
    this.$statDuration.textContent = this.currentRoute.duration || "--";

    // Segments
    this.$statSegments.textContent =
      this.currentRoute.totalSegments ||
      this.currentRoute.segments?.length ||
      "--";

    // Size
    this.$statSize.textContent = this.currentRoute.size || "--";

    // Drive stats data (from log extraction)
    const driveStats = this.currentRoute.driveStats;

    // Distance - prefer drive stats, fallback to GPS metrics
    if (driveStats && driveStats.distance > 0) {
      this.$statDistance.textContent = `${driveStats.distance.toFixed(2)} mi`;
    } else {
      this.$statDistance.textContent = this.currentRoute.mileage || "--";
    }

    // Speed stats - prefer drive stats
    if (driveStats && driveStats.avgSpeed > 0) {
      this.$statAvgSpeed.textContent = `${driveStats.avgSpeed.toFixed(1)} mph`;
    } else {
      this.$statAvgSpeed.textContent = this.currentRoute.avgSpeed || "--";
    }

    if (driveStats && driveStats.maxSpeed > 0) {
      this.$statTopSpeed.textContent = `${driveStats.maxSpeed.toFixed(1)} mph`;
    } else {
      this.$statTopSpeed.textContent = this.currentRoute.topSpeed || "--";
    }

    // Engagement stats
    if (driveStats) {
      this.$statOpEngaged.textContent = `${driveStats.opEngagedPercent.toFixed(
        1
      )}%`;
      this.$statDisengagements.textContent =
        driveStats.disengagementCount || "0";
      this.$statAlerts.textContent = driveStats.alertCount || "0";
      this.$statLaneChanges.textContent = driveStats.laneChanges || "0";
    } else {
      this.$statOpEngaged.textContent = "--";
      this.$statDisengagements.textContent = "--";
      this.$statAlerts.textContent = "--";
      this.$statLaneChanges.textContent = "--";
    }

    // Location start and end
    this.$statLocationStart.textContent =
      this.currentRoute.startLocation || "--";
    this.$statLocationEnd.textContent = this.currentRoute.endLocation || "--";

    // Vehicle fingerprint information
    if (
      this.currentRoute.fingerprint &&
      this.currentRoute.fingerprint.carFingerprint
    ) {
      const fp = this.currentRoute.fingerprint;

      // Show vehicle info panel
      this.$vehiclePanel.style.display = "";

      // Populate basic vehicle info
      this.$vehiclePlatform.textContent = fp.carFingerprint || "--";
      this.$vehicleVin.textContent = fp.carVin || "--";
      this.$vehicleBrand.textContent = fp.brand || "--";
      this.$vehicleSource.textContent = fp.fingerprintSource
        ? fp.fingerprintSource.toUpperCase()
        : "--";

      // Populate firmware table if firmware versions exist
      if (fp.firmwareVersions && fp.firmwareVersions.length > 0) {
        this.$firmwareTableBody.innerHTML = "";

        // Ford part number format: PL3A-14C204-XK or PL3A-14C204-XKK
        // Pattern: [4 alphanumeric]-[5-6 alphanumeric]-[2-3 alphanumeric]
        const fordPartNumberRegex =
          /^[A-Z0-9]{4}-[A-Z0-9]{5,6}-[A-Z0-9]{2,3}$/i;

        // Deduplicate firmware versions based on ECU+version combination
        // Filter to only show Ford part number formats
        const seenFirmware = new Set();
        fp.firmwareVersions.forEach((fw) => {
          const key = `${fw.ecu}:${fw.version}`;
          // Only show firmware entries matching Ford part number format
          if (!seenFirmware.has(key) && fordPartNumberRegex.test(fw.version)) {
            seenFirmware.add(key);
            const row = document.createElement("tr");
            row.innerHTML = `
              <td>${fw.ecu}</td>
              <td>${fw.version}</td>
            `;
            this.$firmwareTableBody.appendChild(row);
          }
        });
      }
    } else {
      // Hide vehicle info panel if no fingerprint data
      this.$vehiclePanel.style.display = "none";
    }
  }

  renderTimeline() {
    if (!this.currentRoute || !this.currentRoute.driveStats) {
      this.$routeTimeline.classList.add("hidden");
      return;
    }

    const stats = this.currentRoute.driveStats;
    if (!stats.duration || stats.duration === 0) {
      this.$routeTimeline.classList.add("hidden");
      return;
    }

    // Show timeline
    this.$routeTimeline.classList.remove("hidden");

    // Setup canvas
    const canvas = this.$timelineCanvas;
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();

    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    canvas.style.width = rect.width + "px";
    canvas.style.height = rect.height + "px";

    const ctx = canvas.getContext("2d");
    ctx.scale(dpr, dpr);

    const width = rect.width;
    const height = rect.height;
    const duration = stats.duration;

    // State colors (matching comma connect style)
    const stateColors = {
      disabled: "#3a5a7f", // Blue
      preEnabled: "#5a7fa3", // Light blue
      enabled: "#4caf50", // Green
      softDisabling: "#ff9800", // Orange
      overriding: "#9e9e9e", // Gray
    };

    // Build time-based state segments
    const stateSegments = [];
    let currentStart = 0;

    // Create segments from state times
    // Note: This is an approximation since we don't have exact state change timestamps
    if (stats.opStateTimes) {
      for (const [state, timeInState] of Object.entries(stats.opStateTimes)) {
        if (timeInState > 0) {
          const segmentEnd = currentStart + timeInState;
          stateSegments.push({
            state: state,
            start: currentStart,
            end: segmentEnd,
            color: stateColors[state] || stateColors["disabled"],
          });
          currentStart = segmentEnd;
        }
      }
    }

    // If no segments, create one default disabled segment
    if (stateSegments.length === 0) {
      stateSegments.push({
        state: "disabled",
        start: 0,
        end: duration,
        color: stateColors["disabled"],
      });
    }

    // Draw state segments
    stateSegments.forEach((segment) => {
      const startX = (segment.start / duration) * width;
      const endX = (segment.end / duration) * width;
      const segmentWidth = endX - startX;

      ctx.fillStyle = segment.color;
      ctx.fillRect(startX, 0, segmentWidth, height);
    });

    // Draw event markers
    const drawEventMarker = (time, color) => {
      const x = (time / duration) * width;
      const markerWidth = 3;

      ctx.fillStyle = color;
      ctx.fillRect(x - markerWidth / 2, 0, markerWidth, height);
    };

    // Disengagements (red)
    if (stats.disengagements) {
      stats.disengagements.forEach((event) => {
        drawEventMarker(event.time, "#f44336");
      });
    }

    // Alerts (orange)
    if (stats.alerts) {
      stats.alerts.forEach((event) => {
        drawEventMarker(event.time, "#ff9800");
      });
    }
  }

  updateTimelinePosition() {
    if (
      !this.currentRoute ||
      !this.currentRoute.driveStats ||
      !this.$timelinePositionIndicator
    ) {
      return;
    }

    // Get current video time
    let currentTime = 0;
    if (this.$videoElement) {
      currentTime = this.$videoElement.currentTime;
    } else if (
      this.player &&
      typeof this.player.getCurrentTime === "function"
    ) {
      currentTime = this.player.getCurrentTime() || 0;
    }

    const duration = this.currentRoute.driveStats.duration;
    if (duration > 0) {
      const percentage = (currentTime / duration) * 100;
      this.$timelinePositionIndicator.style.left = `${percentage}%`;

      // Update timestamp display
      if (this.$timelinePositionTime) {
        const hours = Math.floor(currentTime / 3600);
        const minutes = Math.floor((currentTime % 3600) / 60);
        const seconds = Math.floor(currentTime % 60);
        const timeStr =
          hours > 0
            ? `${hours}:${minutes.toString().padStart(2, "0")}:${seconds
                .toString()
                .padStart(2, "0")}`
            : `${minutes}:${seconds.toString().padStart(2, "0")}`;
        this.$timelinePositionTime.textContent = timeStr;
      }
    }
  }

  getStateAtTime(time) {
    if (!this.currentRoute || !this.currentRoute.driveStats) {
      return null;
    }

    const stats = this.currentRoute.driveStats;
    if (!stats.opStateTimes) {
      return "disabled";
    }

    // Build timeline of state changes
    let currentStart = 0;
    for (const [state, timeInState] of Object.entries(stats.opStateTimes)) {
      if (timeInState > 0) {
        const segmentEnd = currentStart + timeInState;
        if (time >= currentStart && time < segmentEnd) {
          return state;
        }
        currentStart = segmentEnd;
      }
    }

    return "disabled";
  }

  setupTimelineListeners() {
    if (!this.$timelineCanvas) return;

    // Mouse move for tooltip
    this.$timelineCanvas.addEventListener("mousemove", (e) => {
      if (!this.currentRoute || !this.currentRoute.driveStats) return;

      const rect = this.$timelineCanvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const width = rect.width;
      const duration = this.currentRoute.driveStats.duration;
      const time = (x / width) * duration;

      // Show hover indicator at mouse position
      if (this.$timelineHoverIndicator) {
        const percentage = (x / width) * 100;
        this.$timelineHoverIndicator.style.left = `${percentage}%`;
        this.$timelineHoverIndicator.style.display = "block";
      }

      // Format time
      const hours = Math.floor(time / 3600);
      const minutes = Math.floor((time % 3600) / 60);
      const seconds = Math.floor(time % 60);
      const timeStr =
        hours > 0
          ? `${hours}h ${minutes}m ${seconds}s`
          : `${minutes}m ${seconds}s`;

      // Get engagement state at this time
      const state = this.getStateAtTime(time);
      const stateNames = {
        disabled: "Disabled",
        preEnabled: "Pre-Enabled",
        enabled: "Engaged",
        softDisabling: "Soft Disabling",
        overriding: "Overriding",
      };
      const stateColors = {
        disabled: "#3a5a7f",
        preEnabled: "#5a7fa3",
        enabled: "#4caf50",
        softDisabling: "#ff9800",
        overriding: "#9e9e9e",
      };

      // Find relevant events at this time
      const events = [];
      const timeWindow = duration * 0.01; // 1% of total duration as window

      if (this.currentRoute.driveStats.disengagements) {
        this.currentRoute.driveStats.disengagements.forEach((event) => {
          if (Math.abs(event.time - time) < timeWindow) {
            events.push({
              type: "disengage",
              name: event.type,
              color: "#f44336",
            });
          }
        });
      }

      if (this.currentRoute.driveStats.alerts) {
        this.currentRoute.driveStats.alerts.forEach((event) => {
          if (Math.abs(event.time - time) < timeWindow) {
            events.push({
              type: "alert",
              name: event.type,
              color: "#ff9800",
            });
          }
        });
      }

      // Build tooltip with time and state
      let tooltipHTML = `<div class="tooltip-time">${timeStr}</div>`;

      // Add engagement state
      if (state) {
        tooltipHTML += `
          <div class="tooltip-event">
            <div class="event-icon" style="background: ${stateColors[state]}"></div>
            <span>${stateNames[state]}</span>
          </div>
        `;
      }

      // Add events
      if (events.length > 0) {
        events.forEach((event) => {
          tooltipHTML += `
            <div class="tooltip-event">
              <div class="event-icon" style="background: ${event.color}"></div>
              <span>${event.name}</span>
            </div>
          `;
        });
      }

      this.$timelineTooltip.querySelector(".tooltip-content").innerHTML =
        tooltipHTML;
      this.$timelineTooltip.classList.remove("hidden");

      // Position tooltip
      const tooltipRect = this.$timelineTooltip.getBoundingClientRect();
      let left = e.clientX - tooltipRect.width / 2;
      const top = rect.top - tooltipRect.height - 10;

      // Keep tooltip on screen
      if (left < 10) left = 10;
      if (left + tooltipRect.width > window.innerWidth - 10) {
        left = window.innerWidth - tooltipRect.width - 10;
      }

      this.$timelineTooltip.style.left = left + "px";
      this.$timelineTooltip.style.top = top + "px";
    });

    // Mouse leave to hide tooltip and hover indicator
    this.$timelineCanvas.addEventListener("mouseleave", () => {
      this.$timelineTooltip.classList.add("hidden");
      if (this.$timelineHoverIndicator) {
        this.$timelineHoverIndicator.style.display = "none";
      }
    });

    // Click to scrub video
    this.$timelineCanvas.addEventListener("click", (e) => {
      if (!this.currentRoute || !this.currentRoute.driveStats) return;

      const rect = this.$timelineCanvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const width = rect.width;
      const duration = this.currentRoute.driveStats.duration;
      const targetTime = (x / width) * duration;

      // Seek to that time
      this.seekToSeconds(targetTime);
    });
  }

  updatePlayerStarButton() {
    if (!this.currentRoute) return;

    const isStarred = this.currentRoute.isStarred || false;
    this.$playerStarText.textContent = isStarred ? "Preserved" : "Preserve";

    // Update button styling based on starred state
    if (isStarred) {
      this.$playerStarBtn.classList.add("starred");
    } else {
      this.$playerStarBtn.classList.remove("starred");
    }
  }

  closeVideo() {
    // Set flag to prevent error alerts during intentional cleanup
    this.isClosingVideo = true;

    this.$videoPlayer.classList.add("hidden");
    this.hideReplayOverlay();

    // Clear segment timer
    if (this.segmentTimer) {
      clearTimeout(this.segmentTimer);
      this.segmentTimer = null;
    }

    // Stop connection monitoring
    if (this.connectionCheckInterval) {
      clearInterval(this.connectionCheckInterval);
      this.connectionCheckInterval = null;
    }

    // Clean up HLS.js instance if it exists
    if (this.hls) {
      try {
        this.hls.destroy();
        console.log("HLS.js instance destroyed");
      } catch (e) {
        console.error("Error destroying HLS:", e);
      }
      this.hls = null;
    }

    // Clean up h265-web-player instance if it exists
    if (this.player) {
      try {
        // Try different cleanup methods that might be available
        if (typeof this.player.destroy === "function") {
          this.player.destroy();
        } else if (typeof this.player.dispose === "function") {
          this.player.dispose();
        } else if (typeof this.player.cleanup === "function") {
          this.player.cleanup();
        } else if (typeof this.player.stop === "function") {
          this.player.stop();
        } else {
          // If no cleanup method exists, just null the reference
          console.log(
            "Player object has no cleanup method, just clearing reference"
          );
        }
      } catch (e) {
        console.error("Error destroying player:", e);
      }
      this.player = null;
    }

    // Clear global canvas reference
    if (window.canvas) {
      window.canvas = null;
    }

    // Clean up standard video element if it exists
    if (this.$videoElement) {
      this.$videoElement.pause();
      this.$videoElement.src = "";
      this.$videoElement.load();
    }

    document.body.style.overflow = "";
    this.currentRoute = null;

    // Clean up log viewer
    if (this.logSyncInterval) {
      clearInterval(this.logSyncInterval);
      this.logSyncInterval = null;
    }
    this.currentLogMessages = null;
    this.currentLogStartTime = null;
    this.currentLogEndTime = null;
    this.$logMessages.innerHTML = "";
    this.$logMessages.classList.add("hidden");
    this.$logStatus.classList.add("hidden");
    this.$logPanelContent.classList.add("hidden");
    this.$logViewerContainer
      .querySelector(".log-viewer-empty")
      .classList.remove("hidden");
    this.$logCount.textContent = "0 messages";

    // Clean up cereal data viewer
    if (this.cerealSyncInterval) {
      clearInterval(this.cerealSyncInterval);
      this.cerealSyncInterval = null;
    }
    this.currentCerealData = null;
    this.currentCerealStartTime = null;
    this.currentCerealEndTime = null;
    this.currentCerealType = null;
    this.$cerealDataBody.innerHTML = "";
    this.$cerealDataTable.classList.add("hidden");
    this.$cerealPanelContent.classList.add("hidden");
    this.$cerealViewerContainer
      .querySelector(".cereal-viewer-empty")
      .classList.remove("hidden");
    this.$cerealLastUpdate.textContent = "Last updated: --";
    this.$cerealMessageCount.textContent = "0 messages";

    // Clear closing flag after cleanup completes
    setTimeout(() => {
      this.isClosingVideo = false;
    }, 100);
  }

  loadSegment(segmentIndex) {
    if (
      !this.currentRoute ||
      segmentIndex >= this.currentRoute.segments.length
    ) {
      return;
    }

    this.currentSegment = segmentIndex;
    const segment = this.currentRoute.segments[segmentIndex];
    const segmentNumber = segment.number;

    // Auto-reload logs and cereal data if they were previously loaded
    // This ensures sync stays accurate when switching segments
    if (this.currentLogMessages && this.currentLogMessages.length > 0) {
      this.loadLogs();
    }
    if (this.currentCerealData && this.currentCerealData.length > 0) {
      this.loadCerealData();
    }

    // UI is already updated above with camera type info

    // Check if current camera is available in this segment
    const availableCameras = Object.keys(segment.videos);
    if (!availableCameras.includes(this.currentCamera)) {
      // Fall back to first available camera
      if (availableCameras.length > 0) {
        this.currentCamera = availableCameras[0];
        this.updateCameraButtons();
      } else {
        console.error("No video available for this segment");
        return;
      }
    }

    // Get video URL - use selected camera (HEVC or LQ)
    let videoUrl = `${this.API_BASE}/api/video/${this.currentRoute.baseName}/${segmentNumber}/${this.currentCamera}`;
    // Add debug parameter if debug mode is enabled
    if (this.debugMode) {
      videoUrl += "?debug=true";
    }
    const videoCamera = this.currentCamera;
    const videoInfo = segment.videos[this.currentCamera];

    this.showVideoLoading();

    // Update UI to show camera type
    const cameraType =
      videoCamera === "lq"
        ? "LQ (H.264)"
        : `${videoCamera.toUpperCase()} (HEVC)`;
    this.$segmentInfo.textContent = `Segment ${segmentIndex + 1} of ${
      this.currentRoute.totalSegments
    } - ${cameraType}`;

    // Clean up any existing player instance
    if (this.player) {
      try {
        // Try different cleanup methods that might be available
        if (typeof this.player.destroy === "function") {
          this.player.destroy();
        } else if (typeof this.player.dispose === "function") {
          this.player.dispose();
        } else if (typeof this.player.cleanup === "function") {
          this.player.cleanup();
        } else if (typeof this.player.stop === "function") {
          this.player.stop();
        } else {
          // If no cleanup method exists, just null the reference
          console.log(
            "Player object has no cleanup method, just clearing reference"
          );
        }
      } catch (e) {
        console.error("Error destroying old player:", e);
      }
      this.player = null;
    }

    // Detect video type
    const isHEVC = videoCamera !== "lq";

    console.log("=== Video Playback Strategy ===");
    console.log("Video camera:", videoCamera);
    console.log("Is HEVC:", isHEVC);
    console.log("Native HEVC support:", this.hevcSupported);
    console.log("Native HLS support:", this.canUseNativeHLS());
    console.log(
      "HLS.js support:",
      typeof Hls !== "undefined" && Hls.isSupported()
    );
    console.log("WebGL support:", this.webglSupported);
    this.addDebugLog(
      "info",
      `[Playback Strategy] Camera: ${videoCamera}, HEVC: ${isHEVC}, HLS.js: ${
        typeof Hls !== "undefined" && Hls.isSupported()
      }, Native HLS: ${this.canUseNativeHLS()}`
    );

    const preferNativeHLS = this.isSafari && this.canUseNativeHLS();

    // Updated Strategy for cross-browser compatibility:
    // 1. If Safari with native HLS -> always use native playlist (best reliability).
    // 2. HEVC cameras (front/wide/driver) on other browsers:
    //    a. If HLS.js supported -> Use HLS.js
    //    b. Else if native HEVC -> Use direct video
    //    c. Else -> Try h265-web-player or fallback to LQ
    // 3. LQ camera (H.264) on other browsers:
    //    a. If HLS.js supported -> Use HLS.js
    //    b. Else if native HLS -> Use native HLS
    //    c. Else -> Use HTML5 video

    if (preferNativeHLS) {
      console.log("Safari detected - using native HLS route playback");
      this.addDebugLog("info", "[Playback Path] Native Safari HLS selected.");
      this.startNativeHLSRoutePlayback(this.currentRoute);
      return;
    }

    if (isHEVC) {
      // Try HLS.js first for HEVC (works on most modern browsers)
      if (typeof Hls !== "undefined" && Hls.isSupported()) {
        console.log("Using HLS.js for HEVC route playback (cross-browser)");
        this.addDebugLog("info", "[Playback Path] HLS.js for HEVC selected.");
        this.fallbackToStandardVideo();
      } else if (this.canUseNativeHLS()) {
        // Fallback to native HLS for Safari if HLS.js not available
        console.log("Using native Safari HLS for HEVC route playback");
        this.addDebugLog(
          "info",
          "[Playback Path] Native Safari HLS for HEVC selected."
        );
        this.startNativeHLSRoutePlayback(this.currentRoute);
      } else if (this.hevcSupported) {
        console.log("Using native browser HEVC playback (direct video)");
        this.addDebugLog(
          "info",
          "[Playback Path] Direct HEVC playback selected."
        );
        this.fallbackToStandardVideo();
      } else {
        // Try h265-web-player for browsers without native HEVC
        const playerAvailable =
          typeof Player !== "undefined" &&
          typeof Player.prototype.init === "function";

        console.log("Player available:", playerAvailable);

        if (playerAvailable && this.webglSupported) {
          console.log("Attempting to use h265-web-player for HEVC video");
          this.addDebugLog("info", "[Playback Path] h265-web-player selected.");
          this.initH265WebPlayer(videoUrl, videoCamera);
        } else {
          console.log(
            "h265-web-player not available, falling back to LQ camera"
          );
          this.addDebugLog(
            "warning",
            "[Playback Path] HEVC not supported, falling back to LQ."
          );
          // Switch to LQ camera as final fallback for HEVC content
          if (this.currentCamera !== "lq") {
            this.currentCamera = "lq";
            this.updateCameraButtons();
            this.loadSegment(this.currentSegment);
          } else {
            // LQ not available or already trying LQ, try standard video anyway
            this.addDebugLog(
              "error",
              "[Playback Path] LQ fallback failed, attempting direct video."
            );
            this.fallbackToStandardVideo();
          }
        }
      }
    } else {
      // LQ camera (H.264) - prefer HLS.js for consistent behavior
      if (typeof Hls !== "undefined" && Hls.isSupported()) {
        console.log("Using HLS.js for LQ camera route playback");
        this.addDebugLog("info", "[Playback Path] HLS.js for LQ selected.");
        this.fallbackToStandardVideo();
      } else if (this.canUseNativeHLS()) {
        console.log("Using native HLS for LQ camera route playback");
        this.addDebugLog("info", "[Playback Path] Native HLS for LQ selected.");
        this.startNativeHLSRoutePlayback(this.currentRoute);
      } else {
        console.log("Using standard HTML5 video for LQ camera");
        this.addDebugLog(
          "info",
          "[Playback Path] Standard video for LQ selected."
        );
        this.fallbackToStandardVideo();
      }
    }
  }

  playNextSegment() {
    const nextSegment = this.currentSegment + 1;
    if (nextSegment < this.currentRoute.segments.length) {
      this.loadSegment(nextSegment);
    } else {
      // End of route
      this.closeVideo();
    }
  }

  switchCamera(camera) {
    if (camera === this.currentCamera) return;

    // Check if camera is available for current segment
    const segment = this.currentRoute.segments[this.currentSegment];
    if (!segment.videos[camera]) {
      return;
    }

    // Prevent switching to HEVC cameras if not supported
    const isHEVCCamera = camera !== "lq";
    if (isHEVCCamera && !this.canPlayHEVC()) {
      console.warn(`Cannot switch to ${camera} camera - HEVC not supported`);
      return;
    }

    // Store current playback time for sync
    if (this.$videoElement) {
      this.lastPlaybackTime = this.$videoElement.currentTime || 0;
      console.log(
        `Storing playback time for camera sync: ${this.lastPlaybackTime}s`
      );
    }

    this.currentCamera = camera;
    this.updateCameraButtons();
    this.loadSegment(this.currentSegment);
  }

  updateCameraButtons() {
    if (
      !this.currentRoute ||
      this.currentSegment >= this.currentRoute.segments.length
    ) {
      return;
    }

    const segment = this.currentRoute.segments[this.currentSegment];
    const availableVideos = segment.videos || {};
    const canPlayHEVC = this.canPlayHEVC();

    this.$cameraButtons.forEach((btn) => {
      const camera = btn.dataset.camera;
      const isAvailable = availableVideos[camera];
      const isActive = camera === this.currentCamera;
      const isHEVCCamera = camera !== "lq";

      // Hide HEVC camera buttons if HEVC is not supported
      if (isHEVCCamera && !canPlayHEVC) {
        btn.style.display = "none";
        btn.disabled = true;
        return;
      }

      // Show button and update state
      btn.style.display = "";
      btn.disabled = !isAvailable;
      btn.classList.toggle("active", isActive);

      // Add visual indicator for camera type
      btn.title = `${camera.toUpperCase()} camera${
        isHEVCCamera ? " (HEVC)" : " (H.264)"
      }`;
    });
  }

  showVideoLoading() {
    this.$videoLoading.style.display = "block";
  }

  hideVideoLoading() {
    this.$videoLoading.style.display = "none";
  }

  showReplayOverlay() {
    console.log("Showing replay overlay");
    this.$videoReplay.classList.remove("hidden");
    // Pause the video if it's playing
    if (this.$videoElement && !this.$videoElement.paused) {
      this.$videoElement.pause();
    }
  }

  hideReplayOverlay() {
    console.log("Hiding replay overlay");
    this.$videoReplay.classList.add("hidden");
  }

  replayRoute() {
    console.log("Replaying route from beginning");
    this.hideReplayOverlay();

    // For HLS.js playback (full route) or native HLS
    if (this.hls && this.$videoElement) {
      this.seekToSeconds(0);
    }
    // For standard video element playback (single segment fallback)
    else if (this.$videoElement) {
      this.seekToSeconds(0);
    }
  }

  async checkNativeHEVCSupport() {
    // Check if browser supports HEVC/H.265 natively
    // This works on Safari (all versions), Chrome 107+, Edge 107+, and other Chromium browsers

    try {
      const video = document.createElement("video");

      // Test multiple HEVC codec strings
      const hevcCodecs = [
        'video/mp4; codecs="hvc1.1.6.L93.B0"', // HEVC Main profile
        'video/mp4; codecs="hev1.1.6.L93.B0"', // Alternative HEVC format
        'video/mp4; codecs="hvc1"', // Simplified
        'video/mp4; codecs="hev1"', // Simplified alternative
      ];

      for (const codec of hevcCodecs) {
        const support = video.canPlayType(codec);
        if (support === "probably" || support === "maybe") {
          console.log(
            `Native HEVC support detected with codec: ${codec} (${support})`
          );
          return true;
        }
      }

      console.log("No native HEVC support detected");
      return false;
    } catch (e) {
      console.warn("Error checking HEVC support:", e);
      return false;
    }
  }

  checkWebGLSupport() {
    try {
      const canvas = document.createElement("canvas");

      // Check for WebGL 2.0 first (preferred)
      let gl = canvas.getContext("webgl2");
      let version = "WebGL 2.0";

      // Fall back to WebGL 1.0
      if (!gl) {
        gl =
          canvas.getContext("webgl") || canvas.getContext("experimental-webgl");
        version = "WebGL 1.0";
      }

      if (!gl) {
        console.warn("WebGL not supported at all");
        return false;
      }

      console.log(`WebGL support detected: ${version}`);

      // Check WebGL context attributes
      const contextAttributes = gl.getContextAttributes();
      console.log("WebGL context attributes:", contextAttributes);

      // Check for hardware acceleration (antialiasing enabled usually indicates HW acceleration)
      const hasHardwareAcceleration = contextAttributes.antialias !== false;
      console.log(
        "Hardware acceleration:",
        hasHardwareAcceleration ? "Enabled" : "Disabled (software rendering)"
      );

      // Check for WebGL extensions (some may be optional for basic functionality)
      const requiredExtensions = [
        "OES_texture_float",
        "OES_standard_derivatives",
      ];

      const optionalExtensions = ["WEBGL_lose_context"];

      const missingRequired = requiredExtensions.filter((ext) => {
        if (!gl.getExtension(ext)) {
          console.warn(`Missing WebGL extension: ${ext}`);
          return true;
        }
        return false;
      });

      const missingOptional = optionalExtensions.filter((ext) => {
        if (!gl.getExtension(ext)) {
          console.log(`Missing optional WebGL extension: ${ext}`);
          return true;
        }
        return false;
      });

      // For now, don't fail completely if extensions are missing
      // The h265-web-player might work without some extensions
      if (missingRequired.length > 0) {
        console.warn(
          "Some WebGL extensions missing, but attempting HEVC playback anyway:",
          missingRequired
        );
        // Continue instead of failing
      }

      // Additional check: ensure WebGL context can actually be used for video decoding
      try {
        // Test creating textures and framebuffers (required for video decoding)
        const texture = gl.createTexture();
        const framebuffer = gl.createFramebuffer();

        if (!texture || !framebuffer) {
          console.warn(
            "Cannot create WebGL texture/framebuffer for video decoding"
          );
          gl.deleteTexture(texture);
          gl.deleteFramebuffer(framebuffer);
          // Continue anyway - h265-web-player might still work
        } else {
          gl.deleteTexture(texture);
          gl.deleteFramebuffer(framebuffer);
          console.log("WebGL texture/framebuffer creation test passed");
        }
      } catch (e) {
        console.error("WebGL texture/framebuffer test failed:", e);
        return false;
      }

      // Test basic WebGL functionality
      try {
        const shader = gl.createShader(gl.VERTEX_SHADER);
        if (!shader) {
          console.warn("Cannot create WebGL shader");
          return false;
        }
        gl.deleteShader(shader);

        // Test framebuffer creation (needed for video decoding)
        const framebuffer = gl.createFramebuffer();
        if (!framebuffer) {
          console.warn("Cannot create WebGL framebuffer");
          return false;
        }
        gl.deleteFramebuffer(framebuffer);

        console.log("WebGL functionality test passed");
      } catch (e) {
        console.warn("WebGL functionality test failed:", e);
        return false;
      }

      // Log WebGL capabilities
      console.log("WebGL vendor:", gl.getParameter(gl.VENDOR));
      console.log("WebGL renderer:", gl.getParameter(gl.RENDERER));
      console.log("WebGL version:", gl.getParameter(gl.VERSION));
      console.log("Max texture size:", gl.getParameter(gl.MAX_TEXTURE_SIZE));
      console.log(
        "Max renderbuffer size:",
        gl.getParameter(gl.MAX_RENDERBUFFER_SIZE)
      );

      return true;
    } catch (e) {
      console.warn("WebGL detection failed:", e);
      return false;
    }
  }

  /**
   * Initialize Firefox warning banner if Firefox is detected and HEVC is not supported
   */
  initFirefoxWarning() {
    if (!this.$firefoxWarning) {
      console.warn("Firefox warning element not found in DOM");
      return;
    }

    // Check if user has already dismissed the warning
    const dismissed = sessionStorage.getItem("firefoxWarningDismissed");
    if (dismissed === "true") {
      return;
    }

    // Show warning if Firefox is detected (regardless of HEVC support)
    // This informs users about limited functionality
    if (this.isFirefox) {
      console.log("Showing Firefox limited functionality warning");
      this.$firefoxWarning.classList.remove("hidden");
    }
  }

  switchToLQCamera() {
    console.log("Switching to LQ camera for HTML5 playback");

    // Check if LQ camera is available in current segment
    const segment = this.currentRoute.segments[this.currentSegment];
    if (segment && segment.videos && segment.videos.lq) {
      // Switch camera and reload segment
      this.currentCamera = "lq";
      this.updateCameraButtons();
      this.loadSegment(this.currentSegment);
    } else {
      // LQ not available, show error
      this.hideVideoLoading();
      console.error("LQ camera not available in current segment");
      console.error(
        "Available cameras:",
        segment ? Object.keys(segment.videos) : "No segment data"
      );
      console.error(
        "Route baseName:",
        this.currentRoute ? this.currentRoute.baseName : "No route data"
      );
      console.error("Current segment:", this.currentSegment);

      // Check if any cameras are available at all
      const availableCameras = segment ? Object.keys(segment.videos) : [];
      if (availableCameras.length === 0) {
        alert("No video cameras available for this route segment.");
      } else {
        alert(
          `Unable to play video. LQ camera not available. Available cameras: ${availableCameras.join(
            ", "
          )}`
        );
      }
    }
  }

  initH265WebPlayer(videoUrl, videoCamera) {
    // Validate canvas element
    if (!this.$videoCanvas) {
      this.hideVideoLoading();
      alert("Canvas element not available. Please refresh the page.");
      return;
    }

    try {
      console.log("Initializing h265-web-player for:", videoUrl);

      // Ensure canvas has proper dimensions and is visible
      const canvasWidth = this.$videoCanvas.clientWidth || 1280;
      const canvasHeight = this.$videoCanvas.clientHeight || 720;

      // Set canvas dimensions explicitly
      this.$videoCanvas.width = canvasWidth;
      this.$videoCanvas.height = canvasHeight;
      this.$videoCanvas.style.width = canvasWidth + "px";
      this.$videoCanvas.style.height = canvasHeight + "px";

      console.log("Canvas dimensions:", canvasWidth, "x", canvasHeight);

      // Create WebGL context FIRST before initializing player
      // This is required for h265-web-player to work properly
      const glCtx =
        this.$videoCanvas.getContext("webgl") ||
        this.$videoCanvas.getContext("experimental-webgl");

      if (!glCtx) {
        console.warn(
          "WebGL context not available, falling back to native video"
        );
        this.fallbackToStandardVideo();
        return;
      }

      console.log("WebGL context created successfully");

      // Set global canvas variable for h265-web-player library
      window.canvas = this.$videoCanvas;

      this.player = new Player();
      this.player.init({
        width: canvasWidth,
        height: canvasHeight,
        canvas: this.$videoCanvas,
        url: videoUrl,
      });

      console.log("Player initialized successfully");

      // Hide BluePilot spinner immediately - h265-web-player has its own loading indicator
      this.hideVideoLoading();

      // Monitor for h265-web-player errors and fallback if needed
      setTimeout(() => {
        // Check if the canvas has any content (indicating successful decoding)
        try {
          const canvas = this.$videoCanvas;
          if (canvas && this.player) {
            // If player exists but canvas is still blank, assume failure
            const context = canvas.getContext("2d");
            const imageData = context.getImageData(0, 0, 1, 1);
            const hasContent = imageData.data.some((value) => value > 0);

            if (!hasContent) {
              console.warn(
                "h265-web-player initialized but no video content detected, falling back to LQ camera"
              );
              this.hideVideoLoading();
              // Fall back to LQ camera
              if (this.currentCamera !== "lq") {
                this.currentCamera = "lq";
                this.updateCameraButtons();
                this.loadSegment(this.currentSegment);
              } else {
                this.fallbackToStandardVideo();
              }
            } else {
              console.log("h265-web-player appears to be working");
              this.hideVideoLoading();
            }
          } else {
            console.log("h265-web-player appears to be working");
            this.hideVideoLoading();
          }
        } catch (e) {
          console.error("Error checking h265-web-player status:", e);
          this.hideVideoLoading();
          // Fall back to LQ camera on error
          if (this.currentCamera !== "lq") {
            this.currentCamera = "lq";
            this.updateCameraButtons();
            this.loadSegment(this.currentSegment);
          } else {
            this.fallbackToStandardVideo();
          }
        }
      }, 3000); // Give it more time

      // Auto-play next segment after 60 seconds (each segment is ~1 minute)
      this.segmentTimer = setTimeout(() => {
        console.log("Segment duration reached, playing next");
        this.playNextSegment();
      }, 60000);
    } catch (error) {
      console.error("Error initializing h265-web-player:", error);
      this.hideVideoLoading();

      // Fallback to LQ camera
      console.log("Falling back to LQ camera due to h265-web-player error");
      if (this.currentCamera !== "lq") {
        this.currentCamera = "lq";
        this.updateCameraButtons();
        this.loadSegment(this.currentSegment);
      } else {
        this.fallbackToStandardVideo();
      }
    }
  }

  fallbackToDirectVideo() {
    if (!this.currentRoute) return;

    if (this.isSafari && this.canUseNativeHLS()) {
      console.log(
        "Safari detected during direct video fallback - switching to native HLS"
      );
      this.startNativeHLSRoutePlayback(this.currentRoute);
      return;
    }

    const routeBase = this.currentRoute.baseName;
    const segmentNumber =
      this.currentRoute.segments[this.currentSegment]?.number || 0;
    const directUrl = `${this.API_BASE}/api/video/${routeBase}/${segmentNumber}/${this.currentCamera}`;

    console.log("Falling back to direct video playback:", directUrl);

    // Clean up HLS.js instance if it exists
    if (this.hls) {
      try {
        this.hls.destroy();
        console.log("HLS.js instance destroyed");
      } catch (e) {
        console.error("Error destroying HLS:", e);
      }
      this.hls = null;
    }

    // Try direct video playback
    this.fallbackToStandardVideo();
  }

  /**
   * Start native HLS playback for Safari browsers
   * Uses the route-level playlist.m3u8 for seamless multi-segment playback
   * @param {object} route - The route object containing baseName and other metadata
   */
  startNativeHLSRoutePlayback(route) {
    try {
      console.log("Starting native HLS route playback for Safari");

      // Ensure we have a proper video element
      const video = this.ensureVideoElement();

      const camera = this.currentCamera || "front";
      const hlsUrl = `${this.API_BASE}/api/hls/${route.baseName}/${camera}/playlist.m3u8`;

      console.log("Using native HLS for Safari:", hlsUrl);
      console.log("Camera:", camera);

      // Detach any MSE / WebGL players if previously created
      if (this.hls) {
        try {
          this.hls.destroy();
          console.log("HLS.js instance destroyed");
        } catch (e) {
          console.error("Error destroying HLS:", e);
        }
        this.hls = null;
      }

      // Clean up h265-web-player if active
      if (this.player) {
        try {
          if (typeof this.player.destroy === "function") {
            this.player.destroy();
          }
        } catch (e) {
          console.warn("Error destroying h265-web-player:", e);
        }
        this.player = null;
      }

      // Wire up loadedmetadata for route-level duration
      video.onloadedmetadata = () => {
        console.log("Native HLS loaded, total duration:", video.duration);
        this.hideVideoLoading();
      };

      // Add detailed error logging for native HLS player
      video.onerror = (e) => {
        console.error("Native HLS video element error event:", e);
        const err = video.error;
        if (err) {
          console.error("Video Error Code:", err.code);
          console.error("Video Error Message:", err.message);
          this.addDebugLog(
            "error",
            `[HLS Error] Code: ${err.code} - Message: ${err.message}`
          );
        } else {
          console.error("Video error object not available.");
          this.addDebugLog(
            "error",
            `[HLS Error] An unknown video error occurred.`
          );
        }
      };

      // Wire up timeupdate to keep segment/time UI synchronized
      video.ontimeupdate = () => {
        const t = video.currentTime;
        // Calculate segment based on time (with small epsilon to handle rounding)
        const newSegment = Math.min(
          Math.floor(t / 60 + 0.0001),
          (this.currentRoute?.totalSegments || 1) - 1
        );

        if (newSegment !== this.currentSegment) {
          this.currentSegment = newSegment;

          // Update segment info display
          this.$segmentInfo.textContent = `Segment ${
            this.currentSegment + 1
          } of ${
            this.currentRoute.totalSegments
          } - ${this.currentCamera.toUpperCase()}`;

          // Auto-reload logs and cereal data if they were previously loaded
          if (this.currentLogMessages && this.currentLogMessages.length > 0) {
            this.loadLogs();
          }
          if (this.currentCerealData && this.currentCerealData.length > 0) {
            this.loadCerealData();
          }
        }
      };

      // Set the HLS playlist URL
      video.src = hlsUrl;
      video.load();

      // Show loading state
      this.showVideoLoading();

      // Attempt programmatic play
      // iOS often needs muted+playsinline set (done in ensureVideoElement)
      setTimeout(() => {
        video
          .play()
          .then(() => {
            console.log("Native HLS playback started successfully");
            this.hideVideoLoading();
          })
          .catch((e) => {
            console.warn("Autoplay failed (may require user gesture):", e);
            this.hideVideoLoading();
          });
      }, 100);
    } catch (error) {
      console.error("Error starting native HLS playback:", error);
      this.handleVideoError({ error });
    }
  }

  fallbackToStandardVideo() {
    try {
      if (this.isSafari && this.canUseNativeHLS()) {
        console.log(
          "Safari detected in fallbackToStandardVideo - using native HLS"
        );
        this.startNativeHLSRoutePlayback(this.currentRoute);
        return;
      }

      const isHEVC = this.currentCamera !== "lq";
      console.log(
        "fallbackToStandardVideo called for:",
        isHEVC ? "HEVC (remuxed MP4)" : "LQ (MPEG-TS)"
      );

      // Create a video element if it doesn't exist
      if (!this.$videoElement) {
        this.$videoElement = document.createElement("video");
        this.$videoElement.className = "video-element";
        this.$videoElement.controls = true;
        this.$videoElement.autoplay = false; // Disabled autoplay
        this.$videoElement.setAttribute("preload", "metadata"); // Load first frame only (thumbnail)
        this.$videoElement.style.width = "100%";
        this.$videoElement.style.height = "100%";
        this.$videoElement.style.backgroundColor = "#000";

        // Replace canvas with video element
        const videoWrapper = this.$videoCanvas.parentElement;
        videoWrapper.replaceChild(this.$videoElement, this.$videoCanvas);

        // Add event listeners
        this.$videoElement.addEventListener("loadstart", () => {
          console.log("Video loadstart event");
          this.showVideoLoading();
        });
        this.$videoElement.addEventListener("loadedmetadata", () => {
          console.log("Video metadata loaded:", {
            duration: this.$videoElement.duration,
            videoWidth: this.$videoElement.videoWidth,
            videoHeight: this.$videoElement.videoHeight,
          });
        });
        this.$videoElement.addEventListener("canplay", () => {
          console.log("Video can play");

          // Sync to stored playback time when switching cameras (for direct video fallback)
          if (
            this.lastPlaybackTime > 0 &&
            (typeof Hls === "undefined" || !Hls.isSupported())
          ) {
            console.log(
              `Seeking to synced time (direct video): ${this.lastPlaybackTime}s`
            );
            this.$videoElement.currentTime = this.lastPlaybackTime;
            this.lastPlaybackTime = 0; // Reset after use
          }

          this.hideVideoLoading();
        });
        this.$videoElement.addEventListener("playing", () => {
          console.log("Video is playing");
          this.hideVideoLoading();
        });
        this.$videoElement.addEventListener("ended", () => {
          console.log("Full route playback completed");
          this.showReplayOverlay();
        });
        this.$videoElement.addEventListener("timeupdate", () => {
          // Update timeline position indicator
          this.updateTimelinePosition();

          // Track which segment we're in during HLS playback for auto-reloading logs/cereal
          if (this.hls && this.$videoElement) {
            const currentTime = this.$videoElement.currentTime;
            const calculatedSegment = Math.floor(currentTime / 60); // Each segment is 60 seconds

            // If we've moved to a different segment, update and reload data
            if (
              calculatedSegment !== this.currentSegment &&
              calculatedSegment < this.currentRoute.totalSegments
            ) {
              this.currentSegment = calculatedSegment;

              // Update segment info display
              this.$segmentInfo.textContent = `Segment ${
                calculatedSegment + 1
              } of ${
                this.currentRoute.totalSegments
              } - ${this.currentCamera.toUpperCase()}`;

              // Auto-reload logs and cereal data if they were previously loaded
              if (
                this.currentLogMessages &&
                this.currentLogMessages.length > 0
              ) {
                this.loadLogs();
              }
              if (this.currentCerealData && this.currentCerealData.length > 0) {
                this.loadCerealData();
              }
            }
          }
        });
        this.$videoElement.addEventListener("error", (e) =>
          this.handleVideoError(e)
        );
      }

      // Clean up any existing HLS instance (shouldn't be necessary here, but just in case)
      if (this.hls) {
        try {
          this.hls.destroy();
          console.log("HLS.js instance destroyed");
        } catch (e) {
          console.error("Error destroying HLS:", e);
        }
        this.hls = null;
      }

      // Use HLS.js for the full route playlist (allows scrubbing and shows total duration)
      const hlsUrl = `${this.API_BASE}/api/hls/${this.currentRoute.baseName}/${this.currentCamera}/playlist.m3u8`;

      // Debug HLS.js availability
      console.log("=== HLS.js Check in fallbackToStandardVideo ===");
      console.log("typeof Hls:", typeof Hls);
      console.log("Hls defined:", typeof Hls !== "undefined");
      if (typeof Hls !== "undefined") {
        console.log("Hls.isSupported():", Hls.isSupported());
        console.log("Hls.version:", Hls.version || "unknown");
      }
      console.log("===========================================");

      // Always prefer HLS.js for consistent cross-browser behavior
      // Native Safari HLS has issues with on-demand remuxed segments
      if (typeof Hls !== "undefined" && Hls.isSupported()) {
        console.log("Using HLS.js for full route streaming");
        console.log("HLS playlist URL:", hlsUrl);

        // For non-Safari browsers using HLS.js
        // Use conservative buffer settings to handle large segments
        const bufferConfig = {
          maxBufferLength: 30,
          maxMaxBufferLength: 600,
          maxBufferSize: 60 * 1000 * 1000,
          maxBufferHole: 0.5,
        };

        this.hls = new Hls({
          debug: true,
          enableWorker: true,
          lowLatencyMode: false,
          // Platform-specific buffer management
          ...bufferConfig,
          // Better error recovery
          enableSoftwareAES: true,
          fragLoadingTimeOut: 20000,
          manifestLoadingTimeOut: 10000,
          levelLoadingTimeOut: 10000,
          // Backoff for retries
          fragLoadingMaxRetry: 3,
          levelLoadingMaxRetry: 2,
          manifestLoadingMaxRetry: 2,
        });

        // Track consecutive buffer errors for iOS/Safari recovery
        let bufferErrorCount = 0;
        const MAX_BUFFER_ERRORS = 5;

        this.hls.loadSource(hlsUrl);
        this.hls.attachMedia(this.$videoElement);

        this.hls.on(Hls.Events.MANIFEST_PARSED, () => {
          console.log(
            "HLS manifest parsed successfully - full route available for scrubbing"
          );

          // Sync to stored playback time when switching cameras
          if (this.lastPlaybackTime > 0) {
            console.log(`Seeking to synced time: ${this.lastPlaybackTime}s`);
            this.$videoElement.currentTime = this.lastPlaybackTime;
            this.lastPlaybackTime = 0; // Reset after use
          }

          // Autoplay disabled - user must click play
          // this.$videoElement.play().catch((e) => {
          //   console.warn("Autoplay failed:", e);
          // });
        });

        this.hls.on(Hls.Events.ERROR, (_event, data) => {
          console.error("HLS error:", data);

          if (data.fatal) {
            switch (data.type) {
              case Hls.ErrorTypes.NETWORK_ERROR:
                console.error("Fatal network error, trying to recover...");
                // For network errors, try to restart loading with retry logic
                if (this.retryCount < this.maxRetries) {
                  setTimeout(() => {
                    console.log("Attempting to restart HLS loading...");
                    this.hls.startLoad();
                  }, 1000);
                } else {
                  console.error(
                    "Max retries reached, falling back to direct video"
                  );
                  this.hls.destroy();
                  this.fallbackToDirectVideo();
                }
                break;
              case Hls.ErrorTypes.MEDIA_ERROR:
                console.error("Fatal media error, trying to recover...");
                // For media errors, try to recover the media source
                try {
                  this.hls.recoverMediaError();
                } catch (e) {
                  console.error(
                    "Media recovery failed, falling back to direct video"
                  );
                  this.hls.destroy();
                  this.fallbackToDirectVideo();
                }
                break;
              default:
                console.error(
                  "Cannot recover from HLS error, falling back to direct video"
                );
                this.hls.destroy();
                this.fallbackToDirectVideo();
                break;
            }
          } else {
            // Non-fatal errors - log and handle specific cases
            console.warn("Non-fatal HLS error:", data.details);

            // Handle bufferFullError specifically for iOS/Safari
            if (data.details === "bufferFullError") {
              bufferErrorCount++;
              console.warn(
                `Buffer full error (${bufferErrorCount}/${MAX_BUFFER_ERRORS})`
              );

              if (bufferErrorCount >= MAX_BUFFER_ERRORS) {
                console.error(
                  "Too many consecutive buffer errors - falling back to direct video"
                );
                console.error(
                  "This usually means the segments are too large for iOS Safari to handle"
                );
                this.hls.destroy();
                this.fallbackToDirectVideo();
              }
            } else {
              // Reset counter on other error types
              bufferErrorCount = 0;
            }
          }
        });

        // Reset buffer error counter on successful fragment loads
        this.hls.on(Hls.Events.FRAG_LOADED, () => {
          if (bufferErrorCount > 0) {
            console.log(
              "Fragment loaded successfully - resetting buffer error count"
            );
            bufferErrorCount = 0;
          }
        });

        // Handle buffer issues for debugging
        this.hls.on(Hls.Events.BUFFER_APPENDING, (_event, data) => {
          // Log buffer events for debugging
          if (data.type === "video") {
            console.debug("Appending video buffer:", data.data.length, "bytes");
          }
        });

        this.hls.on(Hls.Events.BUFFER_EOS, () => {
          console.debug("Buffer reached end of stream");
        });

        this.hls.on(Hls.Events.BUFFER_RESET, () => {
          console.debug("Buffer reset");
        });
      } else if (
        this.$videoElement.canPlayType("application/vnd.apple.mpegurl")
      ) {
        // Fallback to Safari native HLS (only if HLS.js not available)
        console.warn(
          "⚠️ HLS.js not supported, using native Safari HLS (may have issues with on-demand remuxing)"
        );
        console.log("HLS playlist URL:", hlsUrl);

        // Add event listeners for better debugging
        this.$videoElement.addEventListener("loadedmetadata", () => {
          console.log(
            "Native HLS: Metadata loaded, duration:",
            this.$videoElement.duration
          );
        });

        this.$videoElement.addEventListener("ended", () => {
          console.log("Native HLS: Playback ended");
          this.showReplayOverlay();
        });

        this.$videoElement.addEventListener("error", (e) => {
          console.error("Native HLS: Video error", e);
          const error = this.$videoElement.error;
          if (error) {
            console.error("Error code:", error.code, "Message:", error.message);
          }
        });

        this.$videoElement.src = hlsUrl;
        this.$videoElement.load();

        // Autoplay disabled - user must click play
        // setTimeout(() => {
        //   this.$videoElement.play().catch((e) => {
        //     console.warn("Autoplay failed:", e);
        //   });
        // }, 100);
      } else {
        // HLS not supported, fallback to direct video (segment by segment)
        console.warn(
          "HLS not supported, falling back to direct video playback"
        );
        this.fallbackToDirectVideo();
      }
    } catch (error) {
      console.error("Video initialization failed:", error);
      this.hideVideoLoading();
      alert("Video playback not available. Please check console for details.");
    }
  }

  startConnectionMonitoring() {
    // Check connection status every 30 seconds
    this.connectionCheckInterval = setInterval(() => {
      this.checkServerConnection();
    }, 30000);

    // Also check on page visibility changes (when user returns to tab)
    document.addEventListener("visibilitychange", () => {
      if (!document.hidden) {
        this.checkServerConnection();
      }
    });
  }

  async checkServerConnection() {
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 5000);

      const response = await fetch(`${this.API_BASE}/api/status`, {
        signal: controller.signal,
      });

      clearTimeout(timeoutId);

      if (response.ok) {
        // Server is responsive, reset retry counter
        this.retryCount = 0;
        this.retryDelay = 1000;
      }
    } catch (error) {
      console.warn("Server connection check failed:", error);
      // Don't show alert for connection checks, just log
    }
  }

  async initWebSocket() {
    // Check if WebSocket is supported by the browser
    this.websocketSupported = "WebSocket" in window;

    if (!this.websocketSupported) {
      console.log("WebSocket not supported by browser - using HTTP polling");
      return;
    }

    // Try to connect immediately
    this.attemptWebSocketConnection();

    // Also check periodically in case websockets becomes available later
    this.websocketCheckInterval = setInterval(() => {
      if (!this.useWebSocket && !this.isRetrying) {
        this.attemptWebSocketConnection();
      }
    }, 10000); // Check every 10 seconds
  }

  attemptWebSocketConnection() {
    if (this.isRetrying) {
      return; // Already trying to connect
    }

    // Construct WebSocket URL (same origin, different port)
    const wsProtocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const wsHost = window.location.hostname;
    const wsPort = 8089; // WebSocket server port
    const wsUrl = `${wsProtocol}//${wsHost}:${wsPort}`;

    try {
      console.log(`[WebSocket] Attempting connection to: ${wsUrl}`);
      console.log(`[WebSocket] Browser: ${this.isSafari ? "Safari" : "Other"}`);
      console.log(`[WebSocket] Page protocol: ${window.location.protocol}`);
      console.log(`[WebSocket] Page origin: ${window.location.origin}`);

      this.isRetrying = true;

      this.websocket = new WebSocket(wsUrl);

      // Safari-specific: Add connection timeout to prevent infinite hang
      // If WebSocket doesn't connect within 5 seconds, fall back to HTTP polling
      const connectionTimeout = setTimeout(() => {
        if (!this.websocketConnected && this.websocket) {
          console.warn(
            "[WebSocket] Connection timeout after 5 seconds - falling back to HTTP polling"
          );
          this.websocket.close();
          this.websocketConnected = false;
          this.isRetrying = false;
          this.enableFallbackPolling();
        }
      }, 5000);

      this.websocket.onopen = (event) => {
        console.log("[WebSocket] ✓ Connected successfully");
        clearTimeout(connectionTimeout);
        this.websocketConnected = true;
        this.useWebSocket = true;
        this.isRetrying = false;

        // Disable fallback polling since WebSocket is working
        this.disableFallbackPolling();

        // Update connection status indicator
        this.updateConnectionStatus("websocket");
      };

      this.websocket.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data);
          this.handleWebSocketMessage(message);
        } catch (error) {
          console.error("Error parsing WebSocket message:", error);
        }
      };

      this.websocket.onclose = (event) => {
        console.log("WebSocket disconnected:", event.code, event.reason);
        this.websocketConnected = false;
        this.isRetrying = false;

        // If WebSocket was previously working, try to reconnect
        if (this.useWebSocket) {
          console.log(
            "WebSocket lost - attempting to reconnect in 5 seconds..."
          );
          setTimeout(() => {
            this.attemptWebSocketConnection();
          }, 5000);
        } else {
          // Fall back to HTTP polling
          this.enableFallbackPolling();
        }
      };

      this.websocket.onerror = (error) => {
        console.error("WebSocket error:", error);
        this.websocketConnected = false;
        this.isRetrying = false;
        // Don't show error to user - fallback to polling will handle it
      };
    } catch (error) {
      console.error("Failed to create WebSocket connection:", error);
      this.websocketSupported = false;
      this.isRetrying = false;
    }
  }

  handleWebSocketMessage(message) {
    console.log("WebSocket message received:", message.type, message.data);

    switch (message.type) {
      case "connection_established":
        this.handleWebSocketConnectionEstablished(message.data);
        break;

      case "routes_updated":
        this.handleWebSocketRoutesUpdated(message.data);
        break;

      case "route_added":
        this.handleWebSocketRouteAdded(message.data);
        break;

      case "route_deleted":
        this.handleWebSocketRouteDeleted(message.data);
        break;

      case "route_starred":
      case "route_unstarred":
      case "route_preserved":
      case "route_unpreserved":
        this.handleWebSocketRouteStarred(message.data, message.type);
        break;

      case "status_changed":
        this.handleWebSocketStatusChanged(message.data);
        break;

      case "processing_update":
        this.handleWebSocketProcessingUpdate(message.data);
        break;

      case "processing_started":
        this.handleWebSocketProcessingStarted(message.data);
        break;

      case "processing_completed":
        this.handleWebSocketProcessingCompleted(message.data);
        break;

      case "cache_cleared":
        this.handleWebSocketCacheCleared(message.data);
        break;

      case "disk_updated":
        this.handleWebSocketDiskUpdated(message.data);
        break;

      case "route_export_update":
        this.handleWebSocketRouteExportUpdate(message.data);
        break;

      case "ffmpeg_log":
        this.handleFFmpegLog(message.data);
        break;

      case "param_updated":
        this.handleWebSocketParamUpdated(message.data);
        break;

      case "system_metrics_updated":
        this.handleWebSocketSystemMetricsUpdated(message.data);
        break;

      case "heartbeat":
        // Just a keep-alive message, ignore
        break;

      default:
        console.log("Unknown WebSocket message type:", message.type);
    }
  }

  handleWebSocketDiskUpdated(data) {
    // Disk space changed - update visualization
    console.log("Disk space updated via WebSocket");
    this.updateDiskVisualization();
  }

  handleWebSocketRouteExportUpdate(data) {
    if (
      !this.currentRoute ||
      !data ||
      data.route !== this.currentRoute.baseName ||
      data.camera !== this.currentCamera
    ) {
      return;
    }

    if (data.status === "ready") {
      const fallbackFilename = this.buildRouteExportFilename(
        this.currentRoute,
        this.currentCamera
      );
      this.enqueueRouteDownload(data, fallbackFilename);
      this.setDownloadButtonState({
        loading: false,
        text: "Download Route",
      });
      return;
    }

    this.updateRouteDownloadUIFromStatus(data);

    if (data.status === "error") {
      this.updateRouteDownloadStatus(
        data.message || "Video generation failed",
        "error"
      );
      this.setDownloadButtonState({
        loading: false,
        text: "Download Route",
      });
    }
  }

  handleWebSocketConnectionEstablished(data) {
    // Update device status based on WebSocket connection
    if (data.status) {
      this.setDeviceStatusUI(data.status);
    }
  }

  handleWebSocketRoutesUpdated(data) {
    // Full routes list updated - reload everything
    console.log("Routes updated via WebSocket - reloading");
    this.loadRoutes();
  }

  handleWebSocketRouteAdded(data) {
    // Individual route added - add to list without full reload
    console.log("Route added via WebSocket:", data.route_base, data);

    // Check if route already exists (avoid duplicates)
    const existingRoute = this.routes.find(
      (r) => r.baseName === data.route_base
    );
    if (existingRoute) {
      console.log("Route already exists, ignoring duplicate add event");
      return;
    }

    // If we have full route data, add it directly
    if (data.baseName && data.timestamp) {
      // Add new route to the beginning of the list (most recent first)
      this.routes.unshift(data);

      // Update hash to reflect the new state
      this.routesHash = this.computeRoutesHash(this.routes);

      // Re-render routes and update stats
      this.renderRoutes();
      this.updateStats();

      // Show notification with local timezone
      const displayDate = this.formatLocalDate(data.timestamp);
      const displayTime = this.formatLocalTime(data.timestamp);

      // Calculate end time
      let displayEndTime = "";
      if (data.duration) {
        try {
          // Append 'Z' to treat timestamp as UTC (same as formatLocalTime does)
          let timestamp = data.timestamp;
          if (!timestamp.includes("+") && !timestamp.endsWith("Z")) {
            timestamp = timestamp + "Z";
          }
          const startDate = new Date(timestamp);

          const durationMatch = data.duration.match(
            /(?:(\d+)h\s*)?(?:(\d+)m)?/
          );
          if (durationMatch) {
            const hours = parseInt(durationMatch[1] || 0);
            const minutes = parseInt(durationMatch[2] || 0);
            const totalMinutes = hours * 60 + minutes;
            const endDate = new Date(
              startDate.getTime() + totalMinutes * 60 * 1000
            );
            displayEndTime = endDate.toLocaleTimeString("en-US", {
              hour: "numeric",
              minute: "2-digit",
              hour12: true,
            });
          }
        } catch (e) {
          console.error("Error calculating end time:", e);
        }
      }

      const timeRange = displayEndTime
        ? `${displayTime} - ${displayEndTime}`
        : displayTime;
      this.showNotification(`New route recorded: ${displayDate} ${timeRange}`);
    } else {
      // No full data, do a full reload
      console.log("Incomplete route data, reloading routes list");
      this.loadRoutes();
    }
  }

  handleWebSocketRouteDeleted(data) {
    // Individual route deleted - remove from list without full reload
    console.log("Route deleted via WebSocket:", data.route_base);

    // Remove from local routes array
    this.routes = this.routes.filter((r) => r.baseName !== data.route_base);

    // Update hash to reflect the new state
    this.routesHash = this.computeRoutesHash(this.routes);

    // Re-render routes and update stats
    this.renderRoutes();
    this.updateStats();

    // If currently viewing deleted route, close video player
    if (this.currentRoute && this.currentRoute.baseName === data.route_base) {
      this.closeVideo();
    }
  }

  handleWebSocketRouteStarred(data, eventType) {
    // Route preserved/unpreserved - update local state
    console.log(
      "Route preserve status changed via WebSocket:",
      data.route_base,
      eventType
    );

    const isStarred =
      data?.is_preserved ??
      data?.is_starred ??
      data?.isPreserved ??
      data?.isStarred;

    if (typeof isStarred !== "boolean") {
      console.warn("WebSocket preserve event missing boolean status:", data);
      return;
    }

    // Update in local routes array
    const routeInList = this.routes.find((r) => r.baseName === data.route_base);
    if (routeInList) {
      routeInList.isStarred = isStarred;

      // Update hash to reflect the new state
      this.routesHash = this.computeRoutesHash(this.routes);
    }

    // Update current route if it's the same
    if (this.currentRoute && this.currentRoute.baseName === data.route_base) {
      this.currentRoute.isStarred = isStarred;
      this.updatePlayerStarButton();
    }

    // Re-render routes to show updated star status
    this.renderRoutes();
  }

  handleWebSocketStatusChanged(data) {
    // Device status changed (onroad/offroad)
    console.log("Device status changed via WebSocket:", data.status);

    // Determine UI status based on data
    let uiStatus = data.status;
    if (data.onroad === true) {
      uiStatus = "onroad";
    } else if (data.status === "online") {
      uiStatus = "online";
    }

    this.setDeviceStatusUI(uiStatus);
  }

  handleWebSocketProcessingUpdate(data) {
    // Background processing status update
    console.log("Processing update via WebSocket:", data);

    const { route_base, status, progress, message } = data;

    // Update route in the list if it exists
    const route = this.routes.find((r) => r.baseName === route_base);
    if (route) {
      // Add processing status to route object
      route.processingStatus = status;
      route.processingProgress = progress;

      // If processing completed, refresh that specific route's data
      if (status === "completed") {
        // Mark route as processed - it now has GPS data
        console.log(`Route ${route_base} processing completed`);

        // Reload just this route's data to get updated metrics
        this.refreshSingleRoute(route_base);
      }
    }

    // Log message if provided
    if (message) {
      console.info("Processing:", message);
    }

    // Could add visual indicators in the future:
    // - Show spinner on route card while processing
    // - Show progress bar
    // - Show completion animation
  }

  handleWebSocketProcessingStarted(data) {
    // Batch processing started
    console.log("Processing started:", data.total_routes, "routes");
    // Could show a global processing indicator
  }

  handleWebSocketProcessingCompleted(data) {
    // Batch processing completed
    console.log(
      "Processing completed:",
      data.processed_count,
      "routes in",
      data.total_time,
      "seconds"
    );
    // Could show completion notification or hide processing indicator
  }

  handleWebSocketCacheCleared(data) {
    // Cache was cleared - update UI
    console.log("Cache cleared via WebSocket:", data.cleared);

    // Show notification to user
    const totalCleared = Object.values(data.cleared).reduce(
      (sum, count) => sum + count,
      0
    );
    if (totalCleared > 0) {
      this.showNotification(`Cache cleared: ${totalCleared} items removed`);
    }
  }

  handleWebSocketParamUpdated(data) {
    // Parameter was updated externally (from Qt UI, CLI, etc.)
    console.log("Parameter updated via WebSocket:", data.key, "=", data.value);

    // Update local params cache
    if (this.allParams[data.key]) {
      this.allParams[data.key].value = data.value;
    } else {
      // New param we didn't know about
      this.allParams[data.key] = {
        key: data.key,
        value: data.value,
        category: "other",
        readonly: false,
        critical: false,
        type:
          typeof data.value === "boolean"
            ? "bool"
            : typeof data.value === "number"
            ? Number.isInteger(data.value)
              ? "int"
              : "float"
            : "string",
      };
    }

    // If we're on the params page, re-render the table
    if (this.currentView === "params") {
      // If there's a search active, respect it
      const searchValue = this.$paramsSearch ? this.$paramsSearch.value : "";
      if (searchValue) {
        this.searchParams(searchValue);
      } else {
        this.renderParamsTable();
      }
    }

    // Update landing page params count
    if (this.currentView === "landing") {
      this.updateLandingPage();
    }

    // Show notification if param change source is external
    if (data.source === "external") {
      console.log(`External param change: ${data.key} = ${data.value}`);
    }
  }

  handleWebSocketSystemMetricsUpdated(data) {
    console.log("System metrics updated via WebSocket", data);

    // Update metrics modal if it's currently visible
    if (!this.$metricsModal.classList.contains("hidden")) {
      this.updateMetricsDisplay(data);
    }
  }

  updateMetricsDisplay(data) {
    // Update CPU metrics
    if (data.cpu) {
      const cpuLoad = data.cpu.load_avg[0].toFixed(2);
      document.getElementById("metric-cpu-load").textContent = cpuLoad;
      document.getElementById("metric-cpu-1min").textContent = data.cpu.load_avg[0].toFixed(2);
      document.getElementById("metric-cpu-5min").textContent = data.cpu.load_avg[1].toFixed(2);
      document.getElementById("metric-cpu-cores").textContent = data.cpu.cpu_count;
    }

    // Update Memory metrics
    if (data.memory) {
      const memPercent = data.memory.percent.toFixed(1);
      document.getElementById("metric-memory-percent").textContent = memPercent;
      document.getElementById("metric-memory-used").textContent = (data.memory.used / (1024 ** 3)).toFixed(2) + " GB";
      document.getElementById("metric-memory-total").textContent = (data.memory.total / (1024 ** 3)).toFixed(2) + " GB";
    }

    // Update Disk metrics
    if (data.disk) {
      const diskPercent = data.disk.percent.toFixed(1);
      document.getElementById("metric-disk-percent").textContent = diskPercent;
      document.getElementById("metric-disk-used").textContent = (data.disk.used / (1024 ** 3)).toFixed(2) + " GB";
      document.getElementById("metric-disk-total").textContent = (data.disk.total / (1024 ** 3)).toFixed(2) + " GB";
    }

    // Update Temperature metrics
    if (data.temperature) {
      const avgTemp = ((data.temperature.cpu + data.temperature.gpu) / 2).toFixed(1);
      document.getElementById("metric-temp-avg").textContent = avgTemp;
      document.getElementById("metric-temp-cpu").textContent = data.temperature.cpu.toFixed(1) + "°C";
      document.getElementById("metric-temp-gpu").textContent = data.temperature.gpu.toFixed(1) + "°C";
    }

    // Update FFmpeg metrics
    if (data.ffmpeg) {
      document.getElementById("metric-ffmpeg-active").textContent = data.ffmpeg.active_count;
      document.getElementById("metric-ffmpeg-max").textContent = data.ffmpeg.max_workers;
      const statusText = data.ffmpeg.active_count > 0
        ? `${data.ffmpeg.active_count} active`
        : "Idle";
      document.getElementById("metric-ffmpeg-status").textContent = statusText;
    }

    // Update Cache metrics
    if (data.cache) {
      const cacheSize = (data.cache.size / (1024 ** 3)).toFixed(2);
      document.getElementById("metric-cache-size").textContent = cacheSize;
    }

    // Update timestamp
    const now = new Date();
    document.getElementById("metrics-timestamp").textContent = now.toLocaleTimeString();
  }

  async refreshSingleRoute(routeBase) {
    // Refresh data for a single route without reloading entire list
    try {
      const response = await fetch(`/api/routes/${routeBase}`);
      if (!response.ok) {
        console.warn(`Failed to refresh route ${routeBase}`);
        return;
      }

      const data = await response.json();
      if (data.success) {
        // Update the route in our local array
        const routeIndex = this.routes.findIndex(
          (r) => r.baseName === routeBase
        );
        if (routeIndex >= 0) {
          // Preserve any client-side properties (like processing status)
          const oldRoute = this.routes[routeIndex];
          this.routes[routeIndex] = {
            ...data,
            processingStatus: oldRoute.processingStatus,
            processingProgress: oldRoute.processingProgress,
          };

          // Update hash to reflect the new state
          this.routesHash = this.computeRoutesHash(this.routes);

          // Re-render to show updated data
          this.renderRoutes();

          console.log(`Refreshed route ${routeBase} with updated GPS data`);
        }
      }
    } catch (error) {
      console.error(`Error refreshing route ${routeBase}:`, error);
    }
  }

  setupFallbackPolling() {
    // Setup HTTP polling as fallback when WebSocket is not available or fails

    // Routes polling (every 30 seconds when video player is closed)
    this.routesPollingInterval = setInterval(() => {
      if (
        this.$videoPlayer.classList.contains("hidden") &&
        !this.useWebSocket
      ) {
        this.loadRoutes();
      }
    }, 30000);

    // Status polling (every 30 seconds)
    this.statusPollingInterval = setInterval(() => {
      if (!this.useWebSocket) {
        this.updateDeviceStatus();
      }
    }, 30000);

    // Also check on page visibility changes (when user returns to tab)
    document.addEventListener("visibilitychange", () => {
      if (!document.hidden && !this.useWebSocket) {
        this.checkServerConnection();
      }
    });
  }

  enableFallbackPolling() {
    console.log("Enabling HTTP fallback polling");
    this.useWebSocket = false;
    this.pollingDisabled = false;
    this.updateConnectionStatus("http");

    // Restart polling if it was stopped
    if (!this.routesPollingInterval && !this.statusPollingInterval) {
      this.setupFallbackPolling();
    }
  }

  disableFallbackPolling() {
    console.log("Disabling HTTP fallback polling (WebSocket active)");
    this.pollingDisabled = true;

    // Clear polling intervals completely
    if (this.routesPollingInterval) {
      clearInterval(this.routesPollingInterval);
      this.routesPollingInterval = null;
    }
    if (this.statusPollingInterval) {
      clearInterval(this.statusPollingInterval);
      this.statusPollingInterval = null;
    }

    console.log("HTTP polling stopped - all updates now via WebSocket");
  }

  updateConnectionStatus(type) {
    // Update device status badge to show WebSocket indicator
    console.log(`Connection mode: ${type}`);

    if (type === "websocket") {
      // Show WebSocket icon and enhance badge styling
      this.$websocketIcon.classList.remove("hidden");
      this.$deviceStatus.classList.add("websocket-active");
      this.$deviceStatus.title =
        "Device online (WebSocket - real-time updates)";
    } else {
      // Hide WebSocket icon for HTTP polling or offline
      this.$websocketIcon.classList.add("hidden");
      this.$deviceStatus.classList.remove("websocket-active");
      if (type === "http") {
        this.$deviceStatus.title =
          "Device online (HTTP polling - fallback mode)";
      } else {
        this.$deviceStatus.title = "Device status";
      }
    }
  }

  showNotification(message) {
    // Simple notification system - could be enhanced
    console.info("Notification:", message);

    // For now, just show in console - could add toast notifications
    // TODO: Implement proper toast notification system
  }

  async retryWithBackoff(operation, maxRetries = 3) {
    if (this.isRetrying) {
      console.log("Already retrying, skipping duplicate retry");
      return false;
    }

    this.isRetrying = true;

    for (let attempt = 0; attempt < maxRetries; attempt++) {
      try {
        await operation();
        this.isRetrying = false;
        this.retryCount = 0;
        this.retryDelay = 1000;
        return true; // Success
      } catch (error) {
        this.retryCount++;
        console.warn(`Attempt ${attempt + 1} failed:`, error);

        if (attempt < maxRetries - 1) {
          // Exponential backoff: 1s, 2s, 4s
          const delay = this.retryDelay * Math.pow(2, attempt);
          console.log(`Retrying in ${delay}ms...`);
          await new Promise((resolve) => setTimeout(resolve, delay));
        }
      }
    }

    this.isRetrying = false;
    return false; // All retries failed
  }

  isNetworkError(error) {
    // Check if error is network-related (retryable)
    if (error.name === "AbortError") return true; // Request timeout
    if (error.message && error.message.includes("fetch")) return true; // Network fetch error
    if (error.code === "NETWORK_ERR" || error.code === "NETWORK_ERROR")
      return true;

    // Check video element errors
    if (error.target && error.target.error) {
      const videoError = error.target.error;
      return videoError.code === videoError.MEDIA_ERR_NETWORK;
    }

    return false;
  }

  isPermanentError(error) {
    // Check if error is permanent (not retryable)
    if (error.status === 404) return true; // File not found
    if (error.status === 403) return true; // Forbidden
    if (error.status >= 500) return false; // Server errors are retryable

    // Check video element errors
    if (error.target && error.target.error) {
      const videoError = error.target.error;
      // MEDIA_ERR_SRC_NOT_SUPPORTED and MEDIA_ERR_DECODE are permanent
      return (
        videoError.code === videoError.MEDIA_ERR_SRC_NOT_SUPPORTED ||
        videoError.code === videoError.MEDIA_ERR_DECODE
      );
    }

    return false;
  }

  handleVideoError(e) {
    console.error("Video error:", e);
    this.hideVideoLoading();

    // Ignore errors during intentional video cleanup
    if (this.isClosingVideo) {
      console.log("Ignoring error during video close");
      return;
    }

    // Determine error type and handle appropriately
    const isNetworkErr = this.isNetworkError(e);
    const isPermanentErr = this.isPermanentError(e);

    if (isNetworkErr && !isPermanentErr) {
      // Network error - try to retry
      console.log("Network error detected, attempting retry...");

      const retryOperation = async () => {
        // Try to reload the current segment
        if (this.currentRoute && this.currentSegment !== undefined) {
          this.loadSegment(this.currentSegment);
        }
      };

      // For network errors, try immediate retry without complex async logic
      // since this method isn't async
      if (this.currentRoute && this.currentSegment !== undefined) {
        console.log("Attempting immediate retry for network error...");
        this.loadSegment(this.currentSegment);
        return; // Don't show error alert, trying again
      }
    }

    // If HEVC format failed and we have LQ available, try fallback
    if (this.currentRoute && this.currentCamera !== "lq" && !isPermanentErr) {
      const segment = this.currentRoute.segments[this.currentSegment];
      if (segment && segment.videos && segment.videos.lq) {
        console.log("HEVC playback failed, falling back to LQ (H.264) camera");
        this.currentCamera = "lq";
        this.updateCameraButtons();
        this.loadSegment(this.currentSegment);
        return; // Don't show error, attempting fallback
      }
    }

    // Permanent error or all retries/fallbacks failed - show user-friendly message
    console.error("Video loading failed permanently");
    console.error("Current camera:", this.currentCamera);
    console.error(
      "Route:",
      this.currentRoute ? this.currentRoute.baseName : "No route"
    );
    console.error("Segment:", this.currentSegment);
    console.error(
      "Video element error:",
      this.$videoElement ? this.$videoElement.error : "No video element"
    );

    // Provide helpful error message based on error type
    let message = "Video playback error.";

    if (isNetworkErr) {
      message =
        "Network connection issue. Please check your internet connection and server status.";
    } else if (isPermanentErr) {
      if (this.currentCamera === "lq") {
        message =
          "Video file not found or corrupted. The route may be missing video data.";
      } else {
        message =
          "Video format not supported by your browser. Try using a different browser or the LQ camera option.";
      }
    } else {
      message =
        "Video failed to load. This may be due to server issues or corrupted video files.";
    }

    // Show error info in console for debugging (but not as alert spam)
    console.error("Detailed error information:");
    console.error("- Current camera:", this.currentCamera);
    console.error("- Route:", this.currentRoute?.baseName);
    console.error("- Segment:", this.currentSegment);
    console.error("- Video URL attempted:", this.$videoElement?.src || "None");
    console.error(
      "- Error type:",
      isNetworkErr ? "Network" : isPermanentErr ? "Permanent" : "Unknown"
    );

    // Only show alert for permanent errors or after retries fail
    if (
      isPermanentErr ||
      (isNetworkErr && this.retryCount >= this.maxRetries)
    ) {
      alert(
        message +
          "\n\nIf this problem persists, try refreshing the page or check the browser console for more details."
      );
    }
  }

  async toggleStar(route) {
    try {
      const { isStarred } = await this.sendPreserveToggle(
        route.baseName,
        "route"
      );

      // Update local state
      route.isStarred = isStarred;

      // Update the routes array
      const routeInList = this.routes.find(
        (r) => r.baseName === route.baseName
      );
      if (routeInList) {
        routeInList.isStarred = isStarred;

        // Update hash to reflect the new state
        this.routesHash = this.computeRoutesHash(this.routes);
      }

      // Refresh display without full reload
      this.renderRoutes();
    } catch (error) {
      this.handlePreserveToggleError(error);
    }
  }

  async toggleStarFromPlayer() {
    if (!this.currentRoute) return;

    try {
      const { isStarred } = await this.sendPreserveToggle(
        this.currentRoute.baseName,
        "player"
      );

      // Update current route state
      this.currentRoute.isStarred = isStarred;

      // Update the routes array
      const routeInList = this.routes.find(
        (r) => r.baseName === this.currentRoute.baseName
      );
      if (routeInList) {
        routeInList.isStarred = isStarred;

        // Update hash to reflect the new state
        this.routesHash = this.computeRoutesHash(this.routes);
      }

      // Update player UI
      this.updatePlayerStarButton();

      // Update routes list in background (no reload)
      this.renderRoutes();
    } catch (error) {
      this.handlePreserveToggleError(error);
    }
  }

  async sendPreserveToggle(routeBase, source = "route") {
    const response = await fetch(`${this.API_BASE}/api/preserve/${routeBase}`, {
      method: "POST",
    });

    const rawText = await response.text();
    let data = null;
    if (rawText) {
      try {
        data = JSON.parse(rawText);
      } catch (parseError) {
        console.warn(
          "Failed to parse preserve toggle response as JSON:",
          parseError,
          rawText
        );
      }
    }

    const isStarred =
      data?.isPreserved ??
      data?.isStarred ??
      data?.is_preserved ??
      data?.is_starred;

    if (!response.ok || !data?.success) {
      const error = this.buildPreserveError(response, data, rawText, source);
      throw error;
    }

    if (typeof isStarred !== "boolean") {
      const error = new Error("Invalid preserve status from server");
      error.response = response;
      error.data = data;
      throw error;
    }

    // Update disk visualization if server provided fresh stats
    if (data?.disk_space) {
      this.updateDiskVisualizationFromData(data.disk_space);
    } else {
      // Fall back to requesting updated disk info in the background
      this.updateDiskVisualization();
    }

    return { data, isStarred };
  }

  buildPreserveError(response, data, rawText, source) {
    const parts = [];

    if (data) {
      if (data.error) parts.push(data.error);
      if (data.message && data.message !== data.error) {
        parts.push(data.message);
      }
      if (data.details) parts.push(data.details);
      if (data.hint) parts.push(data.hint);
    }

    if (!parts.length) {
      if (response.statusText) {
        parts.push(`${response.status} ${response.statusText}`);
      } else {
        parts.push(`Request failed with status ${response.status}`);
      }
      if (rawText && !data) {
        parts.push(rawText);
      }
    }

    const errorMessage = parts.filter(Boolean).join("\n\n");
    const error = new Error(errorMessage || "Failed to update preserve status");
    error.response = response;
    error.data = data;
    error.source = source;
    return error;
  }

  handlePreserveToggleError(error) {
    console.error("Error toggling preserve:", error);

    // Update disk visualization if server included fresh data
    if (error?.data?.disk_space) {
      this.updateDiskVisualizationFromData(error.data.disk_space);
    } else {
      // Refresh disk metrics in background so UI stays current
      this.updateDiskVisualization();
    }

    const message =
      error?.message && error.message.trim().length > 0
        ? error.message
        : "Failed to update preserve status.";

    alert(message);
  }

  updateDiskVisualizationFromData(diskData) {
    if (!diskData || !diskData.formatted) {
      // If data is malformed, fall back to full refresh
      this.updateDiskVisualization();
      return;
    }

    try {
      // Show the disk info container only if on routes view
      if (this.currentView === "routes") {
        this.$diskVizContainer.classList.remove("hidden");
      }

      const usedText = diskData.formatted.used ?? "";
      const totalText = diskData.formatted.total ?? "";
      const freeText =
        diskData.formatted.free ?? diskData.formatted.available ?? "";

      if (usedText && totalText && freeText) {
        this.$diskVizStatsText.textContent = `${usedText} / ${totalText} (${freeText} free)`;
      }

      if (
        this.$diskPreservedValue &&
        diskData.formatted.preserved !== undefined
      ) {
        this.$diskPreservedValue.textContent = diskData.formatted.preserved;
      }

      if (
        this.$diskRoutesValue &&
        diskData.formatted.non_preserved !== undefined
      ) {
        this.$diskRoutesValue.textContent = diskData.formatted.non_preserved;
      }

      if (
        typeof diskData.preserved_bytes === "number" &&
        typeof diskData.total_bytes === "number" &&
        this.$diskPreservedBar
      ) {
        const preservedPercent =
          (diskData.preserved_bytes / diskData.total_bytes) * 100;
        this.$diskPreservedBar.style.width = `${preservedPercent}%`;
      }

      if (
        typeof diskData.non_preserved_bytes === "number" &&
        typeof diskData.total_bytes === "number" &&
        this.$diskRoutesBar
      ) {
        const routesPercent =
          (diskData.non_preserved_bytes / diskData.total_bytes) * 100;
        this.$diskRoutesBar.style.width = `${routesPercent}%`;
      }
    } catch (err) {
      console.warn("Failed to update disk visualization from data:", err);
      this.updateDiskVisualization();
    }
  }

  async deleteRoute(route) {
    // Format time range in local timezone
    const displayTime = route.timestamp
      ? this.formatLocalTime(route.timestamp)
      : "";
    let displayEndTime = "";
    if (route.timestamp && route.duration) {
      try {
        // Append 'Z' to treat timestamp as UTC (same as formatLocalTime does)
        let timestamp = route.timestamp;
        if (!timestamp.includes("+") && !timestamp.endsWith("Z")) {
          timestamp = timestamp + "Z";
        }
        const startDate = new Date(timestamp);

        const durationMatch = route.duration.match(/(?:(\d+)h\s*)?(?:(\d+)m)?/);
        if (durationMatch) {
          const hours = parseInt(durationMatch[1] || 0);
          const minutes = parseInt(durationMatch[2] || 0);
          const totalMinutes = hours * 60 + minutes;
          const endDate = new Date(
            startDate.getTime() + totalMinutes * 60 * 1000
          );
          displayEndTime = endDate.toLocaleTimeString("en-US", {
            hour: "numeric",
            minute: "2-digit",
            hour12: true,
          });
        }
      } catch (e) {
        console.error("Error calculating end time:", e);
      }
    }
    const timeRange = displayEndTime
      ? `${displayTime} - ${displayEndTime}`
      : displayTime;

    // Get segment count - handle both array and number formats
    const segmentCount =
      route.totalSegments ||
      (Array.isArray(route.segments) ? route.segments.length : 0);

    const confirmed = await this.showConfirm(
      "Delete Route",
      `Delete route ${
        timeRange || route.baseName
      }?\n\nThis will delete ${segmentCount} segments (${
        route.size
      }) permanently.`,
      "Delete",
      "Cancel"
    );

    if (!confirmed) return;

    try {
      const response = await fetch(
        `${this.API_BASE}/api/delete/${route.baseName}`,
        {
          method: "DELETE",
        }
      );

      const data = await response.json();

      if (!data.success) {
        throw new Error(data.error || "Failed to delete route");
      }

      // Remove from local routes array
      this.routes = this.routes.filter((r) => r.baseName !== route.baseName);

      // Update hash to reflect the new state
      this.routesHash = this.computeRoutesHash(this.routes);

      // Re-render without full reload
      this.renderRoutes();
      this.updateStats();
    } catch (error) {
      console.error("Error deleting route:", error);
      await this.showAlert(
        "Delete Failed",
        `Failed to delete route: ${error.message}`
      );
    }
  }

  async downloadCurrentRoute() {
    if (!this.currentRoute) return;

    const routeBase = this.currentRoute.baseName;
    const camera = this.currentCamera;
    const defaultFilename = this.buildRouteExportFilename(
      this.currentRoute,
      camera
    );
    const exportUrl = `${this.API_BASE}/api/route-export/${routeBase}/${camera}`;

    console.log(
      `Preparing full route download: ${routeBase} camera: ${camera}`
    );

    this.setDownloadButtonState({ loading: true, text: "Preparing…" });
    this.updateRouteDownloadStatus("Preparing full-route video…", "active");

    try {
      const response = await fetch(exportUrl, { method: "POST" });
      const statusData = await response.json();

      if (!response.ok) {
        throw new Error(statusData.error || "Failed to start video export");
      }

      await this.handleRouteExportStatus(
        routeBase,
        camera,
        statusData,
        defaultFilename
      );
    } catch (error) {
      console.error("Error downloading route:", error);
      const message = error.message || "Failed to generate video";
      this.updateRouteDownloadStatus(message, "error");
      if (!this.isNetworkError(error)) {
        alert("Failed to download route: " + message);
      }
    } finally {
      this.setDownloadButtonState({ loading: false, text: "Download Route" });
    }
  }

  setDownloadButtonState({ loading, text }) {
    if (!this.$playerDownloadRouteBtn) return;
    const spinnerIcon = `
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <circle cx="12" cy="12" r="10"/>
        <path d="M12 2a10 10 0 0 1 10 10"/>
      </svg>`;
    const downloadIcon = `
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
        <polyline points="7 10 12 15 17 10"/>
        <line x1="12" y1="15" x2="12" y2="3"/>
      </svg>`;

    const icon = loading ? spinnerIcon : downloadIcon;
    this.$playerDownloadRouteBtn.innerHTML = `${icon}
      ${text}`;
    this.$playerDownloadRouteBtn.disabled = !!loading;
  }

  updateRouteDownloadStatus(message, mode = "info") {
    if (!this.$routeDownloadStatus) return;

    this.$routeDownloadStatus.textContent = message || "";

    this.$routeDownloadStatus.classList.toggle(
      "is-active",
      mode === "active" && !!message
    );
    this.$routeDownloadStatus.classList.toggle(
      "is-error",
      mode === "error" && !!message
    );
  }

  updateRouteDownloadUIFromStatus(status) {
    const state = status.status || "processing";
    const percent = Number.isFinite(status.progressPercent)
      ? Math.round(status.progressPercent)
      : Math.round((status.progress || 0) * 100);

    let buttonText = "Preparing…";
    if (state === "idle") {
      buttonText = "Queued…";
    } else if (state === "ready") {
      buttonText = "Download starting…";
    } else if (percent > 0 && percent < 100) {
      buttonText = `Preparing… ${percent}%`;
    } else if (percent >= 100) {
      buttonText = "Finalizing…";
    }

    const isError = state === "error";
    this.setDownloadButtonState({
      loading: !isError,
      text: isError ? "Download Route" : buttonText,
    });

    const suggested = status.suggestedFilename;
    const message =
      status.message ||
      (state === "ready"
        ? suggested
          ? `Video ready: ${suggested}`
          : "Video ready."
        : state === "error"
        ? "Video generation failed"
        : "Generating full-route video…");
    this.updateRouteDownloadStatus(message, isError ? "error" : "active");
  }

  async handleRouteExportStatus(
    routeBase,
    camera,
    statusData,
    defaultFilename
  ) {
    this.updateRouteDownloadUIFromStatus(statusData);

    if (statusData.status === "ready") {
      this.enqueueRouteDownload(
        statusData,
        defaultFilename ||
          this.buildRouteExportFilename(this.currentRoute, camera)
      );
      return;
    }

    if (statusData.status === "error") {
      throw new Error(
        statusData.error || statusData.message || "Video generation failed"
      );
    }

    await this.pollRouteExport(routeBase, camera, defaultFilename);
  }

  resolveRouteDownloadUrl(downloadUrl, routeBase, camera) {
    const fallbackPath = `/api/download/route/${routeBase}/${camera}`;
    const path = downloadUrl || fallbackPath;
    if (path.startsWith("http://") || path.startsWith("https://")) {
      return path;
    }
    const normalized = path.startsWith("/") ? path : `/${path}`;
    return `${this.API_BASE}${normalized}`;
  }

  triggerRouteDownload(downloadUrl, filename) {
    const link = document.createElement("a");
    link.href = downloadUrl;
    if (filename) {
      link.download = filename;
    }
    link.style.display = "none";
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  }

  enqueueRouteDownload(statusData, defaultFilename) {
    const routeBase =
      statusData.route || (this.currentRoute && this.currentRoute.baseName);
    const camera = statusData.camera || this.currentCamera;

    if (!routeBase || !camera) return;

    const tokenSeed =
      statusData.updatedAt ||
      statusData.filenameOnDisk ||
      statusData.downloadUrl ||
      statusData.status ||
      Date.now().toString();
    const token = `${routeBase}:${camera}:${tokenSeed}`;

    if (this.routeDownloadTokens.has(token)) {
      return;
    }

    this.routeDownloadTokens.add(token);
    this.routeDownloadTokenQueue.push(token);
    if (this.routeDownloadTokenQueue.length > 25) {
      const oldest = this.routeDownloadTokenQueue.shift();
      if (oldest) {
        this.routeDownloadTokens.delete(oldest);
      }
    }

    const filename =
      statusData.filename ||
      statusData.suggestedFilename ||
      defaultFilename ||
      this.buildRouteExportFilename(this.currentRoute, camera);

    const downloadUrl = this.resolveRouteDownloadUrl(
      statusData.downloadUrl,
      routeBase,
      camera
    );

    this.setDownloadButtonState({
      loading: false,
      text: "Download Route",
    });

    this.updateRouteDownloadStatus(`Download started: ${filename}`, "active");
    this.triggerRouteDownload(downloadUrl, filename);
    this.addRouteDownloadHistory({
      route: routeBase,
      camera,
      filename,
      url: downloadUrl,
      timestamp: new Date(),
    });
  }

  addRouteDownloadHistory(entry) {
    if (!entry) return;
    this.routeDownloadHistory.unshift(entry);
    if (this.routeDownloadHistory.length > 5) {
      this.routeDownloadHistory = this.routeDownloadHistory.slice(0, 5);
    }
    this.renderRouteDownloadHistory();
  }

  renderRouteDownloadHistory() {
    if (!this.$routeDownloadHistory) return;

    if (!this.routeDownloadHistory.length) {
      this.$routeDownloadHistory.innerHTML =
        '<span class="route-download-history-empty">No downloads yet</span>';
      return;
    }

    const items = this.routeDownloadHistory
      .map((entry) => {
        const timeLabel = this.formatDownloadHistoryTimestamp(entry.timestamp);
        const locationLabel =
          entry.route && entry.camera ? `${entry.route} · ${entry.camera}` : "";
        const subtitle = locationLabel
          ? `<div class="download-history-meta">${locationLabel}</div>`
          : "";

        return `
          <div class="download-history-item">
            <div class="download-history-details">
              <div class="download-history-filename">${entry.filename}</div>
              ${subtitle}
            </div>
            <div class="download-history-actions">
              <span class="download-history-time">${timeLabel}</span>
              <a
                href="${entry.url}"
                class="download-history-link"
                download="${entry.filename}"
                title="Download again"
              >
                Re-download
              </a>
            </div>
          </div>
        `;
      })
      .join("");

    this.$routeDownloadHistory.innerHTML = items;
  }

  formatDownloadHistoryTimestamp(value) {
    try {
      const date = value instanceof Date ? value : new Date(value);
      if (Number.isNaN(date.getTime())) {
        return "";
      }
      return date.toLocaleTimeString([], {
        hour: "2-digit",
        minute: "2-digit",
      });
    } catch (e) {
      return "";
    }
  }

  buildRouteExportFilename(route, camera) {
    const components = [];

    if (route && route.timestamp) {
      let timestamp = route.timestamp;
      if (!timestamp.includes("+") && !timestamp.endsWith("Z")) {
        timestamp = `${timestamp}Z`;
      }

      const dt = new Date(timestamp);
      if (!Number.isNaN(dt.getTime())) {
        components.push(
          dt.toISOString().slice(0, 16).replace(/[-:]/g, "").replace("T", "_")
        );
      }
    }

    const location = this.composeRouteLocationComponent(route);
    if (location) {
      components.push(location);
    } else if (route && route.baseName) {
      components.push(this.sanitizeFilenameComponent(route.baseName));
    }

    components.push(this.sanitizeFilenameComponent(camera) || "camera");

    const filenameBase =
      components.filter(Boolean).join("_") ||
      `${route?.baseName || "route"}_${camera || "video"}`;

    return `${filenameBase}.mp4`;
  }

  composeRouteLocationComponent(route) {
    if (!route) return null;
    const start = route.startLocation || route.start_location;
    const end = route.endLocation || route.end_location;

    if (!start) return null;

    const startSanitized = this.sanitizeFilenameComponent(start);
    const endSanitized = this.sanitizeFilenameComponent(end);

    if (endSanitized && endSanitized !== startSanitized) {
      return `${startSanitized}-to-${endSanitized}`;
    }

    return startSanitized;
  }

  sanitizeFilenameComponent(value) {
    if (!value) return "";
    return value
      .toString()
      .trim()
      .replace(/[^A-Za-z0-9]+/g, "-")
      .replace(/^-+|-+$/g, "");
  }

  async pollRouteExport(routeBase, camera, defaultFilename) {
    const pollUrl = `${this.API_BASE}/api/route-export/${routeBase}/${camera}`;
    const start = Date.now();
    const timeoutMs = 5 * 60 * 1000;

    while (true) {
      await new Promise((resolve) => setTimeout(resolve, 2000));

      const response = await fetch(pollUrl);
      const statusData = await response.json();

      if (!response.ok) {
        throw new Error(statusData.error || "Failed to fetch export status");
      }

      this.updateRouteDownloadUIFromStatus(statusData);

      if (statusData.status === "ready") {
        this.enqueueRouteDownload(
          statusData,
          defaultFilename ||
            this.buildRouteExportFilename(this.currentRoute, camera)
        );
        return;
      }

      if (statusData.status === "error") {
        throw new Error(
          statusData.error || statusData.message || "Video generation failed"
        );
      }

      if (Date.now() - start > timeoutMs) {
        throw new Error("Timed out waiting for video generation");
      }
    }
  }

  handleKeyboard(e) {
    // Only handle keyboard shortcuts when video player is open
    if (this.$videoPlayer.classList.contains("hidden")) return;

    switch (e.key) {
      case "Escape":
        this.closeVideo();
        break;
      case " ":
        e.preventDefault();
        const videoElement = this.$videoElement || this.player;
        if (videoElement) {
          if (this.$videoElement) {
            if (this.$videoElement.paused) {
              this.$videoElement.play();
            } else {
              this.$videoElement.pause();
            }
          } else if (this.player && typeof this.player.play === "function") {
            if (this.player.isPlaying()) {
              this.player.pause();
            } else {
              this.player.play();
            }
          }
        }
        break;
      case "ArrowRight":
        e.preventDefault();
        if (this.$videoElement) {
          const targetTime = this.$videoElement.currentTime + 10;
          this.seekToSeconds(targetTime);
        } else if (this.player && typeof this.player.seek === "function") {
          const currentTime = this.player.getCurrentTime() || 0;
          this.player.seek(currentTime + 10);
        }
        break;
      case "ArrowLeft":
        e.preventDefault();
        if (this.$videoElement) {
          const targetTime = Math.max(0, this.$videoElement.currentTime - 10);
          this.seekToSeconds(targetTime);
        } else if (this.player && typeof this.player.seek === "function") {
          const currentTime = this.player.getCurrentTime() || 0;
          this.player.seek(Math.max(0, currentTime - 10));
        }
        break;
      case "f":
        e.preventDefault();
        if (document.fullscreenElement) {
          document.exitFullscreen();
        } else {
          this.$videoPlayer.requestFullscreen();
        }
        break;
    }
  }

  // System Metrics Methods
  async openMetrics() {
    this.$metricsModal.classList.remove("hidden");
    await this.loadMetrics();
  }

  closeMetrics() {
    this.$metricsModal.classList.add("hidden");
  }

  // Parameter Value Viewer Methods
  openParamValueModal(key) {
    const param = this.allParams[key];
    if (!param) return;

    // Populate modal with param info
    this.$paramValueKey.textContent = key;
    this.$paramValueType.textContent = param.type || 'unknown';
    this.$paramValueCategory.textContent = param.category || 'other';
    this.$paramValueText.textContent = String(param.value ?? 'null');

    // Store current param key for copy functionality
    this.currentParamKey = key;

    this.$paramValueModal.classList.remove("hidden");
  }

  closeParamValueModal() {
    this.$paramValueModal.classList.add("hidden");
    this.currentParamKey = null;
  }

  async copyParamValue() {
    const value = this.$paramValueText.textContent;

    try {
      await navigator.clipboard.writeText(value);

      // Visual feedback
      const originalText = this.$copyParamValueBtn.innerHTML;
      this.$copyParamValueBtn.innerHTML = `
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <polyline points="20 6 9 17 4 12"/>
        </svg>
        Copied!
      `;
      this.$copyParamValueBtn.disabled = true;

      setTimeout(() => {
        this.$copyParamValueBtn.innerHTML = originalText;
        this.$copyParamValueBtn.disabled = false;
      }, 2000);
    } catch (error) {
      console.error('Failed to copy to clipboard:', error);

      // Fallback: select the text
      const range = document.createRange();
      range.selectNodeContents(this.$paramValueText);
      const selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);

      alert('Please manually copy the selected text (Ctrl+C or Cmd+C)');
    }
  }

  // Export & Backup Modal Methods
  openExportBackupModal() {
    if (!this.currentRoute) return;

    // Populate route info
    this.$exportRouteName.textContent = `Route: ${this.currentRoute.baseName}`;

    // Get segment count - handle both array and number formats
    const segmentCount =
      this.currentRoute.totalSegments ||
      (Array.isArray(this.currentRoute.segments)
        ? this.currentRoute.segments.length
        : 0);
    this.$exportRouteSegments.textContent = `${segmentCount} segments`;

    this.$exportRouteSize.textContent = this.currentRoute.size || "-- GB";
    this.$exportRouteDuration.textContent = this.currentRoute.duration || "--";

    // Calculate backup size estimate
    const sizeMatch = this.currentRoute.size?.match(/[\d.]+/);
    const sizeGB = sizeMatch ? parseFloat(sizeMatch[0]) : 0;
    this.$backupSizeEstimate.textContent =
      sizeGB > 0 ? `~${sizeGB.toFixed(2)} GB` : "--";

    // Reset checkboxes and statuses
    this.$cameraCheckboxes.forEach((cb) => {
      if (cb.value === "front") {
        cb.checked = true;
      } else {
        cb.checked = false;
      }
    });
    this.updateSelectedVideosButton();

    // Load camera file sizes
    this.loadCameraSizes(this.currentRoute.baseName);

    // Hide all status messages
    this.$videoExportStatus.classList.add("hidden");
    this.$backupStatus.classList.add("hidden");

    // Show modal
    this.$exportBackupModal.classList.remove("hidden");
  }

  async loadCameraSizes(routeBase) {
    try {
      const response = await fetch(
        `${this.API_BASE}/api/route/${routeBase}/camera-sizes`
      );
      if (!response.ok) return;

      const data = await response.json();
      const formatSize = (bytes) => {
        if (!bytes) return "--";
        const mb = bytes / (1024 * 1024);
        if (mb < 1024) return `${mb.toFixed(1)} MB`;
        return `${(mb / 1024).toFixed(2)} GB`;
      };

      this.$frontSize.textContent = formatSize(data.front);
      this.$wideSize.textContent = formatSize(data.wide);
      this.$driverSize.textContent = formatSize(data.driver);
      this.$lqSize.textContent = formatSize(data.lq);

      const totalSize =
        (data.front || 0) +
        (data.wide || 0) +
        (data.driver || 0) +
        (data.lq || 0);
      this.$allVideosSize.textContent = formatSize(totalSize);
    } catch (error) {
      console.error("Error loading camera sizes:", error);
    }
  }

  async closeExportBackupModal() {
    // Check if there's an active export operation
    const videoStatusVisible =
      !this.$videoExportStatus.classList.contains("hidden");
    const backupStatusVisible =
      !this.$backupStatus.classList.contains("hidden");

    if (videoStatusVisible || backupStatusVisible) {
      // Ask for confirmation before canceling
      const confirmed = await this.showConfirm(
        "Cancel Export?",
        "An export operation is in progress. Closing this modal will cancel the operation and clean up temporary files.\n\nDo you want to cancel and close?",
        "Cancel Export",
        "Keep Running"
      );

      if (!confirmed) {
        return; // Don't close the modal
      }

      // Cancel the operation and cleanup
      if (this.currentRoute) {
        await this.cancelExportOperation(this.currentRoute.baseName);
      }
    }

    // Reset state and close modal
    this.$videoExportStatus.classList.add("hidden");
    this.$backupStatus.classList.add("hidden");
    this.enableExportButtons();
    this.$exportBackupModal.classList.add("hidden");
  }

  async cancelExportOperation(routeBase) {
    try {
      // Try to cancel videos ZIP operation
      await fetch(`${this.API_BASE}/api/videos-zip/${routeBase}/cancel`, {
        method: "POST",
      }).catch(() => {}); // Ignore errors if not in progress

      // Try to cancel backup operation
      await fetch(`${this.API_BASE}/api/route-backup/${routeBase}/cancel`, {
        method: "POST",
      }).catch(() => {}); // Ignore errors if not in progress

      // Try to cancel individual video export
      const cameras = ["front", "wide", "driver", "lq"];
      for (const camera of cameras) {
        await fetch(
          `${this.API_BASE}/api/route-export/${routeBase}/${camera}/cancel`,
          {
            method: "POST",
          }
        ).catch(() => {}); // Ignore errors if not in progress
      }
    } catch (error) {
      console.error("Error canceling export:", error);
    }
  }

  // Import Backup Modal Methods
  openImportBackupModal() {
    // Reset import
    this.$importFileInput.value = "";
    this.$importFileName.textContent = "No file selected";
    this.$importRouteBtn.disabled = true;
    this.$importStatus.classList.add("hidden");

    // Show modal
    this.$importBackupModal.classList.remove("hidden");
  }

  closeImportBackupModal() {
    this.$importBackupModal.classList.add("hidden");
  }

  // Helper methods to disable/enable export buttons during operations
  disableExportButtons() {
    // Disable all export buttons
    this.$downloadSelectedVideosBtn.disabled = true;
    this.$downloadAllVideosBtn.disabled = true;
    this.$backupRouteBtn.disabled = true;

    // Disable all camera checkboxes
    this.$cameraCheckboxes.forEach((checkbox) => {
      checkbox.disabled = true;
    });
  }

  enableExportButtons() {
    // Re-enable export buttons
    this.$downloadAllVideosBtn.disabled = false;
    this.$backupRouteBtn.disabled = false;

    // Re-enable all camera checkboxes
    this.$cameraCheckboxes.forEach((checkbox) => {
      checkbox.disabled = false;
    });

    // Re-check selected videos button state
    this.updateSelectedVideosButton();
  }

  // Confirmation Modal
  showConfirm(title, message, confirmText = "Confirm", cancelText = "Cancel") {
    return new Promise((resolve) => {
      this.$confirmModalTitle.textContent = title;
      this.$confirmModalMessage.textContent = message;
      this.$confirmModalConfirmBtn.textContent = confirmText;
      this.$confirmModalCancelBtn.textContent = cancelText;
      this.$confirmModalCancelBtn.style.display = "";

      const handleConfirm = () => {
        cleanup();
        resolve(true);
      };

      const handleCancel = () => {
        cleanup();
        resolve(false);
      };

      const cleanup = () => {
        this.$confirmModal.classList.add("hidden");
        this.$confirmModalConfirmBtn.removeEventListener(
          "click",
          handleConfirm
        );
        this.$confirmModalCancelBtn.removeEventListener("click", handleCancel);
        this.$confirmModal.removeEventListener("click", handleBackdropClick);
      };

      const handleBackdropClick = (e) => {
        if (e.target === this.$confirmModal) {
          handleCancel();
        }
      };

      this.$confirmModalConfirmBtn.addEventListener("click", handleConfirm);
      this.$confirmModalCancelBtn.addEventListener("click", handleCancel);
      this.$confirmModal.addEventListener("click", handleBackdropClick);

      this.$confirmModal.classList.remove("hidden");
    });
  }

  // Alert Modal (single OK button)
  showAlert(title, message, okText = "OK") {
    return new Promise((resolve) => {
      this.$confirmModalTitle.textContent = title;
      this.$confirmModalMessage.textContent = message;
      this.$confirmModalConfirmBtn.textContent = okText;
      this.$confirmModalCancelBtn.style.display = "none";

      const handleConfirm = () => {
        cleanup();
        resolve(true);
      };

      const cleanup = () => {
        this.$confirmModal.classList.add("hidden");
        this.$confirmModalConfirmBtn.removeEventListener(
          "click",
          handleConfirm
        );
        this.$confirmModal.removeEventListener("click", handleBackdropClick);
      };

      const handleBackdropClick = (e) => {
        if (e.target === this.$confirmModal) {
          handleConfirm();
        }
      };

      this.$confirmModalConfirmBtn.addEventListener("click", handleConfirm);
      this.$confirmModal.addEventListener("click", handleBackdropClick);

      this.$confirmModal.classList.remove("hidden");
    });
  }

  updateSelectedVideosButton() {
    const selectedCameras = Array.from(this.$cameraCheckboxes)
      .filter((cb) => cb.checked)
      .map((cb) => cb.value);

    const count = selectedCameras.length;
    this.$downloadSelectedVideosBtn.disabled = count === 0;

    if (count === 0) {
      this.$downloadSelectedVideosBtn.innerHTML = `
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
          <polyline points="7 10 12 15 17 10" />
          <line x1="12" y1="15" x2="12" y2="3" />
        </svg>
        Download Selected`;
    } else if (count === 1) {
      this.$downloadSelectedVideosBtn.innerHTML = `
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
          <polyline points="7 10 12 15 17 10" />
          <line x1="12" y1="15" x2="12" y2="3" />
        </svg>
        Download ${selectedCameras[0].toUpperCase()} Video`;
    } else {
      this.$downloadSelectedVideosBtn.innerHTML = `
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
          <polyline points="7 10 12 15 17 10" />
          <line x1="12" y1="15" x2="12" y2="3" />
        </svg>
        Download ${count} Videos (ZIP)`;
    }
  }

  async downloadSelectedVideos() {
    if (!this.currentRoute) return;

    const selectedCameras = Array.from(this.$cameraCheckboxes)
      .filter((cb) => cb.checked)
      .map((cb) => cb.value);

    if (selectedCameras.length === 0) return;

    const routeBase = this.currentRoute.baseName;

    // If only one camera selected, download single video
    if (selectedCameras.length === 1) {
      await this.downloadSingleVideo(routeBase, selectedCameras[0]);
    } else {
      // Multiple cameras - download as ZIP
      await this.downloadVideosAsZip(routeBase, selectedCameras);
    }
  }

  async downloadSingleVideo(routeBase, camera) {
    const defaultFilename = this.buildRouteExportFilename(
      this.currentRoute,
      camera
    );
    const exportUrl = `${this.API_BASE}/api/route-export/${routeBase}/${camera}`;

    this.showExportStatus("video", `Preparing ${camera} video...`, 0);
    this.disableExportButtons();

    try {
      const response = await fetch(exportUrl, { method: "POST" });
      const statusData = await response.json();

      if (!response.ok) {
        throw new Error(statusData.error || "Failed to start video export");
      }

      await this.pollVideoExport(routeBase, camera, defaultFilename);
    } catch (error) {
      console.error("Error downloading video:", error);
      this.showExportStatus("video", `Error: ${error.message}`, 0, "error");
      this.enableExportButtons();
    }
  }

  async downloadVideosAsZip(routeBase, cameras) {
    this.showExportStatus("video", "Preparing videos archive...", 0);
    this.disableExportButtons();

    try {
      // Camera display names
      const cameraNames = {
        front: "Front",
        wide: "Wide",
        driver: "Driver",
        lq: "LQ",
      };

      const totalCameras = cameras.length;
      const zip = new JSZip();
      let completedCameras = 0;

      this.showExportStatus(
        "video",
        `Preparing ${totalCameras} camera video${
          totalCameras > 1 ? "s" : ""
        }...`,
        0
      );

      // Process each camera sequentially using streaming endpoint
      for (const camera of cameras) {
        const cameraName = cameraNames[camera] || camera;
        const baseProgress = Math.round((completedCameras / totalCameras) * 80);

        this.showExportStatus(
          "video",
          `Streaming ${cameraName}... (${
            completedCameras + 1
          }/${totalCameras})`,
          baseProgress
        );

        // Stream video directly from server (no caching/polling needed)
        const streamUrl = `${this.API_BASE}/api/route-export-stream/${routeBase}/${camera}`;
        const videoResponse = await fetch(streamUrl);

        if (!videoResponse.ok) {
          const errorData = await videoResponse.json().catch(() => ({}));
          throw new Error(
            errorData.error || `Failed to stream ${cameraName} video`
          );
        }

        // Download the streamed video
        const videoBlob = await videoResponse.blob();
        const sizeMB = (videoBlob.size / (1024 * 1024)).toFixed(1);

        // Add to ZIP with appropriate filename
        const filename = `${routeBase}_${camera}.mp4`;
        zip.file(filename, videoBlob);

        completedCameras++;

        this.showExportStatus(
          "video",
          `Completed ${cameraName} (${sizeMB} MB) - ${completedCameras}/${totalCameras}`,
          Math.round((completedCameras / totalCameras) * 80)
        );
      }

      // Generate ZIP file with progress
      this.showExportStatus(
        "video",
        `Creating ZIP archive with ${completedCameras} video${
          completedCameras > 1 ? "s" : ""
        }...`,
        80
      );

      const zipBlob = await zip.generateAsync(
        {
          type: "blob",
          compression: "DEFLATE",
          compressionOptions: { level: 6 },
        },
        (metadata) => {
          // Progress callback during compression
          const progress = 80 + Math.round(metadata.percent / 5); // 80-100%
          this.showExportStatus(
            "video",
            `Compressing archive... (${Math.round(metadata.percent)}%)`,
            progress
          );
        }
      );

      // Trigger download
      this.showExportStatus("video", "Archive ready!", 100, "success");
      const filename = `${routeBase}_videos.zip`;
      const url = URL.createObjectURL(zipBlob);

      const a = document.createElement("a");
      a.href = url;
      a.download = filename;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);

      setTimeout(() => this.$videoExportStatus.classList.add("hidden"), 3000);
      this.enableExportButtons();
    } catch (error) {
      console.error("Error creating videos archive:", error);
      this.showExportStatus("video", `Error: ${error.message}`, 0, "error");
      this.enableExportButtons();
    }
  }

  async downloadAllVideos() {
    if (!this.currentRoute) return;
    const routeBase = this.currentRoute.baseName;
    const allCameras = ["front", "wide", "driver", "lq"];

    await this.downloadVideosAsZip(routeBase, allCameras);
  }

  async pollVideoExport(routeBase, camera, filename) {
    const pollUrl = `${this.API_BASE}/api/route-export/${routeBase}/${camera}`;
    const maxAttempts = 120; // 10 minutes max
    let attempts = 0;

    while (attempts < maxAttempts) {
      await new Promise((resolve) => setTimeout(resolve, 5000)); // Poll every 5s
      attempts++;

      try {
        const response = await fetch(pollUrl);
        const status = await response.json();

        if (status.status === "ready") {
          this.showExportStatus("video", "Download ready!", 100, "success");
          const downloadUrl = this.resolveRouteDownloadUrl(
            status.downloadUrl,
            routeBase,
            camera
          );
          this.triggerRouteDownload(downloadUrl, filename);
          setTimeout(
            () => this.$videoExportStatus.classList.add("hidden"),
            3000
          );
          this.enableExportButtons();
          return;
        } else if (status.status === "error") {
          throw new Error(status.message || "Export failed");
        } else {
          const progress = Math.round(
            status.progressPercent || status.progress * 100 || 0
          );
          const message = status.message || "Preparing video...";
          this.showExportStatus("video", `${message} (${progress}%)`, progress);
        }
      } catch (error) {
        console.error("Polling error:", error);
        this.showExportStatus("video", `Error: ${error.message}`, 0, "error");
        this.enableExportButtons();
        return;
      }
    }

    this.showExportStatus("video", "Export timed out", 0, "error");
    this.enableExportButtons();
  }

  async pollVideosZip(routeBase, cameras) {
    const pollUrl = `${this.API_BASE}/api/videos-zip/${routeBase}/status`;
    const maxAttempts = 240; // 20 minutes for multiple videos
    let attempts = 0;

    while (attempts < maxAttempts) {
      await new Promise((resolve) => setTimeout(resolve, 5000));
      attempts++;

      try {
        const response = await fetch(pollUrl);
        const status = await response.json();

        if (status.status === "ready") {
          this.showExportStatus("video", "Archive ready!", 100, "success");
          const filename = `${routeBase}_videos.zip`;
          const downloadUrl = `${this.API_BASE}/api/videos-zip/${routeBase}/download`;
          this.triggerRouteDownload(downloadUrl, filename);
          setTimeout(
            () => this.$videoExportStatus.classList.add("hidden"),
            3000
          );
          this.enableExportButtons();
          return;
        } else if (status.status === "error") {
          throw new Error(status.message || "Archive creation failed");
        } else {
          const progress = Math.round(status.progress * 100 || 0);
          const message = status.message || "Creating archive...";
          this.showExportStatus("video", `${message} (${progress}%)`, progress);
        }
      } catch (error) {
        console.error("Polling error:", error);
        this.showExportStatus("video", `Error: ${error.message}`, 0, "error");
        this.enableExportButtons();
        return;
      }
    }

    this.showExportStatus("video", "Archive creation timed out", 0, "error");
    this.enableExportButtons();
  }

  async backupCurrentRoute() {
    if (!this.currentRoute) return;
    const routeBase = this.currentRoute.baseName;

    this.showExportStatus("backup", "Creating backup archive...", 0);
    this.disableExportButtons();

    try {
      const response = await fetch(
        `${this.API_BASE}/api/route-backup/${routeBase}`,
        {
          method: "POST",
        }
      );

      const statusData = await response.json();

      if (!response.ok) {
        throw new Error(statusData.error || "Failed to start backup");
      }

      await this.pollBackup(routeBase);
    } catch (error) {
      console.error("Error creating backup:", error);
      this.showExportStatus("backup", `Error: ${error.message}`, 0, "error");
      this.enableExportButtons();
    }
  }

  async pollBackup(routeBase) {
    const pollUrl = `${this.API_BASE}/api/route-backup/${routeBase}/status`;
    const maxAttempts = 360; // 30 minutes for full backup
    let attempts = 0;

    while (attempts < maxAttempts) {
      await new Promise((resolve) => setTimeout(resolve, 5000));
      attempts++;

      try {
        const response = await fetch(pollUrl);
        const status = await response.json();

        if (status.status === "ready") {
          this.showExportStatus("backup", "Backup ready!", 100, "success");
          const filename = `${routeBase}.bpbackup.zip`;
          const downloadUrl = `${this.API_BASE}/api/route-backup/${routeBase}/download`;
          this.triggerRouteDownload(downloadUrl, filename);
          setTimeout(() => this.$backupStatus.classList.add("hidden"), 3000);
          this.enableExportButtons();
          return;
        } else if (status.status === "error") {
          throw new Error(status.message || "Backup failed");
        } else {
          const progress = Math.round(status.progress * 100 || 0);
          const message = status.message || "Creating backup...";
          this.showExportStatus(
            "backup",
            `${message} (${progress}%)`,
            progress
          );
        }
      } catch (error) {
        console.error("Polling error:", error);
        this.showExportStatus("backup", `Error: ${error.message}`, 0, "error");
        this.enableExportButtons();
        return;
      }
    }

    this.showExportStatus("backup", "Backup creation timed out", 0, "error");
    this.enableExportButtons();
  }

  handleImportFileSelected(event) {
    const file = event.target.files[0];
    if (file) {
      this.$importFileName.textContent = file.name;
      this.$importRouteBtn.disabled = false;
      this.selectedBackupFile = file;
    } else {
      this.$importFileName.textContent = "No file selected";
      this.$importRouteBtn.disabled = true;
      this.selectedBackupFile = null;
    }
  }

  async importRouteBackup() {
    if (!this.selectedBackupFile) return;

    this.showExportStatus("import", "Uploading backup file...", 0);
    this.$importRouteBtn.disabled = true;
    this.$selectImportFileBtn.disabled = true;

    try {
      const formData = new FormData();
      formData.append("backup", this.selectedBackupFile);

      const response = await fetch(`${this.API_BASE}/api/route-import`, {
        method: "POST",
        body: formData,
      });

      const result = await response.json();

      if (!response.ok) {
        throw new Error(result.error || "Failed to import backup");
      }

      // Poll for import completion
      await this.pollImport(result.importId);
    } catch (error) {
      console.error("Error importing backup:", error);
      this.showExportStatus("import", `Error: ${error.message}`, 0, "error");
      this.$importRouteBtn.disabled = false;
      this.$selectImportFileBtn.disabled = false;
    }
  }

  async pollImport(importId) {
    const pollUrl = `${this.API_BASE}/api/route-import/${importId}/status`;
    const maxAttempts = 360; // 30 minutes
    let attempts = 0;

    while (attempts < maxAttempts) {
      await new Promise((resolve) => setTimeout(resolve, 5000));
      attempts++;

      try {
        const response = await fetch(pollUrl);
        const status = await response.json();

        if (status.status === "completed") {
          this.showExportStatus(
            "import",
            `Import successful! Route restored: ${status.routeName}`,
            100,
            "success"
          );
          this.$importRouteBtn.disabled = false;
          this.$selectImportFileBtn.disabled = false;

          // Reload routes to show the newly imported route
          setTimeout(() => {
            this.closeExportBackupModal();
            this.loadRoutes();
          }, 2000);
          return;
        } else if (status.status === "error") {
          throw new Error(status.message || "Import failed");
        } else {
          const progress = Math.round(status.progress * 100 || 0);
          const message = status.message || "Importing...";
          this.showExportStatus(
            "import",
            `${message} (${progress}%)`,
            progress
          );
        }
      } catch (error) {
        console.error("Polling error:", error);
        this.showExportStatus("import", `Error: ${error.message}`, 0, "error");
        this.$importRouteBtn.disabled = false;
        this.$selectImportFileBtn.disabled = false;
        return;
      }
    }

    this.showExportStatus("import", "Import timed out", 0, "error");
    this.$importRouteBtn.disabled = false;
    this.$selectImportFileBtn.disabled = false;
  }

  showExportStatus(type, message, progress, status = "active") {
    let statusElement, progressFill, messageElement;

    if (type === "video") {
      statusElement = this.$videoExportStatus;
      progressFill = statusElement.querySelector(".export-progress-fill");
      messageElement = statusElement.querySelector(".export-status-message");
    } else if (type === "backup") {
      statusElement = this.$backupStatus;
      progressFill = statusElement.querySelector(".export-progress-fill");
      messageElement = statusElement.querySelector(".export-status-message");
    } else if (type === "import") {
      statusElement = this.$importStatus;
      progressFill = statusElement.querySelector(".export-progress-fill");
      messageElement = statusElement.querySelector(".export-status-message");
    }

    if (!statusElement) return;

    statusElement.classList.remove("hidden", "success", "error");
    if (status === "success" || status === "error") {
      statusElement.classList.add(status);
    }

    messageElement.textContent = message;
    progressFill.style.width = `${progress}%`;
  }

  async loadMetrics() {
    try {
      const response = await fetch(`${this.API_BASE}/api/system/metrics`);
      if (!response.ok) {
        throw new Error(`HTTP error! status: ${response.status}`);
      }

      const data = await response.json();
      if (data.success && data.metrics) {
        this.displayMetrics(data.metrics);
      }
    } catch (error) {
      console.error("Error loading metrics:", error);
      this.showNotification("Failed to load system metrics");
    }
  }

  displayMetrics(metrics) {
    // CPU Metrics
    if (metrics.cpu) {
      const cpuLoad = metrics.cpu.load_1min || 0;
      document.getElementById("metric-cpu-load").textContent =
        cpuLoad.toFixed(2);
      document.getElementById("metric-cpu-1min").textContent =
        cpuLoad.toFixed(2);
      document.getElementById("metric-cpu-5min").textContent = (
        metrics.cpu.load_5min || 0
      ).toFixed(2);
      document.getElementById("metric-cpu-cores").textContent =
        metrics.cpu.core_count || "--";

      // Color based on load (4 cores, so load > 3 is high)
      const cpuElement = document.getElementById("metric-cpu-load");
      cpuElement.className = "value-large";
      if (cpuLoad > 3) cpuElement.classList.add("danger");
      else if (cpuLoad > 2) cpuElement.classList.add("warning");
    }

    // Memory Metrics
    if (metrics.memory) {
      const memPercent = metrics.memory.percent_used || 0;
      document.getElementById("metric-memory-percent").textContent =
        Math.round(memPercent);
      document.getElementById("metric-memory-used").textContent = `${(
        metrics.memory.total_gb - metrics.memory.available_gb
      ).toFixed(1)} GB`;
      document.getElementById(
        "metric-memory-available"
      ).textContent = `${metrics.memory.available_gb.toFixed(1)} GB`;

      // Update progress bar
      const memBar = document.getElementById("metric-memory-bar");
      memBar.style.width = `${memPercent}%`;
      memBar.className = "bar-fill";
      if (memPercent > 90) memBar.classList.add("danger");
      else if (memPercent > 75) memBar.classList.add("warning");

      // Color percentage
      const memElement = document.getElementById("metric-memory-percent");
      memElement.className = "value-large";
      if (memPercent > 90) memElement.classList.add("danger");
      else if (memPercent > 75) memElement.classList.add("warning");
    }

    // Disk Metrics
    if (metrics.disk && metrics.disk["/data"]) {
      const disk = metrics.disk["/data"];
      const diskPercent = disk.percent_used || 0;
      document.getElementById("metric-disk-percent").textContent =
        Math.round(diskPercent);
      document.getElementById("metric-disk-used").textContent = `${(
        disk.total_gb - disk.free_gb
      ).toFixed(1)} GB`;
      document.getElementById(
        "metric-disk-free"
      ).textContent = `${disk.free_gb.toFixed(1)} GB`;

      // Update progress bar
      const diskBar = document.getElementById("metric-disk-bar");
      diskBar.style.width = `${diskPercent}%`;
      diskBar.className = "bar-fill";
      if (diskPercent > 90) diskBar.classList.add("danger");
      else if (diskPercent > 80) diskBar.classList.add("warning");

      // Color percentage
      const diskElement = document.getElementById("metric-disk-percent");
      diskElement.className = "value-large";
      if (diskPercent > 90) diskElement.classList.add("danger");
      else if (diskPercent > 80) diskElement.classList.add("warning");
    }

    // Temperature Metrics
    if (metrics.temperature && metrics.temperature.celsius) {
      const tempC = metrics.temperature.celsius;
      document.getElementById("metric-temp-value").textContent =
        tempC.toFixed(1);
      document.getElementById(
        "metric-temp-fahrenheit"
      ).textContent = `${metrics.temperature.fahrenheit.toFixed(1)}°F`;

      // Color based on temperature
      const tempElement = document.getElementById("metric-temp-value");
      tempElement.className = "value-large";
      if (tempC > 70) tempElement.classList.add("danger");
      else if (tempC > 60) tempElement.classList.add("warning");
      else tempElement.classList.add("success");
    }

    // FFmpeg Metrics
    if (metrics.ffmpeg) {
      const active = metrics.ffmpeg.active_processes || 0;
      const max = metrics.ffmpeg.max_processes || 3;
      document.getElementById("metric-ffmpeg-active").textContent = active;
      document.getElementById("metric-ffmpeg-max").textContent = max;
      document.getElementById("metric-ffmpeg-status").textContent =
        active > 0 ? `${active} active` : "Idle";

      // Color based on utilization
      const ffmpegElement = document.getElementById("metric-ffmpeg-active");
      ffmpegElement.className = "value-large";
      if (active >= max) ffmpegElement.classList.add("warning");
      else if (active > 0) ffmpegElement.classList.add("success");
    }

    // Cache Metrics
    if (metrics.cache) {
      document.getElementById("metric-cache-size").textContent =
        metrics.cache.remux_cache_gb.toFixed(2);
    }

    // Update timestamp
    if (metrics.timestamp) {
      const date = new Date(metrics.timestamp);
      document.getElementById("metrics-timestamp").textContent =
        date.toLocaleTimeString();
    }
  }

  // Log Viewer Methods
  async loadLogs() {
    if (!this.currentRoute) {
      console.warn("No route selected");
      return;
    }

    // Show stop button, hide load button
    this.$stopLogsBtn.classList.remove("hidden");
    this.$loadLogsBtn.classList.add("hidden");

    // Get selected log type
    const activeLogTypeBtn = document.querySelector(".log-type-btn.active");
    const logType = activeLogTypeBtn
      ? activeLogTypeBtn.dataset.logType
      : "rlog";

    // Get filters
    const levelFilter = this.$logLevelFilter.value;
    const searchQuery = this.$logSearchInput.value.trim();

    // Show loading
    this.$logLoading.classList.remove("hidden");
    this.$logMessages.classList.add("hidden");
    this.$logViewerContainer
      .querySelector(".log-viewer-empty")
      .classList.add("hidden");

    try {
      // Build API URL
      const params = new URLSearchParams();
      if (levelFilter !== "all") {
        params.append("level", levelFilter);
      }
      if (searchQuery) {
        params.append("search", searchQuery);
      }
      params.append("max", "500");

      const url = `${this.API_BASE}/api/logs/${this.currentRoute.baseName}/${this.currentSegment}/${logType}?${params}`;

      console.log("Loading logs from:", url);

      const response = await fetch(url);
      const data = await response.json();

      this.$logLoading.classList.add("hidden");

      if (data.success) {
        this.displayLogs(data);
      } else {
        alert(`Error loading logs: ${data.error}`);
        this.$logViewerContainer
          .querySelector(".log-viewer-empty")
          .classList.remove("hidden");
      }
    } catch (error) {
      console.error("Error loading logs:", error);
      this.$logLoading.classList.add("hidden");
      alert(`Failed to load logs: ${error.message}`);
      this.$logViewerContainer
        .querySelector(".log-viewer-empty")
        .classList.remove("hidden");
    }
  }

  displayLogs(logData) {
    const {
      messages,
      total_count,
      returned_count,
      truncated,
      start_time,
      end_time,
    } = logData;

    // Clear previous logs
    this.$logMessages.innerHTML = "";

    if (messages.length === 0) {
      this.$logViewerContainer
        .querySelector(".log-viewer-empty")
        .classList.remove("hidden");
      this.$logMessages.classList.add("hidden");
      this.$logStatus.classList.add("hidden");
      return;
    }

    // Store log data for syncing
    this.currentLogMessages = messages;
    this.currentLogStartTime = start_time;
    this.currentLogEndTime = end_time;

    // Display messages
    messages.forEach((msg) => {
      const messageEl = document.createElement("div");
      messageEl.className = `log-message log-${msg.level}`;
      messageEl.dataset.timestamp = msg.timestamp;

      // Format timestamp (relative to start of segment)
      const relativeTime = msg.timestamp - start_time;
      const minutes = Math.floor(relativeTime / 60);
      const seconds = (relativeTime % 60).toFixed(3);
      const timestampStr = `${minutes}:${seconds.padStart(6, "0")}`;

      messageEl.innerHTML = `
        <span class="log-timestamp">${timestampStr}</span>
        <span class="log-level ${msg.level}">${msg.level}</span>
        <span class="log-text">${this.escapeHtml(msg.message)}</span>
      `;

      this.$logMessages.appendChild(messageEl);
    });

    // Show logs
    this.$logMessages.classList.remove("hidden");
    this.$logStatus.classList.remove("hidden");

    // Update status
    let statusText = `${returned_count} message${
      returned_count !== 1 ? "s" : ""
    }`;
    if (truncated) {
      statusText += ` (showing first ${returned_count} of ${total_count})`;
    }
    this.$logCount.textContent = statusText;

    // If sync is enabled, start syncing
    if (this.$logSyncCheckbox.checked) {
      this.startLogSync();
    }
  }

  stopLogs() {
    // Stop syncing if active
    if (this.logSyncInterval) {
      clearInterval(this.logSyncInterval);
      this.logSyncInterval = null;
    }

    // Uncheck sync checkbox
    if (this.$logSyncCheckbox) {
      this.$logSyncCheckbox.checked = false;
    }

    // Clear log data
    this.currentLogMessages = null;
    this.currentLogStartTime = null;
    this.currentLogEndTime = null;

    // Hide stop button, show load button
    this.$stopLogsBtn.classList.add("hidden");
    this.$loadLogsBtn.classList.remove("hidden");

    console.log("Log streaming stopped");
  }

  startLogSync() {
    // Show stop button, hide load button
    this.$stopLogsBtn.classList.remove("hidden");
    this.$loadLogsBtn.classList.add("hidden");

    // Clear any existing sync interval
    if (this.logSyncInterval) {
      clearInterval(this.logSyncInterval);
    }

    // Check video playback time and highlight matching logs
    this.logSyncInterval = setInterval(() => {
      if (!this.currentLogMessages || !this.$logSyncCheckbox.checked) {
        clearInterval(this.logSyncInterval);
        // Restore button states when sync stops naturally
        this.$stopLogsBtn.classList.add("hidden");
        this.$loadLogsBtn.classList.remove("hidden");
        return;
      }

      // Get current video playback time
      let currentTime = 0;
      if (this.$videoElement) {
        currentTime = this.$videoElement.currentTime;
      } else if (
        this.player &&
        typeof this.player.getCurrentTime === "function"
      ) {
        currentTime = this.player.getCurrentTime() || 0;
      }

      // Calculate absolute timestamp
      // For HLS: currentTime is route-absolute, need time within current segment
      // For single-segment: currentTime is already segment-relative
      let segmentRelativeTime = currentTime;
      if (this.hls) {
        // HLS plays entire route continuously, so get time within current segment
        segmentRelativeTime = currentTime % 60;
      }
      const absoluteTime = this.currentLogStartTime + segmentRelativeTime;

      // Find and highlight logs within 2 seconds of current playback
      const logElements = this.$logMessages.querySelectorAll(".log-message");
      logElements.forEach((el) => {
        const logTime = parseFloat(el.dataset.timestamp);
        const timeDiff = Math.abs(logTime - absoluteTime);

        if (timeDiff < 2) {
          el.classList.add("highlighted");
          // Scroll into view if not visible
          if (timeDiff < 0.5) {
            el.scrollIntoView({ behavior: "smooth", block: "nearest" });
          }
        } else {
          el.classList.remove("highlighted");
        }
      });
    }, 500); // Update twice per second
  }

  escapeHtml(text) {
    const div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
  }

  // Cereal Data Viewer Methods
  async loadCerealData() {
    if (!this.currentRoute) {
      console.warn("No route selected");
      return;
    }

    const messageType = this.$cerealMessageSelect.value;
    if (!messageType) {
      alert("Please select a message type");
      return;
    }

    // Show stop button, hide load button
    this.$stopCerealBtn.classList.remove("hidden");
    this.$loadCerealBtn.classList.add("hidden");

    // Show loading
    this.$cerealLoading.classList.remove("hidden");
    this.$cerealDataTable.classList.add("hidden");
    this.$cerealViewerContainer
      .querySelector(".cereal-viewer-empty")
      .classList.add("hidden");

    try {
      // Build API URL - use qlog by default
      const url = `${this.API_BASE}/api/cereal/${this.currentRoute.baseName}/${this.currentSegment}/qlog/${messageType}`;

      console.log("Loading cereal data from:", url);

      const response = await fetch(url);
      const data = await response.json();

      this.$cerealLoading.classList.add("hidden");

      if (data.success) {
        this.displayCerealData(data);
      } else {
        alert(`Error loading cereal data: ${data.error}`);
        this.$cerealViewerContainer
          .querySelector(".cereal-viewer-empty")
          .classList.remove("hidden");
      }
    } catch (error) {
      console.error("Error loading cereal data:", error);
      this.$cerealLoading.classList.add("hidden");
      alert(`Failed to load cereal data: ${error.message}`);
      this.$cerealViewerContainer
        .querySelector(".cereal-viewer-empty")
        .classList.remove("hidden");
    }
  }

  displayCerealData(cerealData) {
    const { messages, message_type, total_count, start_time, end_time } =
      cerealData;

    if (!messages || messages.length === 0) {
      this.$cerealViewerContainer
        .querySelector(".cereal-viewer-empty")
        .classList.remove("hidden");
      this.$cerealDataTable.classList.add("hidden");
      return;
    }

    // Store cereal data for syncing
    this.currentCerealData = messages;
    this.currentCerealStartTime = start_time;
    this.currentCerealEndTime = end_time;
    this.currentCerealType = message_type;

    // Show the first message by default
    this.displayCerealMessage(messages[0]);

    // Show table
    this.$cerealDataTable.classList.remove("hidden");

    // Update status
    this.$cerealMessageCount.textContent = `${total_count} message${
      total_count !== 1 ? "s" : ""
    }`;
    this.$cerealLastUpdate.textContent = `Last updated: ${new Date().toLocaleTimeString()}`;

    // If sync is enabled, start syncing
    if (this.$cerealSyncCheckbox.checked) {
      this.startCerealSync();
    }
  }

  displayCerealMessage(message) {
    // Clear table
    this.$cerealDataBody.innerHTML = "";

    // Flatten the message object and display in table
    const flatData = this.flattenObject(message.data);

    Object.entries(flatData).forEach(([key, value]) => {
      const row = document.createElement("tr");

      const fieldCell = document.createElement("td");
      fieldCell.className = "cereal-field-name";
      fieldCell.textContent = key;

      const valueCell = document.createElement("td");
      valueCell.className = "cereal-field-value";
      valueCell.textContent = this.formatCerealValue(value);

      row.appendChild(fieldCell);
      row.appendChild(valueCell);
      this.$cerealDataBody.appendChild(row);
    });
  }

  flattenObject(obj, prefix = "") {
    const flattened = {};

    // Handle non-object types (including strings, which are iterable)
    if (obj === null || obj === undefined) {
      return { [prefix || "value"]: obj };
    }

    if (typeof obj !== "object" || obj instanceof Date) {
      return { [prefix || "value"]: obj };
    }

    // Handle arrays
    if (Array.isArray(obj)) {
      return { [prefix || "value"]: obj };
    }

    // Handle plain objects
    for (const key in obj) {
      if (obj.hasOwnProperty(key)) {
        const value = obj[key];
        const newKey = prefix ? `${prefix}.${key}` : key;

        if (
          value !== null &&
          typeof value === "object" &&
          !Array.isArray(value) &&
          !(value instanceof Date)
        ) {
          // Recursively flatten nested objects
          Object.assign(flattened, this.flattenObject(value, newKey));
        } else {
          flattened[newKey] = value;
        }
      }
    }

    return flattened;
  }

  formatCerealValue(value) {
    if (value === null || value === undefined) {
      return "--";
    }
    if (typeof value === "boolean") {
      return value ? "true" : "false";
    }
    if (Array.isArray(value)) {
      if (value.length === 0) return "[]";
      if (value.length > 10) {
        return `[${value.slice(0, 5).join(", ")}, ... (${value.length} items)]`;
      }
      return `[${value.join(", ")}]`;
    }
    if (typeof value === "number") {
      // Format floats nicely
      if (Number.isInteger(value)) {
        return value.toString();
      }
      return value.toFixed(4);
    }
    return String(value);
  }

  stopCereal() {
    // Stop syncing if active
    if (this.cerealSyncInterval) {
      clearInterval(this.cerealSyncInterval);
      this.cerealSyncInterval = null;
    }

    // Uncheck sync checkbox
    if (this.$cerealSyncCheckbox) {
      this.$cerealSyncCheckbox.checked = false;
    }

    // Clear cereal data
    this.currentCerealData = null;
    this.currentCerealStartTime = null;
    this.currentCerealEndTime = null;
    this.currentCerealType = null;

    // Hide stop button, show load button
    this.$stopCerealBtn.classList.add("hidden");
    this.$loadCerealBtn.classList.remove("hidden");

    console.log("Cereal data streaming stopped");
  }

  startCerealSync() {
    // Show stop button, hide load button
    this.$stopCerealBtn.classList.remove("hidden");
    this.$loadCerealBtn.classList.add("hidden");

    // Clear any existing sync interval
    if (this.cerealSyncInterval) {
      clearInterval(this.cerealSyncInterval);
    }

    // Check video playback time and update cereal display
    this.cerealSyncInterval = setInterval(() => {
      if (!this.currentCerealData || !this.$cerealSyncCheckbox.checked) {
        clearInterval(this.cerealSyncInterval);
        // Restore button states when sync stops naturally
        this.$stopCerealBtn.classList.add("hidden");
        this.$loadCerealBtn.classList.remove("hidden");
        return;
      }

      // Get current video playback time
      let currentTime = 0;
      if (this.$videoElement) {
        currentTime = this.$videoElement.currentTime;
      } else if (
        this.player &&
        typeof this.player.getCurrentTime === "function"
      ) {
        currentTime = this.player.getCurrentTime() || 0;
      }

      // Calculate absolute timestamp
      // For HLS: currentTime is route-absolute, need time within current segment
      // For single-segment: currentTime is already segment-relative
      let segmentRelativeTime = currentTime;
      if (this.hls) {
        // HLS plays entire route continuously, so get time within current segment
        segmentRelativeTime = currentTime % 60;
      }
      const absoluteTime = this.currentCerealStartTime + segmentRelativeTime;

      // Find the closest message to current time
      let closestMessage = this.currentCerealData[0];
      let minDiff = Math.abs(closestMessage.timestamp - absoluteTime);

      for (const msg of this.currentCerealData) {
        const diff = Math.abs(msg.timestamp - absoluteTime);
        if (diff < minDiff) {
          minDiff = diff;
          closestMessage = msg;
        }
        // Stop searching if we've gone past the current time
        if (msg.timestamp > absoluteTime) {
          break;
        }
      }

      // Update display with closest message
      this.displayCerealMessage(closestMessage);

      // Update timestamp in status
      const relativeTime =
        closestMessage.timestamp - this.currentCerealStartTime;
      const minutes = Math.floor(relativeTime / 60);
      const seconds = (relativeTime % 60).toFixed(3);
      this.$cerealLastUpdate.textContent = `Showing: ${minutes}:${seconds.padStart(
        6,
        "0"
      )}`;
    }, 100); // Update 10 times per second for smooth syncing
  }

  // ========================================================================
  // FFmpeg Debug Panel Methods
  // ========================================================================

  toggleDebugPanel() {
    this.debugMode = !this.debugMode;

    // Toggle panel visibility
    if (this.debugMode) {
      this.$ffmpegDebugPanel.style.display = "block";
      this.$toggleDebugBtn.classList.add("active");
      this.addDebugLog(
        "info",
        "Debug mode enabled. FFmpeg logs will appear here in real-time."
      );
    } else {
      this.$ffmpegDebugPanel.style.display = "none";
      this.$toggleDebugBtn.classList.remove("active");
    }

    // Reload current segment with debug mode
    if (this.currentRoute && this.debugMode) {
      this.addDebugLog(
        "info",
        `Reloading segment ${this.currentSegment} with debug logging enabled...`
      );
      this.loadSegment(this.currentSegment);
    }
  }

  clearDebugLogs() {
    this.$ffmpegDebugMessages.innerHTML = "";
    this.addDebugLog("info", "Debug logs cleared.");
  }

  handleFFmpegLog(data) {
    // Only show if debug panel is visible
    if (!this.debugMode) {
      return;
    }

    const { route_info, log_type, message, pid } = data;

    // Check if this log is for the current video being played
    if (this.currentRoute) {
      const currentRouteInfo = `${this.currentRoute.route_base}:${this.currentSegment}:${this.currentCamera}`;

      // Only show logs for current route or show all logs (you can make this configurable)
      // For now, let's show all logs but highlight current route
      const isCurrent = route_info === currentRouteInfo;
      this.addDebugLog(log_type, message, pid, isCurrent);
    } else {
      this.addDebugLog(log_type, message, pid, false);
    }
  }

  addDebugLog(type, message, pid = null, isCurrent = true) {
    const logEntry = document.createElement("div");
    logEntry.className = `debug-log-entry debug-log-${type}`;
    if (!isCurrent) {
      logEntry.classList.add("debug-log-other");
    }

    const timestamp = new Date().toLocaleTimeString("en-US", {
      hour12: false,
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
      fractionalSecondDigits: 3,
    });

    const pidText = pid ? `[PID ${pid}] ` : "";
    const typeLabel = type.toUpperCase().padEnd(6, " ");

    logEntry.innerHTML = `
      <span class="debug-log-time">${timestamp}</span>
      <span class="debug-log-type">[${typeLabel}]</span>
      <span class="debug-log-pid">${pidText}</span>
      <span class="debug-log-message">${this.escapeHtml(message)}</span>
    `;

    this.$ffmpegDebugMessages.appendChild(logEntry);

    // Auto-scroll if enabled
    if (this.$debugAutoScroll && this.$debugAutoScroll.checked) {
      this.$ffmpegDebugContent.scrollTop =
        this.$ffmpegDebugContent.scrollHeight;
    }

    // Limit number of log entries to prevent memory issues (keep last 500)
    const maxEntries = 500;
    const entries = this.$ffmpegDebugMessages.children;
    if (entries.length > maxEntries) {
      this.$ffmpegDebugMessages.removeChild(entries[0]);
    }
  }

  escapeHtml(text) {
    const div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
  }

  // =========================================================================
  // View Navigation
  // =========================================================================

  navigateToView(view) {
    console.log(`Navigating to view: ${view}`);
    this.currentView = view;

    // Hide all views
    this.$landingPage.classList.add("hidden");
    this.$routesContainer.classList.add("hidden");
    this.$paramsManager.classList.add("hidden");

    // Hide/show disk panel based on view (only show on routes view)
    if (this.$diskVizContainer) {
      if (view === "routes") {
        this.$diskVizContainer.classList.remove("hidden");
      } else {
        this.$diskVizContainer.classList.add("hidden");
      }
    }

    // Show the requested view
    switch (view) {
      case "landing":
        this.$landingPage.classList.remove("hidden");
        this.$headerTitle.textContent = "BluePilot Web App";
        this.updateLandingPage();
        break;
      case "routes":
        this.$routesContainer.classList.remove("hidden");
        this.$headerTitle.textContent = "Routes";
        break;
      case "params":
        this.$paramsManager.classList.remove("hidden");
        this.$headerTitle.textContent = "Parameters";
        this.loadParams();
        break;
    }
  }

  updateLandingPage() {
    // Update route stats
    this.$landingRouteCount.textContent = this.$routeCount
      ? this.$routeCount.textContent
      : "0 routes";
    this.$landingTotalSize.textContent = this.$totalSize
      ? this.$totalSize.textContent
      : "0 GB";

    // Update params count
    if (Object.keys(this.allParams).length > 0) {
      this.$landingParamsCount.textContent = `${
        Object.keys(this.allParams).length
      } parameters`;
    }
  }

  // =========================================================================
  // Params Manager
  // =========================================================================

  async loadParams() {
    console.log(
      "[BluePilot DEBUG] Loading params... Code version: 2025-11-03-v2"
    );
    try {
      const response = await fetch(`${this.API_BASE}/api/params`);
      const data = await response.json();

      if (data.success) {
        this.allParams = data.params;

        // Debug: Check if params have type and last_modified
        const sampleParam = Object.values(data.params)[0];
        console.log("[BluePilot DEBUG] Sample param:", sampleParam);
        console.log(
          "[BluePilot DEBUG] Has type?",
          sampleParam?.type !== undefined
        );
        console.log(
          "[BluePilot DEBUG] Has last_modified?",
          sampleParam?.last_modified !== undefined
        );
        console.log(
          "[BluePilot DEBUG] Total params:",
          Object.keys(data.params).length
        );

        this.renderParamsTable();
      } else {
        this.$paramsContent.innerHTML = `
          <div style="padding: 2rem; text-align: center; color: var(--bp-error);">
            Failed to load parameters: ${data.error || "Unknown error"}
          </div>
        `;
      }
    } catch (error) {
      console.error("Error loading params:", error);
      this.$paramsContent.innerHTML = `
        <div style="padding: 2rem; text-align: center; color: var(--bp-error);">
          Error loading parameters: ${error.message}
        </div>
      `;
    }
  }

  renderParamsTable(filteredParams = null) {
    console.log(
      "[BluePilot DEBUG] renderParamsTable called - Code version: 2025-11-03-v2"
    );
    const paramsToRender = filteredParams || this.allParams;
    let paramsList = Object.values(paramsToRender);

    if (paramsList.length === 0) {
      this.$paramsContent.innerHTML =
        '<div style="padding: 2rem; text-align: center;">No parameters found</div>';
      return;
    }

    // Sort params based on current sort column and direction
    paramsList = this.sortParams(
      paramsList,
      this.paramsSortColumn,
      this.paramsSortDirection
    );

    // Debug first param to verify type and last_modified are present
    if (paramsList.length > 0) {
      console.log("[BluePilot DEBUG] First param in table:", paramsList[0]);
      console.log("[BluePilot DEBUG] Type:", paramsList[0].type);
      console.log(
        "[BluePilot DEBUG] Last modified:",
        paramsList[0].last_modified
      );
    }

    const rows = paramsList
      .map((param) => {
        const editButton = param.readonly
          ? '<button class="param-edit-btn" disabled>Read-Only</button>'
          : `<button class="param-edit-btn" data-key="${param.key}" ${
              !this.paramsEditMode ? "disabled" : ""
            }>Edit</button>`;

        // Format last modified time
        let lastModifiedText = "Never";
        if (param.last_modified) {
          const date = new Date(param.last_modified * 1000); // Convert Unix timestamp to milliseconds
          const now = new Date();
          const diffMs = now - date;
          const diffMins = Math.floor(diffMs / 60000);
          const diffHours = Math.floor(diffMs / 3600000);
          const diffDays = Math.floor(diffMs / 86400000);

          if (diffMins < 1) {
            lastModifiedText = "Just now";
          } else if (diffMins < 60) {
            lastModifiedText = `${diffMins}m ago`;
          } else if (diffHours < 24) {
            lastModifiedText = `${diffHours}h ago`;
          } else if (diffDays < 7) {
            lastModifiedText = `${diffDays}d ago`;
          } else {
            lastModifiedText = date.toLocaleDateString();
          }
        }

        const valueStr = String(param.value ?? "null");
        const valueDisplay = this.escapeHtml(valueStr);

        return `
        <tr>
          <td><span class="param-key" title="${this.escapeHtml(param.key)}">${this.escapeHtml(param.key)}</span></td>
          <td><span class="param-value" data-key="${this.escapeHtml(param.key)}" title="Click to view full value">${valueDisplay}</span></td>
          <td>
            <span class="param-badge ${param.type}">${param.type}</span>
            ${
              param.readonly
                ? '<span class="param-badge readonly">readonly</span>'
                : ""
            }
            ${
              param.critical
                ? '<span class="param-badge critical">critical</span>'
                : ""
            }
          </td>
          <td><span class="param-badge">${param.category}</span></td>
          <td><span class="param-last-modified">${lastModifiedText}</span></td>
          <td>${editButton}</td>
        </tr>
      `;
      })
      .join("");

    // Helper function to create sortable header
    const createSortableHeader = (column, label) => {
      const sortIcon =
        this.paramsSortColumn === column
          ? this.paramsSortDirection === "asc"
            ? "▲"
            : "▼"
          : "⇅";
      const activeClass = this.paramsSortColumn === column ? "active-sort" : "";
      return `<th class="sortable-header ${activeClass}" data-column="${column}">${label} <span class="sort-icon">${sortIcon}</span></th>`;
    };

    this.$paramsContent.innerHTML = `
      <div class="params-table">
        <table>
          <thead>
            <tr>
              ${createSortableHeader("key", "Parameter")}
              ${createSortableHeader("value", "Value")}
              ${createSortableHeader("type", "Type")}
              ${createSortableHeader("category", "Category")}
              ${createSortableHeader("last_modified", "Last Modified")}
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            ${rows}
          </tbody>
        </table>
      </div>
    `;

    // Attach event listeners to sortable headers
    this.$paramsContent
      .querySelectorAll(".sortable-header")
      .forEach((header) => {
        header.addEventListener("click", (e) => {
          const column = e.currentTarget.getAttribute("data-column");
          this.sortParamsBy(column);
        });
      });

    // Attach event listeners to edit buttons
    if (this.paramsEditMode) {
      this.$paramsContent
        .querySelectorAll(".param-edit-btn:not([disabled])")
        .forEach((btn) => {
          btn.addEventListener("click", (e) => {
            const key = e.target.getAttribute("data-key");
            this.editParam(key);
          });
        });
    }

    // Attach event listeners to parameter values for viewing
    this.$paramsContent.querySelectorAll(".param-value").forEach((valueSpan) => {
      valueSpan.addEventListener("click", (e) => {
        const key = e.target.getAttribute("data-key");
        this.openParamValueModal(key);
      });
    });
  }

  sortParamsBy(column) {
    // If clicking the same column, toggle direction
    if (this.paramsSortColumn === column) {
      this.paramsSortDirection =
        this.paramsSortDirection === "asc" ? "desc" : "asc";
    } else {
      // New column, default to ascending
      this.paramsSortColumn = column;
      this.paramsSortDirection = "asc";
    }

    // Re-render with new sort
    const searchValue = this.$paramsSearch ? this.$paramsSearch.value : "";
    if (searchValue) {
      this.searchParams(searchValue);
    } else {
      this.renderParamsTable();
    }
  }

  sortParams(params, column, direction) {
    const sorted = [...params].sort((a, b) => {
      let aVal, bVal;

      switch (column) {
        case "key":
          aVal = a.key.toLowerCase();
          bVal = b.key.toLowerCase();
          break;
        case "value":
          aVal = String(a.value ?? "").toLowerCase();
          bVal = String(b.value ?? "").toLowerCase();
          break;
        case "type":
          aVal = a.type;
          bVal = b.type;
          break;
        case "category":
          aVal = a.category;
          bVal = b.category;
          break;
        case "last_modified":
          // Sort by timestamp (most recent first when desc)
          aVal = a.last_modified || 0;
          bVal = b.last_modified || 0;
          break;
        default:
          return 0;
      }

      if (aVal < bVal) return direction === "asc" ? -1 : 1;
      if (aVal > bVal) return direction === "asc" ? 1 : -1;
      return 0;
    });

    return sorted;
  }

  searchParams(query) {
    if (!query) {
      this.renderParamsTable();
      return;
    }

    const filtered = {};
    const lowerQuery = query.toLowerCase();

    for (const [key, param] of Object.entries(this.allParams)) {
      if (
        key.toLowerCase().includes(lowerQuery) ||
        String(param.value).toLowerCase().includes(lowerQuery) ||
        param.category.toLowerCase().includes(lowerQuery)
      ) {
        filtered[key] = param;
      }
    }

    this.renderParamsTable(filtered);
  }

  editParam(key) {
    const param = this.allParams[key];
    if (!param) return;

    const currentValue = param.value;
    let newValue;

    if (param.type === "bool") {
      newValue = confirm(
        `Change ${key} from ${currentValue} to ${!currentValue}?`
      );
      if (newValue === null) return;
      newValue = !currentValue;
    } else {
      newValue = prompt(
        `Edit parameter: ${key}\nCurrent value: ${currentValue}\n\nEnter new value:`,
        currentValue
      );
      if (newValue === null) return; // User cancelled

      // Type conversion
      if (param.type === "int") {
        newValue = parseInt(newValue);
        if (isNaN(newValue)) {
          alert("Invalid integer value");
          return;
        }
      } else if (param.type === "float") {
        newValue = parseFloat(newValue);
        if (isNaN(newValue)) {
          alert("Invalid float value");
          return;
        }
      }
    }

    // Confirmation for critical params
    if (param.critical) {
      const confirmed = confirm(
        `⚠️ WARNING: This is a critical parameter!\n\n` +
          `Parameter: ${key}\n` +
          `Current: ${currentValue}\n` +
          `New: ${newValue}\n\n` +
          `Changing this may affect vehicle behavior. Are you sure?`
      );
      if (!confirmed) return;
    }

    this.setParam(key, newValue);
  }

  async setParam(key, value) {
    try {
      const response = await fetch(`${this.API_BASE}/api/params/set`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({ key, value }),
      });

      const data = await response.json();

      if (data.success) {
        alert(`Successfully updated ${key}`);
        // Reload params to reflect change
        await this.loadParams();
      } else {
        alert(`Failed to update parameter: ${data.error}`);
      }
    } catch (error) {
      console.error("Error setting param:", error);
      alert(`Error setting parameter: ${error.message}`);
    }
  }
}

// Initialize app when DOM is ready
function initializeApp() {
  console.log("Initializing BluePilot Routes app...");
  console.log("DOM ready state:", document.readyState);
  console.log("Document body:", !!document.body);

  // Wait a bit more to ensure all elements are loaded
  setTimeout(() => {
    const app = new BluePilotRoutes();
    console.log("App initialized successfully");
  }, 100);
}

// Initialize app when DOM is ready
if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", initializeApp);
} else {
  initializeApp();
}
