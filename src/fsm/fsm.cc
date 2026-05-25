/*
 * Description: Improved Finite State Machine Implementation
 *
 * Copyright (c) 2018 - 2024 Albert Lv <altair.albert@gmail.com>
 *
 * Licensed under the MIT License. See LICENSE file in the project root for full
 * license information.
 *
 * Author: Albert Lv <altair.albert@gmail.com>
 * Version: 1.0.0
 *
 * Changelog:
 * - Fixed naming inconsistencies and typos
 * - Improved error handling with exceptions
 * - Enhanced thread safety
 * - Added state ID system for better performance
 * - Improved API design and usability
 */

#include "xtils/fsm/fsm.h"

#include <algorithm>
#include <sstream>

namespace xtils {
namespace fsm {

// FSM Implementation

StateId FSM::AddState(std::unique_ptr<State> state) {
  return withLock([&]() -> StateId {
    if (!state) {
      throw FSMException("Cannot add null state");
    }

    const std::string& name = state->name();
    // Check for name conflicts
    if (name_to_id_.find(name) != name_to_id_.end()) {
      throw FSMException("State with name '" + name + "' already exists");
    }
    StateId id = generateId();
    state->id_ = id;

    name_to_id_[name] = id;
    states_[id] = std::move(state);

    return id;
  });
}

StateId FSM::AddState(const std::string& name) {
  return AddState(std::make_unique<State>(name));
}

StateId FSM::AddState(const std::string& name, StateCallback on_enter) {
  return AddState(std::make_unique<State>(name, std::move(on_enter)));
}

StateId FSM::AddState(const std::string& name, StateCallback on_enter,
                      StateCallback on_exit) {
  return AddState(
      std::make_unique<State>(name, std::move(on_enter), std::move(on_exit)));
}

void FSM::RegisterEvent(EventType event, const std::string& name) {
  withLock([&]() { event_names_[event] = name; });
}

std::string FSM::GetEventName(EventType event) const {
  return withLock([&]() -> std::string {
    auto it = event_names_.find(event);
    return it != event_names_.end() ? it->second : std::to_string(event);
  });
}

void FSM::AddTransition(const std::string& from, const std::string& to,
                        EventType event,
                        std::shared_ptr<TransitionCondition> condition) {
  withLock([&]() {
    State* from_state = getState(from);
    if (!from_state) throw StateNotFoundException(from);
    State* to_state = getState(to);
    if (!to_state) throw StateNotFoundException(to);
    auto& transitions = from_state->transitions_[event];
    transitions.emplace_back(to_state->id_, std::move(condition));
  });
}

void FSM::AddTransition(StateId from, StateId to, EventType event,
                        std::shared_ptr<TransitionCondition> condition) {
  withLock([&]() {
    State* from_state = getState(from);
    State* to_state = getState(to);

    if (!from_state) {
      throw StateNotFoundException("State ID " + std::to_string(from));
    }
    if (!to_state) {
      throw StateNotFoundException("State ID " + std::to_string(to));
    }

    // Add transition to the from state
    auto& transitions = from_state->transitions_[event];
    transitions.emplace_back(to, std::move(condition));
  });
}

void FSM::AddTransition(const std::string& from, const std::string& to,
                        const std::vector<EventType>& events,
                        std::shared_ptr<TransitionCondition> condition) {
  withLock([&]() {
    State* from_state = getState(from);
    if (!from_state) throw StateNotFoundException(from);
    State* to_state = getState(to);
    if (!to_state) throw StateNotFoundException(to);
    for (EventType event : events) {
      auto cond_copy = condition ? std::make_shared<TransitionCondition>(*condition) : nullptr;
      auto& transitions = from_state->transitions_[event];
      transitions.emplace_back(to_state->id_, std::move(cond_copy));
    }
  });
}

void FSM::Start(const std::string& initial_state) {
  withLock([&]() {
    State* state = getState(initial_state);
    if (!state) throw StateNotFoundException(initial_state);
    current_state_id_ = state->id_;
    initial_state_id_ = state->id_;
    previous_state_id_ = 0;
    is_started_ = true;
    state->onEnter(0);
    addToHistory(0, current_state_id_, 0, true, "FSM started");
  });
}

void FSM::Start(StateId initial_state_id) {
  withLock([&]() {
    State* state = getState(initial_state_id);
    if (!state) {
      throw StateNotFoundException("State ID " +
                                   std::to_string(initial_state_id));
    }
    current_state_id_ = initial_state_id;
    initial_state_id_ = initial_state_id;
    previous_state_id_ = 0;
    is_started_ = true;
    state->onEnter(0);
    addToHistory(0, current_state_id_, 0, true, "FSM started");
  });
}

void FSM::Reset(const std::string& state) {
  withLock([&]() {
    State* target = getState(state);
    if (!target) throw StateNotFoundException(state);
    if (is_started_ && current_state_id_ != 0) {
      State* current = getState(current_state_id_);
      if (current) current->onExit(0);
    }
    previous_state_id_ = current_state_id_;
    current_state_id_ = target->id_;
    is_started_ = true;
    target->onEnter(0);
    addToHistory(previous_state_id_, current_state_id_, 0, true, "FSM reset");
  });
}

void FSM::Reset(StateId state_id) {
  withLock([&]() {
    State* target = getState(state_id);
    if (!target) {
      throw StateNotFoundException("State ID " + std::to_string(state_id));
    }
    if (is_started_ && current_state_id_ != 0) {
      State* current = getState(current_state_id_);
      if (current) current->onExit(0);
    }
    previous_state_id_ = current_state_id_;
    current_state_id_ = state_id;
    is_started_ = true;
    target->onEnter(0);
    addToHistory(previous_state_id_, current_state_id_, 0, true, "FSM reset");
  });
}

void FSM::ProcessEvent(EventType event) {
  withLock([&]() {
    if (!is_started_) {
      throw FSMException("FSM is not started");
    }

    State* current_state = getState(current_state_id_);
    if (!current_state) {
      throw FSMException("Current state not found");
    }

    // Check for transitions on this event
    auto transitions_it = current_state->transitions_.find(event);
    if (transitions_it == current_state->transitions_.end()) {
      // No transitions for this event, just update current state
      current_state->onUpdate(event);
      addToHistory(current_state_id_, current_state_id_, event, false,
                   "No transition");
      return;
    }

    // Try each transition until one succeeds
    const auto& transitions = transitions_it->second;
    for (const auto& transition : transitions) {
      State* target_state = getState(transition.target_state_id);
      if (!target_state) {
        continue;  // Skip invalid transitions
      }

      // Check transition condition (guard)
      if (transition.condition && !transition.condition->CanTransition(
                                      *current_state, *target_state, event)) {
        continue;  // Guard prevented transition
      }

      // Execute transition
      current_state->onExit(event);

      // Execute transition action
      if (transition.condition) {
        transition.condition->ExecuteAction(*current_state, *target_state,
                                            event);
      }

      // Update state
      previous_state_id_ = current_state_id_;
      current_state_id_ = transition.target_state_id;

      // Enter new state
      target_state->onEnter(event);

      std::string desc =
          transition.condition ? transition.condition->name() : "transition";
      addToHistory(previous_state_id_, current_state_id_, event, true, desc);
      return;
    }

    // No valid transition found, update current state
    current_state->onUpdate(event);
    addToHistory(current_state_id_, current_state_id_, event, false,
                 "Transition blocked");
  });
}

bool FSM::IsInState(const std::string& state_name) const {
  return withLock([&]() {
    auto it = name_to_id_.find(state_name);
    return it != name_to_id_.end() && it->second == current_state_id_;
  });
}

bool FSM::IsInState(StateId state_id) const {
  return withLock([&]() { return current_state_id_ == state_id; });
}

std::optional<std::string> FSM::GetCurrentStateName() const {
  return withLock([&]() -> std::optional<std::string> {
    State* state = getState(current_state_id_);
    return state ? std::make_optional(state->name()) : std::nullopt;
  });
}

std::optional<StateId> FSM::GetCurrentStateId() const {
  return withLock([&]() -> std::optional<StateId> {
    return is_started_ ? std::make_optional(current_state_id_) : std::nullopt;
  });
}

std::optional<StateId> FSM::GetStateId(const std::string& name) const {
  return withLock([&]() -> std::optional<StateId> {
    auto it = name_to_id_.find(name);
    if (it == name_to_id_.end()) return std::nullopt;
    return it->second;
  });
}

std::optional<std::string> FSM::GetStateName(StateId id) const {
  return withLock([&]() -> std::optional<std::string> {
    State* state = getState(id);
    return state ? std::make_optional(state->name()) : std::nullopt;
  });
}

std::string FSM::ToDotGraph() const {
  return withLock([&]() -> std::string {
    std::stringstream ss;
    ss << "digraph FSM {\n";
    ss << "  rankdir=LR;\n";
    ss << "  node [shape=rectangle, style=\"rounded\", fontname=\"sans-serif\"];\n";
    ss << "  edge [fontname=\"sans-serif\", fontsize=10];\n\n";

    // Initial state marker
    if (is_started_ && initial_state_id_ != 0) {
      ss << "  __start__ [shape=point, width=0.2, height=0.2];\n";
      State* init = getState(initial_state_id_);
      if (init) {
        ss << "  __start__ -> " << initial_state_id_ << ";\n";
      }
    }

    // Collect and sort states for deterministic output
    std::vector<StateId> state_ids;
    state_ids.reserve(states_.size());
    for (const auto& [id, _] : states_) {
      state_ids.push_back(id);
    }
    std::sort(state_ids.begin(), state_ids.end());

    // Add states
    ss << "\n";
    for (StateId id : state_ids) {
      State* state = getState(id);
      if (!state) continue;
      ss << "  " << id << " [label=\"" << state->name() << "\"";
      if (id == current_state_id_) {
        ss << ", style=\"rounded,filled\", fillcolor=\"#a5d8ff\"";
      }
      ss << "];\n";
    }

    // Add transitions (sorted for determinism)
    ss << "\n";
    for (StateId from_id : state_ids) {
      State* state = getState(from_id);
      if (!state) continue;

      // Sort events
      std::vector<EventType> events;
      events.reserve(state->transitions_.size());
      for (const auto& [evt, _] : state->transitions_) {
        events.push_back(evt);
      }
      std::sort(events.begin(), events.end());

      for (EventType evt : events) {
        const auto& targets = state->transitions_.at(evt);
        for (const auto& transition : targets) {
          State* target = getState(transition.target_state_id);
          if (!target) continue;

          // Get event name
          auto name_it = event_names_.find(evt);
          std::string evt_label = (name_it != event_names_.end())
                                      ? name_it->second
                                      : std::to_string(evt);

          ss << "  " << from_id << " -> " << transition.target_state_id
             << " [label=\"" << evt_label;
          if (transition.condition && !transition.condition->name().empty()) {
            ss << "\\n[" << transition.condition->name() << "]";
          }
          ss << "\"];\n";
        }
      }
    }

    ss << "}\n";
    return ss.str();
  });
}

const std::deque<HistoryEntry>& FSM::GetHistory() const {
  return withLock(
      [&]() -> const std::deque<HistoryEntry>& { return history_; });
}

void FSM::ClearHistory() {
  withLock([&]() { history_.clear(); });
}

void FSM::SetMaxHistorySize(std::size_t size) {
  withLock([&]() {
    max_history_size_ = size;
    while (history_.size() > max_history_size_) {
      history_.pop_front();
    }
  });
}

// Private helper methods

void FSM::addToHistory(StateId from, StateId to, EventType event,
                       bool transitioned, const std::string& desc) {
  history_.emplace_back(from, to, event, transitioned, desc);
  while (history_.size() > max_history_size_) {
    history_.pop_front();
  }
}

State* FSM::getState(StateId id) const {
  // This method should only be called from within withLock
  auto it = states_.find(id);
  return it != states_.end() ? it->second.get() : nullptr;
}

State* FSM::getState(const std::string& name) const {
  // This method should only be called from within withLock
  auto it = name_to_id_.find(name);
  if (it == name_to_id_.end()) {
    return nullptr;
  }
  return getState(it->second);
}

}  // namespace fsm
}  // namespace xtils
