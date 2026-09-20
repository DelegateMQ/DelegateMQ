#include "DelegateMQ.h"
#include <iostream>

#if defined(DMQ_DATABUS)

extern int DataBusLocalTestMain();
extern int DataBusQosTestMain();
extern int DataBusDeadlineTestMain();
extern int DataBusRemoteTestMain();
extern int DataBusSpyTestMain();
extern int DataBusBenchmarkTestMain();
extern int DataBusTypeMismatchTestMain();
extern int DataBusErrorTestMain();
extern int DataBusCapacityTestMain();
extern int NetworkNodeTestMain();

void RunDataBusTests() {
    std::cout << "--- Running DataBus Unit Tests ---" << std::endl;
    DataBusLocalTestMain();
    DataBusQosTestMain();
    DataBusDeadlineTestMain();
    DataBusRemoteTestMain();
    DataBusSpyTestMain();
    DataBusBenchmarkTestMain();
    DataBusTypeMismatchTestMain();
    DataBusErrorTestMain();
    DataBusCapacityTestMain(); // Disabled by default (#if 0) — see DataBusErrorTest.cpp
    NetworkNodeTestMain();
    std::cout << "--- DataBus Unit Tests Completed ---" << std::endl;
}

#endif
