// DmqDataBus.cs — C# library for DelegateMQ cross-language interop.
//
// This module provides a high-level .NET interface for communicating with C++ 
// DelegateMQ applications. It uses a "Shared Native Core" architecture, wrapping 
// the native DmqInterop.dll via P/Invoke.
//
// Key Features:
// - Native Performance: Network handling and reliability logic run on a native thread.
// - Thread Safety: Callbacks are invoked on a native background thread.
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
    /// <summary>
    /// DmqDataBus — A thin C# wrapper around the native DmqInterop.dll.
    /// Manages the lifecycle of the transport and provides a high-level API for 
    /// sending and receiving MessagePack-serialized data.
    /// </summary>
    public sealed class DmqDataBus : IDisposable
    {
        private const string DllName = "DmqInterop.dll";

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void InternalMessageCallback(ushort remoteId, IntPtr data, uint len);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void InternalErrorCallback([MarshalAs(UnmanagedType.LPStr)] string msg);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern int DmqInterop_Start(string remoteHost, int recvPort, int sendPort);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern void DmqInterop_RegisterCallback(ushort remoteId, InternalMessageCallback cb);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern void DmqInterop_RegisterErrorCallback(InternalErrorCallback cb);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern int DmqInterop_Send(ushort remoteId, byte[] data, uint len);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern void DmqInterop_Stop();

        // Keep callback delegate alive to prevent GC collection
        private readonly InternalMessageCallback _internalCallback;
        private readonly InternalErrorCallback _internalErrorCallback;
        private readonly ConcurrentDictionary<ushort, Action<byte[]>> _callbacks = new();
        private Action<string>? _onError;
        private bool _disposed;

        public DmqDataBus()
        {
            _internalCallback = OnMessageReceived;
            _internalErrorCallback = OnErrorReceived;
        }

        /// <summary>
        /// Start the native transport.
        /// </summary>
        public void Start(string remoteHost, int recvPort, int sendPort)
        {
            int result = DmqInterop_Start(remoteHost, recvPort, sendPort);
            if (result != 0)
                throw new Exception($"Failed to start DmqInterop DLL (Error: {result})");
        }

        /// <summary>
        /// Stop the native transport and release all native resources.
        /// This must be called (directly or via Dispose) to ensure a clean shutdown.
        /// Note: This method blocks until the native receive thread has exited.
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
        /// Register a callback for internal library errors.
        /// </summary>
        public void RegisterErrorCallback(Action<string> onError)
        {
            _onError = onError;
            DmqInterop_RegisterErrorCallback(_internalErrorCallback);
        }

        /// <summary>
        /// Register a callback for a specific Remote ID.
        /// The payload is automatically unpacked into a byte array.
        /// </summary>
        public void RegisterCallback(ushort remoteId, Action<byte[]> callback)
        {
            _callbacks[remoteId] = callback;
            DmqInterop_RegisterCallback(remoteId, _internalCallback);
        }

        /// <summary>
        /// Register a callback and automatically deserialize the MessagePack payload.
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
        /// Send an object to a Remote ID using MessagePack serialization.
        /// </summary>
        public void Send<T>(ushort remoteId, T data)
        {
            // Note: Our Serializer.Pack in the original code wrapped items in a List.
            // For better interop, we can pack directly or keep the list convention.
            // Here we use the direct MessagePack serialization.
            byte[] bytes = MessagePackSerializer.Serialize(data);
            int result = DmqInterop_Send(remoteId, bytes, (uint)bytes.Length);
            if (result != 0)
                throw new Exception($"Failed to send message via DmqInterop DLL (Error: {result})");
        }

        private void OnMessageReceived(ushort remoteId, IntPtr data, uint len)
        {
            if (_callbacks.TryGetValue(remoteId, out var callback))
            {
                byte[] managedArray = new byte[len];
                Marshal.Copy(data, managedArray, 0, (int)len);
                callback(managedArray);
            }
        }

        private void OnErrorReceived(string msg)
        {
            _onError?.Invoke(msg);
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
