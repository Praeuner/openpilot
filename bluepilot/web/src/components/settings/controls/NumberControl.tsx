/**
 * Number Control Component
 * Renders touch-friendly +/- buttons with hold-to-repeat for integer and float values
 */

import { useState, useEffect, useCallback, useRef } from 'react'
import type { IntegerControl, FloatControl } from '@/types/panels'
import { useParamsStore } from '@/stores/useParamsStore'
import { usePanelStateStore } from '@/stores/usePanelStateStore'
import { getDynamicDescription } from '@/utils/conditionalEvaluator'
import { ControlCard, Icon } from '@/components/common'
import './NumberControl.css'

interface NumberControlProps {
  control: IntegerControl | FloatControl
  disabled?: boolean
  disabledReason?: string | null
}

// Hold-to-repeat timing constants
const INITIAL_DELAY = 400 // ms before repeat starts
const REPEAT_INTERVAL = 100 // ms between repeats
const FAST_REPEAT_THRESHOLD = 1500 // ms before faster repeat
const FAST_REPEAT_INTERVAL = 50 // ms between fast repeats

export function NumberControl({ control, disabled, disabledReason }: NumberControlProps) {
  const { params, updateParam } = useParamsStore()
  const panelState = usePanelStateStore((state) => state.state)

  const isFloat = control.type === 'float'

  // Get current value
  // For floats: store the actual decimal value (0.70), no division scaling needed
  // For integers: use division to convert stored integer to display decimal (e.g., stored 70 / 100 = display 0.70)
  const storedValue = params[control.param]?.value
  const division = isFloat ? 1 : (control.division || 1)
  const currentValue = storedValue !== undefined ? Number(storedValue) / division : control.min

  const [localValue, setLocalValue] = useState(currentValue)

  // Refs for hold-to-repeat
  const holdStartTimeRef = useRef<number>(0)
  const repeatIntervalRef = useRef<ReturnType<typeof setInterval> | null>(null)
  const repeatTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  useEffect(() => {
    setLocalValue(currentValue)
  }, [currentValue])

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (repeatIntervalRef.current) clearInterval(repeatIntervalRef.current)
      if (repeatTimeoutRef.current) clearTimeout(repeatTimeoutRef.current)
    }
  }, [])

  // Get dynamic description
  const description = getDynamicDescription(control, panelState, params)

  // Get unit based on metric setting
  const isMetric = params['IsMetric']?.value === true
  const unit = isMetric && control.unitMetric ? control.unitMetric : control.unit

  // Decimal places for display
  const decimalPlaces = isFloat ? 2 : 0

  // Save value to backend
  const saveValue = useCallback(async (value: number) => {
    const clamped = Math.max(control.min, Math.min(control.max, value))
    setLocalValue(clamped)

    // For floats: store the display value directly (0.70)
    // For integers: multiply by division and round (display 0.70 * 100 = stored 70)
    const valueToStore = isFloat ? clamped : Math.round(clamped * division)
    await updateParam(control.param, String(valueToStore))
  }, [control.min, control.max, control.param, division, isFloat, updateParam])

  const handleInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const value = parseFloat(e.target.value)
    if (!isNaN(value)) {
      setLocalValue(value)
    }
  }

  const handleInputBlur = async () => {
    await saveValue(localValue)
  }

  // Increment/decrement with bounds checking
  const increment = useCallback(() => {
    setLocalValue((prev) => {
      const newValue = Math.min(control.max, prev + control.increment)
      // For floats: store directly; for integers: multiply by division
      const valueToStore = isFloat ? newValue : Math.round(newValue * division)
      updateParam(control.param, String(valueToStore))
      return newValue
    })
  }, [control.max, control.increment, control.param, division, isFloat, updateParam])

  const decrement = useCallback(() => {
    setLocalValue((prev) => {
      const newValue = Math.max(control.min, prev - control.increment)
      // For floats: store directly; for integers: multiply by division
      const valueToStore = isFloat ? newValue : Math.round(newValue * division)
      updateParam(control.param, String(valueToStore))
      return newValue
    })
  }, [control.min, control.increment, control.param, division, isFloat, updateParam])

  // Start hold-to-repeat
  const startHold = useCallback((action: () => void, checkBounds: () => boolean) => {
    if (disabled) return

    holdStartTimeRef.current = Date.now()

    // Execute once immediately
    action()

    // Start repeat after initial delay
    repeatTimeoutRef.current = setTimeout(() => {
      repeatIntervalRef.current = setInterval(() => {
        if (!checkBounds()) {
          stopHold()
          return
        }

        // Speed up after threshold
        const elapsed = Date.now() - holdStartTimeRef.current
        if (elapsed > FAST_REPEAT_THRESHOLD && repeatIntervalRef.current) {
          clearInterval(repeatIntervalRef.current)
          repeatIntervalRef.current = setInterval(() => {
            if (!checkBounds()) {
              stopHold()
              return
            }
            action()
          }, FAST_REPEAT_INTERVAL)
        }

        action()
      }, REPEAT_INTERVAL)
    }, INITIAL_DELAY)
  }, [disabled])

  // Stop hold-to-repeat
  const stopHold = useCallback(() => {
    if (repeatIntervalRef.current) {
      clearInterval(repeatIntervalRef.current)
      repeatIntervalRef.current = null
    }
    if (repeatTimeoutRef.current) {
      clearTimeout(repeatTimeoutRef.current)
      repeatTimeoutRef.current = null
    }
  }, [])

  // Button handlers
  const handleDecrementStart = () => {
    startHold(decrement, () => localValue > control.min)
  }

  const handleIncrementStart = () => {
    startHold(increment, () => localValue < control.max)
  }

  return (
    <ControlCard
      title={control.title}
      description={description}
      disabled={disabled}
      disabledReason={disabledReason}
      className="number-control"
    >
      <div className="number-control__controls">
        <div className="number-control__btn-wrapper">
          <span className="number-control__limit">min: {control.min}{unit}</span>
          <button
            type="button"
            className="number-control__btn number-control__btn--minus"
            onMouseDown={handleDecrementStart}
            onMouseUp={stopHold}
            onMouseLeave={stopHold}
            onTouchStart={handleDecrementStart}
            onTouchEnd={stopHold}
            disabled={disabled || localValue <= control.min}
            aria-label="Decrease value"
          >
            <Icon name="remove" size={32} />
          </button>
        </div>

        <div className="number-control__value">
          <input
            type="number"
            className="number-control__input"
            value={localValue.toFixed(decimalPlaces)}
            onChange={handleInputChange}
            onBlur={handleInputBlur}
            disabled={disabled}
            min={control.min}
            max={control.max}
            step={control.increment}
            aria-label={`${control.title} value`}
          />
          {unit && <span className="number-control__unit">{unit}</span>}
        </div>

        <div className="number-control__btn-wrapper">
          <span className="number-control__limit">max: {control.max}{unit}</span>
          <button
            type="button"
            className="number-control__btn number-control__btn--plus"
            onMouseDown={handleIncrementStart}
            onMouseUp={stopHold}
            onMouseLeave={stopHold}
            onTouchStart={handleIncrementStart}
            onTouchEnd={stopHold}
            disabled={disabled || localValue >= control.max}
            aria-label="Increase value"
          >
            <Icon name="add" size={32} />
          </button>
        </div>
      </div>
    </ControlCard>
  )
}
