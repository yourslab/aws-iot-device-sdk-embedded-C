/*
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* Standard includes. */
#include <stdio.h>
#include <stdint.h>

/* POSIX socket includes. */
#include <netdb.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>

#include "http_client.h"

/**
 * @brief HTTP server host name.
 *
 * This demo uses the Mosquitto test server. This is a public MQTT server; do not
 * publish anything sensitive to this server.
 */
#define HTTP_DEMO_SERVER                    "httpbin.org"

/**
 * @brief HTTP server port number.
 *
 * In general, port 80 is for plaintext HTTP connections.
 */
#define HTTP_DEMO_PORT                      ( 80 )

/**
 * @brief The length in bytes of the buffer used to store request context.
 */
#define HTTP_DEMO_REQUEST_BUFFER_LENGTH     ( 1024 )

/**
 * @brief The length in bytes of the buffer used to store request context.
 */
#define HTTP_DEMO_RESPONSE_BUFFER_LENGTH    ( 1024 )

/**
 * @brief Paths for different HTTP methods for specified host.
 *
 * For httpbin.org, see http://httpbin.org/#/HTTP_Methods for details on
 * supported REST API.
 */
#define HTTP_DEMO_GET_PATH                  "/ip"
#define HTTP_DEMO_HEAD_PATH                 "/ip"
#define HTTP_DEMO_PUT_PATH                  "/put"
#define HTTP_DEMO_POST_PATH                 "/post"

/*-----------------------------------------------------------*/

/**
 * @brief Establish a TCP connection to the given server.
 *
 * @param[in] pServer Host name of server.
 * @param[in] port Server port.
 *
 * @return A file descriptor representing the TCP socket; -1 on failure.
 */
static int connectToServer( const char * pServer,
                            uint16_t port )
{
    int status, tcpSocket = -1;
    struct addrinfo * pListHead = NULL, * pIndex;
    struct sockaddr * pServerInfo;
    uint16_t netPort = htons( port );
    socklen_t serverInfoLength;

    /* Perform a DNS lookup on the given host name. */
    status = getaddrinfo( pServer, NULL, NULL, &pListHead );

    if( status != -1 )
    {
        /* Attempt to connect to one of the retrieved DNS records. */
        for( pIndex = pListHead; pIndex != NULL; pIndex = pIndex->ai_next )
        {
            tcpSocket = socket( pIndex->ai_family,
                                pIndex->ai_socktype,
                                pIndex->ai_protocol );

            if( tcpSocket == -1 )
            {
                continue;
            }

            pServerInfo = pIndex->ai_addr;

            if( pServerInfo->sa_family == AF_INET )
            {
                /* IPv4 */
                ( ( struct sockaddr_in * ) pServerInfo )->sin_port = netPort;
                serverInfoLength = sizeof( struct sockaddr_in );
            }
            else
            {
                /* IPv6 */
                ( ( struct sockaddr_in6 * ) pServerInfo )->sin6_port = netPort;
                serverInfoLength = sizeof( struct sockaddr_in6 );
            }

            status = connect( tcpSocket, pServerInfo, serverInfoLength );

            if( status == -1 )
            {
                close( tcpSocket );
            }
            else
            {
                break;
            }
        }

        if( pIndex == NULL )
        {
            /* Fail if no connection could be established. */
            status = -1;
        }
        else
        {
            status = tcpSocket;
        }
    }

    if( pListHead != NULL )
    {
        freeaddrinfo( pListHead );
    }

    return status;
}

/*-----------------------------------------------------------*/

/**
 * @brief The transport send function provided to the MQTT context.
 *
 * @param[in] tcpSocket TCP socket.
 * @param[in] pMessage Data to send.
 * @param[in] bytesToSend Length of data to send.
 *
 * @return Number of bytes sent; negative value on error.
 */
static int32_t _transportSend( int tcpSocket,
                               const void * pMessage,
                               size_t bytesToSend )
{
    return ( int32_t ) send( tcpSocket, pMessage, bytesToSend, 0 );
}

/*-----------------------------------------------------------*/

/**
 * @brief The transport receive function provided to the MQTT context.
 *
 * @param[in] tcpSocket TCP socket.
 * @param[out] pBuffer Buffer for receiving data.
 * @param[in] bytesToSend Size of pBuffer.
 *
 * @return Number of bytes received; negative value on error.
 */
static int32_t _transportRecv( int tcpSocket,
                               void * pBuffer,
                               size_t bytesToRecv )
{
    return ( int32_t ) recv( tcpSocket, pBuffer, bytesToRecv, 0 );
}

static void _sendHTTP( HTTPRequestHeaders_t * pRequestHeaders,
                       HTTPRequestInfo_t * pRequestInfo,
                       const char * pMethod,
                       const char * pPath )
{
    pRequestInfo->method = pMethod;
    pRequestInfo->methodLen = sizeof( pMethod ) - 1;
    pRequestInfo->pPath = pPath;
    pRequestInfo->pathLen = sizeof( pPath ) - 1;
    HTTPClient_InitializeRequestHeaders( &requestHeaders, &requestInfo );
}

/**
 * @brief Entry point of demo.
 */
int main( int argc,
          char ** argv )
{
    HTTPStatus_t returnStatus = HTTP_SUCCESS;
    HTTPResponse_t response = { 0 };
    HTTPTransportInterface_t transportInterface = { 0 };
    HTTPRequestHeaders_t requestHeaders = { 0 };
    HTTPRequestInfo_t requestInfo = { 0 };
    HTTPTransportInterface_t transportInterface = { 0 };



    requestHeaders.bufferLen = HTTP_DEMO_BUFFER_LENGTH;
    transportInterface.recv = transportRecv;
    transportInterface.send = transportSend;
    transportInterface.pContext = NULL;

    int tcpSocket = connectToServer( HTTP_DEMO_SERVER, HTTP_DEMO_PORT );

    requestHeaders.pBuffer = ( uint8_t * ) ( HTTP_DEMO_BUFFER_LENGTH );

    if( tcpSocket == -1 )
    {
        returnStatus = HTTP_NETWORK_ERROR;
    }
    else
    {
    }

    if( returnStatus == HTTP_SUCCESS )
    {
        /*********************** Send HTTPS requests. ************************/

        /* The client is now connected to the server. This example will send a
         * GET, HEAD, PUT, and POST request. For AWS IoT profile, the example will
         * only send a POST request.
         **/
        #if defined( HTTP_DEMO_GET_PATH )
            IotLogDebug( "Sending HTTPS GET request...\r\n" );
            _initializeRequestInfo( &requestInfo,
                                    HTTP_METHOD_GET,
                                    HTTP_DEMO_GET_PATH );
        #endif
        #if defined( HTTP_DEMO_HEAD_PATH )
            IotLogDebug( "Sending HTTPS HEAD request...\r\n" );
            _initializeRequestInfo( &requestInfo,
                                    HTTP_METHOD_HEAD,
                                    HTTP_DEMO_HEAD_PATH );
        #endif
        #if defined( HTTP_DEMO_PUT_PATH )
            IotLogDebug( "Sending HTTPS PUT request...\r\n" );
            _initializeRequestInfo( &requestInfo,
                                    HTTP_METHOD_PUT,
                                    HTTP_DEMO_PUT_PATH );
        #endif
        #if defined( HTTP_DEMO_POST_PATH )
            IotLogDebug( "Sending HTTPS POST request...\r\n" );
            _initializeRequestInfo( &requestInfo,
                                    HTTP_METHOD_POST,
                                    HTTP_DEMO_POST_PATH );
        #endif
    }

    return returnStatus;
}
