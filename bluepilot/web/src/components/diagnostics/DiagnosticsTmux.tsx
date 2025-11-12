import { useEffect, useState } from 'react'
import { Button, LoadingSpinner, ToggleSwitch } from '@/components/common'

interface LogResponse {
  success: boolean
  output?: string
  error?: string
  source?: string
  fallback_reason?: string
}

export function DiagnosticsTmux() {
  const [logOutput, setLogOutput] = useState('Connecting…')
  const [logSource, setLogSource] = useState<string | undefined>(undefined)
  const [fallbackReason, setFallbackReason] = useState<string | undefined>(undefined)
  const [loading, setLoading] = useState(false)
  const [autoRefresh, setAutoRefresh] = useState(false)

  const fetchLogs = async () => {
    setLoading(true)
    try {
      const response = await fetch('/api/tmux-output')
      if (response.ok) {
        const data: LogResponse = await response.json()
        if (data.success) {
          setLogOutput(data.output || 'No output available')
          setLogSource(data.source)
          setFallbackReason(data.fallback_reason)
        } else {
          setLogOutput(data.error || 'Error fetching logs')
          setLogSource(undefined)
          setFallbackReason(undefined)
        }
      } else {
        setLogOutput('Failed to fetch logs')
        setLogSource(undefined)
        setFallbackReason(undefined)
      }
    } catch (err) {
      setLogOutput('Error connecting to log service')
      setLogSource(undefined)
      setFallbackReason(undefined)
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    fetchLogs()
  }, [])

  useEffect(() => {
    if (!autoRefresh) return

    const interval = setInterval(fetchLogs, 2000)
    return () => clearInterval(interval)
  }, [autoRefresh])

  return (
    <>
      <div className="diagnostics-controls">
        <div className="control-buttons">
          <ToggleSwitch
            checked={autoRefresh}
            onChange={(checked) => setAutoRefresh(checked)}
            label="Auto-refresh"
            size="compact"
            className="diagnostics-toggle"
          />
          <Button
            variant="primary"
            size="small"
            onClick={fetchLogs}
            className="diagnostics-refresh-btn"
            icon={<span aria-hidden="true">↻</span>}
          >
            Refresh
          </Button>
        </div>
      </div>

      <div className="diagnostics-content">
        {loading ? (
          <LoadingSpinner message="Loading logs..." />
        ) : (
          <>
            <div className="log-source-info">
              <span className="log-source-label">Source:</span>
              <span className="log-source-value">{logSource === 'manager_journal' ? 'manager journal (fallback)' : logSource || 'unknown'}</span>
              {fallbackReason && <span className="log-source-reason">tmux fallback reason: {fallbackReason}</span>}
            </div>
            <pre className="tmux-output" aria-live="polite">
              {logOutput}
            </pre>
          </>
        )}
      </div>
    </>
  )
}
