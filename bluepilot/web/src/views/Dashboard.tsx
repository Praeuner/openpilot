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
  const { metrics, status, diskSpace, fetchStatus, fetchMetrics, fetchDiskSpace, startPolling, stopPolling } = useSystemStore()
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
  }, [fetchStatus, fetchMetrics, fetchDiskSpace, fetchRoutes, fetchParams, fetchPanels, fetchState, startPolling, stopPolling])

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
            </div>

            {!driveStatsLoading && driveStats && (
              <div className="drive-stats-pills-container">
                <div className="drive-stat-pill all-time">
                  <span className="pill-label">All Time Drives</span>
                  <span className="pill-value">{driveStats.all.routes}</span>
                </div>
                <div className="drive-stat-pill all-time">
                  <span className="pill-label">All Time Miles</span>
                  <span className="pill-value">{Math.round(driveStats.all.distanceMiles)}</span>
                </div>
                <div className="drive-stat-pill all-time">
                  <span className="pill-label">All Time Hours</span>
                  <span className="pill-value">{Math.round(driveStats.all.duration / 3600)}</span>
                </div>
                <div className="drive-stat-pill">
                  <span className="pill-label">Week Drives</span>
                  <span className="pill-value">{driveStats.week.routes}</span>
                </div>
                <div className="drive-stat-pill">
                  <span className="pill-label">Week Miles</span>
                  <span className="pill-value">{Math.round(driveStats.week.distanceMiles)}</span>
                </div>
                <div className="drive-stat-pill">
                  <span className="pill-label">Week Hours</span>
                  <span className="pill-value">{Math.round(driveStats.week.duration / 3600)}</span>
                </div>
              </div>
            )}
          </section>

          <section className="dashboard-quick-panel">
            <div className="panel-heading">
              <p>Shortcuts</p>
              <h2>Routes & Diagnostics</h2>
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
                <div className="link-badge">{status?.routes_count || 0}</div>
              </button>
              <button className="quick-link-card diagnostics" onClick={() => navigate('/diagnostics')}>
                <div className="quick-link-icon">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <path d="M12 2v6M12 16v6M4.93 4.93l4.24 4.24M14.83 14.83l4.24 4.24M2 12h6M16 12h6M4.93 19.07l4.24-4.24M14.83 9.17l4.24-4.24" />
                  </svg>
                </div>
                <div className="quick-link-copy">
                  <span className="label">Diagnostics</span>
                  <span className="subtext">Inspect params & health</span>
                </div>
                <div className="link-badge">{paramCount}</div>
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
