/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "DelegateMQ", "index.html", [
    [ "Delegates in C++", "index.html#autotoc_md0", null ],
    [ "Motivation", "index.html#autotoc_md1", null ],
    [ "Advantages", "index.html#autotoc_md2", null ],
    [ "Supported Integrations", "index.html#autotoc_md3", null ],
    [ "Getting Started", "index.html#autotoc_md4", [
      [ "Quick Start", "index.html#autotoc_md5", null ],
      [ "Example Projects", "index.html#autotoc_md6", null ]
    ] ],
    [ "Overview", "index.html#autotoc_md7", [
      [ "Key Concepts", "index.html#autotoc_md8", null ],
      [ "Synchronous Delegates", "index.html#autotoc_md9", null ],
      [ "Asynchronous Delegates", "index.html#autotoc_md10", null ],
      [ "Signal / Slot", "index.html#autotoc_md11", null ]
    ] ],
    [ "DataBus (DDS Lite)", "index.html#autotoc_md12", null ],
    [ "DelegateMQ Tools", "index.html#autotoc_md13", null ],
    [ "Modular Architecture", "index.html#autotoc_md14", null ],
    [ "Features", "index.html#autotoc_md15", null ],
    [ "Documentation", "index.html#autotoc_md16", null ],
    [ "Other Projects Using DelegateMQ", "index.html#autotoc_md17", null ],
    [ "Build and Configuration Guide", "md_docs_2_b_u_i_l_d.html", [
      [ "Table of Contents", "md_docs_2_b_u_i_l_d.html#autotoc_md19", null ],
      [ "Prerequisites", "md_docs_2_b_u_i_l_d.html#autotoc_md21", null ],
      [ "Main Application Build", "md_docs_2_b_u_i_l_d.html#autotoc_md23", null ],
      [ "Example Ecosystem (Sandbox)", "md_docs_2_b_u_i_l_d.html#autotoc_md25", [
        [ "Automated Workspace Sandbox Setup", "md_docs_2_b_u_i_l_d.html#autotoc_md26", null ],
        [ "Manual Sample Build", "md_docs_2_b_u_i_l_d.html#autotoc_md27", null ]
      ] ],
      [ "Configuration and Overrides", "md_docs_2_b_u_i_l_d.html#autotoc_md29", [
        [ "CMake Options", "md_docs_2_b_u_i_l_d.html#autotoc_md30", null ],
        [ "User Config File (DelegateMQConfig.h)", "md_docs_2_b_u_i_l_d.html#autotoc_md31", null ]
      ] ],
      [ "Platform Specifics", "md_docs_2_b_u_i_l_d.html#autotoc_md33", [
        [ "Embedded (ARM/Bare-Metal)", "md_docs_2_b_u_i_l_d.html#autotoc_md34", null ],
        [ "Windows / Linux", "md_docs_2_b_u_i_l_d.html#autotoc_md35", null ]
      ] ]
    ] ],
    [ "DelegateMQ — Technology Comparison", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html", [
      [ "Table of Contents", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md38", null ],
      [ "Signal / Slot Libraries", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md40", [
        [ "Qt Signals & Slots", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md41", null ],
        [ "Boost.Signals2", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md43", null ],
        [ "sigslot / nano-signal-slot", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md45", null ],
        [ "Signal/Slot Summary", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md47", null ]
      ] ],
      [ "Remote / IPC Communication", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md49", [
        [ "DataBus (DDS Lite)", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md50", null ],
        [ "DDS (Data Distribution Service)", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md52", null ],
        [ "gRPC", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md54", null ],
        [ "Raw ZeroMQ / NNG", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md56", null ],
        [ "Remote Summary", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md58", null ]
      ] ],
      [ "Asynchronous Callbacks and Thread Dispatch", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md60", [
        [ "std::async / std::future", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md61", null ],
        [ "OS Message Queues (FreeRTOS, Win32, POSIX)", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md63", null ],
        [ "Boost.Asio / Executors", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md65", null ],
        [ "Async Summary", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md67", null ]
      ] ],
      [ "Unified API: The Core Differentiator", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md69", null ]
    ] ],
    [ "DataBus", "md_docs_2_d_a_t_a_b_u_s.html", [
      [ "Table of Contents", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md71", null ],
      [ "Quick Start", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md72", null ],
      [ "Core Concepts", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md73", null ],
      [ "System Architecture", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md74", [
        [ "High-Level View", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md75", null ]
      ] ],
      [ "System Initialization", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md76", [
        [ "Threading (for Async Subscribers)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md77", null ],
        [ "Timer Loop (for QoS Features)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md78", null ],
        [ "Remote Node Polling (for Remote Distribution)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md79", null ],
        [ "Memory Allocator (Optional)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md80", null ]
      ] ],
      [ "Component Reference", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md81", [
        [ "DataBus (dmq::databus::DataBus)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md82", null ],
        [ "Participant (dmq::databus::Participant)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md83", null ],
        [ "Transport (dmq::transport::ITransport)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md84", null ],
        [ "NetworkNode (dmq::databus::NetworkNode)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md85", null ]
      ] ],
      [ "Internal Data Flow", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md86", null ],
      [ "Remote Integration", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md87", [
        [ "Setup Checklist", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md88", null ],
        [ "Relay Loop Hazard", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md89", null ]
      ] ],
      [ "Design Philosophy", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md90", null ],
      [ "Pub/Sub vs. RPC", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md91", null ],
      [ "Features", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md92", null ],
      [ "Threading and Performance", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md93", null ],
      [ "Quality of Service (QoS)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md94", null ],
      [ "Ordering and Guarantees", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md95", null ],
      [ "Examples", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md96", [
        [ "Local Pub/Sub", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md97", null ],
        [ "Async Dispatch (Thread)", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md98", null ],
        [ "Remote Distribution", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md99", null ],
        [ "Advanced: Cellutron System", "md_docs_2_d_a_t_a_b_u_s.html#autotoc_md100", null ]
      ] ]
    ] ],
    [ "Delegates in C++", "md_docs_2_d_e_t_a_i_l_s.html", [
      [ "Table of Contents", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md102", null ],
      [ "Introduction", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md103", null ],
      [ "Background", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md104", null ],
      [ "Overview", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md105", [
        [ "Mental Model", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md107", null ],
        [ "Invocation Modes", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md109", [
          [ "Synchronous", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md110", null ],
          [ "Asynchronous — Non-Blocking (Fire-and-Forget)", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md111", null ],
          [ "Asynchronous — Blocking (Wait for Result)", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md112", null ],
          [ "Lambdas", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md113", null ]
        ] ],
        [ "Publish / Subscribe with Signal", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md115", null ],
        [ "Remote Delegate", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md117", null ],
        [ "DataBus", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md119", null ],
        [ "Performance Monitoring", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md121", null ],
        [ "Async Public API Pattern", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md123", null ],
        [ "Delegate Invocation Semantics", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md124", null ]
      ] ],
      [ "Usage", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md125", [
        [ "Delegates", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md126", null ],
        [ "Delegate Containers", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md127", null ],
        [ "Synchronous Delegates", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md128", null ],
        [ "Asynchronous Delegates", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md129", [
          [ "Non-Blocking", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md130", null ],
          [ "Blocking", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md131", null ],
          [ "Message Priority", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md132", null ]
        ] ],
        [ "Remote Delegates", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md133", null ],
        [ "Error Handling", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md134", null ],
        [ "Debug Logging", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md135", null ],
        [ "Object Lifetime and Async Delegates", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md136", [
          [ "The Risk: Raw Pointers", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md137", null ],
          [ "The Solution: Shared Pointers", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md138", null ]
        ] ],
        [ "Register and Unregister", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md139", [
          [ "Init/Term Pattern", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md140", null ],
          [ "RAII Pattern", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md141", null ],
          [ "Object Lifetime Usage Guide", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md142", null ]
        ] ]
      ] ],
      [ "Design Details", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md143", [
        [ "Library Dependencies", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md144", null ],
        [ "Fixed-Block Memory Allocator", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md145", null ],
        [ "Function Argument Copy", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md146", null ],
        [ "Caution Using std::bind", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md147", null ],
        [ "Alternatives Considered", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md148", [
          [ "std::function", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md149", null ],
          [ "std::async and std::future", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md150", null ],
          [ "DelegateMQ vs. std::async Feature Comparisons", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md151", null ]
        ] ]
      ] ],
      [ "Library", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md152", [
        [ "Heap Template Parameter Pack", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md153", [
          [ "Argument Heap Copy", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md154", null ],
          [ "Bypassing Argument Heap Copy", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md155", null ],
          [ "Array Argument Heap Copy", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md156", null ]
        ] ]
      ] ],
      [ "Interfaces", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md157", null ],
      [ "Cross-Language Interoperability", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md158", [
        [ "Key Features", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md159", null ],
        [ "Synchronization", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md160", null ]
      ] ],
      [ "Porting Guide", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md161", null ],
      [ "Tests", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md162", [
        [ "Unit Tests", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md163", null ],
        [ "Stress Tests", "md_docs_2_d_e_t_a_i_l_s.html#autotoc_md164", null ]
      ] ]
    ] ],
    [ "DelegateMQ Cross-Language Interop", "md_docs_2_i_n_t_e_r_o_p.html", [
      [ "Architecture: Shared Native Core", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md166", [
        [ "Why this Architecture?", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md167", null ]
      ] ],
      [ "Platform Support", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md169", [
        [ "Desktop & Server", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md170", null ],
        [ "Embedded & RTOS", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md171", null ]
      ] ],
      [ "Native Interop DLL (native/)", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md173", [
        [ "C-API (DmqInterop.h)", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md174", null ],
        [ "Building the DLL", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md175", null ]
      ] ],
      [ "Transport Options", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md177", [
        [ "UDP (Default)", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md178", null ],
        [ "ZeroMQ (ZMQ)", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md179", null ]
      ] ],
      [ "Build Configuration", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md181", null ],
      [ "Data Synchronization & Schema Management", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md183", [
        [ "Strategy 1: Field Order Convention (Default)", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md184", null ],
        [ "Strategy 2: External IDL (e.g., Protobuf)", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md185", null ],
        [ "Strategy 3: Single-Source Code Generation", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md186", null ]
      ] ],
      [ "Error Handling", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md188", [
        [ "C# Usage", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md189", null ],
        [ "Python Usage", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md190", null ]
      ] ],
      [ "Language Support", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md192", [
        [ "C# / .NET", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md193", null ],
        [ "Python", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md194", null ]
      ] ],
      [ "Wire Protocol", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md196", [
        [ "8-Byte Binary Header", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md197", null ],
        [ "ACK Convention", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md198", null ]
      ] ],
      [ "Complete Demo", "md_docs_2_i_n_t_e_r_o_p.html#autotoc_md200", null ]
    ] ],
    [ "Porting Guide", "md_docs_2_p_o_r_t_i_n_g.html", [
      [ "Table of Contents", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md203", null ],
      [ "Porting Checklist", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md205", null ],
      [ "Embedded Systems", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md207", null ],
      [ "Interfaces", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md209", [
        [ "dmq::IThread", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md210", [
          [ "Send dmq::DelegateMsg", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md211", null ],
          [ "Receive dmq::DelegateMsg", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md212", null ]
        ] ],
        [ "dmq::ISerializer", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md214", null ],
        [ "dmq::IDispatcher", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md216", null ]
      ] ],
      [ "Thread Implementations", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md218", [
        [ "Thread Priority and Latency", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md219", null ],
        [ "Message Queueing", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md220", [
          [ "dmq::os::FullPolicy (Back Pressure / Drop)", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md221", null ]
        ] ],
        [ "Watchdog Integration", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md222", [
          [ "Priority Requirement — Critical on Single-Core RTOS", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md223", null ],
          [ "Watchdog Limitations", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md224", null ]
        ] ],
        [ "Performance Monitoring", "md_docs_2_p_o_r_t_i_n_g.html#autotoc_md225", null ]
      ] ]
    ] ],
    [ "Signal — Publish / Subscribe", "md_docs_2_s_i_g_n_a_l_s.html", [
      [ "Table of Contents", "md_docs_2_s_i_g_n_a_l_s.html#autotoc_md228", null ],
      [ "Basic Usage", "md_docs_2_s_i_g_n_a_l_s.html#autotoc_md230", null ],
      [ "Lambda Slots", "md_docs_2_s_i_g_n_a_l_s.html#autotoc_md232", null ],
      [ "Mixed Sync and Async Slots", "md_docs_2_s_i_g_n_a_l_s.html#autotoc_md234", null ],
      [ "When to use dmq::MulticastDelegateSafe instead", "md_docs_2_s_i_g_n_a_l_s.html#autotoc_md236", null ]
    ] ],
    [ "DelegateMQ Tutorial: From Local to Distributed", "md_docs_2_t_u_t_o_r_i_a_l.html", [
      [ "Tutorial Phases", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md238", null ],
      [ "Phase 1: The Direct Call (Synchronous)", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md240", null ],
      [ "Phase 2: The Worker Thread (Asynchronous)", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md242", null ],
      [ "Phase 3: The Observer (Signals)", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md244", [
        [ "Alternative: Manual Management", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md245", null ]
      ] ],
      [ "Phase 4: Periodic Tasks (Timers)", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md247", null ],
      [ "Phase 5: The Network (Remote Delegates)", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md249", null ],
      [ "Phase 6: The Middleware (DataBus)", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md251", [
        [ "High-Level Features:", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md252", null ]
      ] ],
      [ "Summary: Which to Use?", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md254", null ],
      [ "Next Steps", "md_docs_2_t_u_t_o_r_i_a_l.html#autotoc_md255", null ]
    ] ],
    [ "Allocator Suite", "md_src_2delegate-mq_2extras_2allocator_2_r_e_a_d_m_e.html", [
      [ "Features", "md_src_2delegate-mq_2extras_2allocator_2_r_e_a_d_m_e.html#autotoc_md257", null ],
      [ "Components", "md_src_2delegate-mq_2extras_2allocator_2_r_e_a_d_m_e.html#autotoc_md258", null ],
      [ "Basic Usage", "md_src_2delegate-mq_2extras_2allocator_2_r_e_a_d_m_e.html#autotoc_md259", [
        [ "Using XALLOCATOR in a class", "md_src_2delegate-mq_2extras_2allocator_2_r_e_a_d_m_e.html#autotoc_md260", null ],
        [ "Using STL containers", "md_src_2delegate-mq_2extras_2allocator_2_r_e_a_d_m_e.html#autotoc_md261", null ],
        [ "Using xmake_shared", "md_src_2delegate-mq_2extras_2allocator_2_r_e_a_d_m_e.html#autotoc_md262", null ]
      ] ],
      [ "Memory Alignment", "md_src_2delegate-mq_2extras_2allocator_2_r_e_a_d_m_e.html#autotoc_md263", null ],
      [ "Integration with DelegateMQ", "md_src_2delegate-mq_2extras_2allocator_2_r_e_a_d_m_e.html#autotoc_md264", null ]
    ] ],
    [ "DataBus", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html", [
      [ "Quickstart", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md266", null ],
      [ "Features", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md268", null ],
      [ "Basic Usage", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md269", [
        [ "Subscribing to a Topic", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md270", null ],
        [ "Publishing to a Topic", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md271", null ]
      ] ],
      [ "Multi-Process Quickstart — NetworkNode", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md273", [
        [ "Minimal two-node example", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md274", null ],
        [ "Key points", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md275", null ],
        [ "Template parameters", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md276", null ]
      ] ],
      [ "Internal Mechanics", "md_src_2delegate-mq_2extras_2databus_2_r_e_a_d_m_e.html#autotoc_md278", null ]
    ] ],
    [ "Dispatcher Layer", "md_src_2delegate-mq_2extras_2dispatcher_2_r_e_a_d_m_e.html", [
      [ "Overview", "md_src_2delegate-mq_2extras_2dispatcher_2_r_e_a_d_m_e.html#autotoc_md280", null ],
      [ "Key Components", "md_src_2delegate-mq_2extras_2dispatcher_2_r_e_a_d_m_e.html#autotoc_md281", [
        [ "Responsibilities", "md_src_2delegate-mq_2extras_2dispatcher_2_r_e_a_d_m_e.html#autotoc_md282", [
          [ "dmq::util::Dispatcher", "md_src_2delegate-mq_2extras_2dispatcher_2_r_e_a_d_m_e.html#autotoc_md283", null ],
          [ "dmq::RemoteChannel", "md_src_2delegate-mq_2extras_2dispatcher_2_r_e_a_d_m_e.html#autotoc_md284", null ]
        ] ]
      ] ]
    ] ],
    [ "Utility & Helper Layer", "md_src_2delegate-mq_2extras_2util_2_r_e_a_d_m_e.html", [
      [ "Functional Modules", "md_src_2delegate-mq_2extras_2util_2_r_e_a_d_m_e.html#autotoc_md286", [
        [ "Asynchronous Helpers", "md_src_2delegate-mq_2extras_2util_2_r_e_a_d_m_e.html#autotoc_md287", null ],
        [ "Timing & Scheduling", "md_src_2delegate-mq_2extras_2util_2_r_e_a_d_m_e.html#autotoc_md288", null ],
        [ "Reliability Layer (QoS)", "md_src_2delegate-mq_2extras_2util_2_r_e_a_d_m_e.html#autotoc_md289", null ],
        [ "Networking Logic", "md_src_2delegate-mq_2extras_2util_2_r_e_a_d_m_e.html#autotoc_md290", null ],
        [ "Monotonic Messaging and Time", "md_src_2delegate-mq_2extras_2util_2_r_e_a_d_m_e.html#autotoc_md291", null ],
        [ "System Utilities", "md_src_2delegate-mq_2extras_2util_2_r_e_a_d_m_e.html#autotoc_md292", null ]
      ] ],
      [ "Usage", "md_src_2delegate-mq_2extras_2util_2_r_e_a_d_m_e.html#autotoc_md293", null ]
    ] ],
    [ "OS / Threading Layer", "md_src_2delegate-mq_2port_2os_2_r_e_a_d_m_e.html", [
      [ "Implementations", "md_src_2delegate-mq_2port_2os_2_r_e_a_d_m_e.html#autotoc_md299", null ],
      [ "Configuration", "md_src_2delegate-mq_2port_2os_2_r_e_a_d_m_e.html#autotoc_md300", null ],
      [ "Custom Porting", "md_src_2delegate-mq_2port_2os_2_r_e_a_d_m_e.html#autotoc_md301", null ]
    ] ],
    [ "Serialization Layer", "md_src_2delegate-mq_2port_2serialize_2_r_e_a_d_m_e.html", [
      [ "Supported Serializers", "md_src_2delegate-mq_2port_2serialize_2_r_e_a_d_m_e.html#autotoc_md303", null ],
      [ "Embedded Tradeoffs", "md_src_2delegate-mq_2port_2serialize_2_r_e_a_d_m_e.html#autotoc_md304", [
        [ "Guidance", "md_src_2delegate-mq_2port_2serialize_2_r_e_a_d_m_e.html#autotoc_md305", null ]
      ] ]
    ] ],
    [ "MessageSerialize", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html", [
      [ "Features", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html#autotoc_md307", null ],
      [ "Stream Requirements", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html#autotoc_md308", null ],
      [ "Basic Usage", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html#autotoc_md309", [
        [ "Serializing Primitives", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html#autotoc_md310", null ],
        [ "Custom Serializable Objects", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html#autotoc_md311", null ],
        [ "STL Containers and Custom Allocators", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html#autotoc_md312", null ]
      ] ],
      [ "Advanced Features", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html#autotoc_md313", [
        [ "Versioning & Compatibility", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html#autotoc_md314", null ],
        [ "Pointer Containers", "md_src_2delegate-mq_2port_2serialize_2serialize_2_r_e_a_d_m_e.html#autotoc_md315", null ]
      ] ]
    ] ],
    [ "Transport Layer", "md_src_2delegate-mq_2port_2transport_2_r_e_a_d_m_e.html", [
      [ "Core Interfaces", "md_src_2delegate-mq_2port_2transport_2_r_e_a_d_m_e.html#autotoc_md317", null ],
      [ "Implementations", "md_src_2delegate-mq_2port_2transport_2_r_e_a_d_m_e.html#autotoc_md318", [
        [ "Network (IP-based)", "md_src_2delegate-mq_2port_2transport_2_r_e_a_d_m_e.html#autotoc_md319", null ],
        [ "IPC & Serial", "md_src_2delegate-mq_2port_2transport_2_r_e_a_d_m_e.html#autotoc_md320", null ]
      ] ],
      [ "Usage", "md_src_2delegate-mq_2port_2transport_2_r_e_a_d_m_e.html#autotoc_md321", null ]
    ] ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", null ],
        [ "Functions", "namespacemembers_func.html", null ],
        [ "Variables", "namespacemembers_vars.html", null ],
        [ "Typedefs", "namespacemembers_type.html", null ],
        [ "Enumerations", "namespacemembers_enum.html", null ]
      ] ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", null ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Enumerations", "functions_enum.html", null ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Functions", "globals_func.html", null ],
        [ "Variables", "globals_vars.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"_allocator_8cpp.html",
"_timer_delegate_8h.html#ada905f47a8832081bb61f4a9cb524e49",
"classdmq_1_1_delegate_free_remote_3_01_ret_type_07_args_8_8_8_08_4.html#a651408a9ac273d81d8d39712b4242c57",
"classdmq_1_1_delegate_member_3_01_t_class_00_01_ret_type_07_args_8_8_8_08_4.html#a9630317dc0673d283efb22fe897381ed",
"classdmq_1_1_delegate_member_remote_3_01_t_class_00_01_ret_type_07_args_8_8_8_08_4.html#abcd2b07d2b4eb21e7df69868cd29fe7e",
"classdmq_1_1_remote_arg_3_01_arg_01_5_01_4.html#aaffeafd0d89ade4b2b433cee1a75aa7c",
"classdmq_1_1databus_1_1detail_1_1_filter.html",
"classdmq_1_1os_1_1_thread.html#ac329d05b5bc16a24948ac295891f609c",
"classdmq_1_1transport_1_1_dmq_header.html#a2c6f5a01348c86d3bbb52399ba260e36",
"classdmq_1_1transport_1_1_serial_transport.html#a15fd5f52039179ab0a0a7646de6d1b4e",
"classdmq_1_1util_1_1_reliable_transport.html#ae760ac975c0337aebe4a92849502d0fd",
"freertos_2_thread_8h.html#ab586fc445563046cfef7bfed3d721030abf8f3be424eb6a72b21549fbb24ffb57",
"md_docs_2_p_o_r_t_i_n_g.html#autotoc_md218",
"namespacedmq_1_1os.html#ab586fc445563046cfef7bfed3d721030a893b3aaf1661e3717b18e8335ff93a72",
"structdmq_1_1is__shared__ptr_3_01std_1_1shared__ptr_3_01_t_01_4_01_6_01_4.html",
"xallocator_8h.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';