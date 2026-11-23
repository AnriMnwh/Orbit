# core/, ui/ and apps/ are portable and also build into the desktop simulator
# (sim/). platform/ is the watch-only hardware layer.
file(GLOB_RECURSE ORBIT_SRCS
    "core/*.c" "ui/*.c" "apps/*.c" "assets/gen/*.c" "platform/*.c")

idf_component_register(
    SRCS ${ORBIT_SRCS}
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
