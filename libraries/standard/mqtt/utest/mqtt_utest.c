#include <time.h>

#include "unity.h"

#include "mock_mqtt_state.h"
#include "mock_mqtt_lightweight.h"

/* Include paths for public enums, structures, and macros. */
#include "mqtt.h"

/**
 * @brief A valid starting packet ID per MQTT spec. Start from 1.
 */
#define MQTT_NEXT_PACKET_ID_START    ( 1 )

/**
 * @brief Timeout for receiving a packet.
 */
#define MQTT_NO_TIMEOUT_MS           ( 0U )

/**
 * @brief Length of the MQTT network buffer.
 */
#define MQTT_TEST_BUFFER_LENGTH      ( 2056 )

static uint32_t globalEntryTime = 0;

static uint8_t mqttBuffer[ MQTT_TEST_BUFFER_LENGTH ] = { 0 };

/* ============================   UNITY FIXTURES ============================ */

/* Called before each test method. */
void setUp( void )
{
    globalEntryTime = 0;
}

/* Called after each test method. */
void tearDown( void )
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

static void setupTransportInterface( MQTTTransportInterface_t * pTransport )
{
    pTransport->networkContext = 0;
    pTransport->send = transportSendSuccess;
    pTransport->recv = transportRecvSuccess;
}

static void setupCallbacks( MQTTApplicationCallbacks_t * pCallbacks )
{
    pCallbacks->appCallback = eventCallback;
    pCallbacks->getTime = getTime;
}

/**
 * @brief Test that NULL pContext causes MQTT_ProcessLoop to return MQTTBadParameter.
 */
void test_MQTT_ProcessLoop_invalid_params( void )
{
    MQTT_ProcessLoop( NULL, MQTT_NO_TIMEOUT_MS );
}

/* Mocked MQTT_GetIncomingPacketTypeAndLength callback that modifies pIncomingPacket
 * to get full coverage on handleIncomingPublish. */
static MQTTStatus_t modifyIncomingPacketPublish( MQTTTransportRecvFunc_t readFunc,
                                                 MQTTNetworkContext_t networkContext,
                                                 MQTTPacketInfo_t * pIncomingPacket,
                                                 int cmock_num_calls )
{
    /* Remove unsued parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = MQTT_PACKET_TYPE_PUBLISH;
    return MQTTSuccess;
}

/* Mocked MQTT_GetIncomingPacketTypeAndLength callback that modifies pIncomingPacket
 * to get full coverage on handleIncomingAck by setting the type to PUBACK. */
static MQTTStatus_t modifyIncomingPacketPubAck( MQTTTransportRecvFunc_t readFunc,
                                                MQTTNetworkContext_t networkContext,
                                                MQTTPacketInfo_t * pIncomingPacket,
                                                int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = MQTT_PACKET_TYPE_PUBACK;
    return MQTTSuccess;
}

/* Mocked MQTT_GetIncomingPacketTypeAndLength callback that modifies pIncomingPacket
 * to get full coverage on handleIncomingAck by setting the type to PUBREC. */
static MQTTStatus_t modifyIncomingPacketPubRec( MQTTTransportRecvFunc_t readFunc,
                                                MQTTNetworkContext_t networkContext,
                                                MQTTPacketInfo_t * pIncomingPacket,
                                                int cmock_num_calls )
{
    /* Remove unsued parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = MQTT_PACKET_TYPE_PUBREC;
    return MQTTSuccess;
}

/* Mocked MQTT_GetIncomingPacketTypeAndLength callback that modifies pIncomingPacket
 * to get full coverage on handleIncomingAck by setting the type to PUBREL. */
static MQTTStatus_t modifyIncomingPacketPubRel( MQTTTransportRecvFunc_t readFunc,
                                                MQTTNetworkContext_t networkContext,
                                                MQTTPacketInfo_t * pIncomingPacket,
                                                int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = MQTT_PACKET_TYPE_PUBREL;
    return MQTTSuccess;
}

/* Mocked MQTT_GetIncomingPacketTypeAndLength callback that modifies pIncomingPacket
 * to get full coverage on handleIncomingAck by setting the type to PUBCOMP. */
static MQTTStatus_t modifyIncomingPacketPubComp( MQTTTransportRecvFunc_t readFunc,
                                                 MQTTNetworkContext_t networkContext,
                                                 MQTTPacketInfo_t * pIncomingPacket,
                                                 int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = MQTT_PACKET_TYPE_PUBCOMP;
    return MQTTSuccess;
}

/* Mocked MQTT_GetIncomingPacketTypeAndLength callback that modifies pIncomingPacket
 * to get full coverage on handleIncomingAck by setting the type to PINGRESP. */
static MQTTStatus_t modifyIncomingPacketPingResp( MQTTTransportRecvFunc_t readFunc,
                                                  MQTTNetworkContext_t networkContext,
                                                  MQTTPacketInfo_t * pIncomingPacket,
                                                  int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = MQTT_PACKET_TYPE_PINGRESP;
    return MQTTSuccess;
}

/* Mocked MQTT_GetIncomingPacketTypeAndLength callback that modifies pIncomingPacket
 * to get full coverage on handleIncomingAck by setting the type to SUBACK. */
static MQTTStatus_t modifyIncomingPacketSubAck( MQTTTransportRecvFunc_t readFunc,
                                                MQTTNetworkContext_t networkContext,
                                                MQTTPacketInfo_t * pIncomingPacket,
                                                int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = MQTT_PACKET_TYPE_SUBACK;
    return MQTTSuccess;
}

/* Mocked MQTT_GetIncomingPacketTypeAndLength callback that modifies pIncomingPacket
 * to get full coverage on handleIncomingAck by setting the type to UNSUBACK. */
static MQTTStatus_t modifyIncomingPacketUnsubAck( MQTTTransportRecvFunc_t readFunc,
                                                  MQTTNetworkContext_t networkContext,
                                                  MQTTPacketInfo_t * pIncomingPacket,
                                                  int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = MQTT_PACKET_TYPE_UNSUBACK;
    return MQTTSuccess;
}

/* Mocked MQTT_GetIncomingPacketTypeAndLength callback that modifies pIncomingPacket
 * to get full coverage on handleIncomingAck by setting the type to CONNECT. */
static MQTTStatus_t modifyIncomingPacketConnect( MQTTTransportRecvFunc_t readFunc,
                                                 MQTTNetworkContext_t networkContext,
                                                 MQTTPacketInfo_t * pIncomingPacket,
                                                 int cmock_num_calls )
{
    /* Remove unused parameter warnings. */
    ( void ) readFunc;
    ( void ) networkContext;
    ( void ) cmock_num_calls;

    pIncomingPacket->type = MQTT_PACKET_TYPE_CONNECT;
    return MQTTSuccess;
}

/**
 * @brief Test coverage for handleIncomingPublish by using a CMock callback to
 * modify incomingPacket.
 */
void test_MQTT_ProcessLoop_handleIncomingPublish_happy_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Assume QoS = 1 so that a PUBACK will be sent after receiving PUBLISH. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPublish );
    MQTT_DeserializePublish_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStatePublish_ExpectAnyArgsAndReturn( MQTTPubAckSend );
    MQTT_SerializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPublishDone );

    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_TRUE( context.controlPacketSent );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Assume QoS = 2 so that a PUBREC will be sent after receiving PUBLISH. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPublish );
    MQTT_DeserializePublish_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStatePublish_ExpectAnyArgsAndReturn( MQTTPubRecSend );
    MQTT_SerializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPubRecPending );

    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_TRUE( context.controlPacketSent );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
}

/**
 * @brief Test coverage for handleIncomingPublish by using a CMock callback to
 * modify incomingPacket.
 */
void test_MQTT_ProcessLoop_handleIncomingPublish_sad_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Verify that error is propagated when deserialization fails. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPublish );
    MQTT_DeserializePublish_ExpectAnyArgsAndReturn( MQTTBadResponse );

    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTBadResponse, mqttStatus );
}

void expectPacketPath()
{
    /* Mock the receiving of a PUBREC packet type through a callback. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPubRec );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPubRelSend );
    MQTT_SerializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPubCompPending );
}

void test_MQTT_ProcessLoop_example( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    expectPacketPath();

    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
}

/**
 * @brief Test coverage for handleIncomingAck by using a CMock callback to
 * modify incomingPacket.
 */
void test_MQTT_ProcessLoop_handleIncomingAck_happy_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Mock the receiving of a PUBACK packet type through a callback. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPubAck );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPublishDone );

    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Mock the receiving of a PUBREC packet type through a callback. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPubRec );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPubRelSend );
    MQTT_SerializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPubCompPending );

    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Mock the receiving of a PUBREL packet type through a callback. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPubRel );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPubCompSend );
    MQTT_SerializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPublishDone );

    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Mock the receiving of a PUBCOMP packet type through a callback. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPubComp );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPublishDone );

    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Mock the receiving of a PINGRESP packet type through a callback. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPingResp );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_FALSE( context.waitingForPingResp );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Mock the receiving of a SUBACK packet type through a callback. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketSubAck );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Mock the receiving of an UNSUBACK packet type through a callback. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketUnsubAck );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );
}

void test_MQTT_ProcessLoop_handleIncomingAck_sad_paths( void )
{
    MQTTStatus_t mqttStatus;
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    setupTransportInterface( &transport );
    setupCallbacks( &callbacks );
    setupNetworkBuffer( &networkBuffer );

    mqttStatus = MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( MQTTSuccess, mqttStatus );

    /* Verify that error is propagated when deserialization fails upon
     * receiving a CONNECT or some other unknown packet type. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketConnect );
    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTBadResponse, mqttStatus );

    /* Verify that error is propagated when serialization fails upon
     * receiving a PUBREC then sending a PUBREL. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPubRec );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPubRelSend );
    MQTT_SerializeAck_ExpectAnyArgsAndReturn( MQTTNoMemory );
    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTSendFailed, mqttStatus );

    /* Verify that error is propagated when deserialization fails upon
     * receiving a PUBACK. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPubAck );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTBadResponse );
    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTBadResponse, mqttStatus );

    /* Verify that error is propagated when deserialization fails upon
     * receiving a PINGRESP. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPingResp );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTBadResponse );
    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_FALSE( context.waitingForPingResp );
    TEST_ASSERT_EQUAL( MQTTBadResponse, mqttStatus );

    /* Verify that error is propagated when deserialization fails upon
     * receiving a SUBACK. */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketSubAck );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTBadResponse );
    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTBadResponse, mqttStatus );

    /* Verify that MQTTIllegalState is returned if MQTT_UpdateStateAck(...)
     * provides an unknown state such as MQTTStateNull to sendPublishAcks(...). */
    MQTT_GetIncomingPacketTypeAndLength_Stub( modifyIncomingPacketPubRec );
    MQTT_DeserializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTPubRelSend );
    MQTT_SerializeAck_ExpectAnyArgsAndReturn( MQTTSuccess );
    MQTT_UpdateStateAck_ExpectAnyArgsAndReturn( MQTTStateNull );
    /* Run the method to test. */
    mqttStatus = MQTT_ProcessLoop( &context, MQTT_NO_TIMEOUT_MS );
    TEST_ASSERT_EQUAL( MQTTIllegalState, mqttStatus );
}
