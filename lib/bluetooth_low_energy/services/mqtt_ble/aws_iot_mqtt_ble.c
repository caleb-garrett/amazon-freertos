/*
 * Amazon FreeRTOS
 * Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file aws_iot_mqtt_ble.c
 * @brief GATT service for transferring MQTT packets over BLE
 */

/* Build using a config header, if provided. */
#ifdef AWS_IOT_CONFIG_FILE
    #include AWS_IOT_CONFIG_FILE
#endif

#include "aws_ble_config.h"
#include "aws_iot_mqtt_ble.h"
#include "task.h"
#include "FreeRTOSConfig.h"
#include "aws_json_utils.h"

#define mqttBLECHAR_CONTROL_UUID_TYPE \
{ \
    .uu.uu16 = mqttBLECHAR_CONTROL_UUID,\
    .ucType   = eBTuuidType16 \
}
#define mqttBLECHAR_TX_MESG_UUID_TYPE \
{  \
    .uu.uu16 = mqttBLECHAR_TX_MESG_UUID,\
    .ucType  = eBTuuidType16\
}

#define mqttBLECHAR_RX_MESG_UUID_TYPE \
{\
    .uu.uu16 = mqttBLECHAR_RX_MESG_UUID,\
    .ucType  = eBTuuidType16\
}

#define mqttBLECHAR_TX_LARGE_MESG_UUID_TYPE \
{\
    .uu.uu16 = mqttBLECHAR_TX_LARGE_MESG_UUID,\
    .ucType  = eBTuuidType16\
}

#define mqttBLECHAR_RX_LARGE_MESG_UUID_TYPE \
{\
    .uu.uu16 = mqttBLECHAR_RX_LARGE_MESG_UUID,\
    .ucType  = eBTuuidType16\
}
#define mqttBLECCFG_UUID_TYPE \
{\
    .uu.uu16 = mqttBLECCFG_UUID,\
    .ucType  = eBTuuidType16\
}
/**
 * @brief UUID for Device Information Service.
 *
 * This UUID is used in advertisement for the companion apps to discover and connect to the device.
 */
#define mqttBLESERVICE_UUID_TYPE \
{ \
    .uu.uu128 = mqttBLESERVICE_UUID,\
    .ucType   = eBTuuidType128\
}

static uint16_t usCCCDTxMessage;
static uint16_t usCCCDTxLargeMessage;
static uint16_t pusHandlesBuffer[mqttBLEMAX_SVC_INSTANCES][eMQTTBLE_NUMBER];

#define CHAR_HANDLE( svc, ch_idx )        ( ( svc )->pxCharacteristics[ ch_idx ].xAttributeData.xHandle )
#define CHAR_UUID( svc, ch_idx )          ( ( svc )->pxCharacteristics[ ch_idx ].xAttributeData.xUuid )
#define DESCR_HANDLE( svc, descr_idx )    ( ( svc )->pxDescriptors[ descr_idx ].xAttributeData.xHandle )


/*-----------------------------------------------------------------------------------------------------*/
static MqttBLEService_t xMqttBLEServices[ mqttBLEMAX_SVC_INSTANCES ] = { 0 };
static uint16_t * pusPtrToHandleBuffer;

static  BTGattSrvcId_t xServiceCopy =  {
            .xId = 
            {
                .xUuid = mqttBLESERVICE_UUID_TYPE,
                .ucInstId  = 0
            },
            .xServiceType = eBTServiceTypePrimary
         };
static const BLEService_t xMQTTService = 
{
  .xNumberOfAttributes = eMQTTBLE_NUMBER,
  .ppusHandlesBuffer = &pusPtrToHandleBuffer,
  .pxBLEAttributes = 
  {
     {    
          .xAttributeType = eBTDbPrimaryService,
          .pxSrvcId = &xServiceCopy
     },
     {
         .xAttributeType = eBTDbCharacteristic,
         .xCharacteristic = 
         {
              .xUuid = mqttBLECHAR_CONTROL_UUID_TYPE,
              .xPermissions = ( bleconfigCHAR_READ_PERM | bleconfigCHAR_WRITE_PERM ),
              .xProperties = ( eBTPropRead | eBTPropWrite )
          }
     },
     {
         .xAttributeType = eBTDbCharacteristic,
         .xCharacteristic = 
         {
              .xUuid = mqttBLECHAR_TX_MESG_UUID_TYPE,
              .xPermissions = ( bleconfigCHAR_READ_PERM ),
              .xProperties = (  eBTPropRead | eBTPropNotify  )
          }
     },
     {
         .xAttributeType = eBTDbDescriptor,
         .xCharacteristic = 
         {
             .xUuid = mqttBLECCFG_UUID_TYPE,
             .xPermissions = ( bleconfigCHAR_READ_PERM | bleconfigCHAR_WRITE_PERM )
          }
     },
     {
         .xAttributeType = eBTDbCharacteristic,
         .xCharacteristic = 
         {
              .xUuid = mqttBLECHAR_RX_MESG_UUID_TYPE,
              .xPermissions = ( bleconfigCHAR_READ_PERM | bleconfigCHAR_WRITE_PERM ),
              .xProperties = ( eBTPropRead | eBTPropWrite )
          }
     },
     {
         .xAttributeType = eBTDbCharacteristic,
         .xCharacteristic = 
         {
              .xUuid = mqttBLECHAR_TX_LARGE_MESG_UUID_TYPE,
              .xPermissions = ( bleconfigCHAR_READ_PERM ),
              .xProperties = ( eBTPropRead | eBTPropNotify )
          }
     },
     {
         .xAttributeType = eBTDbDescriptor,
         .xCharacteristic = 
         {
             .xUuid = mqttBLECCFG_UUID_TYPE,
             .xPermissions = ( bleconfigCHAR_READ_PERM | bleconfigCHAR_WRITE_PERM )
          }
     },
     {
         .xAttributeType = eBTDbCharacteristic,
         .xCharacteristic = 
         {
              .xUuid = mqttBLECHAR_RX_LARGE_MESG_UUID_TYPE,
              .xPermissions = ( bleconfigCHAR_READ_PERM | bleconfigCHAR_WRITE_PERM ),
              .xProperties = ( eBTPropRead | eBTPropWrite )
          }
     }
  }
};


/**
 * Variable stores the MTU Size for the BLE connection.
 */
static uint16_t usBLEConnMTU;

/*------------------------------------------------------------------------------------------------------*/

/*
 * @brief Creates and starts  a GATT service instance.
 */
static BaseType_t prvInitServiceInstance( uint8_t ucInstId );

/*
 * @brief Gets an MQTT proxy service instance given a GATT service.
 */
static MqttBLEService_t * prxGetServiceInstance( uint16_t usHandle );


/**
 * @brief Reallocate a buffer; data is also copied from the old
 * buffer into the new buffer.
 *
 * @param[in] pucOldBuffer The current network receive buffer.
 * @param[in] xOldBufferSize The size (in bytes) of pucOldBuffer.
 * @param[in] xNewBufferSize Requested size of the reallocated buffer.
 * copied into the new buffer.
 *
 * @return Pointer to a new, reallocated buffer; NULL if buffer reallocation failed.
 */
static uint8_t * prvReallocBuffer( uint8_t * pucOldBuffer,
                                          size_t xOldBufferSize,
                                          size_t xNewBufferSize );

/*
 * @brief Callback to indicate that the service instance has started.
 */
static void vServiceStartedCb( BTStatus_t xStatus,
                               BLEService_t * pxService );

/*
 * @brief Callback to register for events (read) on TX message characteristic.
 */
void vTXMesgCharCallback( BLEAttributeEvent_t * pxEventParam );

/*
 * @brief Callback to register for events (write) on RX message characteristic.
 *
 */
void vRXMesgCharCallback( BLEAttributeEvent_t * pxEventParam );

/*
 * @brief Callback to register for events (read) on TX large message characteristic.
 * Buffers a large message and sends the message in chunks of size MTU at a time as response
 * to the read request. Keeps the buffer until last message is read.
 */
void vTXLargeMesgCharCallback( BLEAttributeEvent_t * pxEventParam );

/*
 * @brief Callback to register for events (write) on RX large message characteristic.
 * Copies the individual write packets into a buffer untill a packet less than BLE MTU size
 * is received. Sends the buffered message to the MQTT layer.
 */
void vRXLargeMesgCharCallback( BLEAttributeEvent_t * pxEventParam );

/*
 * @brief Callback for Client Characteristic Configuration Descriptor events.
 */
void vClientCharCfgDescrCallback( BLEAttributeEvent_t * pxEventParam );

/*
 * @brief This is the callback to the control characteristic. It is used to toggle ( turn on/off)
 * MQTT proxy service by the BLE IOS/Android app.
 */
void vToggleMQTTService( BLEAttributeEvent_t * pxEventParam );

/*
 * @brief Resets the send and receive buffer for the given MQTT Service.
 * Any pending MQTT connection should be closed or the service should be disabled
 * prior to resetting the buffer.
 *
 * @param[in]  pxService Pointer to the MQTT service.
 */
void prvResetBuffer( MqttBLEService_t* pxService );

/*
 * @brief Callback for BLE connect/disconnect. Toggles the Proxy state to off on a BLE disconnect.
 */
static void vConnectionCallback( BTStatus_t xStatus,
                                 uint16_t usConnId,
                                 bool bConnected,
                                 BTBdaddr_t * pxRemoteBdAddr );

/*
 * @brief Callback to receive notification when the MTU of the BLE connection changes.
 */
static void vMTUChangedCallback( uint16_t usConnId,
                                 uint16_t usMtu );

/*
 * @brief stdio.h conflict with posix library on some platform so using extern snprintf so not to include it.
 */
extern int snprintf( char *, size_t, const char *, ... );
/*------------------------------------------------------------------------------------*/

static const BLEAttributeEventCallback_t pxCallBackArray[eMQTTBLE_NUMBER] =
{
  NULL,
  vToggleMQTTService,
  vTXMesgCharCallback,
  vClientCharCfgDescrCallback,
  vRXMesgCharCallback,
  vTXLargeMesgCharCallback,
  vClientCharCfgDescrCallback,
  vRXLargeMesgCharCallback
};

/*-----------------------------------------------------------*/

static MqttBLEService_t * prxGetServiceInstance( uint16_t usHandle )
{
    uint8_t ucId;
    MqttBLEService_t * pxMQTTSvc = NULL;

    for( ucId = 0; ucId < mqttBLEMAX_SVC_INSTANCES; ucId++ )
    {
        /* Check that the handle is included in the service. */
        if(( usHandle > pusHandlesBuffer[ucId][0] )&&
          (usHandle <= pusHandlesBuffer[ucId][eMQTTBLE_NUMBER - 1]))
        {
            pxMQTTSvc = &xMqttBLEServices[ ucId ];
            break;
        }
    }

    return pxMQTTSvc;
}

/*-----------------------------------------------------------*/

static BaseType_t prxSendNotification( MqttBLEService_t * pxMQTTService,
                                MQTTBLEAttributes_t xCharacteristic,
                                uint8_t * pucData,
                                size_t xLen )
{
    BLEAttributeData_t xAttrData = { 0 };
    BLEEventResponse_t xResp;
    BaseType_t xStatus = pdFALSE;
pxMQTTService->pxService->ppusHandlesBuffer[xCharacteristic]
    xAttrData.xHandle = CHAR_HANDLE( pxMQTTService->pxService, xCharacteristic );
    xAttrData.xUuid = CHAR_UUID( pxMQTTService->pxService, xCharacteristic );
    xAttrData.pucData = pucData;
    xAttrData.xSize = xLen;
    xResp.pxAttrData = &xAttrData;
    xResp.xAttrDataOffset = 0;
    xResp.xEventStatus = eBTStatusSuccess;
    xResp.xRspErrorStatus = eBTRspErrorNone;

    if( BLE_SendIndication( &xResp, pxMQTTService->usBLEConnId, false ) == eBTStatusSuccess )
    {
        xStatus = pdTRUE;
    }

    return xStatus;
}

static uint8_t * prvReallocBuffer( uint8_t * pucOldBuffer,
                                          size_t xOldBufferSize,
                                          size_t xNewBufferSize )
{
    uint8_t * pucNewBuffer = NULL;

    /* This function should not be called with a smaller new buffer size or a
     * copy offset greater than the old buffer size. */
    configASSERT( xNewBufferSize >= xOldBufferSize );

    /* Allocate a larger receive buffer. */
    pucNewBuffer = pvPortMalloc( xNewBufferSize );

    /* Copy data into the new buffer. */
    if( pucNewBuffer != NULL )
    {
        ( void ) memcpy( pucNewBuffer,
                         pucOldBuffer,
                         xOldBufferSize );
    }

    /* Free the old buffer. */
    vPortFree( pucOldBuffer );

    return pucNewBuffer;
}

/*-----------------------------------------------------------*/

static BaseType_t prvInitServiceInstance( uint8_t ucInstId )
{
    BTStatus_t xStatus;
    BaseType_t xResult = pdFAIL;
    BLEEventsCallbacks_t xCallback;

      /* Change buffer and instance Id for each copy of the service created */
      pusPtrToHandleBuffer = pusHandlesBuffer[ucInstId];
      xServiceCopy.xId = ucInstId;

      xStatus = BLE_CreateService( (BLEService_t *)&xMQTTService, (BLEAttributeEventCallback_t *)pxCallBackArray );
      if(xStatus == eBTStatusSuccess)
      {
          xResult = pdPASS;
      }

      if( xResult == pdPASS )
      {
              xCallback.pxConnectionCb = vConnectionCallback;

              if( BLE_RegisterEventCb( eBLEConnection, xCallback ) != eBTStatusSuccess )
              {
                      xResult = pdFAIL;
              }
      }

    return xResult;
}

void prvResetBuffer( MqttBLEService_t* pxService )
{

    if( !( pxService->bIsInit ) || ( pxService->xConnection.pxMqttConnection == NULL ) )
    {
    	/*
    	 * Acquire send lock by blocking for a maximum wait time of send timeout.
    	 * This will ensure no more sender tasks enqueue the stream buffer.
    	 * Resets the stream buffer and release the lock.
    	 */
    	( void ) xSemaphoreTake( pxService->xConnection.xSendLock, pxService->xConnection.xSendTimeout  );

    	( void ) xStreamBufferReset( pxService->xConnection.xSendBuffer );

    	( void ) xSemaphoreGive( pxService->xConnection.xSendLock );


    	/*
    	 * Wait for completion of any receive callback.
    	 * Releases any pending receive buffer, Set the buffer to NULL,
    	 * reset the offset and length of the buffer to zero.
    	 */
    	( void ) xSemaphoreTake( pxService->xConnection.xRecvLock, portMAX_DELAY );

    	if( pxService->xConnection.pRecvBuffer != NULL )
    	{
    		vPortFree( pxService->xConnection.pRecvBuffer );
    		pxService->xConnection.pRecvBuffer = NULL;
    	}

    	pxService->xConnection.xRecvOffset = 0;
    	pxService->xConnection.xRecvBufferLen = 0;

    	( void ) xSemaphoreGive( pxService->xConnection.xRecvLock );
    }


}

/*-----------------------------------------------------------*/

void vToggleMQTTService( BLEAttributeEvent_t * pxEventParam )
{
    BLEWriteEventParams_t * pxWriteParam;
    BLEAttributeData_t xAttrData = { 0 };
    BLEEventResponse_t xResp;
    MqttBLEService_t * pxService;
    jsmntok_t xTokens[ mqttBLEMAX_TOKENS ];
    int16_t sNumTokens, sProxyEnable;
    BaseType_t xResult;
    char cMsg[ mqttBLESTATE_MSG_LEN + 1 ];

    pxService = prxGetServiceInstance( pxAttribute->pxCharacteristic->pxParentService );
    configASSERT( ( pxService != NULL ) );

    xResp.pxAttrData = &xAttrData;
    xResp.xRspErrorStatus = eBTRspErrorNone;
    xResp.xEventStatus = eBTStatusFail;
    xResp.xAttrDataOffset = 0;
    xResp.pxAttrData->xHandle = pxAttribute->pxCharacteristic->xAttributeData.xHandle;

    if( ( pxEventParam->xEventType == eBLEWrite ) || ( pxEventParam->xEventType == eBLEWriteNoResponse ) )
    {
        pxWriteParam = pxEventParam->pxParamWrite;

        if( !pxWriteParam->bIsPrep )
        {
            sNumTokens = JsonUtils_Parse( ( const char * ) pxWriteParam->pucValue, pxWriteParam->xLength, xTokens, mqttBLEMAX_TOKENS );

            if( sNumTokens > 0 )
            {
                xResult = JsonUtils_GetInt16Value( ( const char * ) pxWriteParam->pucValue, xTokens, sNumTokens, mqttBLESTATE, strlen( mqttBLESTATE ), &sProxyEnable );

                if( xResult == pdTRUE )
                {
                    if( sProxyEnable == pdTRUE )
                    {
                    	pxService->bIsInit = true;
                    }
                    else
                    {
                    	pxService->bIsInit = false;
                        prvResetBuffer( pxService );
                    }

                    xResp.xEventStatus = eBTStatusSuccess;
                }
            }
        }

        if( pxEventParam->xEventType == eBLEWrite )
        {
            xResp.pxAttrData->pucData = pxWriteParam->pucValue;
            xResp.xAttrDataOffset = pxWriteParam->usOffset;
            xResp.pxAttrData->xSize = pxWriteParam->xLength;
            BLE_SendResponse( &xResp, pxWriteParam->usConnId, pxWriteParam->ulTransId );
        }
    }
    else if( pxEventParam->xEventType == eBLERead )
    {
        xResp.xEventStatus = eBTStatusSuccess;
        xResp.pxAttrData->pucData = ( uint8_t * ) cMsg;
        xResp.xAttrDataOffset = 0;
        xResp.pxAttrData->xSize = snprintf( cMsg, mqttBLESTATE_MSG_LEN, mqttBLESTATE_MESSAGE, pxService->bIsInit);
        BLE_SendResponse( &xResp, pxEventParam->pxParamRead->usConnId, pxEventParam->pxParamRead->ulTransId );
    }
}
/*-----------------------------------------------------------*/

void vTXMesgCharCallback( BLEAttributeEvent_t * pxEventParam )
{
    BLEReadEventParams_t * pxReadParam;
    BLEAttributeData_t xAttrData = { 0 };
    BLEEventResponse_t xResp;
    MqttBLEService_t * pxMQTTService;

    pxMQTTService = prxGetServiceInstance( pxAttribute->pxCharacteristic->pxParentService );
    configASSERT( ( pxMQTTService != NULL ) );

    xResp.pxAttrData = &xAttrData;
    xResp.xRspErrorStatus = eBTRspErrorNone;
    xResp.xEventStatus = eBTStatusFail;
    xResp.xAttrDataOffset = 0;
    xResp.pxAttrData->xHandle = CHAR_HANDLE( pxMQTTService->pxService, eMQTTBLETXMessage );

    if( pxEventParam->xEventType == eBLERead )
    {
        pxReadParam = pxEventParam->pxParamRead;
        xResp.pxAttrData->pucData = NULL;
        xResp.pxAttrData->xSize = 0;
        xResp.xAttrDataOffset = 0;
        xResp.xEventStatus = eBTStatusSuccess;

        BLE_SendResponse( &xResp, pxReadParam->usConnId, pxReadParam->ulTransId );
    }
}

/*-----------------------------------------------------------*/

void vTXLargeMesgCharCallback( BLEAttributeEvent_t * pxEventParam )
{
    BLEReadEventParams_t * pxReadParam;
    BLEAttributeData_t xAttrData = { 0 };
    BLEEventResponse_t xResp;
    MqttBLEService_t * pxService;
    size_t xBytesToSend = 0;
    uint8_t * pucData;


    if( pxEventParam->xEventType == eBLERead )
    {

        pxReadParam = pxEventParam->pxParamRead;
        
        pxService = prxGetServiceInstance( pxReadParam->usAttrHandle );
        configASSERT( ( pxService != NULL ) );


        xResp.pxAttrData = &xAttrData;
        xResp.xRspErrorStatus = eBTRspErrorNone;
        xResp.xEventStatus = eBTStatusFail;
        xResp.xAttrDataOffset = 0;
        xResp.pxAttrData->xHandle = pxAttribute->pxCharacteristic->xAttributeData.xHandle;
        xBytesToSend = mqttBLETRANSFER_LEN( usBLEConnMTU );

        pucData = pvPortMalloc( xBytesToSend );
        if( pucData != NULL )
        {
            xBytesToSend = xStreamBufferReceive( pxService->xConnection.xSendBuffer, pucData, xBytesToSend, ( TickType_t ) 0ULL );
            xResp.pxAttrData->pucData = pucData;
            xResp.pxAttrData->xSize = xBytesToSend;
            xResp.xAttrDataOffset = 0;
            xResp.xEventStatus = eBTStatusSuccess;
        }

        BLE_SendResponse( &xResp, pxReadParam->usConnId, pxReadParam->ulTransId );

        if( pucData != NULL )
        {
            vPortFree( pucData );
        }

        /* If this was the last chunk of a large message, release the send lock */
        if( ( xResp.xEventStatus == eBTStatusSuccess ) &&
            ( xBytesToSend < mqttBLETRANSFER_LEN( usBLEConnMTU ) ) )
        {
            ( void ) xSemaphoreGive( pxService->xConnection.xSendLock );
        }
    }
}

/*-----------------------------------------------------------*/

void vRXLargeMesgCharCallback( BLEAttributeEvent_t * pxEventParam )
{
    BLEWriteEventParams_t * pxWriteParam;
    BLEAttributeData_t xAttrData = { 0 };
    BLEEventResponse_t xResp;
    MqttBLEService_t * pxService;
    BaseType_t xResult = pdTRUE;
    uint8_t *pucBufOffset;

    pxService = prxGetServiceInstance( pxAttribute->pxCharacteristic->pxParentService );
    configASSERT( ( pxService != NULL ) );

    xResp.pxAttrData = &xAttrData;
    xResp.xRspErrorStatus = eBTRspErrorNone;
    xResp.xEventStatus = eBTStatusFail;
    xResp.xAttrDataOffset = 0;
    xResp.pxAttrData->xHandle = pxAttribute->pxCharacteristic->xAttributeData.xHandle;
    xResp.pxAttrData->xUuid = pxAttribute->pxCharacteristic->xAttributeData.xUuid;

    if( ( pxEventParam->xEventType == eBLEWrite ) || ( pxEventParam->xEventType == eBLEWriteNoResponse ) )
    {
        pxWriteParam = pxEventParam->pxParamWrite;

        if( ( !pxWriteParam->bIsPrep ) &&
        		( pxService->xConnection.pxMqttConnection != NULL ) &&
				( pxService->bIsInit ) )
        {
        	if( xSemaphoreTake( pxService->xConnection.xRecvLock, 0 ) == pdTRUE )
        	{
        		/**
        		 * Create a new buffer if the buffer is empty.
        		 */
        		if( pxService->xConnection.pRecvBuffer == NULL )
        		{
        			pxService->xConnection.pRecvBuffer = pvPortMalloc( mqttBLERX_BUFFER_SIZE );
        			if( pxService->xConnection.pRecvBuffer != NULL )
        			{
        				pxService->xConnection.xRecvBufferLen = mqttBLERX_BUFFER_SIZE;

        			}
        			else
        			{
        				xResult = pdFALSE;
					}
        		}
        		else
        		{
        			/**
        			 *  If current buffer can't hold the received data, resize the buffer by twicke the current size.
        			 */
        			if( ( pxService->xConnection.xRecvOffset + pxWriteParam->xLength ) > pxService->xConnection.xRecvBufferLen )
        			{
        				pxService->xConnection.pRecvBuffer = prvReallocBuffer( pxService->xConnection.pRecvBuffer,
        						pxService->xConnection.xRecvBufferLen,
								( 2 * pxService->xConnection.xRecvBufferLen ) );

        				if( pxService->xConnection.pRecvBuffer != NULL )
        				{
        					pxService->xConnection.xRecvBufferLen = ( pxService->xConnection.xRecvBufferLen * 2 );

        				}
        				else
        				{
        					xResult = pdFALSE;
        				}

        			}
        		}

        		if( xResult == pdTRUE )
        		{
        			/**
        			 * Copy the received data into the buffer.
        			 */
        			pucBufOffset = ( uint8_t* ) pxService->xConnection.pRecvBuffer + pxService->xConnection.xRecvOffset;
        			memcpy( pucBufOffset, pxWriteParam->pucValue, pxWriteParam->xLength );
        			pxService->xConnection.xRecvOffset += pxWriteParam->xLength;

        			if( pxWriteParam->xLength < mqttBLETRANSFER_LEN( usBLEConnMTU ) )
        			{

        				( void ) AwsIotMqtt_ReceiveCallback( pxService->xConnection.pxMqttConnection,
        						                pxService->xConnection.pRecvBuffer,
        										0,
												pxService->xConnection.xRecvOffset,
        										vPortFree );

        				pxService->xConnection.pRecvBuffer = NULL;
        				pxService->xConnection.xRecvBufferLen = 0;
        				pxService->xConnection.xRecvOffset = 0;
        			}

        			xResp.xEventStatus = eBTStatusSuccess;
        		}

        		( void ) xSemaphoreGive( pxService->xConnection.xRecvLock );
        	}
        }

        if( pxEventParam->xEventType == eBLEWrite )
        {
            xResp.xAttrDataOffset = pxWriteParam->usOffset;
            xResp.pxAttrData->pucData = pxWriteParam->pucValue;
            xResp.pxAttrData->xSize = pxWriteParam->xLength;
            BLE_SendResponse( &xResp, pxWriteParam->usConnId, pxWriteParam->ulTransId );
        }
    }
}

/*-----------------------------------------------------------*/

void vRXMesgCharCallback( BLEAttributeEvent_t * pxEventParam )
{
    BLEWriteEventParams_t * pxWriteParam;
    BLEAttributeData_t xAttrData = { 0 };
    BLEEventResponse_t xResp;
    MqttBLEService_t * pxService;

    configASSERT( ( pxAttribute->xAttributeType == eBTDbCharacteristic ) );

    pxService = prxGetServiceInstance( pxAttribute->pxCharacteristic->pxParentService );
    configASSERT( ( pxService != NULL ) );

    xResp.pxAttrData = &xAttrData;
    xResp.xRspErrorStatus = eBTRspErrorNone;
    xResp.xEventStatus = eBTStatusFail;
    xResp.xAttrDataOffset = 0;
    xResp.pxAttrData->xHandle = pxAttribute->pxCharacteristic->xAttributeData.xHandle;
    xResp.pxAttrData->xUuid = pxAttribute->pxCharacteristic->xAttributeData.xUuid;

    if( ( pxEventParam->xEventType == eBLEWrite ) || ( pxEventParam->xEventType == eBLEWriteNoResponse ) )
    {
        pxWriteParam = pxEventParam->pxParamWrite;

        if( ( !pxWriteParam->bIsPrep ) &&
               		( pxService->xConnection.pxMqttConnection != NULL ) &&
       				( pxService->bIsInit ) )
        {
        		( void ) AwsIotMqtt_ReceiveCallback( pxService->xConnection.pxMqttConnection,
        				( const void * ) pxWriteParam->pucValue,
						0,
						pxWriteParam->xLength,
						NULL );
        		xResp.xEventStatus = eBTStatusSuccess;
        }

        if( pxEventParam->xEventType == eBLEWrite )
        {
            xResp.xAttrDataOffset = pxWriteParam->usOffset;
            xResp.pxAttrData->pucData = pxWriteParam->pucValue;
            xResp.pxAttrData->xSize = pxWriteParam->xLength;
            BLE_SendResponse( &xResp, pxWriteParam->usConnId, pxWriteParam->ulTransId );
        }
    }
}

/*-----------------------------------------------------------*/

void vClientCharCfgDescrCallback( BLEAttributeEvent_t * pxEventParam )
{
    BLEWriteEventParams_t * pxWriteParam;
    BLEAttributeData_t xAttrData = { 0 };
    BLEEventResponse_t xResp;
    MqttBLEService_t * pxMQTTService;
    uint16_t usCCFGValue;
    MQTTCharacteristicDescr_t xDescr;

    pxMQTTService = prxGetServiceInstance( pxAttribute->pxCharacteristicDescr->pxParentService );
    configASSERT( ( pxMQTTService != NULL ) );

    xResp.pxAttrData = &xAttrData;
    xResp.xRspErrorStatus = eBTRspErrorNone;
    xResp.xEventStatus = eBTStatusFail;
    xResp.xAttrDataOffset = 0;
    xResp.pxAttrData->xHandle = DESCR_HANDLE( pxMQTTService->pxService, eMQTTBLETXMessageDescr );

    if( ( pxEventParam->xEventType == eBLEWrite ) || ( pxEventParam->xEventType == eBLEWriteNoResponse ) )
    {
        pxWriteParam = pxEventParam->pxParamWrite;
        xResp.pxAttrData->xHandle = pxWriteParam->usAttrHandle;

        if( pxWriteParam->xLength == 2 )
        {
            usCCFGValue = ( pxWriteParam->pucValue[ 1 ] << 8 ) | pxWriteParam->pucValue[ 0 ];

            if( pxEventParam->pxParamWrite->usAttrHandle == xMQTTService.pusHandlesBuffer[blemqttATTR_CHAR_DESCR_TX_MESG])
            {
                 usCCCDTxMessage = usCCFGValue;
            }else 
            {
                 usCCCDTxLargeMessage = usCCFGValue;
            }
            pxMQTTService->usDescrVal[ xDescr ]
            xResp.xEventStatus = eBTStatusSuccess;
        }

        if( pxEventParam->xEventType == eBLEWrite )
        {
            xResp.pxAttrData->pucData = pxWriteParam->pucValue;
            xResp.pxAttrData->xSize = pxWriteParam->xLength;
            xResp.xAttrDataOffset = pxWriteParam->usOffset;
            BLE_SendResponse( &xResp, pxWriteParam->usConnId, pxWriteParam->ulTransId );
        }
    }
    else if( pxEventParam->xEventType == eBLERead )
    {
        xResp.pxAttrData->xHandle = pxEventParam->pxParamRead->usAttrHandle;
        xResp.xEventStatus = eBTStatusSuccess;
        xResp.pxAttrData->pucData = ( uint8_t * ) &pxMQTTService->usDescrVal[ xDescr ];
        xResp.pxAttrData->xSize = 2;
        xResp.xAttrDataOffset = 0;
        BLE_SendResponse( &xResp, pxEventParam->pxParamRead->usConnId, pxEventParam->pxParamRead->ulTransId );
    }
}

/*-----------------------------------------------------------*/

static void vConnectionCallback( BTStatus_t xStatus,
                                 uint16_t usConnId,
                                 bool bConnected,
                                 BTBdaddr_t * pxRemoteBdAddr )
{
    uint8_t ucId;
    MqttBLEService_t *pxService;
    
    if( xStatus == eBTStatusSuccess )
    {
        for( ucId = 0; ucId < mqttBLEMAX_SVC_INSTANCES; ucId++ )
        {
            pxService = &xMqttBLEServices[ ucId ];
            if( bConnected == true )
            {
                pxService->usBLEConnId = usConnId;
            }
            else
            {
                configPRINTF( ( "Disconnect received for MQTT service instance %d\n", ucId ) );
                pxService->bIsInit = false;
                pxService->xConnection.pxMqttConnection = NULL;
                prvResetBuffer( pxService );
            }
        }
    }
}

/*-----------------------------------------------------------*/

static void vMTUChangedCallback( uint16_t usConnId,
                                 uint16_t usMtu )
{
	/* Change the MTU size for the connection */
	if( usBLEConnMTU != usMtu )
	{
		configPRINTF( ( "Changing MTU size for BLE connection from %d to %d\n", usBLEConnMTU, usMtu ) );
		usBLEConnMTU = usMtu;
	}

}

/*-----------------------------------------------------------*/

BaseType_t AwsIotMqttBLE_Init( void )
{
    BaseType_t xRet;
    uint8_t ucId;
    BLEEventsCallbacks_t xCallback;
    MqttBLEService_t *pxService;

    xServiceInitLock = xSemaphoreCreateBinary();
    usBLEConnMTU = mqttBLEDEFAULT_MTU_SIZE;

    for( ucId = 0; ucId < mqttBLEMAX_SVC_INSTANCES; ucId++ )
    {
        xRet = prvInitServiceInstance( ucId );
        pxService = &xMqttBLEServices[ ucId ];

        if( xRet == pdPASS )
        {
            xRet = xSemaphoreTake( xServiceInitLock, portMAX_DELAY );
        }

        if( xRet == pdPASS )
        {
            if( pxService->bIsInit == false )
            {
                xRet = pdFAIL;
            }
        }

        if( xRet == pdPASS )
        {
            xCallback.pxConnectionCb = vConnectionCallback;

            if( BLE_RegisterEventCb( eBLEConnection, xCallback ) != eBTStatusSuccess )
            {
                xRet = pdFAIL;
            }
        }

        if( xRet == pdPASS )
        {
            xCallback.pxMtuChangedCb = vMTUChangedCallback;
            if( BLE_RegisterEventCb( eBLEMtuChanged, xCallback ) != eBTStatusSuccess )
            {
                xRet = pdFALSE;
            }
        }

        if( xRet == pdPASS )
        {
            pxService->xConnection.xSendTimeout = pdMS_TO_TICKS( mqttBLEDEFAULT_SEND_TIMEOUT_MS );
            pxService->xConnection.xSendLock = xSemaphoreCreateBinary();
            if( pxService->xConnection.xSendLock != NULL )
            {
                ( void ) xSemaphoreGive( pxService->xConnection.xSendLock );
            }
            else
            {
                xRet = pdFAIL;
            }
        }

        if( xRet == pdPASS )
        {
        	pxService->xConnection.xRecvLock = xSemaphoreCreateBinary();
        	if( pxService->xConnection.xRecvLock != NULL )
        	{
        		( void ) xSemaphoreGive( pxService->xConnection.xRecvLock );
        	}
        	else
        	{
        		xRet = pdFAIL;
        	}

        }

        if( xRet == pdPASS )
        {
        	pxService->xConnection.xSendBuffer = xStreamBufferCreate( mqttBLETX_BUFFER_SIZE, mqttBLEDEFAULT_MTU_SIZE );

            if(pxService->xConnection.xSendBuffer == NULL )
            {
                xRet = pdFALSE;
            }
        }

        if( xRet == pdFALSE )
        {
            if( pxService->xConnection.xSendLock != NULL )
            {
                vSemaphoreDelete( pxService->xConnection.xSendLock );
            }

            if( pxService->xConnection.xRecvLock != NULL )
            {
            	vSemaphoreDelete( pxService->xConnection.xRecvLock );
            }

            if( pxService->xConnection.xSendBuffer != NULL )
            {
                vStreamBufferDelete( pxService->xConnection.xSendBuffer );
            }

            break;
        }
    }

    return xRet;
}

/*-----------------------------------------------------------*/

BaseType_t AwsIotMqttBLE_CreateConnection( AwsIotMqttConnection_t* pxMqttConnection, AwsIotMqttBLEConnection_t* pxConnection )
{
    BaseType_t xRet = pdFALSE;
    MqttBLEService_t* pxService;
    int lX;

    for( lX = 0; lX < mqttBLEMAX_SVC_INSTANCES; lX++ )
    {
    	pxService = &xMqttBLEServices[ lX ];
        if( ( pxService->bIsInit ) && ( pxService->xConnection.pxMqttConnection == NULL ) )
        {
        	pxService->xConnection.pxMqttConnection = pxMqttConnection;
        	*pxConnection =  ( AwsIotMqttBLEConnection_t ) pxService;
        	xRet = pdTRUE;
        }
    }

    return xRet;
}

/*-----------------------------------------------------------*/

void AwsIotMqttBLE_CloseConnection( AwsIotMqttBLEConnection_t xConnection )
{
    MqttBLEService_t * pxService = ( MqttBLEService_t * ) xConnection;
    if( ( pxService != NULL ) && ( pxService->xConnection.pxMqttConnection != NULL ) )
    {
    	pxService->xConnection.pxMqttConnection = NULL;
    }
}

void AwsIotMqttBLE_DestroyConnection( AwsIotMqttBLEConnection_t xConnection )
{
    MqttBLEService_t* pxService = ( MqttBLEService_t * ) xConnection;
    if( ( pxService != NULL ) && ( pxService->xConnection.pxMqttConnection == NULL ) )
    {
        prvResetBuffer( pxService );
    }
}

/*-----------------------------------------------------------*/

BaseType_t AwsIotMqttBLE_SetSendTimeout( AwsIotMqttBLEConnection_t xConnection, uint16_t usTimeoutMS )
{
    BaseType_t xRet = pdFALSE;
    MqttBLEService_t * pxService = ( MqttBLEService_t * ) xConnection;

    if( pxService != NULL )
    {
        pxService->xConnection.xSendTimeout = pdMS_TO_TICKS( usTimeoutMS );
        xRet = pdTRUE;
    }
    return xRet;
}

/*-----------------------------------------------------------*/

size_t AwsIotMqttBLE_Send( void* pvConnection, const void * const pvMessage, size_t xMessageLength )
{
    MqttBLEService_t * pxService = ( MqttBLEService_t * ) pvConnection;
    size_t xSendLen, xRemainingLen = xMessageLength;
    TickType_t xRemainingTime = pxService->xConnection.xSendTimeout;
    TimeOut_t xTimeout;
    uint8_t * pucData;

    vTaskSetTimeOutState( &xTimeout );

    if( ( pxService != NULL ) && ( pxService->bIsInit ) && ( pxService->xConnection.pxMqttConnection != NULL ) )
    {
        if( xMessageLength < ( size_t ) mqttBLETRANSFER_LEN( usBLEConnMTU ) )
        {
            if( prxSendNotification( pxService, eMQTTBLETXMessage, ( uint8_t *) pvMessage, xMessageLength ) == pdTRUE )
            {
                xRemainingLen = 0;
            }else
            {
                 configPRINTF( ( "Failed to send notify\n") );
            }
        }
        else
        {
            if( xSemaphoreTake( pxService->xConnection.xSendLock, xRemainingTime ) == pdPASS )
            {
                xSendLen = ( size_t ) mqttBLETRANSFER_LEN( usBLEConnMTU );
                pucData = ( uint8_t *) pvMessage;

                if( prxSendNotification( pxService, eMQTTBLETXLargeMessage, pucData, xSendLen ) == pdTRUE )
                {
                    xRemainingLen = xRemainingLen - xSendLen;
                    pucData += xSendLen;

                    while( xRemainingLen > 0 )
                    {
                        if( !( pxService->bIsInit ) ||
                        		( pxService->xConnection.pxMqttConnection == NULL ) ||
                        		( xTaskCheckForTimeOut( &xTimeout, &xRemainingTime ) == pdTRUE ) )
                        {
                            xStreamBufferReset( pxService->xConnection.xSendBuffer );
                            xSemaphoreGive( pxService->xConnection.xSendLock );
                            break;
                        }

                        if( xRemainingLen < mqttBLETX_BUFFER_SIZE )
                        {
                            xSendLen = xRemainingLen;
                        }
                        else
                        {
                            xSendLen = mqttBLETX_BUFFER_SIZE;
                        }

                        xSendLen = xStreamBufferSend( pxService->xConnection.xSendBuffer, pucData, xSendLen, xRemainingTime );
                        xRemainingLen -= xSendLen;
                        pucData += xSendLen;

                    }
                }else
                {
                      configPRINTF( ( "Failed to send notify\n") );
                }
            }
        }
    }
    else
    {
        configPRINTF( ( "Failed to send notification, mqtt proxy state:%d \n", pxService->bIsInit ) );
    }

    return( xMessageLength - xRemainingLen );
}
