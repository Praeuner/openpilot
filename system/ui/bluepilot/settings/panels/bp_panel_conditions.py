"""
BluePilot Panel Conditions
Condition evaluation system for dynamic control visibility and enabling
Ported from Qt PanelConditions class
"""

from dataclasses import dataclass, field
from typing import Optional
from openpilot.common.params import Params


@dataclass
class ControlConditions:
  """Stores condition configuration for a control"""
  # Legacy conditions (backward compatibility)
  conditions: dict = field(default_factory=dict)
  has_conditions: bool = False

  # Granular conditions
  enable_conditions: dict = field(default_factory=dict)
  visible_conditions: dict = field(default_factory=dict)
  has_enable_conditions: bool = False
  has_visible_conditions: bool = False

  # Auto-reset
  auto_reset_value: str = ""
  param_name: str = ""
  has_auto_reset: bool = False

  # Dynamic descriptions
  descriptions: dict = field(default_factory=dict)
  desc_conditions: dict = field(default_factory=dict)
  default_description: str = ""
  has_dynamic_descriptions: bool = False

  # Parameter substitutions
  param_substitutions: dict = field(default_factory=dict)
  has_param_substitutions: bool = False


class PanelConditions:
  """
  Evaluates visibility and enable conditions for controls.
  Matches Qt PanelConditions singleton pattern.
  """

  _instance: Optional['PanelConditions'] = None
  _params: Optional[Params] = None

  def __new__(cls):
    if cls._instance is None:
      cls._instance = super().__new__(cls)
      cls._instance._params = Params()
      cls._instance._cache: dict = {}
      cls._instance._cache_valid = False
    return cls._instance

  @classmethod
  def get_instance(cls) -> 'PanelConditions':
    """Get singleton instance"""
    return cls()

  def invalidate_cache(self):
    """Invalidate the condition evaluation cache"""
    self._cache = {}
    self._cache_valid = False

  def _get_param_bool(self, param: str) -> bool:
    """Get boolean parameter value with caching"""
    cache_key = f"bool:{param}"
    if cache_key in self._cache:
      return self._cache[cache_key]

    try:
      value = self._params.get_bool(param)
    except Exception:
      value = False

    self._cache[cache_key] = value
    return value

  def _get_param_value(self, param: str) -> str:
    """Get string parameter value with caching"""
    cache_key = f"str:{param}"
    if cache_key in self._cache:
      return self._cache[cache_key]

    try:
      val = self._params.get(param)
      value = val.decode('utf-8') if val else ""
    except Exception:
      value = ""

    self._cache[cache_key] = value
    return value

  def evaluate_conditions(self, conditions: dict) -> bool:
    """
    Evaluate a condition object.
    Supports:
    - allConditionsTrue: All conditions must be true
    - anyConditionTrue: At least one condition must be true
    - Condition types: paramIsTrue, paramIsFalse, paramEquals, paramNotEquals
    """
    if not conditions:
      return True

    # Handle allConditionsTrue
    all_conditions = conditions.get("allConditionsTrue", [])
    for cond in all_conditions:
      if not self._evaluate_single_condition(cond):
        return False

    # Handle anyConditionTrue
    any_conditions = conditions.get("anyConditionTrue", [])
    if any_conditions:
      any_met = False
      for cond in any_conditions:
        if self._evaluate_single_condition(cond):
          any_met = True
          break
      if not any_met:
        return False

    return True

  def _evaluate_single_condition(self, cond: dict) -> bool:
    """Evaluate a single condition"""
    if "paramIsTrue" in cond:
      return self._get_param_bool(cond["paramIsTrue"])

    if "paramIsFalse" in cond:
      return not self._get_param_bool(cond["paramIsFalse"])

    if "paramEquals" in cond:
      param_data = cond["paramEquals"]
      if isinstance(param_data, dict):
        param = param_data.get("param", "")
        value = param_data.get("value", "")
        return self._get_param_value(param) == str(value)

    if "paramNotEquals" in cond:
      param_data = cond["paramNotEquals"]
      if isinstance(param_data, dict):
        param = param_data.get("param", "")
        value = param_data.get("value", "")
        return self._get_param_value(param) != str(value)

    if "paramGreaterThan" in cond:
      param_data = cond["paramGreaterThan"]
      if isinstance(param_data, dict):
        param = param_data.get("param", "")
        value = param_data.get("value", 0)
        try:
          current = float(self._get_param_value(param) or 0)
          return current > float(value)
        except ValueError:
          return False

    if "paramLessThan" in cond:
      param_data = cond["paramLessThan"]
      if isinstance(param_data, dict):
        param = param_data.get("param", "")
        value = param_data.get("value", 0)
        try:
          current = float(self._get_param_value(param) or 0)
          return current < float(value)
        except ValueError:
          return False

    # Unknown condition type - default to true
    return True

  def check_visibility(self, conditions: ControlConditions) -> bool:
    """Check if control should be visible"""
    if conditions.has_visible_conditions:
      return self.evaluate_conditions(conditions.visible_conditions)
    return True

  def check_enabled(self, conditions: ControlConditions) -> bool:
    """Check if control should be enabled"""
    # Check legacy conditions first
    if conditions.has_conditions:
      if not self.evaluate_conditions(conditions.conditions):
        return False

    # Check granular enable conditions
    if conditions.has_enable_conditions:
      return self.evaluate_conditions(conditions.enable_conditions)

    return True

  def get_dynamic_description(self, conditions: ControlConditions) -> str:
    """Get the appropriate description based on conditions"""
    if not conditions.has_dynamic_descriptions:
      return conditions.default_description

    for key, desc_cond in conditions.desc_conditions.items():
      if self.evaluate_conditions(desc_cond):
        return conditions.descriptions.get(key, conditions.default_description)

    return conditions.default_description

  def apply_param_substitutions(self, text: str, substitutions: dict) -> str:
    """Apply parameter value substitutions to text"""
    if not substitutions or not text:
      return text

    result = text
    for placeholder, param in substitutions.items():
      value = self._get_param_value(param)
      result = result.replace(f"{{{placeholder}}}", value)

    return result

  def get_failed_condition_reasons(self, conditions: dict) -> list:
    """
    Get list of reasons for failed conditions.
    Each condition can have a 'reason' field that explains why it failed.
    """
    if not conditions:
      return []

    reasons = []

    # Check allConditionsTrue
    all_conditions = conditions.get("allConditionsTrue", [])
    for cond in all_conditions:
      if not self._evaluate_single_condition(cond):
        reason = cond.get("reason", "")
        if reason and reason not in reasons:
          reasons.append(reason)

    # Check anyConditionTrue
    any_conditions = conditions.get("anyConditionTrue", [])
    if any_conditions:
      any_met = False
      for cond in any_conditions:
        if self._evaluate_single_condition(cond):
          any_met = True
          break
      if not any_met:
        # Add reasons from all failed any conditions
        for cond in any_conditions:
          reason = cond.get("reason", "")
          if reason and reason not in reasons:
            reasons.append(reason)

    return reasons
