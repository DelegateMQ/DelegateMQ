#ifndef PUMPTRON_SELF_TEST_H
#define PUMPTRON_SELF_TEST_H

namespace pumptron {
namespace gui {

/// @brief Headless end-to-end check of a live controller (simulator or board).
///
/// Drives the controller through the same DataBus commands the operator
/// console sends -- start, speed change, stop, E-STOP, reset, and a GUI-link
/// loss safe-stop -- and verifies the status/telemetry/alarm replies.
/// Requires System::Initialize() to have opened a link.
///
/// @return 0 if every step passed, 1 otherwise.
int RunSelfTest();

} // namespace gui
} // namespace pumptron

#endif
