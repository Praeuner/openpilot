/**
 * Segmented Control Component
 * Renders a button group for choosing from multiple options
 */

import type { SegmentedControl as SegmentedControlType } from '@/types/panels'
import { useParamsStore } from '@/stores/useParamsStore'
import { usePanelStateStore } from '@/stores/usePanelStateStore'
import { getDynamicDescription, evaluateConditions } from '@/utils/conditionalEvaluator'
import './SegmentedControl.css'

interface SegmentedControlProps {
  control: SegmentedControlType
  disabled?: boolean
  disabledReason?: string | null
}

export function SegmentedControl({ control, disabled, disabledReason }: SegmentedControlProps) {
  const { params, updateParam } = useParamsStore()
  const panelState = usePanelStateStore((state) => state.state)

  // Get current value
  const currentValue = params[control.param]?.value || ''

  // Get dynamic description
  const description = getDynamicDescription(control, panelState, params)

  // Filter options based on enableConditions
  const availableOptions = control.options.filter((option) => {
    if (!option.enableConditions) return true
    return evaluateConditions(option.enableConditions, panelState, params)
  })

  const handleSelect = async (value: string) => {
    if (!disabled) {
      await updateParam(control.param, value)
    }
  }

  // Get selected option description
  const selectedOption = availableOptions.find((opt) => opt.value === currentValue)
  const selectedDesc = selectedOption?.desc

  return (
    <div className="segmented-control">
      <div className="segmented-control-header">
        <h4 className="segmented-control-title">{control.title}</h4>
        {disabled && disabledReason && (
          <span className="segmented-control-disabled-reason">{disabledReason}</span>
        )}
      </div>

      {description && !control.showDescBottom && (
        <p
          className="segmented-control-description"
          dangerouslySetInnerHTML={{ __html: description }}
        />
      )}

      <div className="segmented-control-buttons">
        {availableOptions.map((option) => (
          <button
            key={option.value}
            className={`segmented-button ${currentValue === option.value ? 'active' : ''}`}
            onClick={() => handleSelect(option.value)}
            disabled={disabled}
          >
            {option.name.split('\n').map((line, i) => (
              <span key={i}>
                {line}
                {i < option.name.split('\n').length - 1 && <br />}
              </span>
            ))}
          </button>
        ))}
      </div>

      {control.showDescBottom && selectedDesc && (
        <p
          className="segmented-control-selected-desc"
          dangerouslySetInnerHTML={{ __html: selectedDesc }}
        />
      )}

      {control.showDescBottom && description && !selectedDesc && (
        <p
          className="segmented-control-description"
          dangerouslySetInnerHTML={{ __html: description }}
        />
      )}
    </div>
  )
}
