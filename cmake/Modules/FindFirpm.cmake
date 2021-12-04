find_path(FIRPM_INCLUDE_DIR
          NAMES firpm/pm.h
          PATHS /usr/local/include
                /usr/include
)

find_library(FIRPM_LIBRARY
             NAMES firpm
             PATHS /usr/local/lib
                   /usr/lib
                   /usr/lib64
)

set(FIRPM_LIBRARIES ${FIRPM_LIBRARY})
set(FIRPM_INCLUDE_DIRS ${FIRPM_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Firpm DEFAULT_MSG
                                  FIRPM_LIBRARY
                                  FIRPM_INCLUDE_DIR)

mark_as_advanced(FIRPM_INCLUDE_DIR
                 FIRPM_LIBRARY)
