import { useEffect, useState } from 'react'
import { systemAPI } from '@/services/api'
import { Modal } from '@/components/common'
import './MetricsModal.css'

interface MetricsModalProps {
  isOpen: boolean
  onClose: () => void
}

interface SystemMetrics {
  cpu: {
    load_avg: number
    load_1min: number
    load_5min: number
    cores_online: number
  }
  memory: {
    percent: number
    used_gb: number
    available_gb: number
    total_gb: number
  }
  disk: {
    percent: number
    used_gb: number
    free_gb: number
    total_gb: number
  }
  temperature: {
    value: number
    status: 'normal' | 'warning' | 'critical'
  }
}

export const MetricsModal = ({ isOpen, onClose }: MetricsModalProps) => {
  const [metrics, setMetrics] = useState<SystemMetrics | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    if (isOpen) {
      loadMetrics()
      // Refresh metrics every 5 seconds while modal is open
      const interval = setInterval(loadMetrics, 5000)
      return () => clearInterval(interval)
    }
  }, [isOpen])

  const loadMetrics = async () => {
    try {
      const data = await systemAPI.getMetrics()
      setMetrics(data as any)
      setLoading(false)
    } catch (error) {
      console.error('Failed to load metrics:', error)
      setLoading(false)
    }
  }

  if (!isOpen) return null

  return (
    <Modal isOpen={isOpen} onClose={onClose} title="System Health Metrics">
      {loading && !metrics ? (
        <div className="metrics-loading">Loading metrics...</div>
      ) : (
        <div className="metrics-grid">
          {/* CPU Card */}
          <div className="metric-card">
            <h3>CPU</h3>
            <div className="metric-value">
              <span className="value-large">{metrics?.cpu.load_avg.toFixed(2) || '--'}</span>
              <span className="unit">load</span>
            </div>
            <div className="metric-details">
              <div className="metric-row">
                <span>1min:</span>
                <span>{metrics?.cpu.load_1min.toFixed(2) || '--'}</span>
              </div>
              <div className="metric-row">
                <span>5min:</span>
                <span>{metrics?.cpu.load_5min.toFixed(2) || '--'}</span>
              </div>
              <div className="metric-row">
                <span>Cores online:</span>
                <span>{metrics?.cpu.cores_online || '--'}</span>
              </div>
            </div>
          </div>

          {/* Memory Card */}
          <div className="metric-card">
            <h3>Memory</h3>
            <div className="metric-value">
              <span className="value-large">{metrics?.memory.percent.toFixed(1) || '--'}</span>
              <span className="unit">%</span>
            </div>
            <div className="metric-bar">
              <div
                className="metric-bar-fill"
                style={{ width: `${metrics?.memory.percent || 0}%` }}
              />
            </div>
            <div className="metric-details">
              <div className="metric-row">
                <span>Used:</span>
                <span>{metrics?.memory.used_gb.toFixed(1) || '--'} GB</span>
              </div>
              <div className="metric-row">
                <span>Available:</span>
                <span>{metrics?.memory.available_gb.toFixed(1) || '--'} GB</span>
              </div>
            </div>
          </div>

          {/* Disk Card */}
          <div className="metric-card">
            <h3>Disk (/data)</h3>
            <div className="metric-value">
              <span className="value-large">{metrics?.disk.percent.toFixed(1) || '--'}</span>
              <span className="unit">%</span>
            </div>
            <div className="metric-bar">
              <div
                className="metric-bar-fill"
                style={{ width: `${metrics?.disk.percent || 0}%` }}
              />
            </div>
            <div className="metric-details">
              <div className="metric-row">
                <span>Used:</span>
                <span>{metrics?.disk.used_gb.toFixed(1) || '--'} GB</span>
              </div>
              <div className="metric-row">
                <span>Free:</span>
                <span>{metrics?.disk.free_gb.toFixed(1) || '--'} GB</span>
              </div>
            </div>
          </div>

          {/* Temperature Card */}
          <div className="metric-card">
            <h3>Temperature</h3>
            <div className="metric-value">
              <span className={`value-large temp-${metrics?.temperature.status || 'normal'}`}>
                {metrics?.temperature.value || '--'}
              </span>
              <span className="unit">°C</span>
            </div>
            <div className="metric-details">
              <div className="metric-row">
                <span>Status:</span>
                <span className={`status-${metrics?.temperature.status || 'normal'}`}>
                  {metrics?.temperature.status || '--'}
                </span>
              </div>
            </div>
          </div>
        </div>
      )}
    </Modal>
  )
}
