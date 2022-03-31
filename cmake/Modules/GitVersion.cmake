# See:
#   https://ipenguin.ws/2012/11/cmake-automatically-use-git-tags-as.html

execute_process(COMMAND git log --pretty=format:'%h' -n 1
                OUTPUT_VARIABLE GIT_REV
                ERROR_QUIET)

# Check whether we got any revision (which isn't
# always the case, e.g. when someone downloaded a zip
# file from Github instead of a checkout)
if ("${GIT_REV}" STREQUAL "")
    set(GIT_REV "N/A")
    set(GIT_DIFF "")
    set(GIT_TAG "N/A")
    set(GIT_BRANCH "N/A")
else()
    execute_process(
        COMMAND bash -c "git diff --quiet --exit-code"
        RESULT_VARIABLE GIT_DIFF)
    execute_process(
        COMMAND git describe --exact-match --tags
        OUTPUT_VARIABLE GIT_TAG ERROR_QUIET)
    execute_process(
        COMMAND git rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE GIT_BRANCH)
    execute_process(
        COMMAND git describe --tags --match v*
        OUTPUT_VARIABLE GIT_VERSION_TAG)

    string(REPLACE "'" "" GIT_REV "${GIT_REV}")
    string(STRIP "${GIT_TAG}" GIT_TAG)
    string(STRIP "${GIT_BRANCH}" GIT_BRANCH)
    string(STRIP "${GIT_VERSION_TAG}" GIT_VERSION_TAG)

    # Add date suffix if working directory is not clean
    if(GIT_DIFF EQUAL "1")
        set(GIT_DIRTY TRUE)
    else()
        set(GIT_DIRTY FALSE)
    endif()

    if(GIT_DIRTY)
        string(TIMESTAMP GIT_TIMESTAMP "%Y%m%d")
        set(VERSION_SUFFIX ".d${GIT_TIMESTAMP}")
    endif()

    string(REGEX MATCH "v(.*)-(.*)-(.*)" _ ${GIT_VERSION_TAG})
    if(_)
        set(GIT_VERSION_TAG ${CMAKE_MATCH_1})
        set(GIT_DISTANCE ${CMAKE_MATCH_2})
        set(GIT_REVHASH ${CMAKE_MATCH_3})

        set(GIT_VERSION "${GIT_VERSION_TAG}dev${GIT_DISTANCE}+${GIT_REVHASH}${VERSION_SUFFIX}")
        set(GIT_CMAKE_VERSION "${GIT_VERSION_TAG}")
    else()
      set(GIT_VERSION "${GIT_VERSION_TAG}${VERSION_SUFFIX}")
      set(GIT_CMAKE_VERSION "${GIT_VERSION_TAG}")
    endif()
endif()
