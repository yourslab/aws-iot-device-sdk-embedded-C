# Include filepaths for source and include.
include(transportFilePaths.cmake)

# Create target for POSIX implementation of TCP.
add_library( tcp_posix
             ${TCP_TRANSPORT_SOURCES} )

target_include_directories( tcp_posix PUBLIC 
                                        ${COMMON_TRANSPORT_INCLUDE_PUBLIC_DIRS}
                                        ${LOGGING_INCLUDE_DIRS} )

# Create target for POSIX implementation of OpenSSL.
add_library( openssl_posix
             ${OPENSSL_TRANSPORT_SOURCES} )

target_include_directories( openssl_posix PUBLIC 
                                            ${COMMON_TRANSPORT_INCLUDE_PUBLIC_DIRS}
                                            ${LOGGING_INCLUDE_DIRS} )

# Create target for POSIX implementation for reconnect.
add_library( reconnect_posix
             ${RECONNECT_SOURCES} )

target_include_directories( reconnect_posix PUBLIC 
                                        ${COMMON_TRANSPORT_INCLUDE_PUBLIC_DIRS} )