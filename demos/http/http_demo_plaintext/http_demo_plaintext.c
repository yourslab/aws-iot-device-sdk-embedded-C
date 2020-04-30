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
 * This demo uses httpbin.org: A simple HTTP Request & Response Service.
 */
#define SERVER                      "httpbin.org"

/**
 * @brief HTTP server port number.
 *
 * In general, port 80 is for plaintext HTTP connections.
 */
#define PORT                        80

/**
 * @brief Paths for different HTTP methods for specified host.
 *
 * For httpbin.org, see http://httpbin.org/#/HTTP_Methods for details on
 * supported REST API.
 */
#define GET_PATH                    "/ip"
#define HEAD_PATH                   "/ip"
#define PUT_PATH                    "/put"
#define POST_PATH                   "/post"

/**
 * @brief The length in bytes of the user buffer.
 */
#define USER_BUFFER_LENGTH          ( 512 )

/**
 * @brief Some text to send as the request body for a PUT and POST request in
 * this examples.
 */
#define REQUEST_BODY_TEXT           "Hello, world!"
#define REQUEST_BODY_TEXT_LENGTH    ( sizeof( REQUEST_BODY_TEXT ) - 1 )

/**
 * @brief A buffer used to store request headers and reused after sending
 * the request to store the response headers and body.
 *
 * User can also decide to have two separate buffers for request and response.
 */
static uint8_t userBuffer[ USER_BUFFER_LENGTH ] = { 0 };

/**
 * @brief The request body.
 */
static uint8_t requestBodyBuffer[] = REQUEST_BODY_TEXT;

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

/**
 * @brief Send an HTTP request based on a specified method and path.
 *
 * @param[in] pMethod The HTTP request method.
 * @param[in] pPath The Request-URI to the objects of interest.
 *
 * @return #HTTP_SUCCESS if successful.
 */
static HTTPStatus_t _sendHttpRequest( const char * pMethod,
                                      const char * pPath )
{
    HTTPStatus_t returnStatus = HTTP_SUCCESS;
    HTTPRequestHeaders_t requestHeaders = { 0 };
    HTTPRequestInfo_t requestInfo = { 0 };
    HTTPTransportInterface_t transportInterface = { 0 };
    HTTPResponse_t response = { 0 };

    IotLogDebugWithArgs( "Sending HTTP %s request to %s%s...\r\n",
                         pMethod, SERVER, pPath );

    /* Initialize the request context. */
    requestInfo.method = pMethod;
    requestInfo.methodLen = sizeof( pMethod ) - 1;
    requestInfo.pPath = pPath;
    requestInfo.pathLen = sizeof( pPath ) - 1;

    requestHeaders.pBuffer = userBuffer;
    requestHeaders.bufferLen = USER_BUFFER_LENGTH;

    returnStatus = HTTPClient_InitializeRequestHeaders( &requestHeaders,
                                                        &requestInfo );

    if( returnStatus == HTTP_SUCCESS )
    {
        /* Initialize the response context. */
        response.pBuffer = userBuffer;
        response.bufferLen = USER_BUFFER_LENGTH;

        transportInterface.recv = _transportRecv;
        transportInterface.send = _transportSend;
        transportInterface.pContext = NULL;

        /* Send the request and receive the response. */
        returnStatus = HTTPClient_Send( &transportInterface,
                                        &requestHeaders,
                                        requestBodyBuffer,
                                        REQUEST_BODY_TEXT_LENGTH,
                                        &response );
    }

    return returnStatus;
}

/**
 * @brief Entry point of demo.
 */
int main()
{
    HTTPStatus_t returnStatus = HTTP_SUCCESS;

    /* Establish TCP connection and MQTT session. */
    int tcpSocket = connectToServer( SERVER, PORT );

    if( tcpSocket == -1 )
    {
        returnStatus = HTTP_NETWORK_ERROR;
    }

    /*********************** Send HTTPS request. ************************/

    /* The client is now connected to the server. This example will send a
     * GET, HEAD, PUT, and POST request. */

    if( returnStatus == HTTP_SUCCESS )
    {
        returnStatus = _sendHttpRequest( HTTP_METHOD_GET, GET_PATH );
    }

    if( returnStatus == HTTP_SUCCESS )
    {
        returnStatus = _sendHttpRequest( HTTP_METHOD_HEAD, HEAD_PATH );
    }

    if( returnStatus == HTTP_SUCCESS )
    {
        returnStatus = _sendHttpRequest( HTTP_METHOD_PUT, PUT_PATH );
    }

    if( returnStatus == HTTP_SUCCESS )
    {
        returnStatus = _sendHttpRequest( HTTP_METHOD_POST, POST_PATH );
    }

    /**************************** Disconnect. *****************************/

    if( tcpSocket != -1 )
    {
        shutdown( tcpSocket, SHUT_RDWR );
        close( tcpSocket );
    }

    return returnStatus;
}
