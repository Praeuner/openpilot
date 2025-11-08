import { useEffect, useState } from 'react'
import { systemAPI } from '@/services/api'
import { useRoutesStore } from '@/stores/useRoutesStore'
import { ImportBackupModal } from '@/components/modals'
import './DiskSpaceVisualization.css'

interface DiskSpaceData {
  total_bytes: number
  used_bytes: number
  free_bytes: number
  preserved_bytes: number
  non_preserved_bytes: number
  deletion_threshold_bytes: number
  formatted: {
    total: string
    used: string
    free: string
    preserved: string
    non_preserved: string
  }
}

interface CacheData {
  total_bytes: number
  formatted: {
    total: string
  }
}

interface DiskAnalysis {
  success: boolean
  disk: DiskSpaceData
  cache?: CacheData
}

export const DiskSpaceVisualization = () => {
  const [diskData, setDiskData] = useState<DiskAnalysis | null>(null)
  const [loading, setLoading] = useState(true)
  const [showImportModal, setShowImportModal] = useState(false)
  const { routes, clearCache, fetchRoutes } = useRoutesStore()

  useEffect(() => {
    loadDiskAnalysis()
  }, [])

  const loadDiskAnalysis = async () => {
    try {
      setLoading(true)
      const response = await systemAPI.getDiskAnalysis()
      setDiskData(response)
    } catch (error) {
      console.error('Failed to load disk analysis:', error)
    } finally {
      setLoading(false)
    }
  }

  const handleClearCache = async () => {
    if (confirm('Clear all cached data (videos, thumbnails, GPS)?')) {
      await clearCache()
      loadDiskAnalysis() // Reload disk analysis after clearing
    }
  }

  const handleRefresh = async () => {
    await fetchRoutes(1)
    loadDiskAnalysis() // Reload disk analysis after refresh
  }

  const handleImportComplete = async () => {
    // Refresh routes after import
    await fetchRoutes(1)
    loadDiskAnalysis()
  }

  if (loading || !diskData || !diskData.success) {
    return null
  }

  const { disk, cache } = diskData

  // Calculate percentages for gauge bar segments
  const preservedPercent = (disk.preserved_bytes / disk.total_bytes) * 100
  const routesPercent = (disk.non_preserved_bytes / disk.total_bytes) * 100
  const cachePercent = cache ? (cache.total_bytes / disk.total_bytes) * 100 : 0

  // Position threshold marker
  const thresholdPercent = (disk.deletion_threshold_bytes / disk.total_bytes) * 100
  const thresholdPosition = 100 - thresholdPercent

  // Determine warning level
  const usedPercent = (disk.used_bytes / disk.total_bytes) * 100
  let warningLevel: 'none' | 'warning' | 'critical' = 'none'
  let warningText = ''

  if (usedPercent >= 95) {
    warningLevel = 'critical'
    warningText = 'Critical'
  } else if (usedPercent >= 85) {
    warningLevel = 'warning'
    warningText = 'Low Space'
  }

  // Calculate total size (rough estimate from routes)
  const totalSize = routes.length > 0
    ? ((routes.length * 500) / 1024).toFixed(1) + ' GB'
    : '0 GB'

  return (
    <div className="storage-bar">
      <div className="storage-bar-content">
        <div className="storage-bar-left">
          <div className="storage-info">
            <span className="storage-text">
              {disk.formatted.used} / {disk.formatted.total}
            </span>
            {warningLevel !== 'none' && (
              <div className="storage-warning-icon" title={warningText}>
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z" />
                  <line x1="12" y1="9" x2="12" y2="13" />
                  <line x1="12" y1="17" x2="12.01" y2="17" />
                </svg>
              </div>
            )}
          </div>
          <div className="storage-gauge">
            <div
              className="storage-segment storage-preserved"
              style={{ width: `${preservedPercent}%` }}
            />
            <div
              className="storage-segment storage-routes"
              style={{ width: `${routesPercent}%` }}
            />
            {cache && (
              <div
                className="storage-segment storage-cache"
                style={{ width: `${cachePercent}%` }}
              />
            )}
            <div
              className="storage-threshold"
              style={{ left: `${thresholdPosition}%` }}
            />
          </div>
          <div className="storage-legend">
            <div className="storage-legend-item">
              <span className="storage-dot storage-dot-preserved" />
              <span className="storage-legend-text">{disk.formatted.preserved}</span>
            </div>
            <div className="storage-legend-item">
              <span className="storage-dot storage-dot-routes" />
              <span className="storage-legend-text">{disk.formatted.non_preserved}</span>
            </div>
            {cache && (
              <div className="storage-legend-item">
                <span className="storage-dot storage-dot-cache" />
                <span className="storage-legend-text">{cache.formatted.total}</span>
              </div>
            )}
          </div>
          <div className="storage-stats">
            <span className="storage-badge">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z" />
                <circle cx="12" cy="10" r="3" />
              </svg>
              <span>{routes.length} routes</span>
            </span>
            <span className="storage-badge">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z" />
              </svg>
              <span>{totalSize}</span>
            </span>
          </div>
        </div>
        <div className="storage-bar-right">
          <button
            type="button"
            className="icon-btn"
            title="Import route backup"
            onClick={() => setShowImportModal(true)}
          >
            <svg
              width="20"
              height="20"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
            >
              <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
              <polyline points="17 8 12 3 7 8" />
              <line x1="12" y1="3" x2="12" y2="15" />
            </svg>
          </button>
          <button
            type="button"
            className="icon-btn"
            title="Clear all cached data (videos, thumbnails, GPS)"
            onClick={handleClearCache}
          >
            <svg
              width="20"
              height="20"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
            >
              <polyline points="3 6 5 6 21 6" />
              <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
              <line x1="10" y1="11" x2="10" y2="17" />
              <line x1="14" y1="11" x2="14" y2="17" />
            </svg>
          </button>
          <button
            type="button"
            className="icon-btn"
            title="Refresh routes"
            onClick={handleRefresh}
          >
            <svg
              width="20"
              height="20"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
            >
              <path d="M23 4v6h-6M1 20v-6h6" />
              <path d="M3.51 9a9 9 0 0114.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0020.49 15" />
            </svg>
          </button>
        </div>
      </div>
      <ImportBackupModal
        isOpen={showImportModal}
        onClose={() => setShowImportModal(false)}
        onImportComplete={handleImportComplete}
      />
    </div>
  )
}
