#include "DataMgr.h"

// Initialize the static signal
dmq::Signal<void(DataMsg&)> DataMgr::DataMsgCb;
