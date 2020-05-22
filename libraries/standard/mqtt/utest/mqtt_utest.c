#include <time.h>

#include "unity.h"

#include "mock_mqtt_state.h"
#include "mock_mqtt_lightweight.h"

/* Include paths for public enums, structures, and macros. */
#include "mqtt.h"

/**
 * @brief A valid starting packet ID per MQTT spec. Start from 1.
 */
#define MQTT_NEXT_PACKET_ID_START          ( 1 )

/**
 * @brief A PINGREQ packet is always 2 bytes in size, defined by MQTT 3.1.1 spec.
 */
#define MQTT_PACKET_PINGREQ_SIZE           ( 2U )

/**
 * @brief A packet type not handled by MQTT_ProcessLoop.
 */
#define MQTT_PACKET_TYPE_INVALID           ( 0U )

/**
 * @brief Number of milliseconds in a second.
 */
#define MQTT_ONE_SECOND_TO_MS              ( 1000U )

/**
 * @brief Zero timeout in the process loop implies one iteration.
 */
#define MQTT_NO_TIMEOUT_MS                 ( 0U )

/**
 * @brief Length of time spent for single test case with
 * multiple iterations spent in the process loop for coverage.
 */
#define MQTT_SAMPLE_TIMEOUT_MS             ( 1U )

/**
 * @brief Sample length of remaining serialized data.
 */
#define MQTT_SAMPLE_REMAINING_LENGTH       ( 64 )

/**
 * @brief Length of the MQTT network buffer.
 */
#define MQTT_TEST_BUFFER_LENGTH            ( 128 )

/**
 * @brief Sample topic filter to subscribe to.
 */
#define MQTT_SAMPLE_TOPIC_FILTER           "iot"

/**
 * @brief Length of sample topic filter.
 */
#define MQTT_SAMPLE_TOPIC_FILTER_LENGTH    ( sizeof( MQTT_SAMPLE_TOPIC_FILTER ) - 1 )

/**
 * @brief The packet type to be received by the process loop.
 * IMPORTANT: Make sure this is set before calling expectProcessLoopCalls(...).
 */
static uint8_t currentPacketType = MQTT_PACKET_TYPE_INVALID;

/**
 * @brief The return value of modifyIncomingPacket(...) CMock callback that
 * replaces a call to MQTT_GetIncomingPacketTypeAndLength.
 * IMPORTANT: Make sure this is set before calling expectProcessLoopCalls(...).
 */
static MQTTStatus_t modifyIncomingPacketStatus = MQTTSuccess;

/**
 * @brief The return value of modifyPacketSize(...) CMock callback that replaces
 * a call to MQTT_GetPingreqPacketSize.
 */
static MQTTStatus_t modifyPacketSizeStatus = MQTTSuccess;

/**
 * @brief Time at the beginning of each test. Note that this is not updated with
 * a real clock. Instead, we simply increment this variable.
 */
static uint32_t globalEntryTime = 0;

/**
 * @brief A static buffer used by the MQTT library for storing packet data.
 */
static uint8_t mqttBuffer[ MQTT_TEST_BUFFER_LENGTH ] = { 0 };

/* ============================   UNITY FIXTURES ============================ */

/* Called before each test method. */
void setUp()
{
    globalEntryTime = 0;
}

/* Called after each test method. */
void tearDown()
{
}

/* Called at the beginning of the whole suite. */
void suiteSetUp()
{
}

/* Called at the end of the whole suite. */
int suiteTearDown( int numFailures )
{
    return numFailures;
}

/* ============================   Testing MQTT_Init ========================= */

/**
 * @brief Test that MQTT_Init is able to update the context object correctly.
 */
void test_MQTT_Init_happy_path( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    TEST_ASSERT_EQUAL( MQTTNotConnected, context.connectStatus );
    TEST_ASSERT_EQUAL( MQTT_NEXT_PACKET_ID_START, context.nextPacketId );
    /* These Unity assertions take pointers and compare their contents. */
    TEST_ASSERT_EQUAL_MEMORY( &transport, &context.transportInterface, sizeof( transport ) );
    TEST_ASSERT_EQUAL_MEMORY( &callbacks, &context.callbacks, sizeof( callbacks ) );
    TEST_ASSERT_EQUAL_MEMORY( &networkBuffer, &context.networkBuffer, sizeof( networkBuffer ) );
}

/**
 * @brief Test that any NULL parameter causes MQTT_Init to return MQTTBadParameter.
 */
void test_MQTT_Init_invalid_params( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    /* Check that MQTTBadParameter is returned if any NULL parameters are passed. */
    mqttStatus = MQTT_Init( NULL, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );

    mqttStatus = MQTT_Init( &context, NULL, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );

    mqttStatus = MQTT_Init( &context, &transport, NULL, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, NULL );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );
}

/* ======================  Testing MQTT_ProcessLoop ========================= */

/**
 * @brief Initialize pNetworkBuffer using static buffer.
 *
 * @param[in] pNetworkBuffer Network buffer provided for the context.
 */
static void setupNetworkBuffer( MQTTFixedBuffer_t * const pNetworkBuffer )
{
    pNetworkBuffer->pBuffer = mqttBuffer;
    pNetworkBuffer->size = MQTT_TEST_BUFFER_LENGTH;
}

/**
 * @brief Mocked MQTT event callback.
 */
static void eventCallback( MQTTContext_t * pContext,
                           MQTTPacketInfo_t * pPacketInfo,
                           uint16_t packetIdentifier,
                           MQTTPublishInfo_t * pPublishInfo )
{
    ( void ) pContext;
    ( void ) pPacketInfo;
    ( void ) packetIdentifier;
    ( void ) pPublishInfo;
}

/**
 * @brief A mocked timer query function that increments on every call. This
 * guarantees that only a single iteration runs in the ProcessLoop for ease
 * of testing.
 */
static uint32_t getTime( void )
{
    return globalEntryTime++;
}

/**
 * @brief Mocked successful transport send.
 */
static int32_t transportSendSuccess( MQTTNetworkContext_t pContext,
                                     const void * pBuffer,
                                     size_t bytesToWrite )
{
    ( void ) pContext;
    ( void ) pBuffer;
    return bytesToWrite;
}

/**
 * @brief Mocked successful transport read.
 */
static int32_t transportRecvSuccess( MQTTNetworkContext_t pContext,
                                     void * pBuffer,
                                     size_t bytesToRead )
{
    ( void ) pContext;
    ( void ) pBuffer;
    return bytesToRead;
}

/**
 * @brief Initialize the transport interface with the mocked functions for
 * send and receive.
 */
static void setupTransportInterface( MQTTTransportInterface_t * pTransport,
                                     MQTTTransportSendFunc_t sendFunc,
                                     MQTTTransportRecvFunc_t recvFunc )
{
    pTransport->networkContext = 0;
    pTransport->send = sendFunc;
    pTransport->recv = recvFunc;
}

/**
 * @brief Initialize our event and time callback with the mocked functions
 * defined for the purposes this test.
 */
static void setupCallbacks( MQTTApplicationCallbacks_t * pCallbacks )
{
    pCallbacks->appCallback = eventCallback;
    pCallbacks->getTime = getTime;
}

/**
 * @brief MQTT_GetIncomingPacketTypeAndLength callback used by CMock for setting
 * the type of the incoming packet to the same value as currentPacketType then
 * returns modifyIncomingPacketStatus (global variables).
 */
static MQTTStatus_t modifyIncomingPacket( MQTTTransportRecvFunc_t readFunc,
                                          MQTTNetworkContext_t networkContext,
                                          MQTTPacketInfo_t * pIncomingPacket,
                                          int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = currentPacketType;
    pIncomingPacket->remainingLength = MQTT_SAMPLE_REMAINING_LENGTH;
    return modifyIncomingPacketStatus;
}

/**
 * @brief MQTT_GetPingreqPacketSize callback used by CMock for setting the size
 * of a PINGREQ packet and returns successfully.
 */
static MQTTStatus_t modifyPacketSize( size_t * pPacketSize,
                                      int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) cmock_num_calls;

    *pPacketSize = MQTT_PACKET_PINGREQ_SIZE;
    return modifyPacketSizeStatus;
}

/**
 * @brief This helper function is used to expect any calls from the process loop
 * to mocked functions belonging to an external header file. Its parameters
 * are used to provide return values for these mocked functions.
 */
static void expectProcessLoopCalls( MQTTContext_t * const pContext,
                                    MQTTStatus_t deserializeStatus,
                                    MQTTPublishState_t stateAfterDeserialize,
                                    MQTTStatus_t serializeStatus,
                                    MQTTPublishState_t stateAfterSerialize,
                                    MQTTStatus_t processLoopStatus,
                                    bool incomingPublish )
{
    MQTTStatus_t mqttStatus = MQTTSuccess;
    bool expectMoreCalls = true;

    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacket );

    /* More calls are expected only with the following packet types. */
    if( ( currentPacketType != MQTT_PACKET_TYPE_PUBLISH ) &&
        ( currentPacketType != MQTT_PACKET_TYPE_PUBACK ) &&
        ( currentPacketType != MQTT_PACKET_TYPE_PUBREC ) &&
        ( currentPacketType != MQTT_PACKET_TYPE_PUBREL ) &&
        ( currentPacketType != MQTT_PACKET_TYPE_PUBCOMP ) &&
        ( currentPacketType != MQTT_PACKET_TYPE_PINGRESP ) &&
        ( currentPacketType != MQTT_PACKET_TYPE_SUBACK ) &&
        ( currentPacketType != MQTT_PACKET_TYPE_UNSUBACK ) )
    {
        expectMoreCalls = false;
    }

    /* When no data is available, the process loop tries to send a PINGREQ. */
    if( modifyIncomingPacketStatus == MQTTNoDataAvailable )
    {
        if( ( pContext->waitingForPingResp == false ) &&
            ( pContext->keepAliveIntervalSec != 0U ) )
        {
            MQTT_GetPingreqPacketSize_Stub( modifyPacketSize );
            MQTT_SerializePingreq_ExpectAnyArgsAndReturn( serializeStatus );
        }

        expectMoreCalls = false;
    }

    /* Deserialize based on the packet type (PUB or ACK) being received. */
    if( expectMoreCalls )
    {
        if( incomingPublish )
        {
            MQTT_DeserializePublish_ExpectAnyArgsAndReturn( deserializeStatus );
        }
        else
        {
            MQTT_DeserializeAck_ExpectAnyArgsAndReturn( deserializeStatus );
        }

        if( ( deserializeStatus != MQTTSuccess ) ||
            ( currentPacketType == MQTT_PACKET_TYPE_PINGRESP ) ||
            ( currentPacketType == MQTT_PACKET_TYPE_SUBACK ) ||
            ( currentPacketType == MQTT_PACKET_TYPE_UNSUBACK ) )
        {
            expectMoreCalls = false;
        }
    }

    /* Update state based on the packet type (PUB or ACK) being received. */
    if( expectMoreCalls )
    {
        if( incomingPublish )
        {
            MQTT_UpdateStatePublish_ExpectAnyArgsAndReturn( stateAfterDeserialize );
        }
        else
        {
            MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( stateAfterDeserialize );
        }

        if( stateAfterDeserialize == MQTTPublishDone )
        {
            expectMoreCalls = false;
        }
    }

    /* Serialize the packet to be sent in response to the received packet.
     * Observe that there is no reason to serialize a PUBLISH after receiving
     * a packet. */
    if( expectMoreCalls )
    {
        MQTT_SerializeAck_ExpectAnyArgsAndReturn( serializeStatus );

        if( serializeStatus != MQTTSuccess )
        {
            expectMoreCalls = false;
        }
    }

    /* Update the state based on the sent packet. */
    if( expectMoreCalls )
    {
        MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( stateAfterSerialize );
    }

    /* Expect the above calls when running MQTT_ProcessLoop. */
    mqttStatus = MQTT_ProcessLoop( pContext, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( processLoopStatus, mqttStatus );

    /* Any final assertions to end the test. */
    if( mqttStatus == MQTTSuccess )
    {
        if( currentPacketType == MQTT_PACKET_TYPE_PUBLISH )
        {
            TEST_ASSERT_TRUE( pContext->controlPacketSent );
        }

        if( currentPacketType == MQTT_PACKET_TYPE_PINGRESP )
        {
            TEST_ASSERT_FALSE( pContext->waitingForPingResp );
        }
    }
}

/**
 * @brief Test that NULL pContext causes MQTT_ProcessLoop to return MQTTBadParameter.
 */
void test_MQTT_ProcessLoop_invalid_params( void )
{
    MQTTStatus_t mqttStatus = MQTT_ProcessLoop( NULL, MQTT_NO_TIMEOUT_MS );

    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );
}

/**
 * @brief This test case covers all calls to the private method,
 * handleIncomingPublish(...),
 * that results in the process loop returning successfully.
 */
void test_MQTT_ProcessLoop_handleIncomingPublish_happy_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    modifyIncomingPacketStatus = MQTTSuccess;

    /* Assume QoS = 1 so that a PUBACK will be sent after receiving PUBLISH. */
    currentPacketType = MQTT_PACKET_TYPE_PUBLISH;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTPubAckSend,
                            MQTTSuccess, MQTTPublishDone,
                            MQTTSuccess, true );

    /* Assume QoS = 2 so that a PUBREC will be sent after receiving PUBLISH. */
    currentPacketType = MQTT_PACKET_TYPE_PUBLISH;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTPubRecSend,
                            MQTTSuccess, MQTTPubRelPending,
                            MQTTSuccess, true );
}

/**
 * @brief This test case covers all calls to the private method,
 * handleIncomingPublish(...),
 * that results in the process loop returning an error.
 */
void test_MQTT_ProcessLoop_handleIncomingPublish_error_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    modifyIncomingPacketStatus = MQTTSuccess;

    /* Verify that error is propagated when deserialization fails by returning
     * MQTTBadResponse. Any parameters beyond that are actually irrelevant
     * because they are only used as return values for non-expected calls. */
    currentPacketType = MQTT_PACKET_TYPE_PUBLISH;
    expectProcessLoopCalls( &context, MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, true );
}

/**
 * @brief This test case covers all calls to the private method,
 * handleIncomingAck(...),
 * that results in the process loop returning successfully.
 */
void test_MQTT_ProcessLoop_handleIncomingAck_happy_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    modifyIncomingPacketStatus = MQTTSuccess;

    /* Mock the receiving of a PUBACK packet type and expect the appropriate
     * calls made from the process loop. */
    currentPacketType = MQTT_PACKET_TYPE_PUBACK;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTPublishDone,
                            MQTTSuccess, MQTTPublishDone,
                            MQTTSuccess, false );

    /* Mock the receiving of a PUBREC packet type and expect the appropriate
     * calls made from the process loop. */
    currentPacketType = MQTT_PACKET_TYPE_PUBREC;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTPubRelSend,
                            MQTTSuccess, MQTTPubCompPending,
                            MQTTSuccess, false );

    /* Mock the receiving of a PUBREL packet type and expect the appropriate
     * calls made from the process loop. */
    currentPacketType = MQTT_PACKET_TYPE_PUBREL;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTPubCompSend,
                            MQTTSuccess, MQTTPublishDone,
                            MQTTSuccess, false );

    /* Mock the receiving of a PUBCOMP packet type and expect the appropriate
     * calls made from the process loop. */
    currentPacketType = MQTT_PACKET_TYPE_PUBCOMP;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTPublishDone,
                            MQTTSuccess, MQTTPublishDone,
                            MQTTSuccess, false );

    /* Mock the receiving of a PINGRESP packet type and expect the appropriate
     * calls made from the process loop. */
    currentPacketType = MQTT_PACKET_TYPE_PINGRESP;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, false );

    /* Mock the receiving of a SUBACK packet type and expect the appropriate
     * calls made from the process loop. */
    currentPacketType = MQTT_PACKET_TYPE_SUBACK;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, false );

    /* Mock the receiving of an UNSUBACK packet type and expect the appropriate
     * calls made from the process loop. */
    currentPacketType = MQTT_PACKET_TYPE_UNSUBACK;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, false );
}

/**
 * @brief This test case covers all calls to the private method,
 * handleIncomingAck(...),
 * that results in the process loop returning an error.
 */
void test_MQTT_ProcessLoop_handleIncomingAck_error_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    modifyIncomingPacketStatus = MQTTSuccess;

    /* Verify that MQTTBadResponse is propagated when deserialization fails upon
     * receiving an unknown packet type. */
    currentPacketType = MQTT_PACKET_TYPE_INVALID;
    expectProcessLoopCalls( &context, MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, false );

    /* Verify that MQTTSendFailed is propagated when receiving a PUBREC
     * then failing when serializing a PUBREL to send in response. */
    currentPacketType = MQTT_PACKET_TYPE_PUBREC;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTPubRelSend,
                            MQTTNoMemory, MQTTStateNull,
                            MQTTSendFailed, false );

    /* Verify that MQTTBadResponse is propagated when deserialization fails upon
     * receiving a PUBACK. */
    currentPacketType = MQTT_PACKET_TYPE_PUBACK;
    expectProcessLoopCalls( &context, MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, false );

    /* Verify that MQTTBadResponse is propagated when deserialization fails upon
     * receiving a PINGRESP. */
    currentPacketType = MQTT_PACKET_TYPE_PINGRESP;
    expectProcessLoopCalls( &context, MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, false );

    /* Verify that MQTTBadResponse is propagated when deserialization fails upon
     * receiving a SUBACK. */
    currentPacketType = MQTT_PACKET_TYPE_SUBACK;
    expectProcessLoopCalls( &context, MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, MQTTStateNull,
                            MQTTBadResponse, false );

    /* Verify that MQTTIllegalState is returned if MQTT_UpdateStateAck(...)
     * provides an unknown state such as MQTTStateNull to sendPublishAcks(...). */
    currentPacketType = MQTT_PACKET_TYPE_PUBREC;
    expectProcessLoopCalls( &context, MQTTSuccess, MQTTPubRelSend,
                            MQTTSuccess, MQTTStateNull,
                            MQTTIllegalState, false );
}

/**
 * @brief This test case covers all calls to the private method,
 * handleKeepAlive(...),
 * that results in the process loop returning successfully.
 */
void test_MQTT_ProcessLoop_handleKeepAlive_happy_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    modifyIncomingPacketStatus = MQTTNoDataAvailable;
    globalEntryTime = MQTT_ONE_SECOND_TO_MS;

    /* Coverage for the branch path where keep alive interval is greater than 0. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    context.waitingForPingResp = false;
    context.keepAliveIntervalSec = 0;
    expectProcessLoopCalls( &context, MQTTStateNull, MQTTStateNull,
                            MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, false );

    /* Coverage for the branch path where keep alive interval is greater than 0,
     * but the interval has expired yet. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    context.waitingForPingResp = true;
    context.keepAliveIntervalSec = 1;
    context.lastPacketTime = getTime();
    expectProcessLoopCalls( &context, MQTTStateNull, MQTTStateNull,
                            MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, false );

    /* Coverage for the branch path where PING timeout interval hasn't expired. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    context.waitingForPingResp = true;
    context.keepAliveIntervalSec = 1;
    context.lastPacketTime = 0;
    context.pingReqSendTimeMs = MQTT_ONE_SECOND_TO_MS;
    context.pingRespTimeoutMs = MQTT_ONE_SECOND_TO_MS;
    expectProcessLoopCalls( &context, MQTTStateNull, MQTTStateNull,
                            MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, false );

    /* Coverage for the branch path where a PING hasn't been sent out yet. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    context.waitingForPingResp = false;
    context.keepAliveIntervalSec = 1;
    context.lastPacketTime = 0;
    expectProcessLoopCalls( &context, MQTTStateNull, MQTTStateNull,
                            MQTTSuccess, MQTTStateNull,
                            MQTTSuccess, false );
}

/**
 * @brief This test case covers all calls to the private method,
 * handleKeepAlive(...),
 * that results in the process loop returning an error.
 */
void test_MQTT_ProcessLoop_handleKeepAlive_error_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    modifyIncomingPacketStatus = MQTTNoDataAvailable;
    globalEntryTime = MQTT_ONE_SECOND_TO_MS;

    /* Coverage for the branch path where PING timeout interval hasn't expired. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    context.lastPacketTime = 0;
    context.keepAliveIntervalSec = 1;
    context.waitingForPingResp = true;
    expectProcessLoopCalls( &context, MQTTStateNull, MQTTStateNull,
                            MQTTSuccess, MQTTStateNull,
                            MQTTKeepAliveTimeout, false );
}

/**
 * @brief This test mocks a failing transport receive and runs multiple
 * iterations of the process loop, resulting in returning MQTTRecvFailed.
 * This allows us to have full branch and line coverage.
 */
void test_MQTT_ProcessLoop_multiple_iterations( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    MQTT_GetIncomingPacketTypeAndLength_ExpectAnyArgsAndReturn( MQTTRecvFailed );
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_SAMPLE_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTRecvFailed, mqttStatus );
}

/* ========================  Testing MQTT_Subscribe ========================= */

/**
 * @brief Mocked failed transport send.
 */
static int32_t transportSendFail( MQTTNetworkContext_t pContext,
                                  const void * pBuffer,
                                  size_t bytesToWrite )
{
    ( void ) pContext;
    ( void ) pBuffer;
    ( void ) bytesToWrite;
    return -1;
}

/**
 * @brief Mocked failed transport read.
 */
static int32_t transportRecvFail( MQTTNetworkContext_t pContext,
                                  void * pBuffer,
                                  size_t bytesToRead )
{
    ( void ) pContext;
    ( void ) pBuffer;
    ( void ) bytesToRead;
    return -1;
}

/**
 * @brief Initialize pSubscribeInfo using test-defined macros.
 *
 * @param[in] pSubscribeInfo Pointer to MQTT subscription info.
 */
static void setupSubscriptionInfo( MQTTSubscribeInfo_t * pSubscribeInfo )
{
    pSubscribeInfo->qos = MQTTQoS1;
    pSubscribeInfo->pTopicFilter = MQTT_SAMPLE_TOPIC_FILTER;
    pSubscribeInfo->topicFilterLength = MQTT_SAMPLE_TOPIC_FILTER_LENGTH;
}

/**
 * @brief MQTT_GetSubscribePacketSize callback used by CMock for setting the size
 * and remaining length of a SUBSCRIBE or UNSUBSCRIBE packet.
 */
static MQTTStatus_t modifySubscribeUnsubscribePacketSize( const MQTTSubscribeInfo_t * const pSubscriptionList,
                                                          size_t subscriptionCount,
                                                          size_t * pRemainingLength,
                                                          size_t * pPacketSize,
                                                          int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) pSubscriptionList;
    ( void ) subscriptionCount;
    ( void ) cmock_num_calls;

    *pRemainingLength = MQTT_SAMPLE_REMAINING_LENGTH;
    *pPacketSize = MQTT_SAMPLE_REMAINING_LENGTH;

    return MQTTSuccess;
}

/**
 * @brief This test case verifies that MQTT_Subscribe returns MQTTBadParameter
 * with an invalid parameter. This test case also gives us coverage over
 * the private method, validateSubscribeUnsubscribeParams(...).
 */
void test_MQTT_Subscribe_invalid_params( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTSubscribeInfo_t subscribeInfo;

    /* Call subscribe with a NULL context. */
    mqttStatus = MQTT_Subscribe( NULL, &subscribeInfo, 1,
                                 MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );

    /* Call subscribe with a NULL subscription list. */
    mqttStatus = MQTT_Subscribe( &context, NULL, 1,
                                 MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );

    /* Call subscribe with 0 subscriptions. */
    mqttStatus = MQTT_Subscribe( &context, &subscribeInfo, 0,
                                 MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );

    /* Packet ID cannot be 0 per MQTT 3.1.1 spec. */
    mqttStatus = MQTT_Subscribe( &context, &subscribeInfo, 1,
                                 0 );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );
}

/**
 * @brief This test case verifies that MQTT_Subscribe returns successfully
 * when valid parameters are passed and all bytes are sent.
 */
void test_MQTT_Subscribe_happy_path( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;
    MQTTSubscribeInfo_t subscribeInfo;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );
    setupSubscriptionInfo( &subscribeInfo );

    /* Initialize context. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    /* Verify MQTTSuccess is returned with the following mocks. */
    MQTT_GetSubscribePacketSize_Stub( modifySubscribeUnsubscribePacketSize );
    MQTT_SerializeSubscribe_ExpectAnyArgsAndReturn( MQTTSuccess );
    /* Expect the above calls when running MQTT_Subscribe. */
    mqttStatus = MQTT_Subscribe( &context, &subscribeInfo, 1,
                                 MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
}

/**
 * @brief This test case verifies that MQTT_Subscribe returns MQTTSendFailed
 * if transport interface send returns an error.
 */
void test_MQTT_Subscribe_error_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;
    MQTTSubscribeInfo_t subscribeInfo;

    /* Verify that an error is propagated when transport interface returns an error. */
    setupTransportInterface( &transport,
                             transportSendFail, transportRecvFail );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );
    setupSubscriptionInfo( &subscribeInfo );

    /* Initialize context. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    /* Verify MQTTSendFailed is propagated when transport interface returns an error. */
    MQTT_GetSubscribePacketSize_Stub( modifySubscribeUnsubscribePacketSize );
    MQTT_SerializeSubscribe_ExpectAnyArgsAndReturn( MQTTSuccess );
    /* Expect the above calls when running MQTT_Subscribe. */
    mqttStatus = MQTT_Subscribe( &context, &subscribeInfo, 1,
                                 MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTSendFailed, mqttStatus );
}

/* ======================  Testing MQTT_Unsubscribe ========================= */

/**
 * @brief This test case verifies that MQTT_Unsubscribe returns MQTTBadParameter
 * with an invalid parameter. This test case also gives us coverage over
 * the private method, validateSubscribeUnsubscribeParams(...).
 */
void test_MQTT_Unsubscribe_invalid_params( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTSubscribeInfo_t subscribeInfo;

    /* Call subscribe with a NULL context. */
    mqttStatus = MQTT_Unsubscribe( NULL, &subscribeInfo, 1,
                                   MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );

    /* Call subscribe with a NULL subscription list. */
    mqttStatus = MQTT_Unsubscribe( &context, NULL, 1,
                                   MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );

    /* Call subscribe with 0 subscriptions. */
    mqttStatus = MQTT_Unsubscribe( &context, &subscribeInfo, 0,
                                   MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );

    /* Packet ID cannot be 0 per MQTT 3.1.1 spec. */
    mqttStatus = MQTT_Unsubscribe( &context, &subscribeInfo, 1,
                                   0 );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );
}

/**
 * @brief This test case verifies that MQTT_Unsubscribe returns successfully
 * when valid parameters are passed and all bytes are sent.
 */
void test_MQTT_Unsubscribe_happy_path( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;
    MQTTSubscribeInfo_t subscribeInfo;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );
    setupSubscriptionInfo( &subscribeInfo );

    /* Initialize context. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    /* Verify MQTTSuccess is returned with the following mocks. */
    MQTT_GetUnsubscribePacketSize_Stub( modifySubscribeUnsubscribePacketSize );
    MQTT_SerializeUnsubscribe_ExpectAnyArgsAndReturn( MQTTSuccess );
    /* Expect the above calls when running MQTT_Unsubscribe. */
    mqttStatus = MQTT_Unsubscribe( &context, &subscribeInfo, 1,
                                   MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
}

/**
 * @brief This test case verifies that MQTT_Unsubscribe returns MQTTSendFailed
 * if transport interface send returns an error.
 */
void test_MQTT_Unsubscribe_error_path( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;
    MQTTSubscribeInfo_t subscribeInfo;

    /* Verify that an error is propagated when transport interface returns an error. */
    setupTransportInterface( &transport,
                             transportSendFail, transportRecvFail );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );
    setupSubscriptionInfo( &subscribeInfo );

    /* Initialize context. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    /* Verify MQTTSendFailed is propagated when transport interface returns an error. */
    MQTT_GetUnsubscribePacketSize_Stub( modifySubscribeUnsubscribePacketSize );
    MQTT_SerializeUnsubscribe_ExpectAnyArgsAndReturn( MQTTSuccess );
    /* Expect the above calls when running MQTT_Unsubscribe. */
    mqttStatus = MQTT_Unsubscribe( &context, &subscribeInfo, 1,
                                   MQTT_NEXT_PACKET_ID_START );
    TEST_ASSERT_EQUAL( MQTTSendFailed, mqttStatus );
}

/* =========================  Testing MQTT_Ping ============================= */

/**
 * @brief This test case verifies that MQTT_Ping returns MQTTBadParameter
 * with context parameter is NULL.
 */
void test_MQTT_Ping_invalid_params( void )
{
    MQTTStatus_t mqttStatus;

    /* Call ping with a NULL context. */
    mqttStatus = MQTT_Ping( NULL );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );
}

/**
 * @brief This test case verifies that MQTT_Ping returns successfully
 * when valid parameters are passed and all bytes are sent.
 */
void test_MQTT_Ping_happy_path( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport,
                             transportSendSuccess, transportRecvSuccess );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    /* Initialize context. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    /* Verify MQTTSuccess is returned. */
    modifyPacketSizeStatus = MQTTSuccess;
    MQTT_GetPingreqPacketSize_Stub( modifyPacketSize );
    MQTT_SerializePingreq_ExpectAnyArgsAndReturn( MQTTSuccess );
    /* Expect the above calls when running MQTT_Ping. */
    mqttStatus = MQTT_Ping( &context );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    TEST_ASSERT_EQUAL( context.lastPacketTime, context.pingReqSendTimeMs );
    TEST_ASSERT_TRUE( context.waitingForPingResp );
}

/**
 * @brief This test case verifies that MQTT_Ping returns MQTTSendFailed
 * if transport interface send returns an error.
 */
void test_MQTT_Ping_error_path( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport,
                             transportSendFail, transportRecvFail );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    /* Initialize context. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    /* Verify MQTTSendFailed is propagated when transport interface returns an error. */
    modifyPacketSizeStatus = MQTTSuccess;
    MQTT_GetPingreqPacketSize_Stub( modifyPacketSize );
    MQTT_SerializePingreq_ExpectAnyArgsAndReturn( MQTTSuccess );
    /* Expect the above calls when running MQTT_Ping. */
    mqttStatus = MQTT_Ping( &context );
    TEST_ASSERT_EQUAL( MQTTSendFailed, mqttStatus );


    /* Initialize context. */
    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
    /* Verify MQTTBadParameter is propagated when getting PINGREQ packet size fails. */
    modifyPacketSizeStatus = MQTTBadParameter;
    MQTT_GetPingreqPacketSize_Stub( modifyPacketSize );
    /* Expect the above calls when running MQTT_Ping. */
    mqttStatus = MQTT_Ping( &context );
    TEST_ASSERT_EQUAL( MQTTBadParameter, mqttStatus );
}

/* ======================  Testing MQTT_GetPacketId ========================= */

/**
 * @brief This test case verifies that MQTT_GetPacketId returns the current
 * packet ID from the MQTT context and should force it to 1 upon overflow.
 */
void test_MQTT_GetPacketId_complete( void )
{
    MQTTContext_t context;

    /* MQTT_GetPacketId should increment on every call. */
    context.nextPacketId = MQTT_NEXT_PACKET_ID_START;
    TEST_ASSERT_EQUAL( MQTT_NEXT_PACKET_ID_START,
                       MQTT_GetPacketId( &context ) );
    TEST_ASSERT_EQUAL( MQTT_NEXT_PACKET_ID_START + 1,
                       MQTT_GetPacketId( &context ) );

    /* Upon overflow, MQTT_GetPacketId should force next packet ID to 1. */
    context.nextPacketId = UINT16_MAX;
    TEST_ASSERT_EQUAL( UINT16_MAX,
                       MQTT_GetPacketId( &context ) );
    TEST_ASSERT_EQUAL( 1,
                       MQTT_GetPacketId( &context ) );
}

/* ========================================================================== */
