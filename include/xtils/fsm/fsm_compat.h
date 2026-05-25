/*
 * Deprecated FSM API compatibility wrappers.
 * Include via fsm.h (auto-included at bottom).
 */
#pragma once

namespace xtils {
namespace fsm {

// Deprecated free functions
[[deprecated("Use MakeGuard() instead")]]
inline std::shared_ptr<TransitionCondition> makeGuard(const std::string& name,
                                                      TransitionGuard guard) {
  return MakeGuard(name, std::move(guard));
}
[[deprecated("Use MakeAction() instead")]]
inline std::shared_ptr<TransitionCondition> makeAction(
    const std::string& name, TransitionAction action) {
  return MakeAction(name, std::move(action));
}
[[deprecated("Use MakeCondition() instead")]]
inline std::shared_ptr<TransitionCondition> makeCondition(
    const std::string& name, TransitionGuard guard, TransitionAction action) {
  return MakeCondition(name, std::move(guard), std::move(action));
}

}  // namespace fsm
}  // namespace xtils
