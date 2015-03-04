/*
 * NDPluginReframe.cpp
 *
 *  Created on: 2 Mar 2015
 *      Author: Ed Warrick
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
#include "NDPluginReframe.h"

#define MAX(A,B) (A)>(B)?(A):(B)
#define MIN(A,B) (A)<(B)?(A):(B)

static const char *driverName="NDPluginReframe";

/**
 * containsTriggerStart
 * Searches the most recent frame in the buffer for the trigger start condition
 * Side effects: Sets triggerStartOffset_ to the location of the trigger condition if found.
 * \return int, set to 1 if trigger found, 0 otherwise
 */
int NDPluginReframe::containsTriggerStart()
{
  // ###TODO: Implement
  int startCondition, nChannels, nSamples, triggerChannel, triggerFound = 0;
  double threshold, *buffer;
  NDArray *newestArray;
  NDDimension_t *dims;
  // Get the start condition and threshold.
  getIntegerParam(NDPluginReframeTriggerChannel, &triggerChannel);
  getIntegerParam(NDPluginReframeTriggerStartCondition, &startCondition);
  getDoubleParam(NDPluginReframeTriggerStartThreshold, &threshold);
  // Get the most recent array from the buffer
  newestArray = arrayBuffer_->back();
  // Get the dimensions
  // Get a pointer to the data buffer.
  buffer = (double *)newestArray->pData;
  dims = newestArray->dims;
  // ###TODO Check whether we need to look at the other elements of dims (offset, binning, reverse).
  nChannels = dims[0].size;
  nSamples  = dims[1].size;
  // We use convention that x=channel #, y=time, so data is arranged:
  // ( c0t0 c1t0 c2t0 ... cnt0 c0t1 c1t1 ... cnt1 c0t2 ... )
  // Going to choose convention that dims[0] = x, dims[1] = y and ignore value of xDim, yDim in NDArrayInfo for now.

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
/**
  * containsTriggerEnd
  * Searches the most recent frame in the buffer, starting from the triggerStartOffset_, for the trigger end condition.
  * Side effects: Sets triggerEndOffset_ to the offset of the end condition if found.
  * \return int, set to 1 if trigger end found, 0 otherwise.
  */
int NDPluginReframe::containsTriggerEnd()
{
  // ###TODO: Implement

  int endCondition, arrayOffset, arrayIndex = 0, nChannels, nSamples, triggerChannel, triggerFound = 0;
  double threshold, *buffer;
  NDArray *newestArray;
  NDDimension_t *dims;
  // Get end condition and threshold
  getIntegerParam(NDPluginReframeTriggerEndCondition, &endCondition);
  getIntegerParam(NDPluginReframeTriggerChannel, &triggerChannel);
  getDoubleParam(NDPluginReframeTriggerStartThreshold, &threshold);
  // Get current array from buffer.
  newestArray = arrayBuffer_->back();
  dims = newestArray->dims;
  // ###TODO Check whether we need to look at the other elements of dims (offset, binning, reverse).
  nChannels = dims[0].size;
  nSamples  = dims[1].size;
  // Get its start offset. (buffer size - array size)
  arrayOffset = bufferSizeCounts(0) - nSamples;
  // If start offset > array offset search from start offset
  // Else start from beginning of array.
  if (triggerStartOffset_ > arrayOffset) {
      arrayIndex = triggerStartOffset_ - arrayOffset;
  }
  // Get pointer to array data.
  buffer = (double *)newestArray->pData;

  for (int sample = 0; sample < nSamples; sample++) {
      // Trigger channel value is sample no. * no. channels + trigger channel
      double triggerVal = sample * nChannels + triggerChannel;
      // For each trigger element,
  //   If <element.triggerchannel meets condition>
      if (endCondition) { // Trigger on high level
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

  return 0;
}

// Not all three counts must be calculated since none can be assumed to be a fixed size. In particular a bad frame may arrive at any point
// in the window, causing a buffer flush.
//  - pre-trigger: Will be truncated if trigger arrived before buffer full or if bad frame arrives while filling.
//      - No - if no trigger received should not output anything. Should return Null in this case.
//  - gate: Will inherently vary in size. If bad frame arrived after gate start, gate start will be set but not gate end.
//  - post-trigger: Will be truncated if a bad frame arrives after gate end but before post trigger full.
/**
  * constructOutput
  * Uses the pre- and post-trigger sizes and the trigger start and end offsets to construct a single output NDArray of size:
  * <pre trigger> + <gate length> + <post trigger>
  * Where <gate length> = triggerEndOffset_ - triggerStartOffset_.
  * Side effects: Clears the buffer, leaving it containing either a single NDArray containing any bytes left over after the end of the
  * post trigger, or else nothing if the post trigger end aligned with the end of the last NDArray.
  * \return The single reframed NDArray for output.
  */
NDArray *NDPluginReframe::constructOutput()
{
  int preTriggerCounts, triggerCounts, postTriggerCounts, outputCounts, nChannels, sourceOffset, targetOffset, nSamples, carryCounts;
  int preTrigger, postTrigger;
  double *sourceBuffer, *targetBuffer, *carryBuffer;
  NDArray *sourceArray = NULL, *outputArray = NULL, *carryArray = NULL;

  getIntegerParam(NDPluginReframePreTriggerSamples, &preTrigger);
  getIntegerParam(NDPluginReframePostTriggerSamples, &postTrigger);

  // If no trigger has been detected, don't output anything.
  if (triggerStartOffset_ < 0)
      return NULL;

  // Find real pre-trigger counts (this is min of pre-count param, gateStartOffset).
  preTriggerCounts = MIN(preTrigger, triggerStartOffset_);

  // Find gate size (If gateEnd < 0 this is (buffer size - gateStart), else (gateEnd - gateStart))
  if (triggerEndOffset_ < 0) {
      // If we didn't see the trigger end, this is everything after the trigger start
      triggerCounts = bufferSizeCounts(0) - triggerStartOffset_;
  } else {
      // Otherwise it's the # counts between trigger start and trigger end
      triggerCounts = triggerEndOffset_ - triggerStartOffset_;
  }

  // Find real post-trigger counts (If gateEnd < 0 this is 0, else this is min (buffer size - gateEnd, post-count param)).
  if (triggerEndOffset_ < 0) {
      // If we didn't see the trigger end this is zero
      postTriggerCounts = 0;
  } else {
      // Otherwise it's either the full post trigger size, or else all the counts left after the trigger end if there aren't enough to fill the
      // post trigger
      postTriggerCounts = MIN(bufferSizeCounts(0) - triggerEndOffset_, postTrigger);
  }

  // Output size is (pre-trigger + gate size + post-trigger)
  outputCounts = preTriggerCounts + triggerCounts + postTriggerCounts;

  // Allocate buffer of (output size * # channels), correct data type.
  targetBuffer = new double[outputCounts];

  // Offset is (gateStart - pre-trigger counts).
  sourceOffset = triggerStartOffset_ - preTriggerCounts;
  // buffer offset is 0.
  targetOffset = 0;

  // Iterate over the arrays from oldest to newest.
  // ###TODO: Need to either use a different loop condition, or pop the queue elements as we use them.
  std::deque<NDArray *>::iterator iter;

  // While not at end
  for (iter = arrayBuffer_->begin(); iter != arrayBuffer_->end(); iter++) {
      sourceArray = *iter;
      sourceBuffer = (double *)sourceArray->pData;
      nChannels = sourceArray->dims[1].size;
      nSamples = sourceArray->dims[0].size;
  // For each array:
  //   copy from offset to min((array size - offset), (output size - buffer offset)) to target buffer.
      int counts = MIN(nSamples - sourceOffset, outputCounts - targetOffset);
      memcpy(targetBuffer + targetOffset * nChannels, sourceBuffer + sourceOffset * nChannels, counts * nChannels * sizeof(double));
  //   Increment buffer offset by # of written bytes.
      targetOffset += counts;
      // Compute number of counts remaining at end of buffer (to carry to next buffer).
      carryCounts = nSamples - sourceOffset - counts;
      sourceOffset = 0;
  }

  // Handle the carry data left at the end of the last array in the buffer.
  // This is important since a use-case for this plugin is set to trigger immediately and rearm indefinitely, to effectively change the time base for
  // readout (e.g. concatenate arrays from a source which reads out at 1Hz and rebroadcast them as a single array at 0.1Hz). So it is important we do
  // not simply discard the carry data.

  if (sourceBuffer && carryCounts) {
      carryBuffer = (double *)malloc(carryCounts*sizeof(double));
      // Copy the data from the last NDArray, starting from
      memcpy(sourceBuffer + (nSamples - carryCounts) * nChannels, carryBuffer, carryCounts * nChannels * sizeof(double));
      //    NDArray*     alloc     (int ndims, size_t *dims, NDDataType_t dataType, size_t dataSize, void *pData);
      // NDArrayPool copies the dims array in alloc, so OK to allocate this on the stack.
      size_t dims[2] = { nChannels, carryCounts };
      carryArray = this->pNDArrayPool->alloc(2, dims, NDFloat64, carryCounts * nChannels * sizeof(double), carryBuffer);
  }

  // Now tear down the original buffer
  while (arrayBuffer_->front()) {
      sourceArray = arrayBuffer_->front();
      sourceArray->release();
      arrayBuffer_->pop_front();
  }

  // If there is a carry array add it to the buffer
  if (carryArray) {
      arrayBuffer_->push_back(carryArray);
  }

  // Allocate NDArray using the target buffer.
  // return this NDArray.
  if (outputCounts) {
      size_t dims[2] = { nChannels, outputCounts };
      outputArray = this->pNDArrayPool->alloc(2, dims, NDFloat64, outputCounts * nChannels * sizeof(double), targetBuffer);
  }

  return outputArray;
  // ###TODO: Handle attribute copying
  // ###TODO: Do we need to worry about memory cap?
  // ###TODO: NDArrayPool uses malloc, so we should too. It does not copy the data buffer (it just stores the pointer), so we should not free the buffers
  //          here.
  //          Perhaps we could allocate the NDArrays first, then get their data buffers to actually write into? Then we get to use NDArrayPool to check
  //          we're within memory bounds before trying to do any work on the actual buffers.
}

/**
  * bufferSizeCounts
  * The number of samples stored in the buffer, found by walking the buffer and summing the array size for each NDArray.
  * \param[in] start an integer indicating the frame to start counting from. Set this to 0 for the total buffer size; setting
  * it to 1 counts all but the oldest NDArray, and so on.
  * \return The number of samples stored from the start of the requested frame to the end of the buffer.
  */
int NDPluginReframe::bufferSizeCounts(int start)
{
    // ###TODO: Implement
    // Initialise count to 0.
    int count = 0, index = 0;
    // Get buffer iterator.
    std::deque<NDArray *>::iterator iter;

    // While not at end
    for (iter = arrayBuffer_->begin(); iter != arrayBuffer_->end(); iter++, index++) {
        // Skip arrays until we reach start
        if (index >= start) {
            count += (*iter)->dims[1].size;
        }
    }

    return count;
    // ###TODO: In most cases it would be best to implement this as a class variable which is increased/decreased
    // whenever we add/remove an array. This would both be more efficient than recomputing each time and would
    // mean we could expose it as a read-only parameter.
}

/** Function which checks that incoming NDArrays are consistent in dimension (so we don't end up trying to concatenate arrays with
  * more than 2 dimensions, or with differing numbers of channels).
  * \param[in] pArray the array to validate
  * \return 1 if valid, 0 otherwise
  */
int NDPluginReframe::arrayIsValid(NDArray *pArray)
{
    int triggerChannel;
    NDDimension_t *dims, *expectedDims;
    getIntegerParam(NDPluginReframeTriggerChannel, &triggerChannel);
    if (pArray->ndims != 2)
        return 0;

    dims = pArray->dims;
    if (dims[0].size < triggerChannel)
        return 0;

    if (arrayBuffer_->front()) {
        expectedDims = arrayBuffer_->front()->dims;
        if (expectedDims[0].size != dims[0].size)
            return 0;
    }

    return 1;
}

/** Callback function that is called by the NDArray driver with new NDArray data.
  * Stores the number of pre-trigger images prior to the trigger in a ring buffer.
  * Once the trigger has been received stores the number of post-trigger buffers
  * and then exposes the buffers.
  * \param[in] pArray  The NDArray from the callback.
  */
void NDPluginReframe::processCallbacks(NDArray *pArray)
{
    /* This needs to do a few things:
     * Copy the incoming arrays
     * Store them in a buffer
     * Check them for trigger conditions
     * If triggered, check if trigger should switch off
     * If not, check if post buffer full
     * If so, output array and either go to Armed or Idle
     * If no trigger, check if can prune pre-buffer
     * Loop on triggering code in case multiple triggers in frame.
     */
    /* Call the base class method */
    NDPluginDriver::processCallbacks(pArray);

    if (mode_ != Idle) {
        NDArray *pArrayCpy = this->pNDArrayPool->copy(pArray, NULL, 1);

        // Always add the array to the buffer - should never be adding too much data for post-trigger, and if
        // we end up with too much data for the pre-trigger we will prune the oldest arrays later.
        if (pArrayCpy && arrayIsValid(pArrayCpy)) {

            arrayBuffer_->push_back(pArrayCpy);

            // This loop is here because we might be using input frames that are larger than our output frames (i.e. chopping up a large NDArray into
            // several larger chunks). In this case we might need to output several NDArrays for a single call of processCallbacks, so we need
            // to reprocess in order to search for new triggers.
            bool retrigger = true;
            while (retrigger) { // some bytes not checked for triggers
                retrigger = false;
                if (mode_ == Armed) {
                    printf("Checking for trigger\n");
                    // Check for trigger on
                    int softTrigger;
                    getIntegerParam(NDPluginReframeSoftTrigger, &softTrigger);
                    // Use short-circuit eval for OR to use state of soft trigger if no hardware trigger found
                    int triggered = containsTriggerStart() || softTrigger;
                    // Prune buffer if no trigger occurred on this frame.
                    if (!triggered) {
                        int preCounts;
                        getIntegerParam(NDPluginReframePreTriggerSamples, &preCounts);
                        while (arrayBuffer_->size() > 0 && bufferSizeCounts(1) > preCounts) {
                            NDArray *pOldArray_ = arrayBuffer_->front();
                            pOldArray_->release();
                            arrayBuffer_->pop_front();
                        }
                    } else {
                        printf("Trigger found\n");
                        mode_ = Gating;
                    }
                }

                // Will only be 1 trigger off or end of post trigger per frame, so an if is fine here.
                if (mode_ == Gating) {
                    // Check for trigger off
                    int triggerEnded = containsTriggerEnd();
                    setIntegerParam(NDPluginReframeTriggerEnded, triggerEnded);
                    mode_ = Acquiring;
                    printf("Trigger ended\n");
                }

                if (mode_ == Acquiring) {
                    printf("Acquiring post-trigger\n");
                    // Check if post-trigger full
                    int currentPostSize = bufferSizeCounts(0) - triggerEndOffset_;
                    int postSize;
                    getIntegerParam(NDPluginReframePostTriggerSamples, &postSize);
                    // If so, readout.
                    if (currentPostSize >= postSize) {
                        printf("Outputting array\n");
                        NDArray *outputArray = constructOutput();
                        if (outputArray) {
                            this->unlock();
                            doCallbacksGenericPointer(outputArray, NDArrayData, 0);
                            this->lock();
                        }

                        int currentTriggerCount, maxTriggerCount;
                        getIntegerParam(NDPluginReframeTriggerCount, &currentTriggerCount);
                        getIntegerParam(NDPluginReframeTriggerMax, &maxTriggerCount);
                        currentTriggerCount++;
                        setIntegerParam(NDPluginReframeTriggerCount, currentTriggerCount);

                        // Check whether we've reached target # of triggers yet and set to either armed or idle
                        // If max is set to 0, we should re-arm indefinitely.
                        if (currentTriggerCount >= maxTriggerCount && maxTriggerCount > 0) {
                            if (arrayBuffer_->size()) {
                                NDArray *carryArray = arrayBuffer_->front();
                                carryArray->release();
                                arrayBuffer_->pop_front();
                            }
                            mode_ = Idle;
                            printf("Done\n");
                        } else {
                            triggerStartOffset_ = 0;
                            triggerEndOffset_ = 0;
                            setIntegerParam(NDPluginReframeTriggerEnded, 0);
                            // Still some data that has not been searched for triggers, so we should re-run the loop.
                            if (arrayBuffer_->size())
                                retrigger = true;
                            mode_ = Armed;
                            printf("Rearming\n");
                        }
                    }
                } // mode acquiring
            } // while still looking for triggers
        } else { // either couldn't copy the array or it failed to validate, so output any triggered data and reset.
            NDArray *outputArray = constructOutput();
            if (outputArray) {
                this->unlock();
                doCallbacksGenericPointer(outputArray, NDArrayData, 0);
                this->lock();
            }
            triggerStartOffset_ = 0;
            triggerEndOffset_ = 0;
            setIntegerParam(NDPluginReframeTriggerEnded, 0);
            mode_ = Idle;

        }
    } // if not idle

    callParamCallbacks();
}

/** Called when asyn clients call pasynInt32->write().
  * This function performs actions for some parameters.
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus NDPluginReframe::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    static const char *functionName = "writeInt32";

    if (function == NDPluginReframeControl) {
        // If the control is turned on then create our new ring buffer
        if (value == 1) {
            if (arrayBuffer_){
                for (std::deque<NDArray *>::iterator iter = arrayBuffer_->begin(); iter != arrayBuffer_->end(); ++iter) {
                    (*iter)->release();
                }
            } else {
                arrayBuffer_ = new std::deque<NDArray *>;
            }
            // Set the status to buffer filling and clear any residual state current/last trigger
            setIntegerParam(NDPluginReframeSoftTrigger, 0);
            setIntegerParam(NDPluginReframeTriggered, 0);
            setIntegerParam(NDPluginReframeTriggerCount, 0);
            setIntegerParam(NDPluginReframeTriggerEnded, 0);
            setStringParam(NDPluginReframeStatus, "Buffer filling");
            mode_ = Armed;
        } else {
            mode_ = Idle;
        }
    } else {
        // Set the parameter in the parameter library.
        status = (asynStatus) setIntegerParam(function, value);

        // If this parameter belongs to a base class call its method
        if (function < FIRST_NDPLUGIN_REFRAME_PARAM)
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


/** Constructor for NDPluginReframe; most parameters are simply passed to NDPluginDriver::NDPluginDriver.
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
NDPluginReframe::NDPluginReframe(const char *portName, int queueSize, int blockingCallbacks,
                         const char *NDArrayPort, int NDArrayAddr,
                         int maxBuffers, size_t maxMemory,
                         int priority, int stackSize)
    /* Invoke the base class constructor */
    : NDPluginDriver(portName, queueSize, blockingCallbacks,
                   NDArrayPort, NDArrayAddr, 1, NUM_NDPLUGIN_REFRAME_PARAMS, maxBuffers, maxMemory,
                   asynInt32ArrayMask | asynFloat64ArrayMask | asynGenericPointerMask,
                   asynInt32ArrayMask | asynFloat64ArrayMask | asynGenericPointerMask,
                   0, 1, priority, stackSize)
{
    //const char *functionName = "NDPluginReframe";
    arrayBuffer_ = NULL;

    // General
    createParam(NDPluginReframeControlString,               asynParamInt32,      &NDPluginReframeControl);
    createParam(NDPluginReframeStatusString,                asynParamOctet,      &NDPluginReframeStatus);
    createParam(NDPluginReframeSoftTriggerString,           asynParamInt32,      &NDPluginReframeSoftTrigger);
    createParam(NDPluginReframeTriggeredString,             asynParamInt32,      &NDPluginReframeTriggered);
    createParam(NDPluginReframeTriggerDimensionString,      asynParamInt32,   &NDPluginReframeTriggerDimension); // Hard-code to 1 for now
    createParam(NDPluginReframeTriggerChannelString,        asynParamInt32,   &NDPluginReframeTriggerChannel);
    createParam(NDPluginReframePreTriggerSamplesString,     asynParamInt32,   &NDPluginReframePreTriggerSamples);
    createParam(NDPluginReframePostTriggerSamplesString,    asynParamInt32,   &NDPluginReframePostTriggerSamples);
    createParam(NDPluginReframeTriggerStartConditionString, asynParamInt32,   &NDPluginReframeTriggerStartCondition);
    createParam(NDPluginReframeTriggerEndConditionString,   asynParamInt32,   &NDPluginReframeTriggerEndCondition);
    createParam(NDPluginReframeTriggerStartThresholdString, asynParamFloat64, &NDPluginReframeTriggerStartThreshold);
    createParam(NDPluginReframeTriggerEndThresholdString,   asynParamFloat64, &NDPluginReframeTriggerEndThreshold);
    createParam(NDPluginReframeTriggerMaxString,            asynParamInt32,   &NDPluginReframeTriggerMax);
    createParam(NDPluginReframeTriggerEndedString,          asynParamInt32,   &NDPluginReframeTriggerEnded);
    createParam(NDPluginReframeTriggerCountString,          asynParamInt32,   &NDPluginReframeTriggerCount);

    // Set the plugin type string
    setStringParam(NDPluginDriverPluginType, "NDPluginReframe");

    // Set the status to idle
    setStringParam(NDPluginReframeStatus, "Idle");

    // Set to concatenate on dimension 1 (though no enforcement to use 2d arrays yet).
    setIntegerParam(NDPluginReframeTriggerDimension, 1);
    setIntegerParam(NDPluginReframeTriggerChannel, 1);

    setIntegerParam(NDPluginReframePreTriggerSamples, 100);
    setIntegerParam(NDPluginReframePostTriggerSamples, 100);

    setIntegerParam(NDPluginReframeTriggerStartCondition, 1);
    setIntegerParam(NDPluginReframeTriggerEndCondition, 0);

    setDoubleParam(NDPluginReframeTriggerStartThreshold, 1.0);
    setDoubleParam(NDPluginReframeTriggerEndThreshold, 0.0);

    setIntegerParam(NDPluginReframeTriggerMax, 0);

    setIntegerParam(NDPluginReframeTriggerEnded, 0);
    setIntegerParam(NDPluginReframeTriggerCount, 0);

    // Try to connect to the array port
    connectToArrayPort();
}

/** Configuration command */
extern "C" int NDReframeConfigure(const char *portName, int queueSize, int blockingCallbacks,
                                const char *NDArrayPort, int NDArrayAddr,
                                int maxBuffers, size_t maxMemory)
{
    new NDPluginReframe(portName, queueSize, blockingCallbacks, NDArrayPort, NDArrayAddr,
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
static const iocshFuncDef initFuncDef = {"NDReframeConfigure",7,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    NDReframeConfigure(args[0].sval, args[1].ival, args[2].ival,
                     args[3].sval, args[4].ival, args[5].ival,
                     args[6].ival);
}

extern "C" void NDReframeRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

extern "C" {
epicsExportRegistrar(NDReframeRegister);
}
