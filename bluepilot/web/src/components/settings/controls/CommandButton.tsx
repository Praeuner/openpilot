/**
 * Command Button Control Component
 * Renders a button that executes commands or actions
 */

import { useState } from 'react'
import type { CommandButtonControl } from '@/types/panels'
import { usePanelStateStore } from '@/stores/usePanelStateStore'
import { panelAPI } from '@/services/panelAPI'
import { getDynamicDescription } from '@/utils/conditionalEvaluator'
import { Button, ControlCard, Modal } from '@/components/common'
import './CommandButton.css'

interface CommandButtonProps {
  control: CommandButtonControl
  disabled?: boolean
  disabledReason?: string | null
}

export function CommandButton({ control, disabled, disabledReason }: CommandButtonProps) {
  const panelState = usePanelStateStore((state) => state.state)
  const [showConfirm, setShowConfirm] = useState(false)
  const [executing, setExecuting] = useState(false)
  const [result, setResult] = useState<{ success: boolean; message?: string } | null>(null)

  // Get dynamic description - pass empty params since buttons don't depend on param values
  const description = getDynamicDescription(control, panelState, {})

  const handleClick = () => {
    if (control.confirm) {
      setShowConfirm(true)
    } else {
      executeCommand()
    }
  }

  const executeCommand = async () => {
    setShowConfirm(false)
    setExecuting(true)
    setResult(null)

    try {
      if (control.action) {
        // Execute panel command
        const response = await panelAPI.executePanelCommand({
          action: control.action,
          param: control.param,
          value: control.value,
          params: control.params,
        })

        setResult({
          success: response.success,
          message: response.error || (response.success ? 'Command executed successfully' : 'Command failed'),
        })
      } else {
        // Unsupported - requires device UI
        setResult({
          success: false,
          message: 'This command requires the device UI. Please use the settings panel on your Comma device.',
        })
      }
    } catch (error) {
      setResult({
        success: false,
        message: error instanceof Error ? error.message : 'Command failed',
      })
    } finally {
      setExecuting(false)
    }
  }

  const getButtonStyle = () => {
    if (control.button_style) {
      return {
        backgroundColor: control.button_style.background_color,
        color: control.button_style.text_color,
      }
    }
    return {}
  }

  return (
    <>
      <ControlCard
        title={control.title}
        description={description}
        disabled={disabled}
        disabledReason={disabledReason}
        className="command-button-control"
        footer={
          <Button
            className="command-button-btn"
            onClick={handleClick}
            disabled={disabled}
            loading={executing}
            style={getButtonStyle()}
          >
            {control.button_text}
          </Button>
        }
      >
        {result && (
          <div className={`command-button-result ${result.success ? 'success' : 'error'}`}>
            {result.message}
          </div>
        )}
      </ControlCard>

      {showConfirm && (
        <Modal
          isOpen={showConfirm}
          title="Confirm Action"
          onClose={() => setShowConfirm(false)}
          actions={[
            {
              label: control.cancel_button_text || control.confirm_no_text || 'Cancel',
              onClick: () => setShowConfirm(false),
              variant: 'secondary',
            },
            {
              label: control.confirm_button_text || control.confirm_yes_text || 'Confirm',
              onClick: executeCommand,
              variant: 'primary',
            },
          ]}
        >
          <p>{control.confirm_text || `Are you sure you want to ${control.title}?`}</p>
        </Modal>
      )}
    </>
  )
}
