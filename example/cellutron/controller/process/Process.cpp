#include "Process.h"
#include "Logger.h"
#include "actuators/Actuators.h"
#include "extras/util/ThreadMonitor.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {
namespace process {

Process::~Process() { Shutdown(); }

void Process::Initialize()
{
    dmq::util::ThreadMonitor::Register(&m_thread);
    m_thread.SetThreadPriority(PRIORITY_PROCESS);
    m_thread.CreateThread(WATCHDOG_TIMEOUT);
    m_pumpProcess.SetThread(m_thread);
    m_cellProcess.SetThread(m_thread);
    printf("Process: Subsystem initialized.\n");
}

void Process::Shutdown() { m_thread.ExitThread(); }

void Process::Start()
{
    MakeDelegate(this, &Process::InternalStart, m_thread).AsyncInvoke();
}

void Process::Abort()
{
    MakeDelegate(this, &Process::InternalAbort, m_thread).AsyncInvoke();
}

void Process::Fault()
{
    MakeDelegate(this, &Process::InternalFault, m_thread).AsyncInvoke();
}

void Process::InternalStart() { m_cellProcess.StartProcess(); }
void Process::InternalAbort() { m_cellProcess.AbortProcess(); }
void Process::InternalFault() { m_cellProcess.GenerateFault(); }

} // namespace process
} // namespace cellutron
