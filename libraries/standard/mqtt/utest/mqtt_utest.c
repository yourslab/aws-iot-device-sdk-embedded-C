#include <stdbool.h>

#include "unity.h"

#include "mock_mqtt_lightweight.h"

/* Include paths for public enums, structures, and macros. */
#include "mqtt.h"

#define MQTT_TEST_BUFFER_LENGTH    ( 1024 )

static uint8_t buffer[ MQTT_TEST_BUFFER_LENGTH ] = { 0 };

/* ============================   UNITY FIXTURES ============================ */
void setUp( void )
{
}

/* called before each testcase */
void tearDown( void )
{
}

/* called at the beginning of the whole suite */
void suiteSetUp()
{
}

/* called at the end of the whole suite */
int suiteTearDown( int numFailures )
{
}

/* ============================   Testing MQTT_Init ========================= */
void test_Mqtt_Init_complete()
{
    MQTTContext_t context;
    MQTTTransportInterface_t transport;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;

    MQTT_Init( &context, &transport, &callbacks, &networkBuffer );
    TEST_ASSERT_EQUAL( context.connectStatus, MQTTNotConnected );
    /* These Unity assertions take pointers and compare their contents. */
    TEST_ASSERT_EQUAL_MEMORY( &context.transportInterface, &transport, sizeof( transport ) );
    TEST_ASSERT_EQUAL_MEMORY( &context.callbacks, &callbacks, sizeof( callbacks ) );
    TEST_ASSERT_EQUAL_MEMORY( &context.networkBuffer, &networkBuffer, sizeof( networkBuffer ) );
}

/* =====================  Testing MQTT_SerializeConnect ===================== */
void test_Mqtt_SerializeConnect_invalid_params()
{
    MQTTStatus_t mqttStatus = MQTTSuccess;
    size_t remainingLength, packetSize;
    MQTTFixedBuffer_t networkBuffer;
    MQTTConnectInfo_t connectInfo;

    mqttStatus = MQTT_SerializeConnect( NULL, NULL, remainingLength, &networkBuffer );
    TEST_ASSERT_EQUAL( mqttStatus, MQTTBadParameter );

    mqttStatus = MQTT_SerializeConnect( &connectInfo, NULL, remainingLength, &networkBuffer );
    TEST_ASSERT_EQUAL( mqttStatus, MQTTBadParameter );
}
