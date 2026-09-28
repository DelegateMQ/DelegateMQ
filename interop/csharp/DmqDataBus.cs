// DmqDataBus.cs — C# library for DelegateMQ cross-language interop.
//
// This module provides a high-level .NET interface for communicating with C++
// DelegateMQ applications. It uses a "Shared Native Core" architecture, wrapping
// the native DmqInterop.dll via P/Invoke.
//
// Key Features:
// - Native Performance: Network handling and reliability logic run on native threads.
// - Reliable Sends: Each outgoing message is tracked until the peer ACKs it and
//   resent on timeout; its outcome is reported through the status callback.
// - Thread Safety: Callbacks are invoked on native background threads.
// - Consistency: Shares the same wire-protocol and reliability layer as the C++ core.
//
// Requirements:
// - DmqInterop.dll must be built and accessible in the application's output directory.
// - MessagePack NuGet package.
//
// Architecture Details: docs/INTEROP.md

using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using MessagePack;

namespace DelegateMQ.Interop
{
    /// <summary>Outcome of one outgoing message (mirrors DmqSendStatus in DmqInterop.h).</summary>
    public enum SendStatus
    {
        /// <summary>Peer acknowledged the message. Final.</summary>
        Acked = 0,
        /// <summary>An attempt timed out; a retry follows unless DeliveryFailed is reported next.</summary>
        Timeout = 1,
        /// <summary>Abandoned: retries exhausted or the send failed immediately. Final.</summary>
        DeliveryFailed = 2
    }

    /// <summary>Error categories (mirrors DmqErrorCode in DmqInterop.h).</summary>
    public enum DmqErrorCode
    {
        /// <summary>Native exception; the message has details.</summary>
        Exception = 1,
        /// <summary>Too many unacknowledged messages; sends fail until slots free.</summary>
        CapExceeded = 2,
        /// <summary>Timed-out messages piling up (link down or peer overloaded).</summary>
        PendingExceeded = 3,
        /// <summary>Library fault; the process aborts after the callback.</summary>
        Fault = 4,
        /// <summary>Thread watchdog expired; the process aborts after the callback.</summary>
        Watchdog = 5,
        /// <summary>An exception in a user callback or payload deserialization (raised by this wrapper).</summary>
        Callback = 6,
        /// <summary>A transport couldn't be created (bind/join failed).</summary>
        Transport = 7
    }

    /// <summary>
    /// DmqDataBus — A thin C# wrapper around the native DmqInterop.dll.
    /// Manages the lifecycle of the transport and provides a high-level API for
    /// sending and receiving MessagePack-serialized data.
    /// </summary>
    /// <remarks>
    /// Callbacks run on native threads. An exception thrown by a message or status
    /// callback (including MessagePack deserialization) is caught and reported to the
    /// error callback as <see cref="DmqErrorCode.Callback"/> -- an exception escaping
    /// into native code would otherwise terminate the process. Without an error
    /// callback, errors are written to stderr.
    /// </remarks>
    public sealed class DmqDataBus : IDisposable
    {
        private const string DllName = "DmqInterop.dll";

        // The leading IntPtr is the C API's context pointer. This wrapper passes
        // IntPtr.Zero: its delegates are instance methods that already carry state.
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void InternalMessageCallback(IntPtr context, ushort remoteId, IntPtr data, uint len);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void InternalStatusCallback(IntPtr context, ushort remoteId, ushort seqNum, int status);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void InternalErrorCallback(IntPtr context, int code, ushort remoteId, [MarshalAs(UnmanagedType.LPStr)] string msg);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern int DmqInterop_SetReliability(int enabled, int timeoutMs, int maxRetries);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern int DmqInterop_Start(string remoteHost, int recvPort, int sendPort, string multicastGroup);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern void DmqInterop_RegisterCallback(ushort remoteId, InternalMessageCallback? cb, IntPtr context);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern void DmqInterop_RegisterStatusCallback(InternalStatusCallback cb, IntPtr context);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern void DmqInterop_RegisterErrorCallback(InternalErrorCallback cb, IntPtr context);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern int DmqInterop_Send(ushort remoteId, byte[] data, uint len, out ushort seqNum);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern void DmqInterop_Stop();

        // Keep callback delegates alive to prevent GC collection
        private readonly InternalMessageCallback _internalCallback;
        private readonly InternalStatusCallback _internalStatusCallback;
        private readonly InternalErrorCallback _internalErrorCallback;
        private readonly ConcurrentDictionary<ushort, Action<byte[]>> _callbacks = new();
        private Action<ushort, ushort, SendStatus>? _onStatus;
        private Action<DmqErrorCode, ushort, string>? _onError;
        private bool _disposed;

        // .NET's default P/Invoke resolution on Linux/macOS applies a "lib" prefix
        // but keeps the literal extension given in [DllImport] -- it does NOT map
        // ".dll" to ".so"/".dylib". DllName above stays "DmqInterop.dll" (matches
        // the actual build artifact on Windows), so a custom resolver is needed to
        // find the real file on other platforms.
        static DmqDataBus()
        {
            NativeLibrary.SetDllImportResolver(typeof(DmqDataBus).Assembly, ResolveNativeLibrary);
        }

        private static IntPtr ResolveNativeLibrary(string libraryName, System.Reflection.Assembly assembly, DllImportSearchPath? searchPath)
        {
            if (libraryName != DllName)
                return IntPtr.Zero;

            string platformName =
                RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ? "libDmqInterop.so" :
                RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "libDmqInterop.dylib" :
                libraryName;

            return NativeLibrary.TryLoad(platformName, assembly, searchPath, out IntPtr handle) ? handle : IntPtr.Zero;
        }

        public DmqDataBus()
        {
            _internalCallback = OnMessageReceived;
            _internalStatusCallback = OnStatusReceived;
            _internalErrorCallback = OnErrorReceived;

            // Always registered, so native errors are reported (to stderr by default)
            // even before the application registers its own handlers.
            DmqInterop_RegisterErrorCallback(_internalErrorCallback, IntPtr.Zero);
            DmqInterop_RegisterStatusCallback(_internalStatusCallback, IntPtr.Zero);
        }

        /// <summary>
        /// Configure outgoing reliability. Call before <see cref="Start"/>.
        /// </summary>
        /// <param name="enabled">True (default) tracks ACKs and retries; false sends
        /// fire-and-forget with no status callbacks (for peers that don't ACK).</param>
        /// <param name="timeoutMs">Time to wait for an ACK per attempt; &lt;= 0 keeps the default (2 s).</param>
        /// <param name="maxRetries">Resends after the first attempt; &lt; 0 keeps the default (3),
        /// 0 tracks ACKs without resending.</param>
        public void SetReliability(bool enabled = true, int timeoutMs = 0, int maxRetries = -1)
        {
            if (DmqInterop_SetReliability(enabled ? 1 : 0, timeoutMs, maxRetries) != 0)
                throw new InvalidOperationException("SetReliability must be called before Start.");
        }

        /// <summary>
        /// Start the native transport.
        /// </summary>
        /// <param name="remoteHost">IP address of the remote peer (used for the unicast send/command channel).</param>
        /// <param name="recvPort">Local port to join for incoming messages.</param>
        /// <param name="sendPort">Remote port to send messages to (ACKs come back on the same socket).</param>
        /// <param name="multicastGroup">Multicast group address to join for the incoming channel, so
        /// multiple clients can receive the same stream concurrently.</param>
        public void Start(string remoteHost, int recvPort, int sendPort, string multicastGroup)
        {
            int result = DmqInterop_Start(remoteHost, recvPort, sendPort, multicastGroup);
            if (result != 0)
                throw new Exception($"Failed to start DmqInterop DLL (Error: {result})");
        }

        /// <summary>
        /// Stop the native transport and release all native resources.
        /// This must be called (directly or via Dispose) to ensure a clean shutdown.
        /// Note: This method blocks until the native threads have exited.
        /// </summary>
        public void Stop()
        {
            if (!_disposed)
            {
                DmqInterop_Stop();
                _disposed = true;
                GC.SuppressFinalize(this);
            }
        }

        /// <summary>
        /// Register a callback for errors: (code, remoteId or 0, message).
        /// </summary>
        public void RegisterErrorCallback(Action<DmqErrorCode, ushort, string> onError)
        {
            _onError = onError;
        }

        /// <summary>
        /// Register a callback for per-message delivery status: (remoteId, seqNum, status).
        /// seqNum matches the value returned by <see cref="Send(ushort, byte[])"/>.
        /// </summary>
        public void RegisterStatusCallback(Action<ushort, ushort, SendStatus> onStatus)
        {
            _onStatus = onStatus;
        }

        /// <summary>
        /// Register a callback for a specific Remote ID.
        /// The payload is automatically unpacked into a byte array.
        /// </summary>
        public void RegisterCallback(ushort remoteId, Action<byte[]> callback)
        {
            _callbacks[remoteId] = callback;
            DmqInterop_RegisterCallback(remoteId, _internalCallback, IntPtr.Zero);
        }

        /// <summary>
        /// Register a callback and automatically deserialize the MessagePack payload.
        /// A payload that fails to deserialize is reported to the error callback.
        /// </summary>
        public void RegisterCallback<T>(ushort remoteId, Action<T> callback)
        {
            RegisterCallback(remoteId, (data) =>
            {
                var obj = MessagePackSerializer.Deserialize<T>(data);
                callback(obj);
            });
        }

        /// <summary>
        /// Stop delivering messages for a Remote ID.
        /// </summary>
        public void UnregisterCallback(ushort remoteId)
        {
            _callbacks.TryRemove(remoteId, out _);
            DmqInterop_RegisterCallback(remoteId, null, IntPtr.Zero);
        }

        /// <summary>
        /// Send an object to a Remote ID using MessagePack serialization.
        /// </summary>
        /// <returns>The message's sequence number, matching later status callbacks.</returns>
        public ushort Send<T>(ushort remoteId, T data)
        {
            byte[] bytes = MessagePackSerializer.Serialize(data);
            return Send(remoteId, bytes);
        }

        /// <summary>
        /// Send a raw byte array to a Remote ID.
        /// </summary>
        /// <returns>The message's sequence number, matching later status callbacks.</returns>
        public ushort Send(ushort remoteId, byte[] data)
        {
            int result = DmqInterop_Send(remoteId, data, (uint)data.Length, out ushort seqNum);
            if (result != 0)
                throw new Exception($"Failed to send message via DmqInterop DLL (Error: {result})");
            return seqNum;
        }

        private void OnMessageReceived(IntPtr context, ushort remoteId, IntPtr data, uint len)
        {
            if (!_callbacks.TryGetValue(remoteId, out var callback))
                return;
            try
            {
                byte[] managedArray = new byte[len];
                Marshal.Copy(data, managedArray, 0, (int)len);
                callback(managedArray);
            }
            catch (Exception ex)
            {
                ReportError(DmqErrorCode.Callback, remoteId, $"Exception in callback for remote ID {remoteId}: {ex}");
            }
        }

        private void OnStatusReceived(IntPtr context, ushort remoteId, ushort seqNum, int status)
        {
            try
            {
                _onStatus?.Invoke(remoteId, seqNum, (SendStatus)status);
            }
            catch (Exception ex)
            {
                ReportError(DmqErrorCode.Callback, remoteId, $"Exception in status callback: {ex}");
            }
        }

        private void OnErrorReceived(IntPtr context, int code, ushort remoteId, string msg)
        {
            ReportError((DmqErrorCode)code, remoteId, msg ?? string.Empty);
        }

        // Never throws: runs on native threads, where an escaping exception kills the process.
        private void ReportError(DmqErrorCode code, ushort remoteId, string message)
        {
            var onError = _onError;
            if (onError != null)
            {
                try
                {
                    onError(code, remoteId, message);
                    return;
                }
                catch (Exception ex)
                {
                    message += $" (error callback threw: {ex})";
                }
            }
            try
            {
                Console.Error.WriteLine($"DmqDataBus error {code} (remote ID {remoteId}): {message}");
            }
            catch
            {
                // Nothing left to report to.
            }
        }

        /// <summary>
        /// Disposes the transport resources.
        /// User must call this to ensure native threads are joined safely.
        /// </summary>
        public void Dispose()
        {
            Stop();
        }
    }
}
