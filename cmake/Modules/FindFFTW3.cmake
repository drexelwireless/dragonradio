find_package(PkgConfig)
PKG_CHECK_MODULES(PC_FFTW3 "fftw3 >= 3.0")

FIND_PATH(
    FFTW3_INCLUDE_DIRS
    NAMES fftw3.h
    HINTS $ENV{FFTW3_DIR}/include
          ${PC_FFTW3_INCLUDE_DIR}
    PATHS /usr/local/include
          /usr/include
)

foreach(lib "" "f" "f_threads" "l" "q")
    foreach(variant "" "_threads" "_omp")
        string(TOUPPER "${lib}${variant}" LIB)

        FIND_LIBRARY(
            FFTW3${LIB}_LIBRARIES
            NAMES fftw3${lib}${variant} libfftw3${lib}${variant}
            HINTS $ENV{FFTW3_DIR}/lib
                  ${PC_FFTW3_LIBDIR}
            PATHS /usr/local/lib
                  /usr/lib
                  /usr/lib64
        )
    endforeach()
endforeach()

INCLUDE(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFTW3 DEFAULT_MSG
                                  FFTW3_LIBRARIES
                                  FFTW3_OMP_LIBRARIES
                                  FFTW3_THREADS_LIBRARIES
                                  FFTW3F_LIBRARIES
                                  FFTW3F_THREADS_LIBRARIES
                                  FFTW3F_THREADS_LIBRARIES
                                  FFTW3L_LIBRARIES
                                  FFTW3L_THREADS_LIBRARIES
                                  FFTW3L_THREADS_LIBRARIES
                                  FFTW3Q_LIBRARIES
                                  FFTW3Q_THREADS_LIBRARIES
                                  FFTW3Q_THREADS_LIBRARIES
                                  FFTW3_INCLUDE_DIRS)

MARK_AS_ADVANCED(FFTW3_INCLUDE_DIRS
                 FFTW3_LIBRARIES
                 FFTW3_OMP_LIBRARIES
                 FFTW3_THREADS_LIBRARIES
                 FFTW3F_LIBRARIES
                 FFTW3F_OMP_LIBRARIES
                 FFTW3F_THREADS_LIBRARIES
                 FFTW3L_LIBRARIES
                 FFTW3L_OMP_LIBRARIES
                 FFTW3L_THREADS_LIBRARIES
                 FFTW3Q_LIBRARIES
                 FFTW3Q_OMP_LIBRARIES
                 FFTW3Q_THREADS_LIBRARIES)
