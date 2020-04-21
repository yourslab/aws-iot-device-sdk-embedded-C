#include <assert.h>
/* Standard Includes. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "http_client.h"
#include "private/http_client_internal.h"
#include "private/http_client_parse.h"

/*-----------------------------------------------------------*/

/**
 * @brief Send the HTTP headers over the transport send interface.
 *
 * @param[in] pTransport Transport interface.
 * @param[in] pRequestHeaders Request headers to send, it includes the buffer
 * and length.
 *
 * @return #HTTP_SUCCESS if successful. If there was a network error or less
 * bytes than what was specified were sent, then #HTTP_NETWORK_ERROR is returned.
 */
static HTTPStatus_t _sendHttpHeaders( const HTTPTransportInterface_t * pTransport,
                                      const HTTPRequestHeaders_t * pRequestHeaders );

/**
 * @brief Send the HTTP body over the transport send interface.
 *
 * @param[in] pTransport Transport interface.
 * @param[in] pRequestBodyBuf Request body buffer.
 * @param[in] reqBodyLen Length of the request body buffer.
 *
 * @return #HTTP_SUCCESS if successful. If there was a network error or less
 * bytes than what was specified were sent, then #HTTP_NETWORK_ERROR is returned.
 */
static HTTPStatus_t _sendHttpBody( const HTTPTransportInterface_t * pTransport,
                                   const uint8_t * pRequestBodyBuf,
                                   size_t reqBodyBufLen );


/**
 * @brief Receive HTTP response from the transport recv interface.
 *
 * @param[in] pTransport Transport interface.
 * @param[in] pResponse Response buffer.
 * @param[in] bufferLen Length of the response buffer.
 *
 * @return Returns #HTTP_SUCCESS if successful. If there was a network error or
 * more bytes than what was specified were read, then #HTTP_NETWORK_ERROR is
 * returned.
 */
HTTPStatus_t _receiveHttpResponse( const HTTPTransportInterface_t * pTransport,
                                   uint8_t * pBuffer,
                                   size_t bufferLen,
                                   size_t * pBytesReceived );

/**
 * @brief Get the status of the HTTP response given the parsing state and how
 * much data is in the response buffer.
 *
 * @param[in] parsingState State of the parsing on the HTTP response.
 * @param[in] totalReceived The amount of network data received in the response
 * buffer.
 * @param[in] responseBufferLen The length of the response buffer.
 *
 * @return Returns #HTTP_SUCCESS if the parsing state is complete. If
 * the parsing state denotes it never started, then return #HTTP_NO_RESPONSE. If
 * the parsing state is incomplete, then if the response buffer is not full
 * #HTTP_PARTIAL_RESPONSE is returned. If the parsing state is incomplete, then
 * if the response buffer is full #HTTP_INSUFFICIENT_MEMORY is returned.
 */
static HTTPStatus_t _getFinalResponseStatus( HTTPParsingState_t parsingState,
                                             size_t totalReceived,
                                             size_t responseBufferLen );

/**
 * @brief Receive the HTTP response from the network and parse it.
 *
 * @param[in] pTransport Transport interface.
 * @param[in] pResponse Response message to receive data from the network.
 *
 * @return Returns #HTTP_SUCCESS if successful. If there was an issue with receiving
 * the response over the network interface, then #HTTP_NETWORK_ERROR is returned,
 * please see #_receiveHttpResponse. If there was an issue with parsing, then the
 * parsing error that occurred will be returned, please see
 * #_HTTPClient_InitializeParsingContext and #_HTTPClient_ParseResponse. Please
 * see #_getFinalResponseStatus for the status returned when there were no
 * network or parsing errors.
 */
static HTTPStatus_t _receiveAndParseHttpResponse( const HTTPTransportInterface_t * pTransport,
                                                  HTTPResponse_t * pResponse );

/*-----------------------------------------------------------*/

static uint8_t _isNullParam( const void * ptr );

static HTTPStatus_t _addHeader( HTTPRequestHeaders_t * pRequestHeaders,
                                const char * pField,
                                size_t fieldLen,
                                const char * pValue,
                                size_t valueLen );

static uint8_t _isNullPtr( const void * ptr )
{
    /* TODO: Add log. */
    return ptr == NULL;
}

static HTTPStatus_t _addHeader( HTTPRequestHeaders_t * pRequestHeaders,
                                const char * pField,
                                size_t fieldLen,
                                const char * pValue,
                                size_t valueLen )
{
    HTTPStatus_t status = HTTP_INTERNAL_ERROR;
    uint8_t * pBufferCur = NULL;
    size_t toAddLen = 0;

    /* Check for NULL parameters. */
    if( _isNullPtr( pRequestHeaders ) ||
        _isNullPtr( pRequestHeaders->pBuffer ) ||
        _isNullPtr( pField ) || _isNullPtr( pValue ) )
    {
        status = HTTP_INVALID_PARAMETER;
    }

    if( status == HTTP_SUCCESS )
    {
        pBufferCur = pRequestHeaders->pBuffer;

        /* Backtrack before trailing "\r\n" (HTTP header end) if it's already written.
         * Note that this method also writes trailing "\r\n" before returning. */
        if( strncmp( pBufferCur - 2 * HTTP_HEADER_LINE_SEPARATOR_LEN,
                     "\r\n\r\n", 2 * HTTP_HEADER_LINE_SEPARATOR_LEN ) )
        {
            pBufferCur -= HTTP_HEADER_LINE_SEPARATOR_LEN;
            memset( pBufferCur, 0, HTTP_HEADER_LINE_SEPARATOR_LEN );
            pRequestHeaders->headersLen -= 2 * HTTP_HEADER_LINE_SEPARATOR_LEN;
        }

        /* Check if there is enough space in buffer for additional header. */
        toAddLen = fieldLen + HTTP_HEADER_FIELD_SEPARATOR_LEN + valueLen + \
                   HTTP_HEADER_LINE_SEPARATOR_LEN +                        \
                   HTTP_HEADER_LINE_SEPARATOR_LEN;

        if( ( pRequestHeaders->headersLen + toAddLen ) > pRequestHeaders->bufferLen )
        {
            /* TODO: Add log. */
            status = HTTP_INSUFFICIENT_MEMORY;
        }
    }

    if( status == HTTP_SUCCESS )
    {
        /* Write "Field: Value \r\n" to headers. */
        memcpy( pBufferCur, pField, fieldLen );
        pBufferCur += fieldLen;
        memcpy( pBufferCur, HTTP_HEADER_FIELD_SEPARATOR,
                HTTP_HEADER_FIELD_SEPARATOR_LEN );
        pBufferCur += HTTP_HEADER_FIELD_SEPARATOR_LEN;
        memcpy( pBufferCur, pValue, valueLen );
        pBufferCur += valueLen;
        memcpy( pBufferCur, HTTP_HEADER_LINE_SEPARATOR, HTTP_HEADER_LINE_SEPARATOR_LEN );
        pBufferCur += HTTP_HEADER_LINE_SEPARATOR_LEN;
        memcpy( pBufferCur, HTTP_HEADER_LINE_SEPARATOR, HTTP_HEADER_LINE_SEPARATOR_LEN );

        pRequestHeaders->headersLen += toAddLen;
    }

    return status;
}

HTTPStatus_t HTTPClient_InitializeRequestHeaders( HTTPRequestHeaders_t * pRequestHeaders,
                                                  const HTTPRequestInfo_t * pRequestInfo )
{
    HTTPStatus_t status = HTTP_INTERNAL_ERROR;
    size_t toAddLen = 0;
    uint8_t * pBufferCur = pRequestHeaders->pBuffer;
    size_t httpsProtocolVersionLen = STRLEN_LITERAL( HTTP_PROTOCOL_VERSION );

    pRequestHeaders->headersLen = 0;
    pRequestHeaders->flags = pRequestInfo->flags;
    /* Clear user-provided buffer. */
    memset( pRequestHeaders->pBuffer, 0, pRequestHeaders->bufferLen );

    /* Check for null parameters. */
    if( _isNullPtr( pRequestHeaders ) || _isNullPtr( pRequestInfo ) ||
        _isNullPtr( pRequestHeaders->pBuffer ) ||
        _isNullPtr( pRequestInfo->method ) ||
        _isNullPtr( pRequestInfo->pHost ) ||
        _isNullPtr( pRequestInfo->pPath ) )
    {
        status = HTTP_INVALID_PARAMETER;
    }

    /* Check if buffer can fit "<METHOD> <PATH> HTTP/1.1\r\n". */
    toAddLen = pRequestInfo->methodLen +                 \
               SPACE_CHARACTER_LEN +                     \
               pRequestInfo->pathLen +                   \
               SPACE_CHARACTER_LEN +                     \
               STRLEN_LITERAL( HTTP_PROTOCOL_VERSION ) + \
               HTTP_HEADER_LINE_SEPARATOR_LEN;

    if( ( toAddLen + pRequestHeaders->headersLen ) > pRequestHeaders->bufferLen )
    {
        status = HTTP_INSUFFICIENT_MEMORY;
    }
    else
    {
        /* TODO: Add log. */
    }

    if( status == HTTP_SUCCESS )
    {
        /* Write "<METHOD> <PATH> HTTP/1.1\r\n" to start the HTTP header. */
        memcpy( pBufferCur, pRequestInfo->method, pRequestInfo->methodLen );
        pBufferCur += pRequestInfo->methodLen;
        memcpy( pBufferCur, SPACE_CHARACTER, SPACE_CHARACTER_LEN );
        pBufferCur += SPACE_CHARACTER_LEN;

        /* Use "/" as default value if <PATH> is NULL. */
        if( ( pRequestInfo->pPath == NULL ) || ( pRequestInfo->pathLen == 0 ) )
        {
            /* Revise toAddLen to contain <HTTP_EMPTY_PATH_LEN> instead. */
            toAddLen = ( toAddLen - pRequestInfo->pathLen ) + HTTP_EMPTY_PATH_LEN;
            memcpy( pBufferCur, HTTP_EMPTY_PATH, HTTP_EMPTY_PATH_LEN );
            pBufferCur += HTTP_EMPTY_PATH_LEN;
        }
        else
        {
            memcpy( pBufferCur, pRequestInfo->pPath, pRequestInfo->pathLen );
            pBufferCur += pRequestInfo->pathLen;
        }

        memcpy( pBufferCur, SPACE_CHARACTER, SPACE_CHARACTER_LEN );
        pBufferCur += SPACE_CHARACTER_LEN;

        memcpy( pBufferCur, HTTP_PROTOCOL_VERSION, httpsProtocolVersionLen );
        pBufferCur += httpsProtocolVersionLen;
        memcpy( pBufferCur, HTTP_HEADER_LINE_SEPARATOR, HTTP_HEADER_LINE_SEPARATOR_LEN );
        pBufferCur += HTTP_HEADER_LINE_SEPARATOR_LEN;

        pRequestHeaders->headersLen += toAddLen;

        /* Write "User-Agent: <Value>". */
        status = _addHeader( pRequestHeaders,
                             HTTP_USER_AGENT_FIELD,
                             HTTP_USER_AGENT_FIELD_LEN,
                             HTTP_USER_AGENT_VALUE,
                             STRLEN_LITERAL( HTTP_USER_AGENT_VALUE ) );
    }

    if( status == HTTP_SUCCESS )
    {
        pRequestHeaders->pBuffer += toAddLen;
        /* Write "Host: <Value>". */
        status = _addHeader( pRequestHeaders,
                             HTTP_HOST_FIELD,
                             HTTP_HOST_FIELD_LEN,
                             pRequestInfo->pHost,
                             pRequestInfo->hostLen );
    }

    if( status == HTTP_SUCCESS )
    {
        if( HTTP_REQUEST_KEEP_ALIVE_FLAG & pRequestInfo->flags )
        {
            /* Write "Connection: keep-alive". */
            status = _addHeader( pRequestHeaders,
                                 HTTP_CONNECTION_FIELD,
                                 HTTP_CONNECTION_FIELD_LEN,
                                 HTTP_CONNECTION_KEEP_ALIVE_VALUE,
                                 HTTP_CONNECTION_KEEP_ALIVE_VALUE_LEN );
        }
        else
        {
            /* Write "Connection: close". */
            status = _addHeader( pRequestHeaders,
                                 HTTP_CONNECTION_FIELD,
                                 HTTP_CONNECTION_FIELD_LEN,
                                 HTTP_CONNECTION_CLOSE_VALUE,
                                 HTTP_CONNECTION_CLOSE_VALUE_LEN );
        }
    }

    if( status == HTTP_SUCCESS )
    {
        /* Write "\r\n" line to end the HTTP header. */
        if( ( HTTP_HEADER_LINE_SEPARATOR_LEN + pRequestHeaders->headersLen )
            > pRequestHeaders->bufferLen )
        {
            status = HTTP_INSUFFICIENT_MEMORY;
        }
        else
        {
            /* TODO: Add log. */
        }
    }

    if( status == HTTP_SUCCESS )
    {
        memcpy( pBufferCur,
                HTTP_HEADER_LINE_SEPARATOR,
                HTTP_HEADER_LINE_SEPARATOR_LEN );
        pRequestHeaders->headersLen += HTTP_HEADER_LINE_SEPARATOR_LEN;
    }

    return status;
}

HTTPStatus_t HTTPClient_AddHeader( HTTPRequestHeaders_t * pRequestHeaders,
                                   const char * pField,
                                   size_t fieldLen,
                                   const char * pValue,
                                   size_t valueLen )
{
    HTTPStatus_t status = HTTP_INTERNAL_ERROR;

    /* Check if header field is long enough for length to overflow. */
    if( fieldLen > ( UINT32_MAX >> 2 ) )
    {
        /* TODO: Add log. */
        status = HTTP_INVALID_PARAMETER;
    }

    /* Check if header value is long enough for length to overflow. */
    if( valueLen > ( UINT32_MAX >> 2 ) )
    {
        /* TODO: Add log. */
        status = HTTP_INVALID_PARAMETER;
    }

    /* "Content-Length" header must not be set by user if
     * HTTP_REQUEST_DISABLE_CONTENT_LENGTH_FLAG is deactivated. */
    if( !( HTTP_REQUEST_DISABLE_CONTENT_LENGTH_FLAG & pRequestHeaders->flags ) &&
        strncmp( pField,
                 HTTP_CONTENT_LENGTH_FIELD, HTTP_CONTENT_LENGTH_FIELD_LEN ) )
    {
        /* TODO: Add log. */
        status = HTTP_INVALID_PARAMETER;
    }

    /* User must not set "Connection" header through this method. */
    if( strncmp( pField,
                 HTTP_CONNECTION_FIELD, HTTP_CONNECTION_FIELD_LEN ) )
    {
        /* TODO: Add log. */
        status = HTTP_INVALID_PARAMETER;
    }

    /* User must not set "Host" header through this method. */
    if( strncmp( pField,
                 HTTP_HOST_FIELD, HTTP_HOST_FIELD_LEN ) )
    {
        /* TODO: Add log. */
        status = HTTP_INVALID_PARAMETER;
    }

    /* User must not set "User-Agent" header through this method. */
    if( strncmp( pField,
                 HTTP_USER_AGENT_FIELD, HTTP_USER_AGENT_FIELD_LEN ) )
    {
        /* TODO: Add log. */
        status = HTTP_INVALID_PARAMETER;
    }

    if( HTTP_SUCCEEDED( status ) )
    {
        status = _addHeader( pRequestHeaders,
                             pField, fieldLen, pValue, valueLen );
    }

    return status;
}

/*-----------------------------------------------------------*/

HTTPStatus_t HTTPClient_AddRangeHeader( HTTPRequestHeaders_t * pRequestHeaders,
                                        int32_t rangeStart,
                                        int32_t rangeEnd )
{
    /* Create buffer to fit max possible length for "bytes=<start>-<end>".
     * This is the value of the Range header. */
    char rangeValueStr[ HTTP_RANGE_BYTES_VALUE_MAX_LEN ] = { 0 };
    char * pRangeValueCur = &rangeValueStr;
    /* Excluding all the remaining null bytes. */
    size_t rangeValueStrActualLength = 0;

    /* Write "bytes=<start>:<end>" for Range header value. */
    memcpy( rangeValueStr,
            HTTP_RANGE_BYTES_PREFIX_VALUE, HTTP_RANGE_BYTES_PREFIX_VALUE_LEN );
    pRangeValueCur += HTTP_RANGE_BYTES_PREFIX_VALUE_LEN;
    rangeValueStrActualLength += HTTP_RANGE_BYTES_PREFIX_VALUE_LEN;
    memcpy( rangeValueStr, EQUAL_CHARACTER, EQUAL_CHARACTER_LEN );
    pRangeValueCur += EQUAL_CHARACTER_LEN;
    rangeValueStrActualLength += EQUAL_CHARACTER_LEN;
    memcpy( rangeValueStr, itoa( rangeStart ), itoaLength( rangeStart ) );
    pRangeValueCur += itoaLength( rangeStart );
    rangeValueStrActualLength += itoaLength( rangeStart );
    memcpy( rangeValueStr, DASH_CHARACTER, DASH_CHARACTER_LEN );
    pRangeValueCur += DASH_CHARACTER_LEN;
    rangeValueStrActualLength += DASH_CHARACTER_LEN;
    memcpy( rangeValueStr, itoa( rangeEnd ), itoaLength( rangeEnd ) );
    pRangeValueCur += itoaLength( rangeEnd );
    rangeValueStrActualLength += itoaLength( rangeEnd );

    return HTTPClient_AddHeader( pRequestHeaders,
                                 HTTP_RANGE_FIELD, HTTP_RANGE_FIELD_LEN,
                                 rangeValueStr, rangeValueStrActualLength );
}

/*-----------------------------------------------------------*/

static HTTPStatus_t _sendHttpHeaders( const HTTPTransportInterface_t * pTransport,
                                      const HTTPRequestHeaders_t * pRequestHeaders )
{
    HTTPStatus_t returnStatus = HTTP_SUCCESS;
    int32_t transportStatus = 0;

    assert( pTransport != NULL );
    assert( pTransport->send != NULL );
    assert( pRequestHeaders != NULL );

    /* Send the HTTP headers over the network. */
    transportStatus = pTransport->send( pTransport->pContext,
                                        pRequestHeaders->pBuffer,
                                        pRequestHeaders->headersLen );

    if( transportStatus < 0 )
    {
        IotLogErrorWithArgs( "Failed to send HTTP headers: Transport send()"
                             " returned error: Transport Status = %d",
                             transportStatus );
        returnStatus = HTTP_NETWORK_ERROR;
    }
    else if( transportStatus != pRequestHeaders->headersLen )
    {
        IotLogErrorWithArgs( "Failed to send HTTP headers: Transport layer "
                             "did not send the required bytes: Required bytes = %d"
                             ", Sent bytes=%d.",
                             pRequestHeaders->headersLen,
                             transportStatus );
        returnStatus = HTTP_NETWORK_ERROR;
    }
    else
    {
        IotLogDebugWithArgs( "Sent HTTP headers over the transport: Bytes sent "
                             "= %d.",
                             transportStatus );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static HTTPStatus_t _sendHttpBody( const HTTPTransportInterface_t * pTransport,
                                   const uint8_t * pRequestBodyBuf,
                                   size_t reqBodyBufLen )
{
    HTTPStatus_t returnStatus = HTTP_SUCCESS;
    int32_t transportStatus = 0;

    assert( pTransport != NULL );
    assert( pTransport->send != NULL );
    assert( pRequestBodyBuf != NULL );

    transportStatus = pTransport->send( pTransport->pContext,
                                        pRequestBodyBuf,
                                        reqBodyBufLen );

    if( transportStatus < 0 )
    {
        IotLogErrorWithArgs( "Failed to send HTTP body: Transport send() "
                             " returned error: Transport Status = %d",
                             transportStatus );
        returnStatus = HTTP_NETWORK_ERROR;
    }
    else if( transportStatus != reqBodyBufLen )
    {
        IotLogErrorWithArgs( "Failed to send HTTP body: Transport send() "
                             "did not send the required bytes: Required bytes = %d"
                             ", Sent bytes=%d.",
                             reqBodyBufLen,
                             transportStatus );
        returnStatus = HTTP_NETWORK_ERROR;
    }
    else
    {
        IotLogDebugWithArgs( "Sent HTTP body over the transport: Bytes sent = %d.",
                             transportStatus );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

HTTPStatus_t _receiveHttpResponse( const HTTPTransportInterface_t * pTransport,
                                   uint8_t * pBuffer,
                                   size_t bufferLen,
                                   size_t * pBytesReceived )
{
    HTTPStatus_t returnStatus = HTTP_SUCCESS;

    assert( pTransport != NULL );
    assert( pTransport->recv != NULL );
    assert( pBuffer != NULL );
    assert( pBytesReceived != NULL );

    int32_t transportStatus = pTransport->recv( pTransport->pContext,
                                                pBuffer,
                                                bufferLen );

    /* A transport status of less than zero is an error. */
    if( transportStatus < 0 )
    {
        IotLogErrorWithArgs( "Failed to receive HTTP response: Transport recv() "
                             "returned error: Transport status = %d.",
                             transportStatus );
        returnStatus = HTTP_NETWORK_ERROR;
    }
    else if( transportStatus > bufferLen )
    {
        /* There is a bug in the transport recv if more bytes are reported
         * to have been read than the bytes asked for. */
        IotLogErrorWithArgs( "Failed to receive HTTP response: Transport recv() "
                             " read more bytes than expected: Bytes read = %d",
                             transportStatus );
        returnStatus = HTTP_NETWORK_ERROR;
    }
    else if( transportStatus > 0 )
    {
        /* Some or all of the specified data was received. */
        *pBytesReceived = ( size_t ) ( transportStatus );
        IotLogDebugWithArgs( "Received data from the transport: Bytes "
                             "received = %d.",
                             transportStatus );
    }
    else
    {
        /* When a zero is returned from the transport recv it will not be
         * invoked again. */
        IotLogDebug( "Transport recv() returned 0. Receiving transport data"
                     "is complete." );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static HTTPStatus_t _getFinalResponseStatus( HTTPParsingState_t parsingState,
                                             size_t totalReceived,
                                             size_t responseBufferLen )
{
    HTTPStatus_t returnStatus = HTTP_SUCCESS;

    assert( parsingState >= HTTP_PARSING_NONE &&
            parsingState <= HTTP_PARSING_COMPLETE );
    assert( totalReceived <= responseBufferLen );

    /* If no parsing occurred, that means network data was never received. */
    if( parsingState == HTTP_PARSING_NONE )
    {
        IotLogErrorWithArgs( "Response not received: Zero returned from "
                             "transport recv: Total received = % d",
                             totalReceived );
        returnStatus = HTTP_NO_RESPONSE;
    }
    else if( parsingState == HTTP_PARSING_INCOMPLETE )
    {
        if( totalReceived == responseBufferLen )
        {
            IotLogErrorWithArgs( "Response is too large for the response buffer"
                                 ": Response buffer size in bytes = %d",
                                 responseBufferLen );
            returnStatus = HTTP_INSUFFICIENT_MEMORY;
        }
        else
        {
            IotLogErrorWithArgs( "Partial response received: Transport recv "
                                 "returned zero before the complete response: "
                                 "Partial size = %d, Response buffer space "
                                 "left = %d",
                                 totalReceived,
                                 responseBufferLen - totalReceived );
            returnStatus = HTTP_PARTIAL_RESPONSE;
        }
    }
    else
    {
        /* Empty else for MISRA 15.7 compliance. */
    }

    return returnStatus;
}

static HTTPStatus_t _receiveAndParseHttpResponse( const HTTPTransportInterface_t * pTransport,
                                                  HTTPResponse_t * pResponse )
{
    HTTPStatus_t returnStatus = HTTP_SUCCESS;
    size_t totalReceived = 0;
    size_t currentReceived = 0;
    HTTPParsingContext_t parsingContext = { 0 };

    if( pResponse->pBuffer == NULL )
    {
        IotLogError( "Parameter check failed: pResponse->pBuffer is NULL." );
        returnStatus = HTTP_INVALID_PARAMETER;
    }

    if( returnStatus == HTTP_SUCCESS )
    {
        /* Initialize the parsing context. */
        returnStatus = _HTTPClient_InitializeParsingContext( &parsingContext,
                                                             pResponse->pHeaderParsingCallback );
    }

    /* While there are no errors in the transport recv or parsing, the response
     * message is not finished, and there is room in the response buffer. */
    while( ( returnStatus == HTTP_SUCCESS ) &&
           ( parsingContext.state != HTTP_PARSING_COMPLETE ) &&
           ( totalReceived < pResponse->bufferLen ) )
    {
        /* Receive the HTTP response data into the pResponse->pBuffer. */
        returnStatus = _receiveHttpResponse( pTransport,
                                             pResponse->pBuffer + totalReceived,
                                             pResponse->bufferLen - totalReceived,
                                             &currentReceived );

        if( returnStatus == HTTP_SUCCESS )
        {
            if( currentReceived > 0 )
            {
                totalReceived += currentReceived;
                /* Data is received into the buffer and must be parsed. */
                returnStatus = _HTTPClient_ParseResponse( &parsingContext,
                                                          pResponse->pBuffer + totalReceived,
                                                          currentReceived );
            }
            else
            {
                /* If there was no data received, then end receiving and parsing
                 * the response. */
                break;
            }
        }
    }

    if( returnStatus == HTTP_SUCCESS )
    {
        /* For no network or parsing errors, the final status of the response
         * message is derived from the state of the parsing and how much data
         * is in the buffer. */
        returnStatus = _getFinalResponseStatus( parsingContext.state,
                                                totalReceived,
                                                pResponse->bufferLen );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

HTTPStatus_t HTTPClient_Send( const HTTPTransportInterface_t * pTransport,
                              const HTTPRequestHeaders_t * pRequestHeaders,
                              const uint8_t * pRequestBodyBuf,
                              size_t reqBodyBufLen,
                              HTTPResponse_t * pResponse )
{
    HTTPStatus_t returnStatus = HTTP_SUCCESS;

    if( pTransport == NULL )
    {
        IotLogError( "Parameter check failed: pTransport interface is NULL." );
        returnStatus = HTTP_INVALID_PARAMETER;
    }
    else if( pTransport->send == NULL )
    {
        IotLogError( "Parameter check failed: pTransport->send is NULL." );
        returnStatus = HTTP_INVALID_PARAMETER;
    }
    else if( pTransport->recv == NULL )
    {
        IotLogError( "Parameter check failed: pTransport->recv is NULL." );
        returnStatus = HTTP_INVALID_PARAMETER;
    }
    else if( pRequestHeaders == NULL )
    {
        IotLogError( "Parameter check failed: pRequestHeaders is NULL." );
        returnStatus = HTTP_INVALID_PARAMETER;
    }
    else if( pRequestHeaders->pBuffer == NULL )
    {
        IotLogError( "Parameter check failed: pRequestHeaders->pBuffer is NULL." );
        returnStatus = HTTP_INVALID_PARAMETER;
    }
    else
    {
        /* Empty else for MISRA 15.7 compliance. */
    }

    /* Send the headers, which are at one location in memory. */
    if( returnStatus == HTTP_SUCCESS )
    {
        returnStatus = _sendHttpHeaders( pTransport,
                                         pRequestHeaders );
    }

    /* Send the body, which is at another location in memory. */
    if( returnStatus == HTTP_SUCCESS )
    {
        if( pRequestBodyBuf != NULL )
        {
            returnStatus = _sendHttpBody( pTransport,
                                          pRequestBodyBuf,
                                          reqBodyBufLen );
        }
        else
        {
            IotLogDebug( "A request body was not sent: pRequestBodyBuf is NULL." );
        }
    }

    if( returnStatus == HTTP_SUCCESS )
    {
        /* If the application chooses to receive a response, then pResponse
         * will not be NULL. */
        if( pResponse != NULL )
        {
            returnStatus = _receiveAndParseHttpResponse( pTransport,
                                                         pResponse );
        }
        else
        {
            IotLogWarn( "A response was not received: pResponse is NULL. " );
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

HTTPStatus_t HTTPClient_ReadHeader( HTTPResponse_t * pResponse,
                                    const char * pName,
                                    size_t nameLen,
                                    char ** pValue,
                                    size_t * valueLen )
{
    return HTTP_NOT_SUPPORTED;
}

/*-----------------------------------------------------------*/
