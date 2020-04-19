#include "http_client.h"
#include "private/http_client_internal.h"

/*-----------------------------------------------------------*/

/**
 * @brief Send the HTTP headers over the transport send interface.
 *
 * @param pTransport Transport interface.
 * @param pRequestHeaders Request headers to send, it includes the buffer and length.
 *
 * @return #HTTP_SUCCESS if successful. If there was a network error or less
 * bytes than what was specified were sent, then #HTTP_NETWORK_ERROR is returned.
 */
static HTTPStatus_t _sendHttpHeaders( const HTTPTransportInterface_t * pTransport,
                                      const HTTPRequestHeaders_t * pRequestHeaders );

/**
 * @brief Send the HTTP body over the transport send interface.
 *
 * @param pTransport Transport interface.
 * @param pRequestBodyBuf Request body buffer.
 * @param reqBodyLen Length of the request body buffer.
 *
 * @return #HTTP_SUCCESS if successful. If there was a network error or less
 * bytes than what was specified were sent, then #HTTP_NETWORK_ERROR is returned.
 */
static HTTPStatus_t _sendHttpBody( const HTTPTransportInterface_t * pTransport,
                                   const uint8_t * pRequestBodyBuf,
                                   size_t reqBodyBufLen );

/*-----------------------------------------------------------*/

HTTPStatus_t HTTPClient_InitializeRequestHeaders( HTTPRequestHeaders_t * pRequestHeaders,
                                                  const HTTPRequestInfo_t * pRequestInfo )
{
    HTTPStatus_t status = HTTP_INTERNAL_ERROR;
    size_t toAddLen = 0;
    uint8_t * pBufferCur = pRequestHeaders->pBuffer;
    size_t httpsProtocolVersionLen = STRLEN_LITERAL( HTTP_PROTOCOL_VERSION );

    pRequestHeaders->headersLen = 0;
    /* Clear user-provided buffer. */
    memset( pRequestHeaders->pBuffer, 0, pRequestHeaders->bufferLen );

    /* Check for null parameters. */
    if( _isNullParam( pRequestHeaders ) || _isNullParam( pRequestInfo ) ||
        _isNullParam( pRequestHeaders->pBuffer ) ||
        _isNullParam( pRequestInfo->method ) ||
        _isNullParam( pRequestInfo->pHost ) ||
        _isNullParam( pRequestInfo->pPath ) )
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

    if( toAddLen + pRequestHeaders->headersLen > pRequestHeaders->bufferLen )
    {
        status = HTTP_INSUFFICIENT_MEMORY;
    }

    if( HTTP_SUCCEEDED( status ) )
    {
        /* Write "<METHOD> <PATH> HTTP/1.1\r\n" to start the HTTP header. */
        memcpy( pBufferCur, pRequestInfo->method, pRequestInfo->methodLen );
        pBufferCur += pRequestInfo->methodLen;
        memcpy( pBufferCur, SPACE_CHARACTER, SPACE_CHARACTER_LEN );
        pBufferCur += SPACE_CHARACTER_LEN;

        /* Use "/" as default value if <PATH> is NULL. */
        if( ( pRequestInfo->pPath == NULL ) || ( pRequestInfo->pathLen == 0 ) )
        {
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

        /* Write "User-Agent: <Value>". */
        status = _addHeader( pRequestHeaders,
                             HTTP_USER_AGENT_FIELD,
                             HTTP_USER_AGENT_FIELD_LEN,
                             HTTP_USER_AGENT_VALUE,
                             STRLEN_LITERAL( HTTP_USER_AGENT_VALUE ) );
    }
    else
    {
        /* TODO: Add log. */
    }

/*-----------------------------------------------------------*/

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
    if( !( HTTP_REQUEST_DISABLE_CONTENT_LENGTH_FLAG & pRequestInfo->flags ) &&
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

    if( HTTP_FAILED( status ) )
    {
        /* TODO: Add log. */
    }

    return status;
}

/*-----------------------------------------------------------*/

HTTPStatus_t HTTPClient_AddRangeHeader( HTTPRequestHeaders_t * pRequestHeaders,
                                        int32_t rangeStart,
                                        int32_t rangeEnd )
{
    /* Create a buffer to fit max possible length for "bytes=<start>-<end>". */
    size_t maxLen = HTTP_RANGE_FIELD_LEN + HTTP_HEADER_FIELD_SEPARATOR_LEN +  \
                    HTTP_RANGE_BYTES_PREFIX_VALUE_LEN + EQUAL_CHARACTER_LEN + \
                    INT32_STRING_MAX_LEN + DASH_CHARACTER_LEN + INT32_STRING_MAX_LEN;
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

    /* Send the HTTP headers over the network. */
    transportStatus = pTransport->send( pTransport->pContext,
                                        pRequestHeaders->pBuffer,
                                        pRequestHeaders->headersLen );

    if( transportStatus < 0 )
    {
        IotLogErrorWithArgs( "Error in sending the HTTP headers over the transport "
                             "interface: Transport status %d.",
                             transportStatus );
        returnStatus = HTTP_NETWORK_ERROR;
    }
    else if( transportStatus != pRequestHeaders->headersLen )
    {
        IotLogErrorWithArgs( "Failure in sending HTTP headers: Transport layer "
                             "did not send the required bytes: Required Bytes=%d"
                             ", Sent Bytes=%d.",
                             transportStatus );
        returnStatus = HTTP_NETWORK_ERROR;
    }
    else
    {
        /* Empty else MISRA 15.7 */
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

    /* Send the HTTP body over the network. */
    if( pRequestBodyBuf != NULL )
    {
        transportStatus = pTransport->send( pTransport->pContext,
                                            pRequestBodyBuf,
                                            reqBodyBufLen );

        if( transportStatus < 0 )
        {
            IotLogErrorWithArgs( "Error in sending the HTTP body over the "
                                 "transport interface. Transport status %d.",
                                 transportStatus );
            returnStatus = HTTP_NETWORK_ERROR;
        }
        else if( transportStatus != reqBodyBufLen )
        {
            IotLogErrorWithArgs( "Failure in sending HTTP headers: Transport layer "
                                 "did not send the required bytes: Required Bytes=%d"
                                 ", Sent Bytes=%d.",
                                 transportStatus );
            returnStatus = HTTP_NETWORK_ERROR;
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static HTTPStatus_t _receiveHttpResponse( const HTTPTransportInterface_t * pTransport,
                                          HTTPResponse_t * pResponse )
{
    /* TODO: Receive the HTTP response with parsing. */
    return HTTP_SUCCESS;
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
        /* Empty else MISRA 15.7 */
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
        returnStatus = _sendHttpBody( pTransport,
                                      pRequestBodyBuf,
                                      reqBodyBufLen );
    }

    if( returnStatus == HTTP_SUCCESS )
    {
        returnStatus = _receiveHttpResponse( pTransport,
                                             pResponse );
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
