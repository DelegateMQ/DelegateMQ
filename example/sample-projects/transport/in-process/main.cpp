/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#include "DelegateMQ.h"
#include <iostream>
#include <cstdint>
#include <mutex>
#include <string>
#include <chrono>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;
 
std::atomic<bool> processTimerExit = false;
static void ProcessTimers()
{
    while (!processTimerExit.load())
    {
        // Process all delegate-based timers
        Timer::ProcessTimers();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

// The "transport" is simply a shared byte buffer
class Transport
{
public:
    static void Send(const char* data, size_t size) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_buffer.append(data, size);
    }
    // Take everything sent so far (an isolated copy)
    static std::string Receive() {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::string temp;
        temp.swap(m_buffer);
        return temp;
    }
private:
    static std::string m_buffer;
    static std::mutex m_mutex;
};

std::string Transport::m_buffer;
std::mutex Transport::m_mutex;

// Dispatcher sends data to the transport for transmission to the receiver
class BareMetalDispatcher : public IDispatcher
{
public:
    // Send argument data to the transport
    virtual int Dispatch(dmq::ByteWriter& os, DelegateRemoteId id, uint16_t* outSeqNum = nullptr) override {
        Transport::Send(os.data(), os.size());
        os.reset();   // empty the buffer for the next message
        return 0;
    }
};

class Data
{
public:
    int x;
    int y;
    std::string msg;
};

// A hand-written binary format: int32 as 4 raw bytes (sender and receiver
// share the byte order here), a string as a 16-bit length then its bytes.
// Use any serialization scheme as necessary, or a ready-made serializer
// (port/serialize).
inline void WriteValue(dmq::ByteWriter& os, int32_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
inline void WriteValue(dmq::ByteWriter& os, const std::string& s) {
    if (s.size() > UINT16_MAX) { os.setfail(); return; }
    const uint16_t len = static_cast<uint16_t>(s.size());
    os.write(reinterpret_cast<const char*>(&len), sizeof(len));
    os.write(s.data(), s.size());
}
inline void WriteValue(dmq::ByteWriter& os, const Data& data) {
    WriteValue(os, data.x);
    WriteValue(os, data.y);
    WriteValue(os, data.msg);
}

inline void ReadValue(dmq::ByteReader& is, int32_t& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(v));   // short data sets fail()
}
inline void ReadValue(dmq::ByteReader& is, std::string& s) {
    uint16_t len = 0;
    is.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (is.fail() || len > is.remaining()) { is.setfail(); return; }
    s.assign(is.data() + is.tellg(), len);
    is.skip(len);
}
inline void ReadValue(dmq::ByteReader& is, Data& data) {
    ReadValue(is, data.x);
    ReadValue(is, data.y);
    ReadValue(is, data.msg);
}

template <class R>
struct Serializer; // Not defined

// Serialize all target function argument data
template<class RetType, class... Args>
class Serializer<RetType(Args...)> : public ISerializer<RetType(Args...)>
{
public:
    virtual dmq::ByteWriter& Write(dmq::ByteWriter& os, const Args&... args) override {
        (WriteValue(os, args), ...);   // C++17 fold: each argument in order
        return os;
    }

    virtual dmq::ByteReader& Read(dmq::ByteReader& is, Args&... args) override {
        (ReadValue(is, args), ...);
        return is;
    }
};

// Sender is an active object with a thread. The thread sends data to the 
// remote every time the timer expires. 
class Sender
{
public:
    Sender(DelegateRemoteId id) :
        m_thread("Sender"),
        m_sendDelegate(id)
    {
        // Set the delegate interfaces
        m_sendDelegate.SetStream(&m_args);
        m_sendDelegate.SetSerializer(&m_serializer);
        m_sendDelegate.SetDispatcher(&m_dispatcher);

        m_thread.CreateThread();

        // Start a timer to send data
        m_sendTimerConn = m_sendTimer.OnExpired.Connect(MakeDelegate(this, &Sender::Send, m_thread));
        m_sendTimer.Start(std::chrono::milliseconds(50));
    }

    ~Sender()
    {
        m_sendTimerConn.Disconnect();
        m_sendTimer.Stop();
        m_thread.ExitThread();
    }

    // Send data to the remote
    void Send()
    {
        static int cnt = 0;

        Data data;
        data.x = cnt++;
        data.y = cnt++;
        data.msg = "Hello!";

        m_sendDelegate(data);
    }

private:
    Thread m_thread;
    Timer m_sendTimer;

    dmq::ByteWriter m_args;
    BareMetalDispatcher m_dispatcher;
    Serializer<void(Data&)> m_serializer;
    dmq::ScopedConnection m_sendTimerConn;

    // Sender remote delegate
    DelegateMemberRemote<Sender, void(Data&)> m_sendDelegate;
};

// Receiver receives data from the Sender 
class Receiver
{
public:
    Receiver(DelegateRemoteId id) :
        m_thread("Receiver")
    {
        m_recvDelegate = MakeDelegate(this, &Receiver::DataUpdate, id);

        // Set the delegate interfaces
        m_recvDelegate.SetStream(&m_args);
        m_recvDelegate.SetSerializer(&m_serializer);

        // Register the Error Handler. 
        // This prevents the delegate from throwing std::runtime_error on failure.
        m_recvDelegate.SetErrorHandler(MakeDelegate(this, &Receiver::ErrorHandler));

        m_thread.CreateThread();

        // Start a timer to poll data
        m_recvTimerConn = m_recvTimer.OnExpired.Connect(MakeDelegate(this, &Receiver::Poll, m_thread));
        m_recvTimer.Start(std::chrono::milliseconds(50));
    }

    ~Receiver()
    {
        m_recvTimerConn.Disconnect();
        m_thread.ExitThread();
        m_recvDelegate = nullptr;
    }

    // Poll called periodically on m_thread context
    void Poll()
    {
        // Get incoming data
        const std::string received = Transport::Receive();
        dmq::ByteReader arg_data(received.data(), received.size());

        // Incoming remote delegate data arrived?
        if (arg_data.size() > 0)
        {
            // Try/Catch Block
            // Protect the thread loop from crashing on bad data
            try
            {
                // Invoke the receiver target function with the sender's argument data
                m_recvDelegate.Invoke(arg_data);
            }
            catch (const std::exception& e)
            {
                std::cerr << "[Exception] Receiver caught: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "[Exception] Receiver caught unknown error." << std::endl;
            }
        }
    }

    // Receiver target function called when sender remote delegate is invoked
    void DataUpdate(Data& data)
    {
        cout << "Receiver::DataUpdate: " << data.x;
        cout << " " << data.y;
        cout << " " << data.msg;
        cout << endl;
    }

    // Handle DelegateMQ internal errors
    void ErrorHandler(DelegateRemoteId id, DelegateError err, DelegateErrorAux aux)
    {
        // Log error but DO NOT throw.
        std::cerr << "[Error] Receiver ID: " << id << " Code: " << (int)err << std::endl;
    }

private:
    Thread m_thread;
    Timer m_recvTimer;

    dmq::ByteWriter m_args;
    Serializer<void(Data&)> m_serializer;
    dmq::ScopedConnection m_recvTimerConn;

    // Receiver remote delegate
    DelegateMemberRemote<Receiver, void(Data&)> m_recvDelegate;
};

int main(int argc, char* argv[])
{
    int duration = 5;
    if (argc > 1) duration = atoi(argv[1]);

    DelegateRemoteId id = 1;

    // Start the thread that will run ProcessTimers
    std::thread timerThread(ProcessTimers);

    Sender sender(id);
    Receiver receiver(id);

    // Wait here while sender and receiver communicate
    this_thread::sleep_for(chrono::seconds(duration));

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable()) 
        timerThread.join();  

    return 0;
}