# PlatformIO extra script: enable RTTI for C++ files only.
#
# DelegateMQ needs RTTI, but most embedded cores compile C++ with -fno-rtti.
# -frtti in build_flags would also reach C files, where GCC warns that it is
# C++-only. This middleware appends -frtti last on each C++ compile (project
# and library sources), so it overrides the core's -fno-rtti; C files are left
# alone.
Import("env")

CXX_SUFFIXES = (".cpp", ".cc", ".cxx", ".c++")


def rtti_for_cxx(env, node):
    if not node.get_path().endswith(CXX_SUFFIXES):
        return node
    return env.Object(node, CCFLAGS=env["CCFLAGS"] + ["-frtti"])


env.AddBuildMiddleware(rtti_for_cxx)
