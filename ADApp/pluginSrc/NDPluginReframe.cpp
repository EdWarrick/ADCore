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
  int startCondition, nChannels, nSamples, triggerChannel, triggerFound = 0;
  double threshold, *buffer;
  NDArray *newestArray;
  NDDimension_t *dims;

  getIntegerParam(NDPluginReframeTriggerChannel, &triggerChannel);
  getIntegerParam(NDPluginReframeTriggerStartCondition, &startCondition);
  getDoubleParam(NDPluginReframeTriggerStartThreshold, &threshold);

  // Get the most recent array from the buffer, extract the dimensions and get a pointer to the data
  newestArray = arrayBuffer_->back();
  dims = newestArray->dims;
  // ###TODO Check whether we need to look at the other elements of dims (offset, binning, reverse).
  nChannels = dims[0].size;
  nSamples  = dims[1].size;
  buffer = (double *)newestArray->pData;

  // Find offset into buffer of the start of this array
  int arrayOffset = bufferSizeCounts(0) - nSamples;

  for (int sample = 0; sample < nSamples; sample++) {

      double triggerVal = buffer[sample * nChannels + triggerChannel];

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

/**
  * containsTriggerEnd
  * Searches the most recent frame in the buffer, starting from the triggerStartOffset_, for the trigger end condition.
  * Side effects: Sets triggerEndOffset_ to the offset of the end condition if found.
  * \return int, set to 1 if trigger end found, 0 otherwise.
  */
int NDPluginReframe::containsTriggerEnd()
{
  int endCondition, arrayOffset, arrayIndex = 0, nChannels, nSamples, triggerChannel, triggerFound = 0;
  double threshold, *buffer;
  NDArray *newestArray;
  NDDimension_t *dims;

  getIntegerParam(NDPluginReframeTriggerEndCondition, &endCondition);
  getIntegerParam(NDPluginReframeTriggerChannel, &triggerChannel);
  getDoubleParam(NDPluginReframeTriggerEndThreshold, &threshold);

  // Get latest array from buffer and extract dimensions.
  newestArray = arrayBuffer_->back();
  dims = newestArray->dims;
  // ###TODO Check whether we need to look at the other elements of dims (offset, binning, reverse).
  nChannels = dims[0].size;
  nSamples  = dims[1].size;

  // Get its start offset. (buffer size - array size)
  arrayOffset = bufferSizeCounts(0) - nSamples;

  // If trigger start is in this frame then only search the part of the frame after the trigger start.
  if (triggerStartOffset_ > arrayOffset) {
      arrayIndex = triggerStartOffset_ - arrayOffset;
  }

  buffer = (double *)newestArray->pData;
  for (int sample = arrayIndex; sample < nSamples; sample++) {

      double triggerVal = buffer[sample * nChannels + triggerChannel];

      if (endCondition) { // Trigger on high level
          if (triggerVal > threshold) {
              triggerEndOffset_ = arrayOffset + sample;
              triggerFound = 1;
              break;
          }
      } else { // Trigger on low level
          if (triggerVal < threshold) {
              triggerEndOffset_ = arrayOffset + sample;
              triggerFound = 1;
              break;
          }
      }
  }

  return triggerFound;
}

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
  int preTriggerCounts, triggerCounts, postTriggerCounts, outputCounts, nChannels, sourceOffset, targetOffset, nSamples=0, carryCounts=0;
  int preTrigger, postTrigger, arrayCount;
  double *sourceBuffer = NULL, *targetBuffer = NULL, *carryBuffer = NULL;
  NDArray *sourceArray = NULL, *outputArray = NULL, *carryArray = NULL;

  getIntegerParam(NDPluginReframePreTriggerSamples, &preTrigger);
  getIntegerParam(NDPluginReframePostTriggerSamples, &postTrigger);
  getIntegerParam(NDPluginReframeTriggerTotal, &arrayCount);

  // If no trigger has been detected, don't output anything (will arise if we got a bad input array before seeing any triggers).
  if (triggerStartOffset_ < 0 || arrayBuffer_->empty())
      return NULL;

  // Note all three parts of the window must be calculated since none can be assumed to be a fixed size. In particular a bad frame may arrive at any point
  // in the window, causing a buffer flush.
  //  - pre-trigger: Will be truncated if trigger arrived before buffer full.
  //  - gate: Will inherently vary in size. If bad frame arrived after gate start, gate start will be set but not gate end.
  //  - post-trigger: Will be truncated if a bad frame arrives after gate end but before post trigger full.

  // Find real pre-trigger counts (this is preTrigger if we've acquired enough data to fill it, otherwise as much as we have so far).
  preTriggerCounts = MIN(preTrigger, triggerStartOffset_);

  // Find gate size
  if (triggerEndOffset_ < 0) {
      // If we didn't see the trigger end yet, this is everything after the trigger start
      triggerCounts = bufferSizeCounts(0) - triggerStartOffset_;
  } else {
      // Otherwise it's the number of counts between trigger start and trigger end
      triggerCounts = triggerEndOffset_ - triggerStartOffset_;
  }

  // Find real post-trigger counts.
  if (triggerEndOffset_ < 0) {
      // If we didn't see the trigger end this is zero
      postTriggerCounts = 0;
  } else {
      // Otherwise it's either the full post trigger size, or else all the counts left after the trigger end if there aren't enough to fill the
      // post trigger
      postTriggerCounts = MIN(bufferSizeCounts(0) - triggerEndOffset_, postTrigger);
  }

  // Output size is (pre-trigger + gate size + post-trigger)
  // ###TODO: This fails if pre and post trigger are both size 0.
  // This seems to occur if we set post to 0 and trigger immediately. Also would occur in edge case of readout due to bad frame,
  // where triggerendOffset_ happens to fall on the end of the buffer (and pre trigger and gate size both happen to be zero).
  // How to handle this case?
  outputCounts = preTriggerCounts + triggerCounts + postTriggerCounts;
  NDArray *firstArray = arrayBuffer_->front();
  nChannels = firstArray->dims[0].size;

  size_t dims[2] = { nChannels, outputCounts };
  outputArray = this->pNDArrayPool->alloc(2, dims, NDFloat64, 0, NULL);
  targetBuffer = (double *)outputArray->pData;

  // Copy the attributes and timestamp from the first array.
  outputArray->pAttributeList->copy(firstArray->pAttributeList);
  outputArray->timeStamp = firstArray->timeStamp;
  outputArray->epicsTS = firstArray->epicsTS;

  // Offset is (gateStart - pre-trigger counts).
  sourceOffset = triggerStartOffset_ - preTriggerCounts;
  // buffer offset is 0.
  targetOffset = 0;

  // Iterate over the arrays from oldest to newest.
  std::deque<NDArray *>::iterator iter;

  // While not at end
  for (iter = arrayBuffer_->begin(); iter != arrayBuffer_->end(); iter++) {
      sourceArray = *iter;
      sourceBuffer = (double *)sourceArray->pData;
      nChannels = sourceArray->dims[0].size;
      nSamples = sourceArray->dims[1].size;
  // For each array:
  //   copy from offset to min((array size - offset), (output size - buffer offset)) to target buffer.
      // If room in target buffer, copy NDArray from start offset to end. Otherwise, copy as much of NDArray as will fit.
      // The min here is intended to handle the edge cases at the start and end correctly.
      int counts = MIN(nSamples - sourceOffset, outputCounts - targetOffset);
      memcpy(targetBuffer + targetOffset * nChannels, sourceBuffer + sourceOffset * nChannels, counts * nChannels * sizeof(double));
  //   Increment buffer offset by # of written bytes.
      targetOffset += counts;
      // Compute number of counts remaining at end of buffer (to carry to next buffer).
      carryCounts = nSamples - sourceOffset - counts;
      sourceOffset = 0;
  }

  // Handle the carry data left at the end of the last array in the buffer.
  // This is important since a use-case for this plugin is to trigger immediately and rearm indefinitely, to effectively change the time base for
  // readout (e.g. concatenate arrays from a source which reads out at 1Hz and rebroadcast them as a single array at 0.1Hz). So it is important we do
  // not simply discard the carry data.

  if (sourceBuffer && carryCounts) {
      size_t carryDims[2] = { nChannels, carryCounts };
      carryArray = this->pNDArrayPool->alloc(2, carryDims, NDFloat64, 0, NULL);

      carryArray->pAttributeList->copy(sourceArray->pAttributeList);
      carryArray->timeStamp = sourceArray->timeStamp;
      carryArray->epicsTS = sourceArray->epicsTS;

      carryBuffer = (double *)carryArray->pData;
      // Copy the data from the last NDArray, starting from the last sample written to the output + 1.
      memcpy(carryBuffer, sourceBuffer+ (nSamples - carryCounts) * nChannels, carryCounts * nChannels * sizeof(double));
  }

  // Now tear down the original buffer
  while (arrayBuffer_->size()) {
      sourceArray = arrayBuffer_->front();
      sourceArray->release();
      arrayBuffer_->pop_front();
  }

  // If there is a carry array add it to the buffer
  if (carryArray) {
      arrayBuffer_->push_back(carryArray);
      setIntegerParam(NDPluginReframeBufferFrames, 1);
      setIntegerParam(NDPluginReframeBufferSamples, carryCounts);
  } else {
      setIntegerParam(NDPluginReframeBufferFrames, 0);
      setIntegerParam(NDPluginReframeBufferSamples, 0);
  }

  // Handle the unique ID
  outputArray->uniqueId = arrayCount;
  arrayCount++;
  setIntegerParam(NDPluginReframeTriggerTotal, arrayCount);

  return outputArray;
  // ###TODO: Do we need to worry about memory cap?
}

/**
  * bufferSizeCounts
  * Returns the number of samples stored in the buffer, found by walking the buffer and summing the array size for each NDArray.
  * \param[in] start an integer indicating the frame to start counting from. Set this to 0 for the total buffer size; setting
  * it to 1 counts all but the oldest NDArray, and so on.
  * \return The number of samples stored from the start of the requested frame to the end of the buffer.
  */
int NDPluginReframe::bufferSizeCounts(int start)
{
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
    // ###TODO: It may be best to implement this as a class variable which is increased/decreased
    // whenever we add/remove an array. This would both be more efficient than recomputing each time and would
    // mean we could expose it as a read-only parameter.
    // On the other hand, the buffer is unlikely to get huge so looking it up each time might not hurt too much.
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
    // Check the number of dimensions is correct
    if (pArray->ndims != 2)
        return 0;

    // Check the number of channels >= trigger channel
    dims = pArray->dims;
    if (triggerChannel < 0 || dims[0].size < (size_t)triggerChannel)
        return 0;

    if(pArray->dataType != NDFloat64)
        return 0;

    // Check the number of channels is consistent
    if (arrayBuffer_->size()) {
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
    /* Call the base class method */
    NDPluginDriver::processCallbacks(pArray);

    int mode;
    getIntegerParam(NDPluginReframeMode, &mode);

    if (mode != Idle) {
        // ###TODO: Handling of array cap - we need to leave 2 arrays available to construct the output array and carry array.
        NDArray *pArrayCpy = this->pNDArrayPool->copy(pArray, NULL, 1);

        // Check if array is valid and if so add it to the buffer.
        if (pArrayCpy && arrayIsValid(pArrayCpy)) {

            arrayBuffer_->push_back(pArrayCpy);

            // This loop is necessary because we might have multiple triggers, and thus multiple output arrays, for a single frame of input data.
            // retrigger is used to keep track of whether there are any bytes in the buffer which have not yet been checked for triggers.
            bool retrigger = true;
            while (retrigger) { // some bytes not checked for triggers
                retrigger = false;
                if (mode == Armed) {

                    // First test for trigger based on the data
                    int triggered = containsTriggerStart();

                    // Next check for soft trigger
                    if (!triggered) {
                        getIntegerParam(NDPluginReframeSoftTrigger, &triggered);
                        if (triggered) {
                            // Set the trigger offset to the start of the frame containing the soft trigger
                            triggerStartOffset_ = bufferSizeCounts(0) - arrayBuffer_->back()->dims[1].size;
                        }
                    }
                    // Prune buffer if no trigger occurred on this frame.
                    // Problem here is we may want to prune arrays if triggered, if the new array contains the
                    // whole pre-trigger.
                    // So we should prune regardless (should be fine not to prune in other states though since once
                    // the trigger start is received the pre-trigger is fixed).
                    // We will have the problem that the current test assumes the trigger frame
                    // ###TODO: These two branches could be folded into one.
                    if (!triggered) {
                        int preCounts;
                        getIntegerParam(NDPluginReframePreTriggerSamples, &preCounts);
                        // This is testing whether the total number of samples contained in the whole buffer *except* the first frame
                        // is larger than the pre trigger. If so, none of the first frame will contribute and it can be pruned.
                        while (arrayBuffer_->size() > 0 && bufferSizeCounts(1) > preCounts) {
                            NDArray *pOldArray_ = arrayBuffer_->front();
                            pOldArray_->release();
                            arrayBuffer_->pop_front();
                        }
                    } else {
                        // May need to drop the first array, if the whole pre-trigger is contained in the triggering array.
                        while (arrayBuffer_->size() > 0) {
                            int preCounts;
                            getIntegerParam(NDPluginReframePreTriggerSamples, &preCounts);

                            // Start of pre trigger is given by MAX(triggerStartOffset_ - preTrigger, 0)
                            int preTriggerStart = MAX(triggerStartOffset_ - preCounts, 0);
                            NDArray *firstArray = arrayBuffer_->front();
                            size_t firstArraySize = firstArray->dims[1].size;

                            // If start of pre trigger > size of first array
                            if (preTriggerStart > (int)firstArraySize) {
                                // Prune first array
                                arrayBuffer_->pop_front();
                                firstArray->release();
                                // Decrement anything defined relative to start of buffer (e.g. triggerStartOffset_) by size of first array.
                                // I think this is just the triggerStartOffset_.
                                triggerStartOffset_ -= firstArraySize;
                            } else {
                                // If we do need the first array, we're done.
                                break;
                            }
                        }
                        setStringParam(NDPluginReframeStatus, "Gating");
                        setIntegerParam(NDPluginReframeMode, Gating);
                        getIntegerParam(NDPluginReframeMode, &mode);
                    }
                }

                if (mode == Gating) {
                    // Check data for trigger end
                    int triggerEnded = containsTriggerEnd();

                    // Check for soft trigger
                    if (!triggerEnded) {
                        getIntegerParam(NDPluginReframeSoftTrigger, &triggerEnded);
                        if (triggerEnded)
                            triggerEndOffset_ = bufferSizeCounts(0) - arrayBuffer_->back()->dims[1].size;
                    }

                    setIntegerParam(NDPluginReframeTriggerEnded, triggerEnded);
                    if (triggerEnded) {
                        setIntegerParam(NDPluginReframeMode, Acquiring);
                        getIntegerParam(NDPluginReframeMode, &mode);
                        setStringParam(NDPluginReframeStatus, "Acquiring post-trigger");
                    }
                }

                if (mode == Acquiring) {
                    // Current post trigger size is everything after the end of the trigger.
                    int currentPostSize = bufferSizeCounts(0) - triggerEndOffset_;
                    int postSize;
                    getIntegerParam(NDPluginReframePostTriggerSamples, &postSize);

                    // If it has reached the target size, construct the output and do callbacks on it.
                    if (currentPostSize >= postSize) {
                        NDArray *outputArray = constructOutput();
                        if (outputArray) {
                            this->unlock();
                            doCallbacksGenericPointer(outputArray, NDArrayData, 0);
                            this->lock();
                            outputArray->release();
                        }

                        int currentTriggerCount, maxTriggerCount;
                        getIntegerParam(NDPluginReframeTriggerCount, &currentTriggerCount);
                        getIntegerParam(NDPluginReframeTriggerMax, &maxTriggerCount);
                        currentTriggerCount++;
                        setIntegerParam(NDPluginReframeTriggerCount, currentTriggerCount);

                        // If we've reached the target number of triggers set to Idle mode, otherwise re-arm.
                        // If max is set to 0, we should re-arm indefinitely.
                        if (currentTriggerCount >= maxTriggerCount && maxTriggerCount > 0) {
                            if (arrayBuffer_->size()) {
                                NDArray *carryArray = arrayBuffer_->front();
                                carryArray->release();
                                arrayBuffer_->pop_front();
                            }
                            setIntegerParam(NDPluginReframeMode, Idle);
                            getIntegerParam(NDPluginReframeMode, &mode);
                            setStringParam(NDPluginReframeStatus, "Done");
                        } else {
                            triggerStartOffset_ = 0;
                            triggerEndOffset_ = 0;
                            setIntegerParam(NDPluginReframeTriggerEnded, 0);
                            // Still some data that has not been searched for triggers, so we should re-run the loop.
                            if (arrayBuffer_->size())
                                retrigger = true;
                            setIntegerParam(NDPluginReframeMode, Armed);
                            getIntegerParam(NDPluginReframeMode, &mode);
                            setStringParam(NDPluginReframeStatus, "Waiting for trigger");
                        }
                    }
                } // mode acquiring
            } // while still looking for triggers
        } else {
            // either we couldn't copy the array or it failed to validate, so output any triggered data already buffered and reset.
            NDArray *outputArray = constructOutput();
            if (outputArray) {
                this->unlock();
                doCallbacksGenericPointer(outputArray, NDArrayData, 0);
                this->lock();
                outputArray->release();
            }


            triggerStartOffset_ = 0;
            triggerEndOffset_ = 0;
            setIntegerParam(NDPluginReframeTriggerEnded, 0);
            setIntegerParam(NDPluginReframeMode, Idle);
            getIntegerParam(NDPluginReframeMode, &mode);
        }
        setIntegerParam(NDPluginReframeBufferFrames, arrayBuffer_->size());
        setIntegerParam(NDPluginReframeBufferSamples, bufferSizeCounts(0));

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
                while (arrayBuffer_->size()) {
                    NDArray *oldArray = arrayBuffer_->front();
                    arrayBuffer_->pop_front();
                    oldArray->release();
                }
            } else {
                arrayBuffer_ = new std::deque<NDArray *>;
            }
            // Set the status to buffer filling and clear any residual state current/last trigger
            setIntegerParam(NDPluginReframeSoftTrigger, 0);
            setIntegerParam(NDPluginReframeTriggered, 0);
            setIntegerParam(NDPluginReframeTriggerCount, 0);
            setIntegerParam(NDPluginReframeTriggerEnded, 0);
            setIntegerParam(NDPluginReframeBufferFrames, 0);
            setIntegerParam(NDPluginReframeBufferSamples, 0);
            setStringParam(NDPluginReframeStatus, "Waiting for trigger");
            setIntegerParam(NDPluginReframeMode, Armed);
        } else {
            setStringParam(NDPluginReframeStatus, "Idle");
            setIntegerParam(NDPluginReframeMode, Idle);
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
    createParam(NDPluginReframeControlString,               asynParamInt32,   &NDPluginReframeControl);
    createParam(NDPluginReframeStatusString,                asynParamOctet,   &NDPluginReframeStatus);
    createParam(NDPluginReframeSoftTriggerString,           asynParamInt32,   &NDPluginReframeSoftTrigger);
    createParam(NDPluginReframeTriggeredString,             asynParamInt32,   &NDPluginReframeTriggered);
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
    createParam(NDPluginReframeTriggerTotalString,          asynParamInt32,   &NDPluginReframeTriggerTotal);
    createParam(NDPluginReframeBufferFramesString,          asynParamInt32,   &NDPluginReframeBufferFrames);
    createParam(NDPluginReframeBufferSamplesString,         asynParamInt32,   &NDPluginReframeBufferSamples);
    createParam(NDPluginReframeModeString,                  asynParamInt32,   &NDPluginReframeMode);

    // Set the plugin type string
    setStringParam(NDPluginDriverPluginType, "NDPluginReframe");

    // Set the status to idle
    setStringParam(NDPluginReframeStatus, "Idle");
    setIntegerParam(NDPluginReframeMode, Idle);

    // Set to concatenate on dimension 1 (though no enforcement to use 2d arrays yet).
    setIntegerParam(NDPluginReframeTriggerDimension, 1);
    setIntegerParam(NDPluginReframeTriggerChannel, 0);

    setIntegerParam(NDPluginReframePreTriggerSamples, 100);
    setIntegerParam(NDPluginReframePostTriggerSamples, 100);

    setIntegerParam(NDPluginReframeTriggerStartCondition, 1);
    setIntegerParam(NDPluginReframeTriggerEndCondition, 0);

    setDoubleParam(NDPluginReframeTriggerStartThreshold, 1.0);
    setDoubleParam(NDPluginReframeTriggerEndThreshold, 0.0);

    setIntegerParam(NDPluginReframeTriggerMax, 0);

    setIntegerParam(NDPluginReframeTriggerEnded, 0);
    setIntegerParam(NDPluginReframeTriggerCount, 0);
    setIntegerParam(NDPluginReframeTriggerTotal, 0);

    setIntegerParam(NDPluginReframeBufferFrames, 0);
    setIntegerParam(NDPluginReframeBufferSamples, 0);

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
