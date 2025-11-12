import { useEffect, useMemo, useState } from 'react'
import { Button, LoadingSpinner, Modal, ToggleSwitch } from '@/components/common'
import { useParamsStore } from '@/stores/useParamsStore'
import type { Parameter } from '@/types'
import { formatParamValueForDisplay } from '@/utils/params'

type SortColumn = 'key' | 'value' | 'type' | 'category' | 'last_modified'
type SortDirection = 'asc' | 'desc'

const SORT_OPTIONS: { label: string; value: SortColumn }[] = [
  { label: 'Parameter', value: 'key' },
  { label: 'Value', value: 'value' },
  { label: 'Type', value: 'type' },
  { label: 'Category', value: 'category' },
  { label: 'Last Modified', value: 'last_modified' },
]

const NUMERIC_TYPES = new Set(['number', 'int', 'float'])
const BOOLEAN_TYPES = new Set(['boolean', 'bool'])

const getSortValue = (param: Parameter, column: SortColumn): string | number | boolean | null | undefined => {
  switch (column) {
    case 'key':
      return param.key
    case 'value':
      return param.value
    case 'type':
      return param.type
    case 'category':
      return param.category ?? ''
    case 'last_modified':
      return param.last_modified ?? 0
    default:
      return ''
  }
}

export function DiagnosticsParameters() {
  const {
    params,
    loading,
    error,
    fetchParams,
    updateParam,
    searchQuery,
    setSearchQuery,
    getFilteredParams,
  } = useParamsStore()
  const [editingParam, setEditingParam] = useState<Parameter | null>(null)
  const [editValue, setEditValue] = useState('')
  const [editMode, setEditMode] = useState(false)
  const [sortColumn, setSortColumn] = useState<SortColumn>('key')
  const [sortDirection, setSortDirection] = useState<SortDirection>('asc')
  const [viewValueModal, setViewValueModal] = useState<Parameter | null>(null)

  useEffect(() => {
    fetchParams()
  }, [fetchParams])

  const formatLastModified = (timestamp?: number): string => {
    if (!timestamp) return 'Never'

    const date = new Date(timestamp * 1000)
    const now = new Date()
    const diffMs = now.getTime() - date.getTime()
    const diffMins = Math.floor(diffMs / 60000)
    const diffHours = Math.floor(diffMs / 3600000)
    const diffDays = Math.floor(diffMs / 86400000)

    if (diffMins < 1) return 'Just now'
    if (diffMins < 60) return `${diffMins}m ago`
    if (diffHours < 24) return `${diffHours}h ago`
    if (diffDays < 7) return `${diffDays}d ago`
    return date.toLocaleDateString()
  }

  const formatLastModifiedFull = (timestamp?: number): string => {
    if (!timestamp) return 'Never modified'

    const date = new Date(timestamp * 1000)
    return date.toLocaleString(undefined, {
      year: 'numeric',
      month: 'long',
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      hour12: true,
    })
  }

  const sortedParams = useMemo(() => {
    const filtered = getFilteredParams()
      .filter((param) => param.key && param.key !== 'null' && param.key !== 'undefined')

    return filtered.sort((a, b) => {
      let aVal = getSortValue(a, sortColumn)
      let bVal = getSortValue(b, sortColumn)

      if (aVal === undefined || aVal === null) aVal = ''
      if (bVal === undefined || bVal === null) bVal = ''

      if (typeof aVal === 'string') aVal = aVal.toLowerCase()
      if (typeof bVal === 'string') bVal = bVal.toLowerCase()

      if (aVal < bVal) return sortDirection === 'asc' ? -1 : 1
      if (aVal > bVal) return sortDirection === 'asc' ? 1 : -1
      return 0
    })
  }, [getFilteredParams, sortColumn, sortDirection])

  const toggleSortDirection = () => {
    setSortDirection((prev) => (prev === 'asc' ? 'desc' : 'asc'))
  }

  const handleEdit = (param: Parameter) => {
    setEditingParam(param)
    setEditValue(String(param.value))
  }

  const handleSave = async () => {
    if (!editingParam) return

    let value: string | number | boolean = editValue
    const type = editingParam.type?.toLowerCase()

    if (type && NUMERIC_TYPES.has(type)) {
      const parsed = Number(editValue)
      value = Number.isNaN(parsed) ? 0 : parsed
    } else if (type && BOOLEAN_TYPES.has(type)) {
      value = editValue === 'true'
    }

    await updateParam(editingParam.key, value)
    setEditingParam(null)
  }

  const handleViewValue = (param: Parameter) => {
    setViewValueModal(param)
  }

  const copyToClipboard = (text: string) => {
    navigator.clipboard.writeText(text)
  }

  const handleManualRefresh = () => {
    fetchParams()
  }

  const formattedModalValue = viewValueModal ? formatParamValueForDisplay(viewValueModal) : null

  return (
    <>
      <div className="diagnostics-controls">
        <div className="diagnostics-params-controls">
          <div className="search-container">
            <input
              type="text"
              className="search-input"
              placeholder="Search parameters..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
            />
            {searchQuery && (
              <button className="search-clear" onClick={() => setSearchQuery('')} aria-label="Clear search">
                ✕
              </button>
            )}
          </div>
          <ToggleSwitch
            checked={editMode}
            onChange={(checked) => setEditMode(checked)}
            label="Edit Mode"
            size="compact"
            className="diagnostics-toggle"
            title="Enable parameter editing (use with caution)"
          />
          <div className="params-sort-controls">
            <label htmlFor="diagnostics-sort">Sort</label>
            <select
              id="diagnostics-sort"
              value={sortColumn}
              onChange={(e) => setSortColumn(e.target.value as SortColumn)}
            >
              {SORT_OPTIONS.map((option) => (
                <option key={option.value} value={option.value}>
                  {option.label}
                </option>
              ))}
            </select>
            <button
              type="button"
              className="sort-direction-btn"
              onClick={toggleSortDirection}
              title={`Switch to ${sortDirection === 'asc' ? 'descending' : 'ascending'} order`}
            >
              {sortDirection === 'asc' ? '↑ Asc' : '↓ Desc'}
            </button>
          </div>
        </div>
        <div className="control-buttons">
          <Button
            variant="primary"
            size="small"
            onClick={handleManualRefresh}
            className="diagnostics-refresh-btn"
            icon={<span aria-hidden="true">↻</span>}
          >
            Refresh
          </Button>
        </div>
      </div>

      <div className="diagnostics-content">
        {loading && Object.keys(params).length === 0 ? (
          <LoadingSpinner message="Loading parameters..." />
        ) : error ? (
          <div className="diagnostics-error">
            <h2>Error Loading Parameters</h2>
            <p>{error}</p>
          </div>
        ) : sortedParams.length === 0 ? (
          <div className="no-results">
            <p>No parameters found</p>
          </div>
        ) : (
          <div className="params-list">
            {sortedParams.map((param) => {
              const formattedValue = formatParamValueForDisplay(param)
              const condensedValue = (() => {
                const singleLine = formattedValue.display.includes('\n')
                  ? formattedValue.display
                      .split('\n')
                      .map((line) => line.trim())
                      .filter(Boolean)
                      .join(' ')
                  : formattedValue.display
                return singleLine.length > 160 ? `${singleLine.substring(0, 157)}…` : singleLine
              })()

              return (
                <div className="param-row" key={param.key}>
                  <div className="param-row__header">
                    <div className="param-row__title-block">
                      <div className="param-row__title-line">
                        <span className="param-key" title={param.key}>
                          {param.key}
                        </span>
                        <div className="param-row__status-chips">
                          {param.readonly && <span className="param-badge readonly">readonly</span>}
                          {param.critical && <span className="param-badge critical">critical</span>}
                        </div>
                      </div>
                      <div className="param-last-modified">Last modified: {formatLastModified(param.last_modified)}</div>
                    </div>
                    <div className="param-row__actions">
                      {param.readonly ? (
                        <Button size="small" variant="ghost" className="param-edit-btn" disabled>
                          Read-Only
                        </Button>
                      ) : (
                        <Button
                          size="small"
                          variant="primary"
                          className="param-edit-btn"
                          onClick={() => handleEdit(param)}
                          disabled={!editMode || param.type === 'bytes'}
                          title={param.type === 'bytes' ? 'Binary parameters are view-only' : undefined}
                        >
                          Edit
                        </Button>
                      )}
                    </div>
                  </div>
                  <div className="param-row__value" title="Click to view full value" onClick={() => handleViewValue(param)}>
                    <span className="value-label">Value</span>
                    <span className="value-content">{condensedValue}</span>
                    {formattedValue.formatLabel && (
                      <span className="value-format-badge">{formattedValue.formatLabel}</span>
                    )}
                    {param.type === 'bytes' && param.byte_length !== undefined && (
                      <span className="value-format-badge">{param.byte_length} bytes</span>
                    )}
                  </div>
                  <div className="param-row__meta">
                    <div className="meta-block">
                      <span className="meta-label">Type</span>
                      <span className={`param-badge ${param.type}`}>{param.type}</span>
                    </div>
                    <div className="meta-block">
                      <span className="meta-label">Category</span>
                      {param.category ? <span className="param-badge category">{param.category}</span> : <span className="meta-placeholder">—</span>}
                    </div>
                  </div>
                </div>
              )
            })}
          </div>
        )}
      </div>

      <Modal
        isOpen={editingParam !== null}
        onClose={() => setEditingParam(null)}
        title={`Edit ${editingParam?.key}`}
        size="small"
      >
        <div className="edit-param-modal">
          <div className="param-info">
            <p><strong>Type:</strong> {editingParam?.type}</p>
            {editingParam?.description && <p><strong>Description:</strong> {editingParam.description}</p>}
          </div>
          {editingParam?.type && BOOLEAN_TYPES.has(editingParam.type.toLowerCase()) ? (
            <select value={editValue} onChange={(e) => setEditValue(e.target.value)} className="edit-input">
              <option value="true">true</option>
              <option value="false">false</option>
            </select>
          ) : (
            <input
              type={editingParam?.type && NUMERIC_TYPES.has(editingParam.type.toLowerCase()) ? 'number' : 'text'}
              value={editValue}
              onChange={(e) => setEditValue(e.target.value)}
              className="edit-input"
            />
          )}
          <div className="modal-actions">
            <Button variant="secondary" onClick={() => setEditingParam(null)}>
              Cancel
            </Button>
            <Button variant="primary" onClick={handleSave}>
              Save
            </Button>
          </div>
        </div>
      </Modal>

      <Modal
        isOpen={viewValueModal !== null}
        onClose={() => setViewValueModal(null)}
        title="Parameter Details"
        size="large"
      >
        {viewValueModal && (
          <div className="view-param-modal">
            <div className="param-detail-header">
              <div className="param-detail-key">
                <span className="param-key-label">Parameter Key</span>
                <span className="param-key-value">{viewValueModal.key}</span>
              </div>
              <div className="param-detail-badges">
                <span className={`param-badge ${viewValueModal.type}`}>{viewValueModal.type}</span>
                {viewValueModal.readonly && <span className="param-badge readonly">readonly</span>}
                {viewValueModal.critical && <span className="param-badge critical">critical</span>}
                {viewValueModal.category && <span className="param-badge category">{viewValueModal.category}</span>}
              </div>
            </div>

            <div className="param-detail-metadata">
              <div className="metadata-row">
                <span className="metadata-label">Type:</span>
                <span className="metadata-value">{viewValueModal.type}</span>
              </div>
              {viewValueModal.category && (
                <div className="metadata-row">
                  <span className="metadata-label">Category:</span>
                  <span className="metadata-value">{viewValueModal.category}</span>
                </div>
              )}
              <div className="metadata-row">
                <span className="metadata-label">Last Modified:</span>
                <span className="metadata-value">{formatLastModifiedFull(viewValueModal.last_modified)}</span>
              </div>
              {viewValueModal.description && (
                <div className="metadata-row">
                  <span className="metadata-label">Description:</span>
                  <span className="metadata-value">{viewValueModal.description}</span>
                </div>
              )}
              <div className="metadata-row">
                <span className="metadata-label">Read-Only:</span>
                <span className="metadata-value">{viewValueModal.readonly ? 'Yes' : 'No'}</span>
              </div>
              {viewValueModal.type === 'bytes' && (
                <>
                  <div className="metadata-row">
                    <span className="metadata-label">Byte Length:</span>
                    <span className="metadata-value">
                      {viewValueModal.byte_length !== undefined ? viewValueModal.byte_length : 'Unknown'}
                    </span>
                  </div>
                  {viewValueModal.decoded?.summary?.carName && (
                    <div className="metadata-row">
                      <span className="metadata-label">Vehicle:</span>
                      <span className="metadata-value">{viewValueModal.decoded.summary.carName}</span>
                    </div>
                  )}
                  {viewValueModal.decoded?.summary?.carFingerprint && (
                    <div className="metadata-row">
                      <span className="metadata-label">Fingerprint:</span>
                      <span className="metadata-value">{viewValueModal.decoded.summary.carFingerprint}</span>
                    </div>
                  )}
                  {viewValueModal.decoded?.summary?.carVin && (
                    <div className="metadata-row">
                      <span className="metadata-label">VIN:</span>
                      <span className="metadata-value">{viewValueModal.decoded.summary.carVin}</span>
                    </div>
                  )}
                </>
              )}
              {viewValueModal.critical && (
                <div className="metadata-row critical-warning">
                  <span className="metadata-label">⚠️ Critical Parameter:</span>
                  <span className="metadata-value">Modifying this parameter requires extra caution</span>
                </div>
              )}
            </div>

            <div className="param-value-section">
              <div className="value-section-header">
                <span className="value-section-title">Value</span>
                {formattedModalValue?.formatLabel && (
                  <span className="value-format-badge">{formattedModalValue.formatLabel}</span>
                )}
              </div>
              <div className="param-value-display">
                <pre className={formattedModalValue?.isBinary ? 'binary-value' : ''}>
                  {formattedModalValue?.display}
                </pre>
              </div>
            </div>

            {viewValueModal.type === 'bytes' && (
              <>
                {formattedModalValue?.decodedString && (
                  <div className="param-value-section">
                    <div className="value-section-header">
                      <span className="value-section-title">
                        Decoded ({viewValueModal.decoded?.format ?? 'binary'})
                      </span>
                    </div>
                    <div className="param-value-display">
                      <pre>{formattedModalValue.decodedString}</pre>
                    </div>
                  </div>
                )}
                <div className="param-value-section">
                  <div className="value-section-header">
                    <span className="value-section-title">Raw ({viewValueModal.raw_format ?? 'base64'})</span>
                    {viewValueModal.byte_length !== undefined && (
                      <span className="value-format-badge">{viewValueModal.byte_length} bytes</span>
                    )}
                  </div>
                  <div className="param-value-display">
                    <pre className="binary-value">{viewValueModal.raw_value ?? '—'}</pre>
                  </div>
                </div>
                {formattedModalValue?.hexPreview && (
                  <div className="param-value-section">
                    <div className="value-section-header">
                      <span className="value-section-title">Hex Preview</span>
                    </div>
                    <div className="param-value-display">
                      <pre className="binary-value">{formattedModalValue.hexPreview}</pre>
                    </div>
                  </div>
                )}
              </>
            )}

            <div className="modal-actions">
              <Button
                variant="secondary"
                onClick={() => {
                  if (formattedModalValue) {
                    copyToClipboard(formattedModalValue.raw)
                  }
                }}
              >
                Copy Value
              </Button>
              {viewValueModal.type === 'bytes' && viewValueModal.raw_value && (
                <Button variant="secondary" onClick={() => copyToClipboard(viewValueModal.raw_value!)}>
                  Copy Raw (base64)
                </Button>
              )}
              <Button variant="secondary" onClick={() => copyToClipboard(viewValueModal.key)}>
                Copy Key
              </Button>
              <Button variant="primary" onClick={() => setViewValueModal(null)}>
                Close
              </Button>
            </div>
          </div>
        )}
      </Modal>
    </>
  )
}
