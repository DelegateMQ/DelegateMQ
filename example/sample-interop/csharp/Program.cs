// Program.cs — C# client for DelegateMQ cross-language interop demonstration.
//
// This application receives sensor data from and sends commands to a C++ server 
// using the DelegateMQ native core DLL and the DmqDataBus C# wrapper.
//
// Requirements:
// - DmqInterop.dll must be built and copied to the application output directory.
// - MessagePack NuGet package.

using System;
using System.Threading;
using DelegateMQ.Interop;
using MessagePack;

namespace CsharpSample
{
    // 1. Define Data Structures (Must match C++ MSGPACK_DEFINE order)
    [MessagePackObject]
    public class SensorData
    {
        [Key(0)] public int Id { get; set; }
        [Key(1)] public float Value { get; set; }
    }

    [MessagePackObject]
    public class Command
    {
        [Key(0)] public int PollingRateMs { get; set; }
    }

    class Program
    {
        // Configuration: Match these with the C++ Server settings
        const string ServerHost = "127.0.0.1";
        const int DataRecvPort = 8000; // Server's PUB port
        const int CmdSendPort = 8001;  // Server's SUB port

        // Topic IDs (Must match C++ and Python definitions)
        const ushort SensorDataId = 100;
        const ushort CommandId = 101;

        static void Main(string[] args)
        {
            Console.WriteLine("Starting C# Interop Sample...");

            // 2. Initialize the DataBus wrapper
            // Note: DmqDataBus is IDisposable and will shut down native threads when disposed.
            using var bus = new DmqDataBus();

            // 3. Setup: Register interest and start transport
            // The wrapper handles MessagePack deserialization automatically.
            bus.RegisterCallback<SensorData>(SensorDataId, (data) =>
            {
                Console.WriteLine($"[RECV] SensorData: id={data.Id} val={data.Value}");
            });

            try
            {
                // Start the background native receive loop
                bus.Start(ServerHost, DataRecvPort, CmdSendPort);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR: {ex.Message}");
                return;
            }

            // 4. Main Loop: Toggle server polling rate every 5 seconds
            Console.WriteLine("Running polling rate toggle loop (Press Ctrl+C to exit)...");
            int pollingRate = 250;
            while (true)
            {
                var cmd = new Command { PollingRateMs = pollingRate };
                Console.WriteLine($"[SEND] Command: pollingRateMs={cmd.PollingRateMs}");
                
                // Send serialized command to the C++ server via the DLL
                bus.Send(CommandId, cmd);

                // Toggle between 250 and 1000ms
                pollingRate = (pollingRate == 250) ? 1000 : 250;

                Thread.Sleep(5000);
            }
        }
    }
}
