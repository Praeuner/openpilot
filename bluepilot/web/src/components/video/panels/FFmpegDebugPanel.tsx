import { useState, useRef, useEffect } from 'react'
import { useWebSocketStore } from '@/stores/useWebSocketStore'
import { useFFmpegDebugStore } from '@/stores/useFFmpegDebugStore'
import './Panels.css'

interface FFmpegDebugPanelProps {
  show: boolean
}

export const FFmpegDebugPanel = ({ show }: FFmpegDebugPanelProps) => {
  const [autoScroll, setAutoScroll] = useState(true)
  const messagesRef = useRef<HTMLDivElement>(null)
  const { connected } = useWebSocketStore()
  const { messages, clearMessages } = useFFmpegDebugStore()

  useEffect(() => {
    if (autoScroll && messagesRef.current) {
      messagesRef.current.scrollTop = messagesRef.current.scrollHeight
    }
  }, [messages, autoScroll])

  const getLogTypeClass = (logType: string) => {
    switch (logType) {
      case 'start':
        return 'log-type-start'
      case 'end':
        return 'log-type-end'
      case 'error':
        return 'log-type-error'
      case 'output':
      default:
        return 'log-type-output'
    }
  }

  const formatTimestamp = (isoString: string) => {
    return new Date(isoString).toLocaleTimeString()
  }

  if (!show) return null

  return (
    <div className="bottom-panel" id="ffmpeg-debug-panel">
      <div className="panel-header">
        <div className="panel-title">
          <svg
            className="panel-icon"
            width="20"
            height="20"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2"
          >
            <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
            <polyline points="14 2 14 8 20 8" />
            <line x1="12" y1="18" x2="12" y2="12" />
            <line x1="9" y1="15" x2="15" y2="15" />
          </svg>
          <div className="panel-title-text">
            <span className="panel-title-label">FFmpeg Debug Logs</span>
            <span className="panel-title-subtitle">Real-time transcoder output</span>
          </div>
        </div>
        <div className="panel-header-actions">
          <button
            type="button"
            className="btn btn-sm panel-action-btn"
            onClick={clearMessages}
          >
            Clear
          </button>
          <label className="debug-auto-scroll-label">
            <input
              type="checkbox"
              id="debug-auto-scroll"
              checked={autoScroll}
              onChange={(e) => setAutoScroll(e.target.checked)}
            />
            Auto-scroll
          </label>
        </div>
      </div>
      <div className="panel-content" id="ffmpeg-debug-content">
        <div ref={messagesRef} className="debug-messages">
          {messages.length === 0 ? (
            <div className="debug-message-empty">
              <p>FFmpeg debug logs will appear here...</p>
              <p className="debug-message-hint">
                {connected
                  ? 'Waiting for FFmpeg processes to start...'
                  : 'WebSocket not connected. Connect to see real-time logs.'}
              </p>
            </div>
          ) : (
            messages.map((msg, index) => (
              <div key={index} className={`debug-message ${getLogTypeClass(msg.log_type)}`}>
                <span className="debug-timestamp">[{formatTimestamp(msg.timestamp)}]</span>
                <span className="debug-log-type">[{msg.log_type.toUpperCase()}]</span>
                {msg.pid && <span className="debug-pid">[PID: {msg.pid}]</span>}
                <span className="debug-route-info">[{msg.route_info}]</span>
                <span className="debug-text">{msg.message}</span>
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  )
}
