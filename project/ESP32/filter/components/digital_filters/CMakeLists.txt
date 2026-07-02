idf_component_register(SRCS "digital_filters.c"
    INCLUDE_DIRS "include")

# Enable aggressive optimisation for the filter hot-path.
# The Xtensa LX7 benefits greatly from loop unrolling and auto-vectorisation.
target_compile_options(${COMPONENT_LIB}
    PRIVATE
    -O3
    -funroll-loops
)

target_compile_options(${COMPONENT_LIB}
    PRIVATE
    -save-temps=obj
)
