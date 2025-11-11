/**
 * Toggle Control Component
 * Renders a boolean toggle switch for panel settings
 */

import { useState } from 'react'
import type { ToggleControl as ToggleControlType } from '@/types/panels'
import { useParamsStore } from '@/stores/useParamsStore'
import { usePanelStateStore } from '@/stores/usePanelStateStore'
import { getDynamicDescription, getDynamicTitle, getDynamicStyle } from '@/utils/conditionalEvaluator'
import { Modal } from '@/components/common'
import './ToggleControl.css'

interface ToggleControlProps {
  control: ToggleControlType
  disabled?: boolean
  disabledReason?: string | null
}

export function ToggleControl({ control, disabled, disabledReason }: ToggleControlProps) {
  const { params, updateParam } = useParamsStore()
  const panelState = usePanelStateStore((state) => state.state)
  const [showConfirm, setShowConfirm] = useState(false)
  const [pendingValue, setPendingValue] = useState<boolean | null>(null)

  // Get current value
  const currentValue = params[control.param]?.value
  const isEnabled = currentValue === true || currentValue === '1' || currentValue === 1

  // Get dynamic content
  const title = getDynamicTitle(control, panelState, params)
  const description = getDynamicDescription(control, panelState, params)
  const style = getDynamicStyle(control, panelState, params)

  const handleToggle = async () => {
    const newValue = !isEnabled

    // Show confirmation if required
    if (control.confirm || control.confirmation) {
      setPendingValue(newValue)
      setShowConfirm(true)
      return
    }

    // Otherwise, update immediately
    await updateParam(control.param, newValue ? '1' : '0')
  }

  const handleConfirm = async () => {
    if (pendingValue !== null) {
      await updateParam(control.param, pendingValue ? '1' : '0')
      setPendingValue(null)
    }
    setShowConfirm(false)
  }

  const handleCancel = () => {
    setPendingValue(null)
    setShowConfirm(false)
  }

  return (
    <>
      <div className="toggle-control" style={style}>
        <div className="toggle-control-content">
          <div className="toggle-control-header">
            <h4 className="toggle-control-title">{title}</h4>
            {disabled && disabledReason && (
              <span className="toggle-control-disabled-reason">{disabledReason}</span>
            )}
          </div>
          {description && (
            <p
              className="toggle-control-description"
              dangerouslySetInnerHTML={{ __html: description }}
            />
          )}
        </div>
        <label className="toggle-switch">
          <input
            type="checkbox"
            checked={isEnabled}
            onChange={handleToggle}
            disabled={disabled}
          />
          <span className="toggle-slider"></span>
        </label>
      </div>

      {showConfirm && (
        <Modal
          isOpen={showConfirm}
          title="Confirm Change"
          onClose={handleCancel}
          actions={[
            { label: control.confirm_no_text || 'Cancel', onClick: handleCancel, variant: 'secondary' },
            { label: control.confirm_yes_text || 'Confirm', onClick: handleConfirm, variant: 'primary' },
          ]}
        >
          <p>{control.confirm_text || `Are you sure you want to ${pendingValue ? 'enable' : 'disable'} ${title}?`}</p>
        </Modal>
      )}
    </>
  )
}
