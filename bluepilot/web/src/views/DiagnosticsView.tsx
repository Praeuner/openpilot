import { useState } from 'react'
import { Header } from '@/components/layout/Header'
import { useParamsStore } from '@/stores/useParamsStore'
import { DiagnosticsParameters } from '@/components/diagnostics/DiagnosticsParameters'
import { DiagnosticsTmux } from '@/components/diagnostics/DiagnosticsTmux'
import './DiagnosticsView.css'

interface DiagnosticsViewProps {
  deviceStatus: 'online' | 'onroad' | 'offline' | 'checking'
}

export function DiagnosticsView({ deviceStatus }: DiagnosticsViewProps) {
  const [selectedTab, setSelectedTab] = useState<'parameters' | 'tmux'>('parameters')
  const paramsCount = useParamsStore((state) => Object.keys(state.params).length)

  return (
    <>
      <Header deviceStatus={deviceStatus} />
      <div className="diagnostics-view">
        <div className="diagnostics-header">
          <h1>Diagnostics</h1>
          <p>View parameters and live system output</p>
        </div>

        <div className="diagnostics-tabs">
          <button
            className={`diagnostics-tab ${selectedTab === 'parameters' ? 'active' : ''}`}
            onClick={() => setSelectedTab('parameters')}
          >
            Parameters ({paramsCount})
          </button>
          <button
            className={`diagnostics-tab ${selectedTab === 'tmux' ? 'active' : ''}`}
            onClick={() => setSelectedTab('tmux')}
          >
            Live Output
          </button>
        </div>

        {selectedTab === 'parameters' ? <DiagnosticsParameters /> : <DiagnosticsTmux />}
      </div>
    </>
  )
}
