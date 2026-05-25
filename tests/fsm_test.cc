#include "xtils/fsm/fsm.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils::fsm;

// Test Events
enum TestEvents : EventType {
  EVENT_A = 1,
  EVENT_B = 2,
  EVENT_C = 3,
  EVENT_TIMER = 4,
  EVENT_INVALID = 999
};

TEST_CASE("FSM Basic State Management") {
  FSM fsm;

  SUBCASE("Add states") {
    auto state1_id = fsm.AddState("State1");
    auto state2_id = fsm.AddState("State2");

    CHECK(state1_id != state2_id);
    CHECK(fsm.GetStateId("State1").value() == state1_id);
    CHECK(fsm.GetStateId("State2").value() == state2_id);
    CHECK(fsm.GetStateName(state1_id) == "State1");
    CHECK(fsm.GetStateName(state2_id) == "State2");
  }

  SUBCASE("Duplicate state names should throw") {
    fsm.AddState("DuplicateState");
    CHECK_THROWS_AS(fsm.AddState("DuplicateState"), FSMException);
  }

  SUBCASE("Get non-existent state should throw") {
    CHECK(!fsm.GetStateId("NonExistent").has_value());
  }

  SUBCASE("Get state name for invalid ID") {
    CHECK_FALSE(fsm.GetStateName(999999).has_value());
  }
}

TEST_CASE("FSM State Callbacks") {
  FSM fsm;

  bool on_enter_called = false;
  bool on_exit_called = false;
  EventType received_event = 0;

  SUBCASE("State with callbacks") {
    auto state_id = fsm.AddState(
        "CallbackState",
        [&](const State& state, EventType event) {
          on_enter_called = true;
          received_event = event;
        },
        [&](const State& state, EventType event) { on_exit_called = true; });

    auto state2_id = fsm.AddState("State2");

    fsm.AddTransition("CallbackState", "State2", EVENT_A);

    fsm.Start("CallbackState");
    CHECK(on_enter_called);
    CHECK(received_event == 0);  // Start event

    on_enter_called = false;
    fsm.ProcessEvent(EVENT_A);
    CHECK(on_exit_called);
  }
}

TEST_CASE("FSM Transitions") {
  FSM fsm;

  fsm.AddState("State1");
  fsm.AddState("State2");
  fsm.AddState("State3");

  SUBCASE("Basic transition") {
    fsm.AddTransition("State1", "State2", EVENT_A);
    fsm.Start("State1");

    CHECK(fsm.IsInState("State1"));
    CHECK_FALSE(fsm.IsInState("State2"));

    fsm.ProcessEvent(EVENT_A);

    CHECK_FALSE(fsm.IsInState("State1"));
    CHECK(fsm.IsInState("State2"));
    CHECK(fsm.GetCurrentStateName() == "State2");
  }

  SUBCASE("Multiple transitions from same state") {
    fsm.AddTransition("State1", "State2", EVENT_A);
    fsm.AddTransition("State1", "State3", EVENT_B);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_B);
    CHECK(fsm.IsInState("State3"));

    fsm.Reset("State1");
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("State2"));
  }

  SUBCASE("No valid transition") {
    fsm.AddTransition("State1", "State2", EVENT_A);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_B);       // No transition for this event
    CHECK(fsm.IsInState("State1"));  // Should stay in same state
  }

  SUBCASE("Transition with multiple events") {
    std::vector<EventType> events = {EVENT_A, EVENT_B, EVENT_C};
    fsm.AddTransition("State1", "State2", events);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_B);
    CHECK(fsm.IsInState("State2"));

    fsm.Reset("State1");
    fsm.ProcessEvent(EVENT_C);
    CHECK(fsm.IsInState("State2"));
  }
}

TEST_CASE("FSM Transition Conditions") {
  FSM fsm;

  fsm.AddState("State1");
  fsm.AddState("State2");

  SUBCASE("Guard conditions") {
    bool allow_transition = true;

    auto guard = MakeGuard(
        "test_guard", [&](const State& from, const State& to, EventType event) {
          return allow_transition;
        });

    fsm.AddTransition("State1", "State2", EVENT_A, guard);
    fsm.Start("State1");

    // Guard allows transition
    allow_transition = true;
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("State2"));

    // Reset and test blocked transition
    fsm.Reset("State1");
    allow_transition = false;
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("State1"));  // Should stay in State1
  }

  SUBCASE("Action conditions") {
    bool action_executed = false;

    auto action = MakeAction("test_action",
                             [&](const State& from, const State& to,
                                 EventType event) { action_executed = true; });

    fsm.AddTransition("State1", "State2", EVENT_A, action);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_A);
    CHECK(action_executed);
    CHECK(fsm.IsInState("State2"));
  }

  SUBCASE("Combined guard and action") {
    bool guard_checked = false;
    bool action_executed = false;

    auto condition = MakeCondition(
        "combined_condition",
        [&](const State& from, const State& to, EventType event) {
          guard_checked = true;
          return true;
        },
        [&](const State& from, const State& to, EventType event) {
          action_executed = true;
        });

    fsm.AddTransition("State1", "State2", EVENT_A, condition);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_A);
    CHECK(guard_checked);
    CHECK(action_executed);
    CHECK(fsm.IsInState("State2"));
  }
}

TEST_CASE("FSM Control Operations") {
  FSM fsm;

  fsm.AddState("Initial");
  fsm.AddState("Running");
  fsm.AddState("Stopped");

  SUBCASE("Start FSM") {
    CHECK_FALSE(fsm.GetCurrentStateId().has_value());

    fsm.Start("Initial");
    CHECK(fsm.GetCurrentStateId().has_value());
    CHECK(fsm.IsInState("Initial"));
  }

  SUBCASE("Start with invalid state") {
    CHECK_THROWS_AS(fsm.Start("NonExistent"), StateNotFoundException);
  }

  SUBCASE("Reset FSM") {
    fsm.AddTransition("Initial", "Running", EVENT_A);
    fsm.Start("Initial");
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("Running"));

    fsm.Reset("Stopped");
    CHECK(fsm.IsInState("Stopped"));
  }

  SUBCASE("Process event without starting") {
    CHECK_THROWS_AS(fsm.ProcessEvent(EVENT_A), FSMException);
  }
}

TEST_CASE("FSM History Management") {
  FSM fsm;

  fsm.AddState("State1");
  fsm.AddState("State2");
  fsm.AddTransition("State1", "State2", EVENT_A);

  SUBCASE("History tracking") {
    fsm.Start("State1");
    CHECK(fsm.GetHistory().size() == 1);  // Start event

    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.GetHistory().size() == 2);  // Transition event

    const auto& history = fsm.GetHistory();
    CHECK(history[0].transition_occurred == true);  // Start
    CHECK(history[1].transition_occurred == true);  // Transition
  }

  SUBCASE("History size limit") {
    fsm.SetMaxHistorySize(2);

    fsm.Start("State1");
    fsm.ProcessEvent(EVENT_A);        // Should have 2 entries
    fsm.ProcessEvent(EVENT_INVALID);  // Should trigger history cleanup

    CHECK(fsm.GetHistory().size() <= 2);
  }

  SUBCASE("Clear history") {
    fsm.Start("State1");
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.GetHistory().size() > 0);

    fsm.ClearHistory();
    CHECK(fsm.GetHistory().size() == 0);
  }
}

TEST_CASE("FSM DOT Graph Generation") {
  FSM fsm;

  fsm.AddState("Start");
  fsm.AddState("Process");
  fsm.AddState("End");

  fsm.AddTransition("Start", "Process", EVENT_A);
  fsm.AddTransition("Process", "End", EVENT_B);

  SUBCASE("Generate DOT graph") {
    std::string dot = fsm.ToDotGraph();

    CHECK(dot.find("digraph FSM") != std::string::npos);
    CHECK(dot.find("Start") != std::string::npos);
    CHECK(dot.find("Process") != std::string::npos);
    CHECK(dot.find("End") != std::string::npos);
    CHECK(dot.find("->") != std::string::npos);
  }

  SUBCASE("Current state highlighting") {
    fsm.Start("Process");
    std::string dot = fsm.ToDotGraph();

    CHECK(dot.find("Process") != std::string::npos);
    CHECK(dot.find("fillcolor") != std::string::npos);
  }

  SUBCASE("Event name registration and DOT output") {
    fsm.RegisterEvent(EVENT_A, "start_process");
    fsm.RegisterEvent(EVENT_B, "finish_process");
    fsm.Start("Start");

    std::string dot = fsm.ToDotGraph();

    // Event names should appear instead of numbers
    CHECK(dot.find("start_process") != std::string::npos);
    CHECK(dot.find("finish_process") != std::string::npos);
    // Initial state marker
    CHECK(dot.find("__start__") != std::string::npos);
  }

  SUBCASE("GetEventName") {
    fsm.RegisterEvent(EVENT_A, "event_alpha");
    CHECK(fsm.GetEventName(EVENT_A) == "event_alpha");
    CHECK(fsm.GetEventName(EVENT_B) == std::to_string(EVENT_B));
  }
}

TEST_CASE("FSM Thread Safety") {
  FSM fsm;
  fsm.EnableThreadSafety(true);

  fsm.AddState("ThreadSafe1");
  fsm.AddState("ThreadSafe2");
  fsm.AddTransition("ThreadSafe1", "ThreadSafe2", EVENT_A);

  SUBCASE("Thread-safe operations") {
    fsm.Start("ThreadSafe1");
    CHECK(fsm.IsInState("ThreadSafe1"));

    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("ThreadSafe2"));

    // These operations should work without throwing in thread-safe mode
    auto current_state = fsm.GetCurrentStateName();
    CHECK(current_state.has_value());

    const auto& history = fsm.GetHistory();
    CHECK(history.size() > 0);
  }

  SUBCASE("Reentrant callback (no deadlock with recursive_mutex)") {
    // If onEnter calls IsInState/GetCurrentStateName, it must not deadlock
    std::string captured_name;
    FSM fsm2;
    fsm2.EnableThreadSafety(true);
    fsm2.AddState("A");
    fsm2.AddState("B",
        [&](const State&, EventType) {
          // Re-enter FSM from within callback
          captured_name = fsm2.GetCurrentStateName().value_or("unknown");
        });
    fsm2.AddTransition("A", "B", EVENT_A);
    fsm2.Start("A");
    fsm2.ProcessEvent(EVENT_A);
    // After transition, onEnter(B) called -> GetCurrentStateName() == "B"
    CHECK(captured_name == "B");
    CHECK(fsm2.IsInState("B"));
  }
}

TEST_CASE("FSM Concurrent Access") {
  FSM fsm;
  fsm.EnableThreadSafety(true);

  fsm.AddState("Idle");
  fsm.AddState("Running");
  fsm.AddState("Done");
  fsm.AddTransition("Idle", "Running", EVENT_A);
  fsm.AddTransition("Running", "Done", EVENT_B);
  fsm.AddTransition("Done", "Idle", EVENT_C);
  fsm.Start("Idle");

  SUBCASE("Concurrent ProcessEvent and IsInState") {
    std::atomic<bool> stop{false};
    std::atomic<int> query_count{0};

    // Reader thread: continuously queries state
    std::thread reader([&]() {
      while (!stop.load()) {
        fsm.IsInState("Idle");
        fsm.GetCurrentStateName();
        fsm.GetHistory();
        query_count++;
      }
    });

    // Writer thread: cycles through states
    for (int i = 0; i < 1000; ++i) {
      fsm.ProcessEvent(EVENT_A);  // Idle -> Running
      fsm.ProcessEvent(EVENT_B);  // Running -> Done
      fsm.ProcessEvent(EVENT_C);  // Done -> Idle
    }

    stop.store(true);
    reader.join();

    CHECK(fsm.IsInState("Idle"));
    CHECK(query_count.load() > 0);
  }
}

TEST_CASE("FSM History Recording Policy") {
  FSM fsm;

  fsm.AddState("S1");
  fsm.AddState("S2");
  fsm.AddTransition("S1", "S2", EVENT_A);
  fsm.Start("S1");

  SUBCASE("Default: only successful transitions recorded") {
    fsm.ProcessEvent(EVENT_INVALID);  // No transition, should NOT be recorded
    auto history = fsm.GetHistory();
    // Only the Start entry should exist
    CHECK(history.size() == 1);
    CHECK(history[0].description == "FSM started");

    fsm.ProcessEvent(EVENT_A);  // Successful transition
    history = fsm.GetHistory();
    CHECK(history.size() == 2);
    CHECK(history[1].transition_occurred == true);
  }

  SUBCASE("With record_failed_events: all events recorded") {
    fsm.SetRecordFailedEvents(true);
    fsm.ProcessEvent(EVENT_INVALID);  // Should be recorded
    auto history = fsm.GetHistory();
    CHECK(history.size() == 2);  // Start + failed event
    CHECK(history[1].transition_occurred == false);
    CHECK(history[1].description == "No transition");
  }
}

TEST_CASE("FSM Error Handling") {
  FSM fsm;

  SUBCASE("Invalid state operations") {
    CHECK_THROWS_AS(fsm.Start("Invalid"), StateNotFoundException);
    CHECK_THROWS_AS(fsm.Reset("Invalid"), StateNotFoundException);
  }

  SUBCASE("Invalid transition operations") {
    fsm.AddState("Valid");

    // Try to add transition with invalid states
    CHECK_THROWS_AS(fsm.AddTransition("Invalid", "Valid", EVENT_A),
                    StateNotFoundException);
    CHECK_THROWS_AS(fsm.AddTransition("Valid", "Invalid", EVENT_A),
                    StateNotFoundException);
  }
}

TEST_CASE("Traffic Light Example") {
  FSM fsm;

  // Create a traffic light system
  fsm.AddState("Red");
  fsm.AddState("Yellow");
  fsm.AddState("Green");
  fsm.AddState("Emergency");

  SUBCASE("Traffic light cycle") {
    fsm.AddTransition("Red", "Green", EVENT_TIMER);
    fsm.AddTransition("Green", "Yellow", EVENT_TIMER);
    fsm.AddTransition("Yellow", "Red", EVENT_TIMER);

    // Emergency transitions from any state
    std::vector<std::string> normal_states = {"Red", "Yellow", "Green"};
    for (const auto& state : normal_states) {
      fsm.AddTransition(state, "Emergency", EVENT_C);
    }
    fsm.AddTransition("Emergency", "Red", EVENT_A);

    fsm.Start("Red");

    // Normal cycle
    fsm.ProcessEvent(EVENT_TIMER);  // Red -> Green
    CHECK(fsm.IsInState("Green"));

    fsm.ProcessEvent(EVENT_TIMER);  // Green -> Yellow
    CHECK(fsm.IsInState("Yellow"));

    fsm.ProcessEvent(EVENT_TIMER);  // Yellow -> Red
    CHECK(fsm.IsInState("Red"));

    // Emergency override
    fsm.ProcessEvent(EVENT_C);
    CHECK(fsm.IsInState("Emergency"));

    // Return to normal
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("Red"));
  }
}

TEST_CASE("Door State Machine Example") {
  FSM fsm;

  enum DoorEvents : EventType {
    OPEN_CMD = 10,
    CLOSE_CMD = 11,
    LOCK_CMD = 12,
    UNLOCK_CMD = 13
  };

  fsm.AddState("Closed");
  fsm.AddState("Open");
  fsm.AddState("Locked");

  SUBCASE("Door operations") {
    // Add transitions with guards
    auto door_guard =
        MakeGuard("door_security_check",
                  [](const State& from, const State& to, EventType event) {
                    if (event == OPEN_CMD && from.name() == "Locked") {
                      return false;  // Cannot open locked door
                    }
                    return true;
                  });

    // Normal transitions
    fsm.AddTransition("Closed", "Open", OPEN_CMD);
    fsm.AddTransition("Open", "Closed", CLOSE_CMD);
    fsm.AddTransition("Closed", "Locked", LOCK_CMD);
    fsm.AddTransition("Locked", "Closed", UNLOCK_CMD);

    // Guarded transition
    fsm.AddTransition("Locked", "Open", OPEN_CMD, door_guard);

    fsm.Start("Closed");

    fsm.ProcessEvent(OPEN_CMD);  // Closed -> Open
    CHECK(fsm.IsInState("Open"));

    fsm.ProcessEvent(CLOSE_CMD);  // Open -> Closed
    CHECK(fsm.IsInState("Closed"));

    fsm.ProcessEvent(LOCK_CMD);  // Closed -> Locked
    CHECK(fsm.IsInState("Locked"));

    fsm.ProcessEvent(OPEN_CMD);  // LOCKED -> LOCKED (blocked by guard)
    CHECK(fsm.IsInState("Locked"));

    fsm.ProcessEvent(UNLOCK_CMD);  // Locked -> Closed
    CHECK(fsm.IsInState("Closed"));
  }
}

// Simple test runner
int main() {
  doctest::Context context;

  // Run tests
  int result = context.run();

  if (result == 0) {
    std::cout << "All FSM tests passed!" << std::endl;
  } else {
    std::cout << "Some FSM tests failed!" << std::endl;
  }

  return result;
}
