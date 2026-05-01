#ifndef _PROCESS_H
#define _PROCESS_H

#include "DelegateMQ.h"
#include "CellProcess.h"
#include "PumpProcess.h"
#include "actuators/Actuators.h"

namespace cellutron {
namespace process {

/// @brief Singleton that owns CellProcess and the ProcessThread.
///
/// Public methods are thread-safe: they post to ProcessThread via async delegates
/// and return immediately.
class Process {
public:
    static Process& GetInstance() {
        static Process instance;
        return instance;
    }

    /// Create ProcessThread and wire state machines to it.
    void Initialize();

    /// Shut down ProcessThread.
    void Shutdown();

    /// Start a cell processing run (thread-safe).
    void Start();

    /// Abort the current run (thread-safe).
    void Abort();

    /// Inject a fault from an external source e.g. Safety CPU (thread-safe).
    void Fault();

    /// Get the high-level cell process state machine.
    CellProcess& GetCellProcess() { return m_cellProcess; }

    /// Get the pump process state machine.
    PumpProcess& GetPumpProcess() { return m_pumpProcess; }

private:
    Process() : m_cellProcess(actuators::Actuators::GetInstance().GetCentrifuge(), m_pumpProcess) {}
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    void InternalStart();
    void InternalAbort();

    void InternalFault();

    dmq::os::Thread            m_thread{"ProcessThread", 50, dmq::os::FullPolicy::FAULT};
    PumpProcess       m_pumpProcess;
    CellProcess       m_cellProcess;
};

} // namespace process
} // namespace cellutron

#endif // _PROCESS_H
