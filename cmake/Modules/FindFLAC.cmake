find_path(FLAC_INCLUDE_DIR
          NAMES FLAC/all.h
          PATHS /usr/local/include
                /usr/include
)

find_library(FLAC_LIBRARY
             NAMES FLAC
             PATHS /usr/local/lib
                   /usr/lib
                   /usr/lib64
)

set(FLAC_LIBRARIES ${FLAC_LIBRARY})
set(FLAC_INCLUDE_DIRS ${FLAC_INCLUDE_DIR})

find_path(FLACXX_INCLUDE_DIR
          NAMES FLAC++/all.h
          PATHS /usr/local/include
                /usr/include
)

find_library(FLACXX_LIBRARY
             NAMES FLAC++
             PATHS /usr/local/lib
                   /usr/lib
                   /usr/lib64
)

set(FLACXX_LIBRARIES ${FLACXX_LIBRARY})
set(FLACXX_INCLUDE_DIRS ${FLACXX_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FLAC DEFAULT_MSG
                                  FLAC_LIBRARY
                                  FLACXX_LIBRARY
                                  FLAC_INCLUDE_DIR
                                  FLACXX_INCLUDE_DIR)

mark_as_advanced(FLAC_INCLUDE_DIR
                 FLAC_LIBRARY
                 FLACXX_INCLUDE_DIR
                 FLACXX_LIBRARY)
