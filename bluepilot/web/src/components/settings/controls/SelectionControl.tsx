/**
 * Selection Control Component
 * Renders a dropdown/select for choosing from multiple options
 */

import type { SelectionControl as SelectionControlType } from '@/types/panels'
import { useParamsStore } from '@/stores/useParamsStore'
import { usePanelStateStore } from '@/stores/usePanelStateStore'
import { getDynamicDescription, evaluateConditions } from '@/utils/conditionalEvaluator'
import './SelectionControl.css'

interface SelectionControlProps {
  control: SelectionControlType
  disabled?: boolean
  disabledReason?: string | null
}

export function SelectionControl({ control, disabled, disabledReason }: SelectionControlProps) {
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

  // Get unit based on metric setting
  const isMetric = params['IsMetric']?.value === true
  const unit = isMetric && control.unitMetric ? control.unitMetric : control.unit

  const handleChange = async (e: React.ChangeEvent<HTMLSelectElement>) => {
    await updateParam(control.param, e.target.value)
  }

  return (
    <div className="selection-control">
      <div className="selection-control-content">
        <div className="selection-control-header">
          <h4 className="selection-control-title">{control.title}</h4>
          {disabled && disabledReason && (
            <span className="selection-control-disabled-reason">{disabledReason}</span>
          )}
        </div>
        {description && (
          <p
            className="selection-control-description"
            dangerouslySetInnerHTML={{ __html: description }}
          />
        )}
      </div>
      <select
        className="selection-control-select"
        value={String(currentValue)}
        onChange={handleChange}
        disabled={disabled || availableOptions.length === 0}
      >
        {availableOptions.map((option) => {
          const displayName = unit ? option.name.replace('{unit}', unit) : option.name
          return (
            <option key={option.value} value={option.value}>
              {displayName}
            </option>
          )
        })}
      </select>
    </div>
  )
}
