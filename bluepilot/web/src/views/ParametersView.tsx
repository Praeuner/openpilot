import { useEffect, useState, useMemo, useRef } from 'react'
import { useVirtualizer } from '@tanstack/react-virtual'
import { Header } from '@/components/layout/Header'
import { useParamsStore } from '@/stores/useParamsStore'
import { LoadingSpinner, Button, Modal, ToggleSwitch } from '@/components/common'
import type { Parameter } from '@/types'
import './ParametersView.css'

interface ParametersViewProps {
  deviceStatus?: 'online' | 'onroad' | 'offline' | 'checking'
}

type SortColumn = 'key' | 'value' | 'type' | 'category' | 'last_modified'
type SortDirection = 'asc' | 'desc'

export const ParametersView = ({ deviceStatus = 'checking' }: ParametersViewProps) => {
  const { params, loading, fetchParams, updateParam, searchQuery, setSearchQuery, getFilteredParams } =
    useParamsStore()
  const [editingParam, setEditingParam] = useState<Parameter | null>(null)
  const [editValue, setEditValue] = useState<string>('')
  const [editMode, setEditMode] = useState(false)
  const [sortColumn, setSortColumn] = useState<SortColumn>('key')
  const [sortDirection, setSortDirection] = useState<SortDirection>('asc')
  const [viewValueModal, setViewValueModal] = useState<Parameter | null>(null)

  const parentRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    fetchParams()
  }, [fetchParams])

  // Format last modified time (relative)
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

  // Format last modified time (full)
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

  // Format value for display with type-specific handling
  const formatValueForDisplay = (param: Parameter): { display: string; raw: string; isBinary: boolean } => {
    const value = param.value

    // Handle null/undefined
    if (value === null || value === undefined) {
      return { display: 'null', raw: 'null', isBinary: false }
    }

    // Handle booleans
    if (typeof value === 'boolean') {
      return { display: String(value), raw: String(value), isBinary: false }
    }

    // Handle numbers
    if (typeof value === 'number') {
      return { display: String(value), raw: String(value), isBinary: false }
    }

    // Handle strings
    const strValue = String(value)

    // Check if it looks like JSON
    if (strValue.trim().startsWith('{') || strValue.trim().startsWith('[')) {
      try {
        const parsed = JSON.parse(strValue)
        const formatted = JSON.stringify(parsed, null, 2)
        return { display: formatted, raw: strValue, isBinary: false }
      } catch {
        // Not valid JSON, treat as regular string
      }
    }

    // Check if it contains replacement characters (indicates binary data)
    const isBinary = strValue.includes('\uFFFD') || /[\x00-\x08\x0B\x0C\x0E-\x1F]/.test(strValue)

    if (isBinary) {
      // For binary data, show hex representation
      const hexDisplay = Array.from(strValue)
        .map((char) => {
          const code = char.charCodeAt(0)
          return code.toString(16).padStart(2, '0').toUpperCase()
        })
        .join(' ')
      return { display: hexDisplay, raw: strValue, isBinary: true }
    }

    return { display: strValue, raw: strValue, isBinary: false }
  }

  // Sort parameters
  const sortedParams = useMemo(() => {
    const filtered = getFilteredParams()
      // Filter out parameters with null/undefined keys
      .filter(param => param.key && param.key !== 'null' && param.key !== 'undefined')

    return filtered.sort((a, b) => {
      let aVal: any = a[sortColumn]
      let bVal: any = b[sortColumn]

      // Handle undefined values
      if (aVal === undefined) aVal = ''
      if (bVal === undefined) bVal = ''

      // Convert to string for comparison
      if (sortColumn === 'value') {
        aVal = String(aVal)
        bVal = String(bVal)
      }

      // Sort
      if (aVal < bVal) return sortDirection === 'asc' ? -1 : 1
      if (aVal > bVal) return sortDirection === 'asc' ? 1 : -1
      return 0
    })
  }, [getFilteredParams, sortColumn, sortDirection])

  // Virtual scrolling
  const rowVirtualizer = useVirtualizer({
    count: sortedParams.length,
    getScrollElement: () => parentRef.current,
    estimateSize: () => 45, // Estimated row height
    overscan: 10,
  })

  const handleSort = (column: SortColumn) => {
    if (sortColumn === column) {
      setSortDirection(sortDirection === 'asc' ? 'desc' : 'asc')
    } else {
      setSortColumn(column)
      setSortDirection('asc')
    }
  }

  const handleEdit = (param: Parameter) => {
    setEditingParam(param)
    setEditValue(String(param.value))
  }

  const handleSave = async () => {
    if (!editingParam) return

    let value: string | number | boolean = editValue

    // Convert to appropriate type
    if (editingParam.type === 'number') {
      value = Number(editValue)
    } else if (editingParam.type === 'boolean') {
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

  const renderSortIcon = (column: SortColumn) => {
    if (sortColumn !== column) {
      return <span className="sort-icon">⇅</span>
    }
    return <span className="sort-icon">{sortDirection === 'asc' ? '▲' : '▼'}</span>
  }

  if (loading && Object.keys(params).length === 0) {
    return (
      <>
        <Header deviceStatus={deviceStatus} />
        <div className="loading">
          <LoadingSpinner size="large" message="Loading parameters..." />
        </div>
      </>
    )
  }

  return (
    <>
      <Header deviceStatus={deviceStatus} />
      <div className="params-manager">
        <div className="params-header">
          <h2>Parameters</h2>
          <div className="params-controls">
            <input
              type="text"
              id="params-search"
              placeholder="Search parameters..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
            />
            <ToggleSwitch
              id="params-edit-toggle"
              checked={editMode}
              onChange={(checked) => setEditMode(checked)}
              label="Edit Mode"
              size="compact"
              className="params-edit-toggle"
              title="Enable parameter editing (use with caution)"
            />
          </div>
        </div>
        <div className="params-content" ref={parentRef}>
          {sortedParams.length === 0 ? (
            <div className="empty-state">
              <p>No parameters found</p>
            </div>
          ) : (
            <div className="params-table">
              <table>
                <thead>
                  <tr>
                    <th
                      className={`sortable-header ${sortColumn === 'key' ? 'active-sort' : ''}`}
                      onClick={() => handleSort('key')}
                    >
                      Parameter {renderSortIcon('key')}
                    </th>
                    <th
                      className={`sortable-header ${sortColumn === 'value' ? 'active-sort' : ''}`}
                      onClick={() => handleSort('value')}
                    >
                      Value {renderSortIcon('value')}
                    </th>
                    <th
                      className={`sortable-header ${sortColumn === 'type' ? 'active-sort' : ''}`}
                      onClick={() => handleSort('type')}
                    >
                      Type {renderSortIcon('type')}
                    </th>
                    <th
                      className={`sortable-header ${sortColumn === 'category' ? 'active-sort' : ''}`}
                      onClick={() => handleSort('category')}
                    >
                      Category {renderSortIcon('category')}
                    </th>
                    <th
                      className={`sortable-header ${sortColumn === 'last_modified' ? 'active-sort' : ''}`}
                      onClick={() => handleSort('last_modified')}
                    >
                      Last Modified {renderSortIcon('last_modified')}
                    </th>
                    <th>Actions</th>
                  </tr>
                </thead>
                <tbody
                  style={{
                    height: `${rowVirtualizer.getTotalSize()}px`,
                    position: 'relative',
                  }}
                >
                  {rowVirtualizer.getVirtualItems().map((virtualRow) => {
                    const param = sortedParams[virtualRow.index]
                    const valueStr = String(param.value ?? 'null')
                    const valueDisplay = valueStr.length > 50 ? valueStr.substring(0, 50) + '...' : valueStr

                    return (
                      <tr
                        key={param.key}
                        style={{
                          position: 'absolute',
                          top: 0,
                          left: 0,
                          width: '100%',
                          height: `${virtualRow.size}px`,
                          transform: `translateY(${virtualRow.start}px)`,
                        }}
                      >
                        <td>
                          <span className="param-key" title={param.key}>
                            {param.key}
                          </span>
                        </td>
                        <td>
                          <span
                            className="param-value"
                            title="Click to view full value"
                            onClick={() => handleViewValue(param)}
                          >
                            {valueDisplay}
                          </span>
                        </td>
                        <td>
                          <span className={`param-badge ${param.type}`}>{param.type}</span>
                          {param.readonly && <span className="param-badge readonly">readonly</span>}
                          {param.critical && <span className="param-badge critical">critical</span>}
                        </td>
                        <td>
                          {param.category && <span className="param-badge">{param.category}</span>}
                        </td>
                        <td>
                          <span className="param-last-modified">
                            {formatLastModified(param.last_modified)}
                          </span>
                        </td>
                        <td>
                          {param.readonly ? (
                            <Button
                              size="small"
                              variant="ghost"
                              className="param-edit-btn"
                              disabled
                            >
                              Read-Only
                            </Button>
                          ) : (
                            <Button
                              size="small"
                              variant="primary"
                              className="param-edit-btn"
                              onClick={() => handleEdit(param)}
                              disabled={!editMode}
                            >
                              Edit
                            </Button>
                          )}
                        </td>
                      </tr>
                    )
                  })}
                </tbody>
              </table>
            </div>
          )}
        </div>
      </div>

      {/* Edit Parameter Modal */}
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
          {editingParam?.type === 'boolean' ? (
            <select
              value={editValue}
              onChange={(e) => setEditValue(e.target.value)}
              className="edit-input"
            >
              <option value="true">true</option>
              <option value="false">false</option>
            </select>
          ) : (
            <input
              type={editingParam?.type === 'number' ? 'number' : 'text'}
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

      {/* View Value Modal */}
      <Modal
        isOpen={viewValueModal !== null}
        onClose={() => setViewValueModal(null)}
        title="Parameter Details"
        size="large"
      >
        {viewValueModal && (
          <div className="view-param-modal">
            {/* Parameter Header */}
            <div className="param-detail-header">
              <div className="param-detail-key">
                <span className="param-key-label">Parameter Key</span>
                <span className="param-key-value">{viewValueModal.key}</span>
              </div>
              <div className="param-detail-badges">
                <span className={`param-badge ${viewValueModal.type}`}>{viewValueModal.type}</span>
                {viewValueModal.readonly && <span className="param-badge readonly">readonly</span>}
                {viewValueModal.critical && <span className="param-badge critical">critical</span>}
                {viewValueModal.category && (
                  <span className="param-badge category">{viewValueModal.category}</span>
                )}
              </div>
            </div>

            {/* Parameter Metadata */}
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
              {viewValueModal.critical && (
                <div className="metadata-row critical-warning">
                  <span className="metadata-label">⚠️ Critical Parameter:</span>
                  <span className="metadata-value">Modifying this parameter requires extra caution</span>
                </div>
              )}
            </div>

            {/* Parameter Value */}
            <div className="param-value-section">
              <div className="value-section-header">
                <span className="value-section-title">Value</span>
                {(() => {
                  const formatted = formatValueForDisplay(viewValueModal)
                  return formatted.isBinary && <span className="value-format-badge">Binary (Hex)</span>
                })()}
              </div>
              <div className="param-value-display">
                <pre className={formatValueForDisplay(viewValueModal).isBinary ? 'binary-value' : ''}>
                  {formatValueForDisplay(viewValueModal).display}
                </pre>
              </div>
            </div>

            {/* Action Buttons */}
            <div className="modal-actions">
              <Button
                variant="secondary"
                onClick={() => {
                  const formatted = formatValueForDisplay(viewValueModal)
                  copyToClipboard(formatted.raw)
                }}
              >
                Copy Value
              </Button>
              <Button
                variant="secondary"
                onClick={() => copyToClipboard(viewValueModal.key)}
              >
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
