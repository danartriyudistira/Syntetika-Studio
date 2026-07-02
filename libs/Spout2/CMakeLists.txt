# Spout2 - GPU texture sharing SDK
# Header-only interface library (SpoutLibrary.h)
# SpoutLibrary.dll is loaded at runtime (GetProcAddress)

add_library(syntetika_spoutlibrary INTERFACE)
target_include_directories(syntetika_spoutlibrary INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(syntetika_spoutlibrary INTERFACE SPOUT_SUPPORT=1)
add_library(syntetika::spoutlibrary ALIAS syntetika_spoutlibrary)
