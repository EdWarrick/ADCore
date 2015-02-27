/*
 * NDPluginCircularBuff.cpp
 *
 * Scope style triggered image recording
 * Author: Alan Greer/Edmund Warrick
 *
 * Created June 21, 2013
 */

// ###TODO: Rename references to "counts" (for ADC mode) with "samples", as this is less ambiguous.

// C dependencies
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// C++ dependencies
using namespace std;

// EPICS dependencies
#include <epicsString.h>
#include <epicsMutex.h>
#include <epicsExport.h>
#include <iocsh.h>

// Project dependencies
#include "NDArray.h"
#include "NDPluginCircularBuff.h"

#define MAX(A,B) (A)>(B)?(A):(B)
#define MIN(A,B) (A)<(B)?(A):(B)

static const char *driverName="NDPluginCircularBuff";

int NDPluginCircularBuff::containsTriggerStart()
{
  // ###TODO: Implement
  int startCondition, nChannels, nSamples, triggerFound = 0;
  double threshold, *buffer;
  NDArray *newestArray;
  NDArrayInfo_t *arrayInfo;
  NDDimension_t *dims;
  // Get the start condition and threshold.
  getIntegerParam(NDPluginCircularBuffTriggerStartCondition, &startCondition);
  getDoubleParam(NDPluginCircularBuffTriggerStartThreshold, &threshold);
  // Get the most recent array from the buffer
  newestArray = arrayBuffer_->back();
  // Get the dimensions
  // Get a pointer to the data buffer.
  buffer = (double *)newestArray->pData;
  dims = newestArray->dims;
  nChannels = dims[0];
  nSamples = dims[1];
  // We use convention that x=channel #, y=time, so data is arranged:
  // ( c0t0 c1t0 c2t0 ... cnt0 c0t1 c1t1 ... cnt1 c0t2 ... )
  // Going to choose convention that dims[0] = x, dims[1] = y and ignore value of xDim, yDim in NDArrayInfo for now.
  int triggerChannel;
  getIntegerParam(NDPluginCircularBuffTriggerChannel, &triggerChannel);
  if (triggerChannel > nChannels) {
      // Bugger
      // ###TODO: Move array validation to a single point of responsibility, not here!
  }

  // Find offset into buffer of this array (i.e. total buffer size - this array size, since this is the last array in the buffer)
  int arrayOffset = bufferSizeCounts(0) - nSamples;

  for (int sample = 0; sample < nSamples; sample++) {
      // Trigger channel value is sample no. * no. channels + trigger channel
      double triggerVal = sample * nChannels + triggerChannel;
      // For each trigger element,
  //   If <element.triggerchannel meets condition>
      if (startCondition) { // Trigger on high level
	  if (triggerVal > threshold) {
	      triggerStartOffset_ = arrayOffset + sample;
	      triggerFound = 1;
	      break;
	  }
      } else { // Trigger on low level
	  if (triggerVal < threshold) {
	      triggerStartOffset_ = arrayOffset + sample;
	      triggerFound = 1;
	      break;
	  }
      }
  }

  return triggerFound;
}

// ###TODO: This relies on gate start being set accurately, so we need to make sure cases where we trigger based on something else (soft trigger or attribute)
// handle this case properly.
int NDPluginCircularBuff::containsTriggerEnd()
{
  // ###TODO: Implement

  int endCondition, startOffset, nChannels, nSamples, triggerFound = 0;
  double threshold, *buffer;
  NDArray *newestArray;
  NDArrayInfo_t *arrayInfo;
  NDDimension_t *dims;
  // Get end condition and threshold
  getIntegerParam(NDPluginCircularBuffTriggerEndCondition, &endCondition);
  // Get start offset
  // Get current array from buffer.
  // Get its start offset. (buffer size - array size)
  // If start offset > array offset search from start offset
  // Else start from beginning of array.
  // Get pointer to array data.
  // For each element,
  //   If <element.triggerchannel meets condition>
  //     Set gateEndOffset_ to current index
  //     break
  // If found gate end
  //   return 1
  // else
  //   return 0
  return 0;
}

// Not all three counts must be calculated since none can be assumed to be a fixed size. In particular a bad frame may arrive at any point
// in the window, causing a buffer flush.
//  - pre-trigger: Will be truncated if trigger arrived before buffer full or if bad frame arrives while filling.
//      - No - if no trigger received should not output anything. Should return Null in this case.
//  - gate: Will inherently vary in size. If bad frame arrived after gate start, gate start will be set but not gate end.
//  - post-trigger: Will be truncated if a bad frame arrives after gate end but before post trigger full.
NDArray *NDPluginCircularBuff::constructOutput()
{
  // ###TODO: Implement
  // If gateStartCount < 0
  //   return null
  // Find real pre-trigger counts (this is min of pre-count param, gateStartOffset).
  // Find gate size (If gateEnd < 0 this is (buffer size - gateStart), else (gateEnd - gateStart))
  // Find real post-trigger counts (If gateEnd < 0 this is 0, else this is min (buffer size - gateEnd, post-count param)).
  // Output size is (pre-trigger + gate size + post-trigger)
  // Allocate buffer of (output size * # channels), correct data type.
  // Offset is (gateStart - pre-trigger counts).
  // buffer offset is 0.
  // For each array:
  //   copy from offset to min((array size - offset), (output size - buffer offset)) to target buffer.
  //   Increment offset and buffer offset by # of written bytes.
  // Allocate NDArray using the target buffer.
  // return this NDArray.
  // Handle the rump data (i.e. whatever's left at the end of the post trigger).
  return NULL;
}

int NDPluginCircularBuff::bufferSizeCounts(int start)
{
  // ###TODO: Implement
  // Initialise count to 0.
  // Get buffer iterator.
  // While not at end
  //   If index >= start
  //     count += iterator->size()
  //   Advance iterator
  // return count
  // ###TODO: In most cases it would be best to implement this as a class variable which is increased/decreased
  // whenever we add/remove an array. This would both be more efficient than recomputing each time and would
  // mean we could expose it as a read-only parameter.
  return 0;
}

/** Callback function that is called by the NDArray driver with new NDArray data.
  * Stores the number of pre-trigger images prior to the trigger in a ring buffer.
  * Once the trigger has been received stores the number of post-trigger buffers
  * and then exposes the buffers.
  * \param[in] pArray  The NDArray from the callback.
  */
void NDPluginCircularBuff::processCallbacks(NDArray *pArray)
{
    /* 
     * It is called with the mutex already locked.  It unlocks it during long calculations when private
     * structures don't need to be protected.
     */
    int scopeControl, preCount, postCount, currentImage, currentPostCount, softTrigger;
    NDArray *pArrayCpy = NULL;
    NDAttribute *triggerAttribute;
    NDArrayInfo arrayInfo;
    int triggered = 0;
    // ###TODO: This would be a good candidate for an enum.
    int mode = 0;

    //const char* functionName = "processCallbacks";

    /* Call the base class method */
    NDPluginDriver::processCallbacks(pArray);
    
    pArray->getInfo(&arrayInfo);

    // Retrieve the running state
    getIntegerParam(NDPluginCircularBuffControl,  &scopeControl);
    getIntegerParam(NDPluginCircularBuffPreTrigger,  &preCount);
    getIntegerParam(NDPluginCircularBuffPostTrigger,  &postCount);
    getIntegerParam(NDPluginCircularBuffCurrentImage,  &currentImage);
    getIntegerParam(NDPluginCircularBuffPostCount,  &currentPostCount);
    getIntegerParam(NDPluginCircularBuffSoftTrigger, &softTrigger);

    // Are we running?
    if (scopeControl) {

      // Check for a soft trigger
      if (softTrigger) {
        triggered = 1;
        setIntegerParam(NDPluginCircularBuffTriggered, triggered);
      } else {
	  getIntegerParam(NDPluginCircularBuffTriggered, &triggered);
	  getIntegerParam(NDPluginCircularBuffADCMode, &mode);
	  if (!triggered) {
	      // Check for the trigger meta-data in the NDArray
	      triggerAttribute = pArray->pAttributeList->find(NDPluginCircularBuffTriggeredAttribute);
	      if (triggerAttribute != NULL){
		  // Read the attribute to see if a trigger happened on this frame
		  triggerAttribute->getValue(NDAttrInt32, (void *)&triggered);
		  setIntegerParam(NDPluginCircularBuffTriggered, triggered);
	      }
	  } else if (mode == 1) {  // If in ADC mode, search for trigger condition in data.
	      triggered = containsTriggerStart();
	      setIntegerParam(NDPluginCircularBuffTriggered, triggered);
	  }
      }

      // First copy the buffer into our buffer pool so we can release the resource on the driver
      pArrayCpy = this->pNDArrayPool->copy(pArray, NULL, 1);

      if (pArrayCpy){

        // Set the Attribute to triggered
        if (softTrigger){
          pArrayCpy->pAttributeList->add(NDPluginCircularBuffTriggeredAttribute, 
                                        "External trigger (1 = detected)", NDAttrInt32, (void *)&softTrigger);
        }

        // Have we detected a trigger event yet?
        if (!triggered){
            // No trigger so add the NDArray to the pre-trigger ring
            if (mode == 0) { // camera mode
        	arrayBuffer_->push_back(pArrayCpy);
        	if (arrayBuffer_->size() > (size_t)preCount) {
        	    pOldArray_ = arrayBuffer_->front();
        	    pOldArray_->release();
        	    pOldArray_ = NULL;
        	    arrayBuffer_->pop_front();
        	}


        	// Set the size
        	setIntegerParam(NDPluginCircularBuffCurrentImage,  arrayBuffer_->size());
        	if (arrayBuffer_->size() == (size_t)preCount){
        	    setStringParam(NDPluginCircularBuffStatus, "Buffer Wrapping");
        	}
            } else if (mode == 1) { // ADC mode
                // Add new frame to buffer to construct the pre-trigger window
        	arrayBuffer_->push_back(pArrayCpy);

        	// Prune the oldest data as for camera mode, but here we use the total number of counts stored in all the NDArrays,
        	// not the number of NDArrays. If the oldest stored buffer no longer contains any counts needed for the pre-trigger,
        	// we can prune it.
        	int preCounts;
        	getIntegerParam(NDPluginCircularBuffPreTriggerSamples, &preCounts);
        	while (arrayBuffer_->size() > 0 && bufferSizeCounts(1) > preCounts) {
        	    pOldArray_ = arrayBuffer_->front();
        	    pOldArray_->release();
        	    pOldArray_ = NULL;
        	    arrayBuffer_->pop_front();
        	}
            }
        } else { // triggered
            // Trigger detected
            // If camera mode
            // Start making frames available if trigger has occured
            setStringParam(NDPluginCircularBuffStatus, "Flushing");

            if (mode == 0) { // Camera mode
        	// Has the trigger occured on this frame?
        	if (previousTrigger_ == 0){
        	    previousTrigger_ = 1;

        	    // Yes, so flush the ring first
        	    while (arrayBuffer_->size() > 0){
        		this->unlock();
        		doCallbacksGenericPointer(arrayBuffer_->front(), NDArrayData, 0);
        		this->lock();
        		arrayBuffer_->front()->release();
        		arrayBuffer_->pop_front();

        	    }
        	}

        	currentPostCount++;
        	setIntegerParam(NDPluginCircularBuffPostCount,  currentPostCount);

        	this->unlock();
        	doCallbacksGenericPointer(pArrayCpy, NDArrayData, 0);
        	this->lock();
        	if (pArrayCpy){
        	    pArrayCpy->release();
        	}
            } else { // ADC mode
        	// Always add the array to our buffer, to construct the post-trigger window.
        	// ###TODO: Duplicates pre-trigger branch, consider restructuring.
        	arrayBuffer_->push_back(pArrayCpy);

        	int triggerEnded;
        	getIntegerParam(NDPluginCircularBuffTriggerEnded, &triggerEnded);
        	if (!triggerEnded) {
        	    triggerEnded = containsTriggerEnd();
        	    setIntegerParam(NDPluginCircularBuffTriggerEnded, triggerEnded);
        	}
        	// Decide whether we are ready to output data yet - this depends on whether we
        	// have enough data stored to fill the post-trigger buffer.
        	// We don't care for this purpose how many pre-trigger counts we have, nor how long the
        	// gate window is.
        	if (triggerEnded) {
        	    int currentPostSize = bufferSizeCounts(0) - triggerEndOffset_;
        	    int postSize;
        	    getIntegerParam(NDPluginCircularBuffPostTriggerSamples, &postSize);
        	    // If we've reached our target, readout.
        	    if (currentPostSize >= postSize) {
        		NDArray *outputArray = constructOutput();
        		this->unlock();
        		doCallbacksGenericPointer(outputArray, NDArrayData, 0);
        		this->lock();
        		// ### TODO: Teardown buffer ready for next trigger.
        	    }
        	}
            }
        }

        // Stop recording once we have reached the post-trigger count, wait for a restart
        if (mode == 0 && currentPostCount >= postCount){
          setIntegerParam(NDPluginCircularBuffControl, 0);
          setStringParam(NDPluginCircularBuffStatus, "Acquisition Completed");
        }
      } else {
        //printf("pArray NULL, failed to copy the data\n");
      }
    } else {
      // Currently do nothing
    }
            
    callParamCallbacks();
}



/** Called when asyn clients call pasynInt32->write().
  * This function performs actions for some parameters.
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus NDPluginCircularBuff::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    int preCount;
    static const char *functionName = "writeInt32";

    // ###TODO: Implement ADC-specific params.
    if (function == NDPluginCircularBuffControl){
	if (value == 1){
	    // If the control is turned on then create our new ring buffer
	    getIntegerParam(NDPluginCircularBuffPreTrigger,  &preCount);
	    if (arrayBuffer_){
		for (std::deque<NDArray *>::iterator iter = arrayBuffer_->begin(); iter != arrayBuffer_->end(); ++iter) {
		    (*iter)->release();
		}
		delete arrayBuffer_;
	    }
	    arrayBuffer_ = new deque<NDArray *>();
	    if (pOldArray_){
		pOldArray_->release();
          }
          pOldArray_ = NULL;
 
          previousTrigger_ = 0;

          // Set the status to buffer filling
          setIntegerParam(NDPluginCircularBuffSoftTrigger, 0);
          setIntegerParam(NDPluginCircularBuffTriggered, 0);
          setIntegerParam(NDPluginCircularBuffPostCount, 0);
          setStringParam(NDPluginCircularBuffStatus, "Buffer filling");
        } else {
          // Control is turned off, before we have finished
          // Set the trigger value off, reset counter
          setIntegerParam(NDPluginCircularBuffSoftTrigger, 0);
          setIntegerParam(NDPluginCircularBuffTriggered, 0);
          setIntegerParam(NDPluginCircularBuffCurrentImage, 0);
          setStringParam(NDPluginCircularBuffStatus, "Acquisition Stopped");
        }

        // Set the parameter in the parameter library.
        status = (asynStatus) setIntegerParam(function, value);

    }  else if (function == NDPluginCircularBuffSoftTrigger){
        // Set the parameter in the parameter library.
        status = (asynStatus) setIntegerParam(function, value);

        // Set a soft trigger
        setIntegerParam(NDPluginCircularBuffTriggered, 1);

    }  else if (function == NDPluginCircularBuffPreTrigger){
        int postCount = 0;
        // Check the number of pre and post do not exceed max buffers
        getIntegerParam(NDPluginCircularBuffPostTrigger,  &postCount);
        if ((postCount + value) > (maxBuffers_ - 1)){
          setStringParam(NDPluginCircularBuffStatus, "Pre count too high");
        } else if (value < 0) {
          setStringParam(NDPluginCircularBuffStatus, "Pre count can't be negative");
        } else {
          // Set the parameter in the parameter library.
          status = (asynStatus) setIntegerParam(function, value);
        }
    }  else if (function == NDPluginCircularBuffPostTrigger){
        int preCount = 0;
        // Check the number of pre and post do not exceed max buffers
        getIntegerParam(NDPluginCircularBuffPreTrigger,  &preCount);
        if ((preCount + value) > (maxBuffers_ - 1)){
          setStringParam(NDPluginCircularBuffStatus, "Post count too high");
        } else if (value <= 0) {
          setStringParam(NDPluginCircularBuffStatus, "Post count must be at least 1");
        } else {
          // Set the parameter in the parameter library.
          status = (asynStatus) setIntegerParam(function, value);
        }
    }  else {

        // Set the parameter in the parameter library.
        status = (asynStatus) setIntegerParam(function, value);

        // If this parameter belongs to a base class call its method
        if (function < FIRST_NDPLUGIN_CIRCULAR_BUFF_PARAM)
            status = NDPluginDriver::writeInt32(pasynUser, value);
    }
    
    // Do callbacks so higher layers see any changes
    status = (asynStatus) callParamCallbacks();
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%d", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%d\n", 
              driverName, functionName, function, value);
    return status;
}


/** Constructor for NDPluginCircularBuff; most parameters are simply passed to NDPluginDriver::NDPluginDriver.
  * After calling the base class constructor this method sets reasonable default values for all of the
  * parameters.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] queueSize The number of NDArrays that the input queue for this plugin can hold when
  *            NDPluginDriverBlockingCallbacks=0.  Larger queues can decrease the number of dropped arrays,
  *            at the expense of more NDArray buffers being allocated from the underlying driver's NDArrayPool.
  * \param[in] blockingCallbacks Initial setting for the NDPluginDriverBlockingCallbacks flag.
  *            0=callbacks are queued and executed by the callback thread; 1 callbacks execute in the thread
  *            of the driver doing the callbacks.
  * \param[in] NDArrayPort Name of asyn port driver for initial source of NDArray callbacks.
  * \param[in] NDArrayAddr asyn port driver address for initial source of NDArray callbacks.
  * \param[in] maxBuffers The maximum number of NDArray buffers that the NDArrayPool for this driver is
  *            allowed to allocate. Set this to -1 to allow an unlimited number of buffers.
  * \param[in] maxMemory The maximum amount of memory that the NDArrayPool for this driver is
  *            allowed to allocate. Set this to -1 to allow an unlimited amount of memory.
  * \param[in] priority The thread priority for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
  * \param[in] stackSize The stack size for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
  */
NDPluginCircularBuff::NDPluginCircularBuff(const char *portName, int queueSize, int blockingCallbacks,
                         const char *NDArrayPort, int NDArrayAddr,
                         int maxBuffers, size_t maxMemory,
                         int priority, int stackSize)
    /* Invoke the base class constructor */
    : NDPluginDriver(portName, queueSize, blockingCallbacks,
                   NDArrayPort, NDArrayAddr, 1, NUM_NDPLUGIN_CIRCULAR_BUFF_PARAMS, maxBuffers, maxMemory,
                   asynInt32ArrayMask | asynFloat64ArrayMask | asynGenericPointerMask,
                   asynInt32ArrayMask | asynFloat64ArrayMask | asynGenericPointerMask,
                   0, 1, priority, stackSize)
{
    //const char *functionName = "NDPluginCircularBuff";
    arrayBuffer_ = NULL;

    maxBuffers_ = maxBuffers;

    // General
    createParam(NDPluginCircularBuffControlString,      asynParamInt32,      &NDPluginCircularBuffControl);
    createParam(NDPluginCircularBuffStatusString,       asynParamOctet,      &NDPluginCircularBuffStatus);
    createParam(NDPluginCircularBuffSoftTriggerString,  asynParamInt32,      &NDPluginCircularBuffSoftTrigger);
    createParam(NDPluginCircularBuffTriggeredString,    asynParamInt32,      &NDPluginCircularBuffTriggered);
    createParam(NDPluginCircularBuffADCModeString,            asynParamInt32,   &NDPluginCircularBuffADCMode);

    // Scope
    createParam(NDPluginCircularBuffPreTriggerString,   asynParamInt32,      &NDPluginCircularBuffPreTrigger);
    createParam(NDPluginCircularBuffPostTriggerString,  asynParamInt32,      &NDPluginCircularBuffPostTrigger);
    createParam(NDPluginCircularBuffCurrentImageString, asynParamInt32,      &NDPluginCircularBuffCurrentImage);
    createParam(NDPluginCircularBuffPostCountString,    asynParamInt32,      &NDPluginCircularBuffPostCount);

    // ADC
    createParam(NDPluginCircularBuffTriggerDimensionString,      asynParamInt32,   &NDPluginCircularBuffTriggerDimension); // Hard-code to 1 for now
    createParam(NDPluginCircularBuffTriggerChannelString,        asynParamInt32,   &NDPluginCircularBuffTriggerChannel);
    createParam(NDPluginCircularBuffPreTriggerSamplesString,     asynParamInt32,   &NDPluginCircularBuffPreTriggerSamples);
    createParam(NDPluginCircularBuffPostTriggerSamplesString,    asynParamInt32,   &NDPluginCircularBuffPostTriggerSamples);
    createParam(NDPluginCircularBuffTriggerStartConditionString, asynParamInt32,   &NDPluginCircularBuffTriggerStartCondition);
    createParam(NDPluginCircularBuffTriggerEndConditionString,   asynParamInt32,   &NDPluginCircularBuffTriggerEndCondition);
    createParam(NDPluginCircularBuffTriggerStartThresholdString, asynParamFloat64, &NDPluginCircularBuffTriggerStartThreshold);
    createParam(NDPluginCircularBuffTriggerEndThresholdString,   asynParamFloat64, &NDPluginCircularBuffTriggerEndThreshold);
    createParam(NDPluginCircularBuffTriggerMaxString,            asynParamInt32,   &NDPluginCircularBuffTriggerMax);
    createParam(NDPluginCircularBuffTriggerEndedString,          asynParamInt32,   &NDPluginCircularBuffTriggerEnded);
    createParam(NDPluginCircularBuffTriggerCountString,          asynParamInt32,   &NDPluginCircularBuffTriggerCount);

    // Set the plugin type string
    setStringParam(NDPluginDriverPluginType, "NDPluginCircularBuff");

    // Set the status to idle
    setStringParam(NDPluginCircularBuffStatus, "Idle");

    // Init the current frame count to zero
    setIntegerParam(NDPluginCircularBuffCurrentImage, 0);
    setIntegerParam(NDPluginCircularBuffPostCount, 0);

    // Init the scope control to off
    setIntegerParam(NDPluginCircularBuffControl, 0);

    // Init the pre and post count to 100
    setIntegerParam(NDPluginCircularBuffPreTrigger, 100);
    setIntegerParam(NDPluginCircularBuffPostTrigger, 100);

    // Init to camera mode
    setIntegerParam(NDPluginCircularBuffADCMode, 0);

    // Set to concatenate on dimension 1 (though no enforcement to use 2d arrays yet).
    setIntegerParam(NDPluginCircularBuffTriggerDimension, 1);
    setIntegerParam(NDPluginCircularBuffTriggerChannel, 1);

    setIntegerParam(NDPluginCircularBuffPreTriggerSamples, 100);
    setIntegerParam(NDPluginCircularBuffPostTriggerSamples, 100);

    setIntegerParam(NDPluginCircularBuffTriggerStartCondition, 1);
    setIntegerParam(NDPluginCircularBuffTriggerEndCondition, 0);

    setDoubleParam(NDPluginCircularBuffTriggerStartThreshold, 1.0);
    setDoubleParam(NDPluginCircularBuffTriggerEndThreshold, 0.0);

    setIntegerParam(NDPluginCircularBuffTriggerMax, 0);

    setIntegerParam(NDPluginCircularBuffTriggerEnded, 0);
    setIntegerParam(NDPluginCircularBuffTriggerCount, 0);

    // Try to connect to the array port
    connectToArrayPort();
}

/** Configuration command */
extern "C" int NDCircularBuffConfigure(const char *portName, int queueSize, int blockingCallbacks,
                                const char *NDArrayPort, int NDArrayAddr,
                                int maxBuffers, size_t maxMemory)
{
    new NDPluginCircularBuff(portName, queueSize, blockingCallbacks, NDArrayPort, NDArrayAddr,
                      maxBuffers, maxMemory, 0, 2000000);
    return(asynSuccess);
}

/* EPICS iocsh shell commands */
static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "frame queue size",iocshArgInt};
static const iocshArg initArg2 = { "blocking callbacks",iocshArgInt};
static const iocshArg initArg3 = { "NDArrayPort",iocshArgString};
static const iocshArg initArg4 = { "NDArrayAddr",iocshArgInt};
static const iocshArg initArg5 = { "maxBuffers",iocshArgInt};
static const iocshArg initArg6 = { "maxMemory",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1,
                                            &initArg2,
                                            &initArg3,
                                            &initArg4,
                                            &initArg5,
                                            &initArg6};
static const iocshFuncDef initFuncDef = {"NDCircularBuffConfigure",7,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    NDCircularBuffConfigure(args[0].sval, args[1].ival, args[2].ival,
                     args[3].sval, args[4].ival, args[5].ival,
                     args[6].ival);
}

extern "C" void NDCircularBuffRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

extern "C" {
epicsExportRegistrar(NDCircularBuffRegister);
}


