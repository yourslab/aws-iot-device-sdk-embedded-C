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

#include <stdlib.h>

/* OpenSSL app header. */
#include "include/openssl_app.h"
/* Plaintext app header. */
#include "include/plaintext_app.h"

/*-----------------------------------------------------------*/

/**
 * @brief Entry point of demo.
 *
 * This example resolves a domain, establishes a TCP connection, validates the
 * server's certificate using the root CA certificate defined in the config header,
 * then finally performs a TLS handshake with the HTTP server so that all communication
 * is encrypted. After which, HTTP Client library API is used to send a GET, HEAD,
 * PUT, and POST request in that order. For each request, the HTTP response from the
 * server (or an error code) is logged.
 *
 * @note This example is single-threaded and uses statically allocated memory.
 *
 */
int main( int argc,
          char ** argv )
{
    int returnStatus = EXIT_SUCCESS;

    ( void ) argc;
    ( void ) argv;

    returnStatus = TlsApp_SendRequests();

    if( returnStatus == EXIT_SUCCESS )
    {
        returnStatus = PlaintextApp_SendRequests();
    }

    return returnStatus;
}
