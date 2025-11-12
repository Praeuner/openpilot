import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { Header } from '@/components/layout/Header'
import { LoadingSpinner } from '@/components/common'
import { PanelGroup } from '@/components/settings/PanelGroup'
import { BackupRestore } from '@/components/settings/BackupRestore'
import { FavoritesPanel } from '@/components/settings/FavoritesPanel'
import { useSystemStore } from '@/stores/useSystemStore'
import { useRoutesStore } from '@/stores/useRoutesStore'
import { useParamsStore } from '@/stores/useParamsStore'
import { usePanelsStore } from '@/stores/usePanelsStore'
import { usePanelStateStore } from '@/stores/usePanelStateStore'
import { formatSize } from '@/utils/format'
import './Dashboard.css'

interface DashboardProps {
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

export const Dashboard = ({ deviceStatus = 'checking' }: DashboardProps) => {
  const navigate = useNavigate()
  const { metrics, status, diskSpace, deviceInfo, fetchStatus, fetchMetrics, fetchDiskSpace, fetchDeviceInfo, startPolling, stopPolling } = useSystemStore()
  const { fetchRoutes } = useRoutesStore()
  const { params, fetchParams } = useParamsStore()
  const { panels, loadedPanels, loading: panelsLoading, fetchPanels, fetchPanel } = usePanelsStore()
  const { state, fetchState } = usePanelStateStore()
  const [selectedPanelId, setSelectedPanelId] = useState<string | null>(null)
  const [searchQuery, setSearchQuery] = useState('')
  const [driveStats, setDriveStats] = useState<DriveStatsResponse | null>(null)
  const [driveStatsLoading, setDriveStatsLoading] = useState(true)

  useEffect(() => {
    console.log('Dashboard mounted, fetching data...')
    fetchStatus()
    fetchMetrics()
    fetchDiskSpace()
    fetchDeviceInfo()
    fetchRoutes(1)
    fetchParams()
    fetchPanels()
    fetchState()
    fetchDriveStats()

    // Start polling for system metrics updates every 5 seconds
    startPolling(5000)

    // Cleanup on unmount
    return () => {
      stopPolling()
    }
  }, [fetchStatus, fetchMetrics, fetchDiskSpace, fetchDeviceInfo, fetchRoutes, fetchParams, fetchPanels, fetchState, startPolling, stopPolling])

  // Auto-select Favorites panel on mount
  useEffect(() => {
    if (!selectedPanelId) {
      setSelectedPanelId('favorites')
    }
  }, [selectedPanelId])

  // Load selected panel configuration
  useEffect(() => {
    if (selectedPanelId && selectedPanelId !== 'favorites' && !loadedPanels[selectedPanelId]) {
      fetchPanel(selectedPanelId)
    }
  }, [selectedPanelId, loadedPanels, fetchPanel])

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

  // Calculate uptime display from metrics
  const getUptimeDisplay = () => {
    if (metrics?.uptime_seconds && metrics.uptime_seconds > 0) {
      const hours = Math.floor(metrics.uptime_seconds / 3600)
      const minutes = Math.floor((metrics.uptime_seconds % 3600) / 60)
      return `${hours}h ${minutes}m`
    }
    return 'N/A'
  }

  const selectedPanel = selectedPanelId ? loadedPanels[selectedPanelId] : null

  // Filter panel groups based on search query
  const filteredGroups = selectedPanel?.groups.map((group) => {
    if (!searchQuery.trim()) {
      return group
    }

    const query = searchQuery.toLowerCase()
    const filteredControls = group.controls.filter((control) => {
      const title = control.title?.toLowerCase() || ''
      const desc = control.desc?.toLowerCase() || ''
      return title.includes(query) || desc.includes(query)
    })

    return {
      ...group,
      controls: filteredControls,
    }
  }).filter((group) => group.controls.length > 0)

  return (
    <>
      <Header deviceStatus={deviceStatus} />
      <div className="dashboard-page">
        <div className="dashboard-header">
          <h1>BluePilot Dashboard</h1>
          <p>Your central hub for monitoring, configuration, and diagnostics</p>
        </div>

        <div className="dashboard-insights-grid">
          <section className="dashboard-status-panel">
            <div className="panel-heading">
              <p>Live telemetry</p>
              <h2>System Status</h2>
            </div>
            <div className="status-pills-container">
              <div className="status-pill">
                <span className="pill-label">Uptime</span>
                <span className="pill-value">{getUptimeDisplay()}</span>
              </div>
              <div className="status-pill">
                <span className="pill-label">CPU</span>
                <span className="pill-value">
                  {metrics?.temperature ? `${metrics.temperature.toFixed(1)}°C` : 'N/A'}
                </span>
              </div>
              <div className="status-pill">
                <span className="pill-label">Storage</span>
                <span className="pill-value">
                  {diskSpace ? formatSize(diskSpace.free) : 'N/A'}
                </span>
              </div>
              <div className="status-pill">
                <span className="pill-label">Memory</span>
                <span className="pill-value">
                  {metrics?.memory_percent ? `${metrics.memory_percent.toFixed(1)}%` : 'N/A'}
                </span>
              </div>
              <div className="status-pill">
                <span className="pill-label">Dongle ID</span>
                <span className="pill-value">{deviceInfo?.dongle_id || 'N/A'}</span>
              </div>
              <div className="status-pill">
                <span className="pill-label">Serial</span>
                <span className="pill-value">{deviceInfo?.serial || 'N/A'}</span>
              </div>
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

            {!driveStatsLoading && driveStats && (
              <div className="drive-stats-section">
                <div className="drive-stats-group">
                  <h4 className="drive-stats-title">All Time</h4>
                  <div className="drive-stats-pills-row">
                    <div className="drive-stat-pill all-time">
                      <span className="pill-label">Drives</span>
                      <span className="pill-value">{driveStats.all.routes}</span>
                    </div>
                    <div className="drive-stat-pill all-time">
                      <span className="pill-label">Miles</span>
                      <span className="pill-value">{Math.round(driveStats.all.distanceMiles)}</span>
                    </div>
                    <div className="drive-stat-pill all-time">
                      <span className="pill-label">Hours</span>
                      <span className="pill-value">{Math.round(driveStats.all.duration / 3600)}</span>
                    </div>
                  </div>
                </div>
                <div className="drive-stats-group">
                  <h4 className="drive-stats-title">This Week</h4>
                  <div className="drive-stats-pills-row">
                    <div className="drive-stat-pill">
                      <span className="pill-label">Drives</span>
                      <span className="pill-value">{driveStats.week.routes}</span>
                    </div>
                    <div className="drive-stat-pill">
                      <span className="pill-label">Miles</span>
                      <span className="pill-value">{Math.round(driveStats.week.distanceMiles)}</span>
                    </div>
                    <div className="drive-stat-pill">
                      <span className="pill-label">Hours</span>
                      <span className="pill-value">{Math.round(driveStats.week.duration / 3600)}</span>
                    </div>
                  </div>
                </div>
              </div>
            )}
          </section>

          <section className="dashboard-quick-panel">
            <div className="panel-heading">
              <p>Shortcuts</p>
              <h2>Quick Access</h2>
            </div>
            <div className="quick-links-grid">
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

        {/* Settings Section */}
        <div className="dashboard-settings">
          <div className="settings-header-row">
            <h2>Settings</h2>
            {/* Search Bar */}
            <div className="settings-search">
              <input
                type="text"
                className="settings-search-input"
                placeholder="Search settings..."
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
              />
              {searchQuery && (
                <button
                  className="settings-search-clear"
                  onClick={() => setSearchQuery('')}
                  aria-label="Clear search"
                >
                  ✕
                </button>
              )}
            </div>
          </div>

          {panelsLoading && panels.length === 0 ? (
            <LoadingSpinner message="Loading settings..." />
          ) : (
            <>
              {/* Panel Tabs */}
              <div className="settings-tabs">
                <button
                  className={`settings-tab ${selectedPanelId === 'favorites' ? 'active' : ''}`}
                  onClick={() => setSelectedPanelId('favorites')}
                >
                  ⭐ Favorites
                </button>
                {panels.map((panel) => (
                  <button
                    key={panel.id}
                    className={`settings-tab ${selectedPanelId === panel.id ? 'active' : ''}`}
                    onClick={() => setSelectedPanelId(panel.id)}
                  >
                    {panel.name}
                  </button>
                ))}
              </div>

              {/* Panel Content */}
              <div className="settings-panel-content">
                {selectedPanelId === 'favorites' ? (
                  <FavoritesPanel />
                ) : selectedPanel ? (
                  <>
                    {selectedPanel.menuDescription && !searchQuery && (
                      <div className="settings-panel-description">
                        {selectedPanel.menuDescription}
                      </div>
                    )}

                    {selectedPanelId === 'bp_device_panel' && !searchQuery && (
                      <div className="panel-group">
                        <h3>Backup & Restore</h3>
                        <BackupRestore />
                      </div>
                    )}

                    {filteredGroups && filteredGroups.length > 0 ? (
                      filteredGroups.map((group) => (
                        <PanelGroup
                          key={group.groupName}
                          group={group}
                          state={state}
                          panelId={selectedPanelId || undefined}
                        />
                      ))
                    ) : searchQuery ? (
                      <div className="settings-no-results">
                        <p>No settings found for "{searchQuery}"</p>
                        <button onClick={() => setSearchQuery('')}>Clear search</button>
                      </div>
                    ) : null}
                  </>
                ) : (
                  <LoadingSpinner message="Loading panel..." />
                )}
              </div>
            </>
          )}
        </div>
      </div>
    </>
  )
}
