find_path(CAP_INCLUDE_DIR
          NAMES sys/capability.h
          PATHS /usr/local/include
                /usr/include
)

find_library(CAP_LIBRARY
             NAMES cap
             PATHS /usr/local/lib
                   /usr/lib
                   /usr/lib64
)

set(CAP_LIBRARIES ${CAP_LIBRARY})
set(CAP_INCLUDE_DIRS ${CAP_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Cap DEFAULT_MSG
                                  CAP_LIBRARY
                                  CAP_INCLUDE_DIR)

mark_as_advanced(CAP_INCLUDE_DIR
                 CAP_LIBRARY)
