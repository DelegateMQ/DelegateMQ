// Program.cs — C# DataBus interop client.
//
// Receives DataMsg (sensor/actuator data) from and sends CommandMsg (polling
// rate) to the C++ DataBus Interop Server in ../server/.
//
// Uses the generic DmqDataBus library from interop/csharp/.
//
// Requirements:
//   dotnet add package MessagePack   (or restore from DataBusInteropClient.csproj)
//
// Start the C++ server first, then:
//   dotnet run [duration_seconds]

using System;
using System.Threading;
using DelegateMQ.Interop;

// ---------------------------------------------------------------------------
// Configuration — must match C++ server (SystemIds.h, CMakeLists.txt ports)
// ---------------------------------------------------------------------------
const string ServerHost   = "127.0.0.1";
const int    DataRecvPort = 8000;    // C++ server PUBs DataMsg here  — we bind
const int    CmdSendPort  = 8001;    // C++ server SUBs CommandMsg here — we send

const ushort DataMsgId = 100;        // SystemTopic::DataMsgId
const ushort CmdMsgId  = 101;        // SystemTopic::CommandMsgId

int duration = args.Length > 0 ? int.Parse(args[0]) : 30;

// ---------------------------------------------------------------------------
// Callback — invoked on the DmqDataBus receive thread
// ---------------------------------------------------------------------------
static void OnDataMsg(byte[] payload)
{
    // DataMsg: MSGPACK_DEFINE(actuators, sensors)
    // Wire:    [ [[id, position, voltage], ...], [[id, supplyV, readingV], ...] ]
    dynamic[] fields    = Serializer.Unpack(payload);
    object[]  actuators = (object[])fields[0];
    object[]  sensors   = (object[])fields[1];

    Console.WriteLine($"[RECV] DataMsg: {actuators.Length} actuator(s), {sensors.Length} sensor(s)");
    foreach (object[] a in actuators)
        Console.WriteLine($"  Actuator  id={a[0]}  pos={a[1]}  voltage={a[2]}V");
    foreach (object[] s in sensors)
        Console.WriteLine($"  Sensor    id={s[0]}  supply={s[1]}V  reading={s[2]}V");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
using var bus = new DmqDataBus();
bus.RegisterCallback(DataMsgId, OnDataMsg);
bus.Start(ServerHost, DataRecvPort, CmdSendPort);

Console.WriteLine($"DataBus C# Client — connected to {ServerHost}");
Console.WriteLine($"  Receiving DataMsg   on port {DataRecvPort}");
Console.WriteLine($"  Sending  CommandMsg  on port {CmdSendPort}");
Console.WriteLine($"  Duration: {duration}s");

// Give the server a moment before sending the first command
Thread.Sleep(1000);

// CommandMsg: MSGPACK_DEFINE(pollingRateMs) — request 500ms polling interval
bus.Send(CmdMsgId, Serializer.Pack(500));
Console.WriteLine("[SEND] CommandMsg: pollingRateMs=500");

Thread.Sleep((duration - 1) * 1000);

Console.WriteLine("Done.");
