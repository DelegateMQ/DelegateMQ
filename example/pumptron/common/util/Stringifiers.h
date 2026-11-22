#ifndef PUMPTRON_STRINGIFIERS_H
#define PUMPTRON_STRINGIFIERS_H

/// @file Stringifiers.h
/// @brief Readable text for each Pumptron topic, for DataBus::Monitor()
///        subscribers: the GUI's Bus Monitor pane and dmq-spy (through the
///        GUI's or the controller's SpyBridge).
///
/// Shared by the GUI and the controller so both nodes show the same text in
/// dmq-spy. snprintf only (no iostreams), so it costs little flash on a board.

namespace pumptron {

/// Register a stringifier for every topic that crosses the link. Call once,
/// before the first publish you want to see as text.
void RegisterStringifiers();

} // namespace pumptron

#endif // PUMPTRON_STRINGIFIERS_H
