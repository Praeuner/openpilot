/**
 * Number Control Component
 * Renders a slider/input for integer and float values
 */

import { useState, useEffect } from 'react'
import type { IntegerControl, FloatControl } from '@/types/panels'
import { useParamsStore } from '@/stores/useParamsStore'
import { usePanelStateStore } from '@/stores/usePanelStateStore'
import { getDynamicDescription } from '@/utils/conditionalEvaluator'
import { ControlCard } from '@/components/common'
import './NumberControl.css'

interface NumberControlProps {
  control: IntegerControl | FloatControl
  disabled?: boolean
  disabledReason?: string | null
}

export function NumberControl({ control, disabled, disabledReason }: NumberControlProps) {
  const { params, updateParam } = useParamsStore()
  const panelState = usePanelStateStore((state) => state.state)

  // Get current value
  const storedValue = params[control.param]?.value
  const division = control.division || 1
  const currentValue = storedValue !== undefined ? Number(storedValue) / division : control.min

  const [localValue, setLocalValue] = useState(currentValue)

  useEffect(() => {
    setLocalValue(currentValue)
  }, [currentValue])

  // Get dynamic description
  const description = getDynamicDescription(control, panelState, params)

  // Get unit based on metric setting
  const isMetric = params['IsMetric']?.value === true
  const unit = isMetric && control.unitMetric ? control.unitMetric : control.unit

  const handleSliderChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const value = parseFloat(e.target.value)
    setLocalValue(value)
  }

  const handleSliderRelease = async () => {
    // Convert back to stored format
    const storedValue = Math.round(localValue * division)
    await updateParam(control.param, String(storedValue))
  }

  const handleInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const value = parseFloat(e.target.value)
    if (!isNaN(value)) {
      setLocalValue(value)
    }
  }

  const handleInputBlur = async () => {
    // Clamp value to min/max
    const clamped = Math.max(control.min, Math.min(control.max, localValue))
    setLocalValue(clamped)

    // Convert back to stored format
    const storedValue = Math.round(clamped * division)
    await updateParam(control.param, String(storedValue))
  }

  // Calculate step based on control type
  const step = control.increment

  const metaDisplay = (
    <div className="number-control__value-display">
      <input
        type="number"
        className="number-control__input"
        value={localValue.toFixed(control.type === 'float' ? 2 : 0)}
        onChange={handleInputChange}
        onBlur={handleInputBlur}
        disabled={disabled}
        min={control.min}
        max={control.max}
        step={step}
      />
      {unit && <span className="number-control__unit">{unit}</span>}
    </div>
  )

  return (
    <ControlCard
      title={control.title}
      description={description}
      disabled={disabled}
      disabledReason={disabledReason}
      className="number-control"
      meta={metaDisplay}
    >
      <div className="number-control__slider">
        <span className="number-control__limit">
          {control.min}
          {unit}
        </span>
        <input
          type="range"
          className="number-control__range"
          min={control.min}
          max={control.max}
          step={step}
          value={localValue}
          onChange={handleSliderChange}
          onMouseUp={handleSliderRelease}
          onTouchEnd={handleSliderRelease}
          disabled={disabled}
        />
        <span className="number-control__limit">
          {control.max}
          {unit}
        </span>
      </div>
    </ControlCard>
  )
}
