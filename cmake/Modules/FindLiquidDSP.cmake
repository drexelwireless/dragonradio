find_path(LIQUIDDSP_INCLUDE_DIR
          NAMES liquid/liquid.h
          PATHS /usr/local/include
                /usr/include
)

find_library(LIQUIDDSP_LIBRARY
             NAMES liquid
             PATHS /usr/local/lib
                   /usr/lib
                   /usr/lib64
)

set(LIQUIDDSP_LIBRARIES ${LIQUIDDSP_LIBRARY})
set(LIQUIDDSP_INCLUDE_DIRS ${LIQUIDDSP_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LiquidDSP DEFAULT_MSG
                                  LIQUIDDSP_LIBRARY
                                  LIQUIDDSP_INCLUDE_DIR)

mark_as_advanced(LIQUIDDSP_INCLUDE_DIR
                 LIQUIDDSP_LIBRARY)
