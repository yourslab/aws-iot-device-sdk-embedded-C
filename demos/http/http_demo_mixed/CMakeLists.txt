set( DEMO_NAME "http_demo_mixed" )
# Demo target.
add_executable(${DEMO_NAME})

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

# Verify the minimum OpenSSL version required
if( OPENSSL_VERSION MATCHES "0.9$" OR OPENSSL_VERSION MATCHES "1.0$"  )
    message( FATAL_ERROR "OpenSSL 1.1.0 or later required: OpenSSL ${OPENSSL_VERSION} found." )
endif()

set( CMAKE_THREAD_PREFER_PTHREAD ON )
find_package(Threads REQUIRED)

include(CheckIPOSupported)
check_ipo_supported(RESULT supported OUTPUT error)

if( supported )
    message(STATUS "IPO / LTO enabled")
    set_property(TARGET ${DEMO_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif()

add_library(
    openssl_app "src/openssl_app.c"
)

target_include_directories(
    openssl_app
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(
    openssl_app
    PRIVATE
        http
        openssl_posix
        OpenSSL::Crypto
        OpenSSL::SSL
	# SSL uses Threads and on some platforms require explicit linking.
        Threads::Threads
	# SSL uses Dynamic Loading and on some platforms requires explicit linking.
        ${CMAKE_DL_LIBS}
)

add_library(
    plaintext_app "src/plaintext_app.c"
)

target_include_directories(
    plaintext_app
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(
    plaintext_app
    PRIVATE
        http
        plaintext_posix
)

target_sources(
    ${DEMO_NAME}
    PRIVATE
        "${DEMO_NAME}.c"
)

target_link_libraries(
    ${DEMO_NAME}
    PRIVATE
        openssl_app
        plaintext_app
)

target_include_directories(
    http
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_include_directories(
    ${DEMO_NAME}
    PUBLIC
        ${LOGGING_INCLUDE_DIRS}
)

# Download the Amazon Root CA certificate
message( "Downloading the Amazon Root CA certificate..." )
file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/certificates)
execute_process(
    COMMAND curl --url https://www.amazontrust.com/repository/AmazonRootCA1.pem 
    -o ${CMAKE_CURRENT_LIST_DIR}/certificates/AmazonRootCA1.crt
)

#Copy the certificates and client key to the binary directory.
add_custom_command(
    TARGET
        ${DEMO_NAME}
    POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_LIST_DIR}/certificates"
        "$<TARGET_FILE_DIR:${DEMO_NAME}>/certificates"
)
