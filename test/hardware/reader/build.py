Import("env")
from pathlib import Path
root=Path(env.subst("$PROJECT_DIR"))
env.Append(CPPPATH=[str(root/p) for p in (
    'test/hardware/reader','src','lib/hal','lib/Logging','sdk/driver',
    'lib/NativeApps/include','.pio/libdeps/x4-ci-staging/ArduinoJson/src',
    '.pio/libdeps/x4-ci-staging/SdFat/src')])
env.Append(LIBS=[env.BuildLibrary("$BUILD_DIR/ci-reader-staging",str(root/'test/hardware/reader'),src_filter="+<SerialStaging.cpp>")])
