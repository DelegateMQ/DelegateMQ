#ifndef PUMPTRON_GUI_CORE_DUMP_FILE_H
#define PUMPTRON_GUI_CORE_DUMP_FILE_H

#include "messages/CoreDumpMsg.h"
#include <string>

namespace pumptron {

/// @brief Human-readable text for a controller core dump, without the
///        receive timestamp (so identical dumps compare equal).
std::string FormatCoreDump(const CoreDumpMsg& msg);

/// @brief Write `body` (from FormatCoreDump) with a receive timestamp to
///        dump_YYYYMMDD_HHMMSS.txt in the working directory.
/// @param[out] path   File written.
/// @return false if the file could not be written.
bool WriteCoreDumpFile(const std::string& body, std::string& path);

} // namespace pumptron

#endif
