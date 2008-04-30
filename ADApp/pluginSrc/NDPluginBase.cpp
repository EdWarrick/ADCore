/*
 * drvNDPlugin.c
 * 
 * Asyn driver for callbacks to save area detector data to files.
 *
 * Author: Mark Rivers
 *
 * Created April 5, 2008
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <epicsThread.h>
#include <epicsString.h>
#include <epicsTimer.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsMessageQueue.h>
#include <cantProceed.h>

#include <asynStandardInterfaces.h>

#define DEFINE_AD_STANDARD_PARAMS 1
#include "ADInterface.h"
#include "ADUtils.h"

#include "NDPluginBase.h"

/* The command strings are the userParam argument for asyn device support links
 * The asynDrvUser interface in this driver parses these strings and puts the
 * corresponding enum value in pasynUser->reason */
static asynParamString_t NDPluginBaseParamString[] = {
    {NDPluginBaseArrayPort,         "NDARRAY_PORT" },
    {NDPluginBaseArrayAddr,         "NDARRAY_ADDR" },
    {NDPluginBaseArrayCounter,      "ARRAY_COUNTER"},
    {NDPluginBaseDroppedArrays,     "DROPPED_ARRAYS" },
    {NDPluginBaseEnableCallbacks,   "ENABLE_CALLBACKS" },
    {NDPluginBaseBlockingCallbacks, "BLOCKING_CALLBACKS" },
    {NDPluginBaseMinCallbackTime,   "MIN_CALLBACK_TIME" },
    {NDPluginBaseUniqueId,          "UNIQUE_ID" },
    {NDPluginBaseTimeStamp,         "TIME_STAMP" },
    {NDPluginBaseDataType,          "DATA_TYPE" },
    {NDPluginBaseNDimensions,       "ARRAY_NDIMENSIONS"},
    {NDPluginBaseDimensions,        "ARRAY_DIMENSIONS"}
};

#define NUM_ND_PLUGIN_BASE_PARAMS (sizeof(NDPluginBaseParamString)/sizeof(NDPluginBaseParamString[0]))

static const char *driverName="NDPluginBase";

void NDPluginBase::processCallbacks(NDArray_t *pArray)
{
    int addr = 0;
    int arrayCounter;
    int i, dimsChanged;
    
    ADParam->getInteger(this->params[addr], NDPluginBaseArrayCounter, &arrayCounter);
    arrayCounter++;
    ADParam->setInteger(this->params[addr], NDPluginBaseArrayCounter, arrayCounter);
    ADParam->setInteger(this->params[addr], NDPluginBaseNDimensions, pArray->ndims);
    ADParam->setInteger(this->params[addr], NDPluginBaseDataType, pArray->dataType);
    ADParam->setInteger(this->params[addr], NDPluginBaseUniqueId, pArray->uniqueId);
    ADParam->setDouble (this->params[addr], NDPluginBaseTimeStamp, pArray->timeStamp);
    /* See if the array dimensions have changed.  If so then do callbacks on them. */
    for (i=0, dimsChanged=0; i<pArray->ndims; i++) {
        if (pArray->dims[i].size != this->dimsPrev[i]) {
            this->dimsPrev[i] = pArray->dims[i].size;
            dimsChanged = 1;
        }
    }
    if (dimsChanged) {
        doCallbacksInt32Array(this->dimsPrev, pArray->ndims, NDPluginBaseDimensions, 0);
    }
}

static void driverCallback(void *drvPvt, asynUser *pasynUser, void *handle)
{
    NDPluginBase *pNDPluginBase = (NDPluginBase *)drvPvt;
    pNDPluginBase->driverCallback(pasynUser, handle);
}

void NDPluginBase::driverCallback(asynUser *pasynUser, void *handle)
{
    /* This callback function is called from the detector driver when a new array arrives.
     * It calls the processCallbacks function.
     * It can either do the callbacks directly (if BlockingCallbacks=1) or by queueing
     * the arrays to be processed by a background task (if BlockingCallbacks=0).
     * In the latter case arrays can be dropped if the queue is full.
     */
     
    NDArray_t *pArray = (NDArray_t *)handle;
    epicsTimeStamp tNow;
    double minCallbackTime, deltaTime;
    int status;
    int blockingCallbacks;
    int arrayCounter, droppedArrays;
    char *functionName = "driverCallback";

    epicsMutexLock(mutexId);

    status |= ADParam->getDouble(this->params[0], NDPluginBaseMinCallbackTime, &minCallbackTime);
    status |= ADParam->getInteger(this->params[0], NDPluginBaseArrayCounter, &arrayCounter);
    status |= ADParam->getInteger(this->params[0], NDPluginBaseDroppedArrays, &droppedArrays);
    status |= ADParam->getInteger(this->params[0], NDPluginBaseBlockingCallbacks, &blockingCallbacks);
    
    epicsTimeGetCurrent(&tNow);
    deltaTime = epicsTimeDiffInSeconds(&tNow, &this->lastProcessTime);

    if (deltaTime > minCallbackTime) {  
        /* Time to process the next array */
        
        /* The callbacks can operate in 2 modes: blocking or non-blocking.
         * If blocking we call processCallbacks directly, executing them
         * in the detector callback thread.
         * If non-blocking we put the array on the queue and it executes
         * in our background thread. */
        /* Update the time we last posted an array */
        epicsTimeGetCurrent(&tNow);
        memcpy(&this->lastProcessTime, &tNow, sizeof(tNow));
        if (blockingCallbacks) {
            processCallbacks(pArray);
        } else {
            /* Increase the reference count again on this array
             * It will be released in the background task when processing is done */
            NDArrayBuff->reserve(pArray);
            /* Try to put this array on the message queue.  If there is no room then return
             * immediately. */
            status = epicsMessageQueueTrySend(this->msgQId, &pArray, sizeof(&pArray));
            if (status) {
                asynPrint(this->pasynUser, ASYN_TRACE_FLOW, 
                    "%s:%s message queue full, dropped array %d\n",
                    driverName, functionName, arrayCounter);
                droppedArrays++;
                status |= ADParam->setInteger(this->params[0], NDPluginBaseDroppedArrays, droppedArrays);
                /* This buffer needs to be released */
                NDArrayBuff->release(pArray);
            }
        }
    }
    ADParam->callCallbacksAddr(this->params[0], 0);
    epicsMutexUnlock(this->mutexId);
}



void processTask(void *drvPvt)
{
    NDPluginBase *pPvt = (NDPluginBase *)drvPvt;
    
    pPvt->processTask();
}

void NDPluginBase::processTask(void)
{
    /* This thread prcoess a new array when it arrives */

    /* Loop forever */
    NDArray_t *pArray;
    
    while (1) {
        /* Wait for an array to arrive from the queue */    
        epicsMessageQueueReceive(this->msgQId, &pArray, sizeof(&pArray));
        
        /* Take the lock.  The function we are calling must release the lock
         * during time-consuming operations when it does not need it. */
        epicsMutexLock(this->mutexId);
        /* Call the function that does the callbacks to standard asyn interfaces */
        processCallbacks(pArray); 
        epicsMutexUnlock(this->mutexId); 
        
        /* We are done with this array buffer */       
        NDArrayBuff->release(pArray);
    }
}

asynStatus NDPluginBase::setArrayInterrupt(int enableCallbacks)
{
    asynStatus status = asynSuccess;
    const char *functionName = "setArrayInterrupt";
    
    /* Lock the port.  May not be necessary to do this. */
    status = pasynManager->lockPort(this->pasynUserHandle);
    if (status != asynSuccess) {
        asynPrint(this->pasynUser, ASYN_TRACE_ERROR,
            "%s::%s ERROR: Can't lock array port: %s\n",
            driverName, functionName, this->pasynUserHandle->errorMessage);
        return(status);
    }
    if (enableCallbacks) {
        status = this->pasynHandle->registerInterruptUser(
                    this->asynHandlePvt, this->pasynUserHandle,
                    ::driverCallback, this, &this->asynHandleInterruptPvt);
        if (status != asynSuccess) {
            asynPrint(this->pasynUser, ASYN_TRACE_ERROR,
                "%s::%s ERROR: Can't register for interrupt callbacks on detector port: %s\n",
                driverName, functionName, this->pasynUserHandle->errorMessage);
            return(status);
        }
    } else {
        if (this->asynHandleInterruptPvt) {
            status = this->pasynHandle->cancelInterruptUser(this->asynHandlePvt, 
                            this->pasynUserHandle, this->asynHandleInterruptPvt);
            this->asynHandleInterruptPvt = NULL;
            if (status != asynSuccess) {
                asynPrint(this->pasynUser, ASYN_TRACE_ERROR,
                    "%s::%s ERROR: Can't unregister for interrupt callbacks on detector port: %s\n",
                    driverName, functionName, this->pasynUserHandle->errorMessage);
                return(status);
            }
        }
    }
    /* Unlock the port.  May not be necessary to do this. */
    status = pasynManager->unlockPort(this->pasynUserHandle);
    if (status != asynSuccess) {
        asynPrint(this->pasynUser, ASYN_TRACE_ERROR,
            "%s::%s ERROR: Can't unlock array port: %s\n",
            driverName, functionName, this->pasynUserHandle->errorMessage);
        return(status);
    }
    return(asynSuccess);
}

asynStatus NDPluginBase::connectToArrayPort(void)
{
    asynStatus status;
    asynInterface *pasynInterface;
    NDArray_t array;
    int isConnected;
    int enableCallbacks;
    char arrayPort[20];
    int arrayAddr;
    const char *functionName = "connectToArrayPort";

    ADParam->getString(this->params[0], NDPluginBaseArrayPort, sizeof(arrayPort), arrayPort);
    ADParam->getInteger(this->params[0], NDPluginBaseArrayAddr, &arrayAddr);
    ADParam->getInteger(this->params[0], NDPluginBaseEnableCallbacks, &enableCallbacks);
    status = pasynManager->isConnected(this->pasynUserHandle, &isConnected);
    if (status) isConnected=0;

    /* If we are currently connected cancel interrupt request */    
    if (isConnected) {       
        status = setArrayInterrupt(0);
    }
    
    /* Disconnect the array port from our asynUser.  Ignore error if there is no device
     * currently connected. */
    pasynManager->exceptionCallbackRemove(this->pasynUserHandle);
    pasynManager->disconnect(this->pasynUserHandle);

    /* Connect to the array port driver */
    status = pasynManager->connectDevice(this->pasynUserHandle, arrayPort, arrayAddr);
    if (status != asynSuccess) {
        asynPrint(this->pasynUser, ASYN_TRACE_ERROR,
                  "%s::%s ERROR: Can't connect to array port %s address %d: %s\n",
                  driverName, functionName, arrayPort, arrayAddr, this->pasynUserHandle->errorMessage);
        pasynManager->exceptionDisconnect(this->pasynUser);
        return (status);
    }

    /* Find the asynHandle interface in that driver */
    pasynInterface = pasynManager->findInterface(this->pasynUserHandle, asynHandleType, 1);
    if (!pasynInterface) {
        asynPrint(this->pasynUser, ASYN_TRACE_ERROR,
                  "%s::connectToPort ERROR: Can't find asynHandle interface on array port %s address %d\n",
                  driverName, arrayPort, arrayAddr);
        pasynManager->exceptionDisconnect(this->pasynUser);
        return(asynError);
    }
    this->pasynHandle = (asynHandle *)pasynInterface->pinterface;
    this->asynHandlePvt = pasynInterface->drvPvt;
    pasynManager->exceptionConnect(this->pasynUser);

    /* Read the current array parameters from the array driver */
    /* Lock the port. Defintitely necessary to do this. */
    status = pasynManager->lockPort(this->pasynUserHandle);
    if (status != asynSuccess) {
        asynPrint(this->pasynUser, ASYN_TRACE_ERROR,
            "%s::%s ERROR: Can't lock array port: %s\n",
            driverName, functionName, this->pasynUserHandle->errorMessage);
        return(status);
    }
    /* Read the current array, but only request 0 bytes so no data are actually transferred */
    array.dataSize = 0;
    status = this->pasynHandle->read(this->asynHandlePvt, this->pasynUserHandle, &array);
    if (status != asynSuccess) {
        asynPrint(this->pasynUser, ASYN_TRACE_ERROR,
            "%s::%s ERROR: reading array data:%s\n",
            driverName, functionName, this->pasynUserHandle->errorMessage);
    } else {
        ADParam->callCallbacksAddr(this->params[0], 0);
    }
    /* Unlock the port.  Definitely necessary to do this. */
    status = pasynManager->unlockPort(this->pasynUserHandle);
    if (status != asynSuccess) {
        asynPrint(this->pasynUser, ASYN_TRACE_ERROR,
            "%s::%s ERROR: Can't unlock array port: %s\n",
            driverName, functionName, this->pasynUserHandle->errorMessage);
    }
    
    /* Enable or disable interrupt callbacks */
    status = setArrayInterrupt(enableCallbacks);

    return(status);
}   


asynStatus NDPluginBase::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    int isConnected;
    int currentlyPosting;
    const char* functionName = "writeInt32";

    status = getAddress(pasynUser, functionName, &addr); if (status != asynSuccess) return(status);
    epicsMutexLock(this->mutexId);

    /* See if we are connected */
    status = pasynManager->isConnected(this->pasynUserHandle, &isConnected);
    if (status) {isConnected=0; status=asynSuccess;}

    /* See if we are currently getting callbacks so we don't add more than 1 callback request */
    currentlyPosting = (this->asynHandleInterruptPvt != NULL);

    /* Set the parameter in the parameter library. */
    status = (asynStatus) ADParam->setInteger(this->params[addr], function, value);

    switch(function) {
        case NDPluginBaseEnableCallbacks:
            if (value) {  
                if (isConnected && !currentlyPosting) {
                    /* We need to register to be called with interrupts from the detector driver on 
                     * the asynHandle interface. */
                    status = setArrayInterrupt(1);
                }
            } else {
                /* If we are currently connected and there is a callback registered, cancel it */    
                if (isConnected && currentlyPosting) {
                    status = setArrayInterrupt(0);
                }
            }
            break;
       case NDPluginBaseArrayAddr:
            connectToArrayPort();
            break;
        default:
            break;
    }
    
    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) ADParam->callCallbacksAddr(this->params[addr], addr);
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%d", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%d, isConnected=%d, currentlyPosting=%d\n", 
              driverName, functionName, function, value, isConnected, currentlyPosting);
    epicsMutexUnlock(this->mutexId);
    return status;
}


asynStatus NDPluginBase::writeOctet(asynUser *pasynUser, const char *value, 
                                    size_t nChars, size_t *nActual)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char *functionName = "writeOctet";

    status = getAddress(pasynUser, functionName, &addr); if (status != asynSuccess) return(status);
    epicsMutexLock(this->mutexId);
    /* Set the parameter in the parameter library. */
    status = (asynStatus)ADParam->setString(this->params[addr], function, (char *)value);

    switch(function) {
        case NDPluginBaseArrayPort:
            connectToArrayPort();
        default:
            break;
    }
    
     /* Do callbacks so higher layers see any changes */
    status = (asynStatus)ADParam->callCallbacksAddr(this->params[addr], addr);

    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%s", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:writeOctet: function=%d, value=%s\n", 
              driverName, functionName, function, value);
    *nActual = nChars;
    epicsMutexUnlock(this->mutexId);
    return status;
}

asynStatus NDPluginBase::readInt32Array(asynUser *pasynUser, epicsInt32 *value, 
                                         size_t nElements, size_t *nIn)
{
    int function = pasynUser->reason;
    int addr=0;
    size_t ncopy;
    asynStatus status = asynSuccess;
    const char *functionName = "readInt32Array";

    status = getAddress(pasynUser, functionName, &addr); if (status != asynSuccess) return(status);
    epicsMutexLock(this->mutexId);

    switch(function) {
        case NDPluginBaseDimensions:
            ncopy = ND_ARRAY_MAX_DIMS;
            if (nElements < ncopy) ncopy = nElements;
            memcpy(value, this->dimsPrev, ncopy*sizeof(*this->dimsPrev));
            *nIn = ncopy;
            break;
    }
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d", 
                  driverName, functionName, status, function);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:writeOctet: function=%d\n", 
              driverName, functionName, function);
    epicsMutexUnlock(this->mutexId);
    return status;
}
    

asynStatus NDPluginBase::drvUserCreate(asynUser *pasynUser,
                                       const char *drvInfo, 
                                       const char **pptypeName, size_t *psize)
{
    int status;
    int param;
    static char *functionName = "drvUserCreate";

    /* See if this parameter is defined for the NDPluginBase class */
    status = findParam(NDPluginBaseParamString, NUM_ND_PLUGIN_BASE_PARAMS, drvInfo, &param);

    /* If not then try the ADStandard param strings */
    if (status) status = findParam(ADStandardParamString, NUM_AD_STANDARD_PARAMS, drvInfo, &param);
    
    if (status == asynSuccess) {
        pasynUser->reason = param;
        if (pptypeName) {
            *pptypeName = epicsStrDup(drvInfo);
        }
        if (psize) {
            *psize = sizeof(param);
        }
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
                  "%s:%s:, drvInfo=%s, param=%d\n", 
                  driverName, functionName, drvInfo, param);
        return(asynSuccess);
    } else {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                     "%s:%s:, unknown drvInfo=%s", 
                     driverName, functionName, drvInfo);
        return(asynError);
    }
}

/* Constructor */
NDPluginBase::NDPluginBase(const char *portName, int queueSize, int blockingCallbacks, 
                           const char *NDArrayPort, int NDArrayAddr, int maxAddr, int paramTableSize)

    : asynParamBase(portName, maxAddr, paramTableSize)
    
{
    asynStatus status;
    char *functionName = "NDPluginBase";
    asynUser *pasynUser;

    /* Initialize some members to 0 */
    memset(&this->lastProcessTime, 0, sizeof(this->lastProcessTime));
    memset(&this->dimsPrev, 0, sizeof(this->dimsPrev));
    this->pasynHandle = NULL;
    this->asynHandlePvt = NULL;
    this->asynHandleInterruptPvt = NULL;
       
    /* Create asynUser for communicating with NDArray port */
    pasynUser = pasynManager->createAsynUser(0, 0);
    pasynUser->userPvt = this;
    this->pasynUserHandle = pasynUser;
    this->pasynUserHandle->reason = NDArrayData;

    /* Create the message queue for the input arrays */
    this->msgQId = epicsMessageQueueCreate(queueSize, sizeof(NDArray_t*));
    if (!this->msgQId) {
        printf("%s:%s: epicsMessageQueueCreate failure\n", driverName, functionName);
        return;
    }
    
    /* Create the thread that handles the NDArray callbacks */
    status = (asynStatus)(epicsThreadCreate("NDPluginTask",
                                epicsThreadPriorityMedium,
                                epicsThreadGetStackSize(epicsThreadStackMedium),
                                (EPICSTHREADFUNC)::processTask,
                                this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate failure\n", driverName, functionName);
        return;
    }

    /* Set the initial values of some parameters */
    ADParam->setInteger(this->params[0], NDPluginBaseArrayCounter, 0);
    ADParam->setInteger(this->params[0], NDPluginBaseDroppedArrays, 0);
    ADParam->setString (this->params[0], NDPluginBaseArrayPort, NDArrayPort);
    ADParam->setInteger(this->params[0], NDPluginBaseArrayAddr, NDArrayAddr);
    
}

