# Found at: https://github.com/omf2097/openomf/blob/master/cmake-scripts/Findconfuse.cmake

SET(CONFUSE_SEARCH_PATHS
    /usr/local/
    /usr
    /opt
)

FIND_PATH(CONFUSE_INCLUDE_DIR confuse.h 
    HINTS 
    PATH_SUFFIXES include
    PATHS ${CONFUSE_SEARCH_PATHS}
)
FIND_LIBRARY(CONFUSE_LIBRARY confuse 
    HINTS
    PATH_SUFFIXES lib64 lib bin
    PATHS ${CONFUSE_SEARCH_PATHS}
)

IF(CONFUSE_INCLUDE_DIR AND CONFUSE_LIBRARY)
   SET(CONFUSE_FOUND TRUE)
ENDIF (CONFUSE_INCLUDE_DIR AND CONFUSE_LIBRARY)


IF(CONFUSE_FOUND)
    MESSAGE(STATUS "Found libConfuse: ${CONFUSE_LIBRARY}")
ELSE(CONFUSE_FOUND)
    MESSAGE(WARNING "Could not find libConfuse")
ENDIF(CONFUSE_FOUND)
