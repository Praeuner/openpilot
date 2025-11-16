import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { Header } from '@/components/layout/Header'
import { useSystemStore } from '@/stores/useSystemStore'
import { useRoutesStore } from '@/stores/useRoutesStore'
import { useParamsStore } from '@/stores/useParamsStore'
import './Home.css'

interface HomeProps {
  deviceStatus?: 'online' | 'onroad' | 'offline' | 'checking'
}

interface DriveStats {
  routes: number
  distance: number
  distanceMiles: number
  duration: number
  durationMinutes: number
  averageSpeed: number
}

interface DriveStatsResponse {
  success: boolean
  all: DriveStats
  week: DriveStats
  source?: string
  error?: string
  cloud_error?: string
}

export const Home = ({ deviceStatus = 'checking' }: HomeProps) => {
  const navigate = useNavigate()
  const { status, deviceInfo, metrics, diskSpace, fetchStatus, fetchDeviceInfo } = useSystemStore()
  const { fetchRoutes } = useRoutesStore()
  const { params, fetchParams } = useParamsStore()
  const [driveStats, setDriveStats] = useState<DriveStatsResponse | null>(null)
  const [driveStatsLoading, setDriveStatsLoading] = useState(true)

  useEffect(() => {
    console.log('Home mounted, fetching data...')
    fetchStatus()
    fetchDeviceInfo()
    fetchRoutes(1)
    fetchParams()
    fetchDriveStats()
  }, [fetchStatus, fetchDeviceInfo, fetchRoutes, fetchParams])

  const fetchDriveStats = async () => {
    setDriveStatsLoading(true)
    try {
      const response = await fetch('/api/drive-stats')
      if (response.ok) {
        const responseData = await response.json()
        if (responseData.success) {
          setDriveStats(responseData)
        }
      }
    } catch (error) {
      console.error('Error fetching drive stats:', error)
    } finally {
      setDriveStatsLoading(false)
    }
  }

  const paramCount = Object.keys(params).length

  // Format uptime from seconds to "Xh Ym" format
  const getUptimeDisplay = () => {
    if (metrics?.uptime_seconds && metrics.uptime_seconds > 0) {
      const hours = Math.floor(metrics.uptime_seconds / 3600)
      const minutes = Math.floor((metrics.uptime_seconds % 3600) / 60)
      return `${hours}h ${minutes}m`
    }
    return 'N/A'
  }

  // Get color class for CPU temperature
  const getTempColorClass = (temp?: number): string => {
    if (!temp) return 'normal'
    if (temp >= 85) return 'critical'
    if (temp >= 70) return 'warning'
    return 'normal'
  }

  // Get color class for memory usage
  const getMemoryColorClass = (percent?: number): string => {
    if (!percent) return 'normal'
    if (percent >= 85) return 'critical'
    if (percent >= 70) return 'warning'
    return 'normal'
  }

  // Get color class for storage (based on percentage used - inverted logic)
  const getStorageColorClass = (): string => {
    if (!diskSpace?.total || !diskSpace?.free) return 'normal'
    const percentUsed = ((diskSpace.total - diskSpace.free) / diskSpace.total) * 100
    if (percentUsed >= 90) return 'critical'  // < 10% free
    if (percentUsed >= 80) return 'warning'   // < 20% free
    return 'normal'
  }

  // Format storage for display
  const getStorageDisplay = (): string => {
    if (!diskSpace?.free) return 'N/A'
    const gb = diskSpace.free / (1024 ** 3)
    if (gb >= 1) {
      return `${gb.toFixed(1)}GB`
    }
    return `${(diskSpace.free / (1024 ** 2)).toFixed(0)}MB`
  }

  return (
    <>
      <Header
        deviceStatus={deviceStatus}
        subtitle="Settings, routes, and diagnostics"
      />
      <div className="dashboard-page">
        <div className="dashboard-insights-grid">
          <section className="dashboard-status-panel">
            <div className="panel-heading">
              <h2>System Status</h2>
            </div>
            <div className="status-pills-container">
              {/* System Metrics */}
              <div className="status-pills-row">
                <div className="status-pill" title="System uptime">
                  <svg
                    className="pill-icon"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="2"
                  >
                    <circle cx="12" cy="12" r="10" />
                    <polyline points="12 6 12 12 16 14" />
                  </svg>
                  <span className="pill-label">Uptime</span>
                  <span className="pill-value">{getUptimeDisplay()}</span>
                </div>

                <div className={`status-pill ${getTempColorClass(metrics?.temperature)}`} title="CPU Temperature">
                  <svg
                    className="pill-icon"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="2"
                  >
                    <path d="M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z" />
                  </svg>
                  <span className="pill-label">CPU</span>
                  <span className="pill-value">
                    {metrics?.temperature ? `${metrics.temperature.toFixed(1)}°C` : 'N/A'}
                  </span>
                </div>

                <div className={`status-pill ${getMemoryColorClass(metrics?.memory_percent)}`} title="Memory Usage">
                  <svg
                    className="pill-icon"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="2"
                  >
                    <rect x="4" y="4" width="16" height="16" rx="2" ry="2" />
                    <rect x="9" y="9" width="6" height="6" />
                    <line x1="9" y1="1" x2="9" y2="4" />
                    <line x1="15" y1="1" x2="15" y2="4" />
                    <line x1="9" y1="20" x2="9" y2="23" />
                    <line x1="15" y1="20" x2="15" y2="23" />
                    <line x1="20" y1="9" x2="23" y2="9" />
                    <line x1="20" y1="14" x2="23" y2="14" />
                    <line x1="1" y1="9" x2="4" y2="9" />
                    <line x1="1" y1="14" x2="4" y2="14" />
                  </svg>
                  <span className="pill-label">Memory</span>
                  <span className="pill-value">
                    {metrics?.memory_percent ? `${metrics.memory_percent.toFixed(0)}%` : 'N/A'}
                  </span>
                </div>

                <div className={`status-pill ${getStorageColorClass()}`} title="Storage Free">
                  <svg
                    className="pill-icon"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="2"
                  >
                    <ellipse cx="12" cy="5" rx="9" ry="3" />
                    <path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3" />
                    <path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5" />
                  </svg>
                  <span className="pill-label">Storage</span>
                  <span className="pill-value">{getStorageDisplay()}</span>
                </div>
              </div>

              {/* Device Info */}
              <div className="status-pills-row">
                <div className="status-pill">
                  <span className="pill-label">Dongle ID</span>
                  <span className="pill-value">{deviceInfo?.dongle_id || 'N/A'}</span>
                </div>
                <div className="status-pill">
                  <span className="pill-label">Serial</span>
                  <span className="pill-value">{deviceInfo?.serial || 'N/A'}</span>
                </div>
              </div>

              {/* Version Info */}
              <div className="status-pills-row">
                <div className="status-pill">
                  <span className="pill-label">BP Version</span>
                  <span className="pill-value">{deviceInfo?.bp_version ? `v${deviceInfo.bp_version}` : 'N/A'}</span>
                </div>
                <div className="status-pill">
                  <span className="pill-label">SP Version</span>
                  <span className="pill-value">{deviceInfo?.sp_version || 'N/A'}</span>
                </div>
                <div className="status-pill">
                  <span className="pill-label">OP Version</span>
                  <span className="pill-value">{deviceInfo?.op_version || 'N/A'}</span>
                </div>
              </div>
            </div>
          </section>

          <section className="dashboard-drive-stats-panel">
            <div className="panel-heading">
              <h2>Drive Statistics</h2>
            </div>
            {driveStatsLoading ? (
              <div className="drive-stats-loading">Loading...</div>
            ) : driveStats ? (
              <div className="drive-stats-content">
                <div className="drive-stats-group">
                  <h3 className="stats-period-title">All Time</h3>
                  <div className="stats-cards-grid">
                    <div className="stat-card all-time">
                      <div className="stat-value">{driveStats.all.routes.toLocaleString()}</div>
                      <div className="stat-label">Total Drives</div>
                    </div>
                    <div className="stat-card all-time">
                      <div className="stat-value">{Math.round(driveStats.all.distanceMiles).toLocaleString()}</div>
                      <div className="stat-label">Miles Driven</div>
                    </div>
                    <div className="stat-card all-time">
                      <div className="stat-value">{Math.round(driveStats.all.duration / 3600).toLocaleString()}</div>
                      <div className="stat-label">Hours Driven</div>
                    </div>
                  </div>
                </div>
                <div className="drive-stats-group">
                  <h3 className="stats-period-title">This Week</h3>
                  <div className="stats-cards-grid">
                    <div className="stat-card">
                      <div className="stat-value">{driveStats.week.routes}</div>
                      <div className="stat-label">Drives</div>
                    </div>
                    <div className="stat-card">
                      <div className="stat-value">{Math.round(driveStats.week.distanceMiles)}</div>
                      <div className="stat-label">Miles</div>
                    </div>
                    <div className="stat-card">
                      <div className="stat-value">{Math.round(driveStats.week.duration / 3600)}</div>
                      <div className="stat-label">Hours</div>
                    </div>
                  </div>
                </div>
              </div>
            ) : (
              <div className="drive-stats-empty">No drive data available</div>
            )}
          </section>

          <section className="dashboard-quick-panel">
            <div className="panel-heading">
              <h2>Quick Access</h2>
            </div>
            <div className="quick-links-grid">
              <button className="quick-link-card settings" onClick={() => navigate('/settings')}>
                <div className="quick-link-icon">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <circle cx="12" cy="12" r="3" />
                    <path d="M12 1v6m0 6v6m8.66-7.5l-5.2 3M8.54 14l-5.2 3m10.4-15l-5.2 9m5.2 3l-5.2 9" />
                  </svg>
                </div>
                <div className="quick-link-copy">
                  <span className="label">Settings</span>
                  <span className="subtext">Configure BluePilot</span>
                </div>
              </button>
              <button className="quick-link-card routes" onClick={() => navigate('/routes')}>
                <div className="quick-link-icon">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z" />
                    <circle cx="12" cy="10" r="3" />
                  </svg>
                </div>
                <div className="quick-link-copy">
                  <span className="label">Routes</span>
                  <span className="subtext">Review recordings</span>
                </div>
                <span className="link-badge">{status?.routes_count || 0}</span>
              </button>
              <button className="quick-link-card parameters" onClick={() => navigate('/parameters')}>
                <div className="quick-link-icon">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <circle cx="12" cy="12" r="3" />
                    <path d="M12 1v6m0 6v6m8.66-7.5l-5.2 3M8.54 14l-5.2 3m10.4-15l-5.2 9m5.2 3l-5.2 9" />
                  </svg>
                </div>
                <div className="quick-link-copy">
                  <span className="label">Parameters</span>
                  <span className="subtext">Manage system params</span>
                </div>
                <span className="link-badge">{paramCount}</span>
              </button>
              <button className="quick-link-card logs" onClick={() => navigate('/logs')}>
                <div className="quick-link-icon">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
                    <polyline points="14 2 14 8 20 8" />
                    <line x1="16" y1="13" x2="8" y2="13" />
                    <line x1="16" y1="17" x2="8" y2="17" />
                    <polyline points="10 9 9 9 8 9" />
                  </svg>
                </div>
                <div className="quick-link-copy">
                  <span className="label">System Logs</span>
                  <span className="subtext">View live system logs</span>
                </div>
              </button>
            </div>
          </section>
        </div>
      </div>
    </>
  )
}
