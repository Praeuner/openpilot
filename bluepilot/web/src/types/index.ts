// Core type definitions for BluePilot Web App

export interface VehicleFingerprint {
  carFingerprint?: string
  carVin?: string
  brand?: string
  fingerprintSource?: string
  fuzzyFingerprint?: boolean
  firmwareVersions?: Record<string, string>
}

export interface Route {
  baseName: string  // Primary identifier from backend
  id?: string       // Alias for baseName
  date: string
  duration: string
  distance?: string
  mileage?: string
  size: string
  sizeBytes?: number
  segments: number
  totalSegments?: number
  preserved?: boolean
  isStarred?: boolean  // Alternative name for preserved
  timestamp?: string
  start_time?: string
  end_time?: string
  start_lat?: number
  start_lon?: number
  end_lat?: number
  end_lon?: number
  avgSpeed?: string
  avg_speed?: string
  topSpeed?: string
  top_speed?: string
  engagement_time?: number
  disengagements?: number
  alerts?: number
  driveStats?: {
    opEngagedPercent?: number
    alertCount?: number
    disengagementCount?: number
  }
  alert_count?: number
  op_engaged_percent?: number
  startLocation?: string
  start_location?: string
  endLocation?: string
  end_location?: string
  fingerprint?: VehicleFingerprint
  thumbnail?: string
  has_thumbnail?: boolean
  hasThumbnail?: boolean
  platform?: string
  processing?: boolean
}

export interface VideoInfo {
  codec: string
  width?: number
  height?: number
  size?: number
  duration?: number
}

export interface RouteSegment {
  number: number
  name: string
  path: string
  videos: Record<string, VideoInfo>  // Camera type -> video info (e.g., {front: {...}, wide: {...}})
}

export interface RouteDetails extends Omit<Route, 'segments'> {
  segments: RouteSegment[]  // Backend returns 'segments' array for details, not 'segments_info'
  totalSegments?: number  // Total segment count
  total_segments?: number  // Alternative name from backend
}

export interface Parameter {
  key: string
  value: string | number | boolean
  description?: string
  type: 'string' | 'number' | 'boolean'
  category?: string
  readonly?: boolean
  critical?: boolean
  last_modified?: number  // Unix timestamp
}

export interface SystemMetrics {
  cpu_load: number
  cpu_cores: number
  memory_used: number
  memory_total: number
  memory_percent: number
  disk_used: number
  disk_total: number
  disk_percent: number
  temperature?: number
  ffmpeg_processes: number
  cache_size: number
}

export interface ExportJob {
  route_id: string
  type: 'video' | 'backup'
  status: 'queued' | 'processing' | 'completed' | 'failed'
  progress: number
  message?: string
  download_url?: string
  created_at: string
}

export interface WebSocketMessage {
  type: string
  data?: unknown
}

export interface ServerStatus {
  status: 'running' | 'idle' | 'error'
  routes_count: number
  params_count: number
  error?: string
  timestamp: string
  onroad?: boolean
  online?: boolean
}

export interface VideoCamera {
  id: string
  label: string
  available: boolean
}

export interface VideoSegment {
  segment: number
  cameras: VideoCamera[]
}

export type CameraType = 'front' | 'wide' | 'driver' | 'lq'  // Backend uses these names

export interface DiskSpace {
  total: number
  used: number
  free: number
  percent: number
}

export interface VehicleInfo {
  platform: string
  vin?: string
  brand?: string
  firmware_version?: string
}

export interface VideoPlayerState {
  playing: boolean
  currentTime: number
  duration: number
  segment: number
  camera: CameraType
  muted: boolean
  volume: number
  fullscreen: boolean
  buffering: boolean
}
