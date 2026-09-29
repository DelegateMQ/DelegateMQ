Follow the steps below to execute the projects.

1. Setup sandbox environment for external library dependencies (see [Example Ecosystem (Sandbox)](../../../docs/BUILD.md#example-ecosystem-sandbox) in `BUILD.md`). 
2. Execute CMake command in `pub` and `sub` directories.  
   `cmake -B build .`
3. Build client and server applications within the `build` directory.
4. Start `delegate_pub_app` first.
5. Start `delegate_sub_app` second.
6. Client and server communicate and output debug data to the console.

