# PlatformIO extra script: put the FreeRTOS library's headers on the path of
# every build, DelegateMQ included.
#
# This example links DelegateMQ from this repository (lib_deps symlink), whose
# sample projects carry their own FreeRTOS headers. PlatformIO's dependency
# finder then thinks DelegateMQ provides FreeRTOS.h and doesn't give it the
# FreeRTOS library's include path. Not needed with
# lib_deps = endurodave/DelegateMQ: the registry package has no FreeRTOS copies.
import glob
import os

Import("env")

libdeps = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))
for header in glob.glob(os.path.join(libdeps, "*", "src", "FreeRTOS.h")):
    env.Append(CPPPATH=[os.path.dirname(header)])
