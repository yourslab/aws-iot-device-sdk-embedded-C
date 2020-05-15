# Logging target.
add_library( logging 
             ${CMAKE_CURRENT_LIST_DIR}/posix/logging.c
             ${CMAKE_CURRENT_LIST_DIR}/posix/clock_posix.c )

# Logging target include path. 
target_include_directories( logging 
                            PUBLIC 
                                ${CMAKE_CURRENT_LIST_DIR}/include
                            PRIVATE
                                ${MODULES_DIR}/standard/utilities/include )
