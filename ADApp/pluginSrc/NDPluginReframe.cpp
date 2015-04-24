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
#include <typeinfo>
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
template <typename epicsType>
bool NDPluginReframe::containsTriggerStart()
{
  int startCondition, nChannels, nSamples, triggerChannel, arrayIndex;
  bool triggerFound = false;
  epicsType *buffer;
  double threshold;
  NDArray *array;
  NDDimension_t *dims;

  const char *functionName = "containsTriggerStart";
  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Searching for trigger on starting from %d\n", driverName, functionName, triggerOnIndex_);

  getIntegerParam(NDPluginReframeTriggerChannel, &triggerChannel);
  getIntegerParam(NDPluginReframeTriggerStartCondition, &startCondition);
  getDoubleParam(NDPluginReframeTriggerStartThreshold, &threshold);

  //Get the oldest unchecked array from the buffer:
  std::deque<NDArray *>::iterator iter = arrayBuffer_->begin();
  int arrayOffset = 0;
  // Advance the iterator to skip arrays that have already been checked.
  while (iter != arrayBuffer_->end() && triggerOnIndex_ >= arrayOffset + (int)(*iter)->dims[1].size) {
      arrayOffset += (*iter)->dims[1].size;
      iter++;
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Advance array offset to %d (%d)\n", driverName, functionName, arrayOffset, triggerOnIndex_);
  }

  // Get the most recent array from the buffer, extract the dimensions and get a pointer to the data
  while (iter != arrayBuffer_->end()) {
      array = *iter;
      dims = array->dims;
      // ###TODO Check whether we need to look at the other elements of dims (offset, binning, reverse).
      nChannels = dims[0].size;
      nSamples  = dims[1].size;
      buffer = (epicsType *)array->pData;

      // Find offset into buffer of the start of this array
      if (triggerOnIndex_ > arrayOffset) {
          arrayIndex = triggerOnIndex_ - arrayOffset;
      } else {
          arrayIndex = 0;
      }


      for (int sample = arrayIndex; sample < nSamples; sample++) {

          epicsType triggerVal = buffer[sample * nChannels + triggerChannel];

          if (startCondition) { // Trigger on high level
              if (triggerVal > static_cast<epicsType>(threshold)) {
                  // Off index should point to this sample, on index should point to the next.
                  // This is so that the next trigger search start will start from the next sample, but we won't miss cases
                  // where the on and off conditions are the same, so we should trigger on and off on the same sample.
                  triggerOnIndex_ = arrayOffset + sample + 1;
                  triggerOffIndex_ = arrayOffset + sample;
                  triggerFound = true;
                  break;
              }
          } else { // Trigger on low level
              if (triggerVal < static_cast<epicsType>(threshold)) {
                  triggerOnIndex_ = arrayOffset + sample + 1;
                  triggerOffIndex_ = arrayOffset + sample;
                  triggerFound = true;
                  break;
              }
          }
      }

      if (triggerFound)
          break;

      // Mark this entire array as having been searched, and advance the iterator
      arrayOffset += (*iter)->dims[1].size;
      triggerOnIndex_ = arrayOffset;
      triggerOffIndex_ = arrayOffset;
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Trigger not found, advanced indices to %d\n", driverName, functionName, triggerOnIndex_);
      iter++;
  }

  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Trigger state: %d\n", driverName, functionName, triggerFound);
  return triggerFound;
}

/**
  * containsTriggerEnd
  * Searches the most recent frame in the buffer, starting from the triggerStartOffset_, for the trigger end condition.
  * Side effects: Sets triggerEndOffset_ to the offset of the end condition if found.
  * \return int, set to 1 if trigger end found, 0 otherwise.
  */
template <typename epicsType>
bool NDPluginReframe::containsTriggerEnd()
{

    int endCondition, arrayIndex = 0, nChannels, nSamples, triggerChannel, triggerFound = 0;
    epicsType *buffer;
    double threshold;
    NDArray *array;
    NDDimension_t *dims;

    const char *functionName = "containsTriggerEnd";
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Searching for trigger off starting from %d\n", driverName, functionName, triggerOffIndex_);

    getIntegerParam(NDPluginReframeTriggerEndCondition, &endCondition);
    getIntegerParam(NDPluginReframeTriggerChannel, &triggerChannel);
    getDoubleParam(NDPluginReframeTriggerEndThreshold, &threshold);

    //Get the oldest unchecked array from the buffer:
    std::deque<NDArray *>::iterator iter = arrayBuffer_->begin();
    int arrayOffset = 0;
    // Advance the iterator to skip arrays that have already been checked.
    while (iter != arrayBuffer_->end() && triggerOffIndex_ >= arrayOffset+(int)(*iter)->dims[1].size) {
        arrayOffset += (*iter)->dims[1].size;
        iter++;
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Advance array offset to %d (%d)\n", driverName, functionName, arrayOffset, triggerOffIndex_);
    }

    // Get the most recent array from the buffer, extract the dimensions and get a pointer to the data
    while (iter != arrayBuffer_->end()) {
        // Get latest array from buffer and extract dimensions.
        array = arrayBuffer_->back();
        dims = array->dims;
        // ###TODO Check whether we need to look at the other elements of dims (offset, binning, reverse).
        nChannels = dims[0].size;
        nSamples  = dims[1].size;

        // If trigger start is in this frame then only search the part of the frame after the trigger start.
        if (triggerOnIndex_ > arrayOffset) {
            arrayIndex = triggerOnIndex_ - arrayOffset - 1;
        } else {
            arrayIndex = 0;
        }

        buffer = (epicsType *)array->pData;
        for (int sample = arrayIndex; sample < nSamples; sample++) {

            epicsType triggerVal = buffer[sample * nChannels + triggerChannel];

            if (endCondition) { // Trigger on high level
                if (triggerVal > static_cast<epicsType>(threshold)) {
                    // We don't allow overlapping gates, so increment triggerOnIndex_ as well.
                    triggerOffIndex_ = arrayOffset + sample + 1;
                    triggerOnIndex_ = arrayOffset + sample + 1;
                    triggerFound = true;
                    break;
                }
            } else { // Trigger on low level
                if (triggerVal < static_cast<epicsType>(threshold)) {
                    triggerOffIndex_ = arrayOffset + sample + 1;
                    triggerOnIndex_ = arrayOffset + sample + 1;
                    triggerFound = true;
                    break;
                }
            }
        }
        if (triggerFound)
            break;

        arrayOffset += (*iter)->dims[1].size;
        triggerOnIndex_ = arrayOffset;
        triggerOffIndex_ = arrayOffset;
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Trigger not found, advanced indices to %d\n", driverName, functionName, triggerOnIndex_);
        iter++;
    }

    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Trigger off found: %d\n", driverName, functionName, triggerFound);
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
template <typename epicsType>
NDArray *NDPluginReframe::constructOutput(Trigger *trig)
{
  int preTriggerCounts, triggerCounts, postTriggerCounts, outputCounts, nChannels, sourceOffset, targetOffset, nSamples=0, carryCounts=0;
  int preTrigger, postTrigger, arrayCount, overlap, ignoredTrigs;
  epicsType *sourceBuffer = NULL, *targetBuffer = NULL, *carryBuffer = NULL;
  NDArray *sourceArray = NULL, *outputArray = NULL, *carryArray = NULL;

  const char *functionName = "constructOutput";
  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Constructing output array for trigger at %d, %d\n", driverName, functionName, trig->startOffset, trig->stopOffset);

  getIntegerParam(NDPluginReframePreTriggerSamples, &preTrigger);
  getIntegerParam(NDPluginReframePostTriggerSamples, &postTrigger);
  getIntegerParam(NDPluginReframeTriggerTotal, &arrayCount);
  getIntegerParam(NDPluginReframeOverlappingTriggers, &overlap);
  getIntegerParam(NDPluginReframeIgnoredCount, &ignoredTrigs);

  // If no trigger has been detected, don't output anything (will arise if we got a bad input array before seeing any triggers).
  if (trig->startOffset < 0 || arrayBuffer_->empty()) {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Short circuit return (%d, %d)\n", driverName, functionName, trig->startOffset, arrayBuffer_->empty());
      return NULL;
  }

  // Note all three parts of the window must be calculated since none can be assumed to be a fixed size. In particular a bad frame may arrive at any point
  // in the window, causing a buffer flush.
  //  - pre-trigger: Will be truncated if trigger arrived before buffer full.
  //  - gate: Will inherently vary in size. If bad frame arrived after gate start, gate start will be set but not gate end.
  //  - post-trigger: Will be truncated if a bad frame arrives after gate end but before post trigger full.

  // Find real pre-trigger counts (this is preTrigger if we've acquired enough data to fill it, otherwise as much as we have so far).
  preTriggerCounts = MIN(preTrigger, trig->startOffset);

  // Find gate size
  if (trig->stopOffset < 0) {
      // If we didn't see the trigger end yet, this is everything after the trigger start
      triggerCounts = bufferSizeCounts(0) - trig->startOffset;
  } else {
      // Otherwise it's the number of counts between trigger start and trigger end
      triggerCounts = trig->stopOffset - trig->startOffset;
  }

  // Find real post-trigger counts.
  if (trig->stopOffset < 0) {
      // If we didn't see the trigger end this is zero
      postTriggerCounts = 0;
  } else {
      // Otherwise it's either the full post trigger size, or else all the counts left after the trigger end if there aren't enough to fill the
      // post trigger
      postTriggerCounts = MIN(bufferSizeCounts(0) - trig->stopOffset, postTrigger);
  }

  // Output size is (pre-trigger + gate size + post-trigger)
  // ###TODO: This fails if pre and post trigger are both size 0.
  // This seems to occur if we set post to 0 and trigger immediately. Also would occur in edge case of readout due to bad frame,
  // where triggerendOffset_ happens to fall on the end of the buffer (and pre trigger and gate size both happen to be zero).
  // How to handle this case?
  outputCounts = preTriggerCounts + triggerCounts + postTriggerCounts;

  std::deque<NDArray *>::iterator iter=arrayBuffer_->begin();
  int skippedCounts = 0;
  // Advance the iterator to the first array that actually contributes to the output.
  while (trig->startOffset - preTrigger > (int)(*iter)->dims[1].size + skippedCounts) {
      skippedCounts += (*iter)->dims[1].size + skippedCounts;
      iter++;
  }
  // Use the first contributing array to set the # channels, data type and metadata for the output.
  NDArray *firstArray = *iter;
  nChannels = firstArray->dims[0].size;

  size_t dims[2] = { nChannels, outputCounts };
  if(typeid(epicsType) == typeid(epicsInt8))
      outputArray = this->pNDArrayPool->alloc(2, dims, NDInt8, 0, NULL);
  else if (typeid(epicsType) == typeid(epicsUInt8))
      outputArray = this->pNDArrayPool->alloc(2, dims, NDUInt8, 0, NULL);
  else if(typeid(epicsType) == typeid(epicsInt16))
      outputArray = this->pNDArrayPool->alloc(2, dims, NDInt16, 0, NULL);
  else if(typeid(epicsType) == typeid(epicsUInt16))
      outputArray = this->pNDArrayPool->alloc(2, dims, NDUInt16, 0, NULL);
  else if(typeid(epicsType) == typeid(epicsInt32))
      outputArray = this->pNDArrayPool->alloc(2, dims, NDInt32, 0, NULL);
  else if(typeid(epicsType) == typeid(epicsUInt32))
      outputArray = this->pNDArrayPool->alloc(2, dims, NDUInt32, 0, NULL);
  else if(typeid(epicsType) == typeid(epicsFloat32))
      outputArray = this->pNDArrayPool->alloc(2, dims, NDFloat32, 0, NULL);
  else if(typeid(epicsType) == typeid(epicsFloat64))
      outputArray = this->pNDArrayPool->alloc(2, dims, NDFloat64, 0, NULL);
  else {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Short-circuit return (unrecognised data type)\n", driverName, functionName);
      return NULL;
  }

  targetBuffer = (epicsType *)outputArray->pData;

  // Copy the attributes and timestamp from the first array.
  outputArray->pAttributeList->copy(firstArray->pAttributeList);
  outputArray->timeStamp = firstArray->timeStamp;
  outputArray->epicsTS = firstArray->epicsTS;

  // Offset in source array is (gateStart - offset of source array - pre-trigger counts).
  sourceOffset = trig->startOffset - skippedCounts - preTriggerCounts;
  targetOffset = 0;

  // Iterate over the arrays to construct the output buffer
  while (iter != arrayBuffer_->end()) {
      sourceArray = *iter;
      sourceBuffer = (epicsType *)sourceArray->pData;
      nChannels = sourceArray->dims[0].size;
      nSamples = sourceArray->dims[1].size;
      // If room in target buffer, copy NDArray from start offset to end. Otherwise, copy as much of NDArray as will fit.
      // The min here is intended to handle the edge cases at the start and end correctly.
      int counts = MIN(nSamples - sourceOffset, outputCounts - targetOffset);
      memcpy(targetBuffer + targetOffset * nChannels, sourceBuffer + sourceOffset * nChannels, counts * nChannels * sizeof(epicsType));
      targetOffset += counts;
      // Compute number of counts remaining at end of buffer (to carry to next buffer).
      carryCounts = nSamples - sourceOffset - counts;
      sourceOffset = 0;
      iter++;
  }

  // If we don't permit overlapping output, remove any data we are about to output from the stored buffer.
  if (!overlap) {
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Truncating buffer after constructing output (%d samples carried, indexes %d, %d)\n", driverName, functionName, carryCounts, triggerOnIndex_, triggerOffIndex_);

      // Handle the carry data  at the end of the last array in the buffer.
      // This is important since a use-case for this plugin is to trigger immediately and rearm indefinitely, to effectively change the time base for
      // readout (e.g. concatenate arrays from a source which reads out at 1Hz and rebroadcast them as a single array at 0.1Hz). So it is important we do
      // not simply discard the carry data.
      if (/*sourceBuffer && */carryCounts) {
          size_t carryDims[2] = { nChannels, carryCounts };

          if(typeid(epicsType) == typeid(epicsInt8))
              carryArray = this->pNDArrayPool->alloc(2, carryDims, NDInt8, 0, NULL);
          else if(typeid(epicsType) == typeid(epicsUInt8))
              carryArray = this->pNDArrayPool->alloc(2, carryDims, NDUInt8, 0, NULL);
          else if(typeid(epicsType) == typeid(epicsInt16))
              carryArray = this->pNDArrayPool->alloc(2, carryDims, NDInt16, 0, NULL);
          else if(typeid(epicsType) == typeid(epicsUInt16))
              carryArray = this->pNDArrayPool->alloc(2, carryDims, NDUInt16, 0, NULL);
          else if(typeid(epicsType) == typeid(epicsInt32))
              carryArray = this->pNDArrayPool->alloc(2, carryDims, NDInt32, 0, NULL);
          else if(typeid(epicsType) == typeid(epicsUInt32))
              carryArray = this->pNDArrayPool->alloc(2, carryDims, NDUInt32, 0, NULL);
          else if(typeid(epicsType) == typeid(epicsFloat32))
              carryArray = this->pNDArrayPool->alloc(2, carryDims, NDFloat32, 0, NULL);
          else if(typeid(epicsType) == typeid(epicsFloat64))
              carryArray = this->pNDArrayPool->alloc(2, carryDims, NDFloat64, 0, NULL);

          carryArray->pAttributeList->copy(sourceArray->pAttributeList);
          carryArray->timeStamp = sourceArray->timeStamp;
          carryArray->epicsTS = sourceArray->epicsTS;

          carryBuffer = (epicsType *)carryArray->pData;
          memcpy(carryBuffer, sourceBuffer+ (nSamples - carryCounts) * nChannels, carryCounts * nChannels * sizeof(epicsType));

          // This is necessary since we destroy all the original arrays before appending the carry array, so we need to add the carry offset here so we don't
          // hit problems with an index being shifted below zero and wrongly pinned to -1.
          // ###TODO: Really should replace this whole mechanism by just storing an offset for buffer start, both to simplify this logic and to avoid the unnecessary memcpy.
          triggerOnIndex_ += carryCounts;
          triggerOffIndex_ += carryCounts;

          std::deque<Trigger *>::iterator iter;
          for (iter = triggerQueue_->begin(); iter != triggerQueue_->end(); iter++) {
              Trigger *shiftTrig = *iter;
              shiftTrig->startOffset += carryCounts;
              shiftTrig->stopOffset += carryCounts;
          }
      }

      // Now tear down the original buffer
      while (arrayBuffer_->size()) {
          sourceArray = arrayBuffer_->front();
          size_t size = sourceArray->dims[1].size;
          sourceArray->release();
          arrayBuffer_->pop_front();

          // Update the indices and offsets for each trigger to reflect the moved start of the buffer
          triggerOnIndex_ = MAX(triggerOnIndex_ - (int)size, -1);
          triggerOffIndex_ = MAX(triggerOffIndex_ - (int)size, -1);

          std::deque<Trigger *>::iterator iter;
          for (iter = triggerQueue_->begin(); iter != triggerQueue_->end(); iter++) {
              Trigger *shiftTrig = *iter;
              shiftTrig->startOffset = MAX(shiftTrig->startOffset - (int)size, -1);
              shiftTrig->stopOffset = MAX(shiftTrig->stopOffset - (int)size, -1);
          }
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Shifting buffer start by %lu\n", driverName, functionName, size);

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

      // Triggers will have been shifted if truncation occurred, so check if any are now before the start of the buffer:
      while (!triggerQueue_->empty() && triggerQueue_->front()->startOffset < 0) {
          Trigger *strandedTrig = triggerQueue_->front();
          triggerQueue_->pop_front();
          ignoredTrigs++;
          // Handling "stranded" triggers is tricky. If both start and end are now behind the start of the array, we can discard the trigger.
          // However if the start is now stranded but the end still falls in relevant data, or if the trigger is still waiting for the end condition,
          // we need to retest to see if the data from the start of the array would still pass the trigger condition. If so, we construct a new trigger
          // and prepend it to the queue.
          // Note we are resetting the indexes after each trigger, so if more than one trigger is straddling the buffer start we'll wind up searching the
          // same region twice. We don't currently permit overlapping gates however, so this shouldn't ever occur.
          if (strandedTrig->stopOffset >= 0 || !strandedTrig->done) {
             // Cache the trigger index
             int trigOnCache = triggerOnIndex_;
             int trigOffCache = triggerOffIndex_;
             triggerOnIndex_=0;
             triggerOffIndex_=0;
             // Do the test (is there an on condition and does it belong to this trigger?)
             if (containsTriggerStart<epicsType>() && (triggerOnIndex_ <= strandedTrig->stopOffset + 1 || strandedTrig->done)) {
                 Trigger *newTrig = new Trigger;
                 newTrig->startOffset = triggerOnIndex_-1;
                 newTrig->stopOffset = strandedTrig->stopOffset;
                 newTrig->done = strandedTrig->done;
                 triggerQueue_->push_front(newTrig);
                 ignoredTrigs--;
                 asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Shifting stranded trigger start to %d\n", driverName, functionName, newTrig->startOffset);
             } else {
                 asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Discarding stranded trigger (end at %d)\n", driverName, functionName, strandedTrig->stopOffset);
             }
             // Restore the trigger index
             triggerOnIndex_ = trigOnCache;
             triggerOffIndex_ = trigOffCache;
          } else {
              asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Discarding stranded trigger\n", driverName, functionName);
          }

          delete strandedTrig;
      }


  }

  // Handle the unique ID
  outputArray->uniqueId = arrayCount;
  arrayCount++;
  setIntegerParam(NDPluginReframeTriggerTotal, arrayCount);
  setIntegerParam(NDPluginReframeIgnoredCount, ignoredTrigs);

  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Returning output array of size %lu\n", driverName, functionName, outputArray->dims[1].size);
  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: %d samples still stored in buffer; start index %d, stop index %d\n", driverName, functionName, bufferSizeCounts(0), triggerOnIndex_, triggerOffIndex_);

  return outputArray;
  // ###TODO: Do we need to worry about memory cap? In particular what if our intermediate arrays would be large enough to exceed the cap?
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
    int count = 0, index = 0;
    std::deque<NDArray *>::iterator iter;

    // Skip arrays until we reach start, then sum the array sizes from then until the end of the buffer
    for (iter = arrayBuffer_->begin(); iter != arrayBuffer_->end(); iter++, index++) {
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

    // Check the number of channels is consistent
    if (arrayBuffer_->size()) {
        expectedDims = arrayBuffer_->front()->dims;
        if (expectedDims[0].size != dims[0].size)
            return 0;
    }

    // Check the data type hasn't changed
    if (arrayBuffer_->size()) {
        if (arrayBuffer_->front()->dataType != pArray->dataType)
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
    const char *functionName = "processCallbacks";
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Processing array of size %lu\n", driverName, functionName, pArray->dims[1].size);

    /* Call the base class method */
    NDPluginDriver::processCallbacks(pArray);

    int mode;
    getIntegerParam(NDPluginReframeMode, &mode);

    if (mode != Idle) {
        // ###TODO: Handling of array cap - we need to leave 2 arrays available to construct the output array and carry array.
        NDArray *pArrayCpy = this->pNDArrayPool->copy(pArray, NULL, 1);
        handleNewArray(pArrayCpy);
    }

    callParamCallbacks();
}

void NDPluginReframe::handleNewArray(NDArray *pArrayCpy)
{
    int outputCount;
    const char *functionName = "handleNewArray";

    getIntegerParam(NDPluginReframeOutputCount, &outputCount);

    if (pArrayCpy && arrayIsValid(pArrayCpy)) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: Array is valid\n", driverName, functionName);
        switch(pArrayCpy->dataType) {
            case NDInt8:
                handleNewArrayT<epicsInt8>(pArrayCpy);
                break;
            case NDUInt8:
                handleNewArrayT<epicsUInt8>(pArrayCpy);
                break;
            case NDInt16:
                handleNewArrayT<epicsInt16>(pArrayCpy);
                break;
            case NDUInt16:
                handleNewArrayT<epicsUInt16>(pArrayCpy);
                break;
            case NDInt32:
                handleNewArrayT<epicsInt32>(pArrayCpy);
                break;
            case NDUInt32:
                handleNewArrayT<epicsUInt32>(pArrayCpy);
                break;
            case NDFloat32:
                handleNewArrayT<epicsFloat32>(pArrayCpy);
                break;
            case NDFloat64:
                handleNewArrayT<epicsFloat64>(pArrayCpy);
                break;
            default:
                asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "%s, %s: Data type %i not supported\n", driverName, functionName, pArrayCpy->dataType);
                break;
        }
    } else {
        // either we couldn't copy the array or else it failed to validate, so output any triggered data already buffered and reset.
        if(arrayBuffer_->size()) {
            while(!triggerQueue_->empty()) {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Flushing pending triggers due to inconsistent input\n", driverName, functionName);
                NDArray *outputArray = NULL;
                Trigger *trig = triggerQueue_->front();
                switch(arrayBuffer_->front()->dataType) {
                    case NDInt8:
                        outputArray = constructOutput<epicsInt8>(trig);
                        break;
                    case NDUInt8:
                        outputArray = constructOutput<epicsUInt8>(trig);
                        break;
                    case NDInt16:
                        outputArray = constructOutput<epicsInt16>(trig);
                        break;
                    case NDUInt16:
                        outputArray = constructOutput<epicsUInt16>(trig);
                        break;
                    case NDInt32:
                        outputArray = constructOutput<epicsInt32>(trig);
                        break;
                    case NDUInt32:
                        outputArray = constructOutput<epicsUInt32>(trig);
                        break;
                    case NDFloat32:
                        outputArray = constructOutput<epicsFloat32>(trig);
                        break;
                    case NDFloat64:
                        outputArray = constructOutput<epicsFloat64>(trig);
                        break;
                    default:
                        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "%s, %s: Data type %d not supported\n", driverName, functionName, arrayBuffer_->front()->dataType);
                        break;

                }

                if (outputArray) {
                    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Outputting partial array of size %lu\n", driverName, functionName, outputArray->dims[1].size);
                    this->unlock();
                    doCallbacksGenericPointer(outputArray, NDArrayData, 0);
                    this->lock();
                    outputArray->release();
                    outputCount++;
                }
            }
        }

        triggerOnIndex_ = 0;
        triggerOffIndex_ = 0;
        setIntegerParam(NDPluginReframeTriggerEnded, 0);
        setIntegerParam(NDPluginReframeMode, Idle);
        setIntegerParam(NDPluginReframeOutputCount, outputCount);
    }
}

template <typename epicsType>
void NDPluginReframe::handleNewArrayT(NDArray *pArrayCpy)
{
    int triggersIgnored, maxTrigs, postTrigger, preTrigger, rearmMode, trigCount, maxTrigCount, softTrig;
    const char *functionName = "handleNewArrayT";
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Buffering new array\n", driverName, functionName);

    getIntegerParam(NDPluginReframeMaxTriggers, &maxTrigs);
    getIntegerParam(NDPluginReframePostTriggerSamples, &postTrigger);
    getIntegerParam(NDPluginReframePreTriggerSamples, &preTrigger);
    getIntegerParam(NDPluginReframeRearmMode, &rearmMode);
    getIntegerParam(NDPluginReframeTriggerCount, &trigCount);
    getIntegerParam(NDPluginReframeTriggerMax, &maxTrigCount);
    getIntegerParam(NDPluginReframeSoftTrigger, &softTrig);

    arrayBuffer_->push_back(pArrayCpy);

    // Search the new array for triggers
    while (triggerOnIndex_ < bufferSizeCounts(0) || triggerOffIndex_ < bufferSizeCounts(0)) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Searching for trigger\n", driverName, functionName);

        if (triggerQueue_->empty() || triggerQueue_->back()->stopOffset >= 0) {
            // Check for real triggers
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Searching for new trigger start\n", driverName, functionName);
            if (softTrig) {
                // Handle soft trigger; this overrides any hardware triggers.
                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Soft trigger is on\n", driverName, functionName);
                // Advance the indexes so we skip the hardware trigger search
                triggerOnIndex_ = bufferSizeCounts(0);
                triggerOffIndex_ = bufferSizeCounts(0);

                // Should be OK not to move triggerOnIndex_ in this case, so further real trigs in this array will be detected.
                if (!maxTrigs || (maxTrigs > 0 && triggerQueue_->size() < (size_t)maxTrigs)) {
                    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Adding trigger to queue\n", driverName, functionName);

                    Trigger *trig = new Trigger;
                    trig->startOffset = bufferSizeCounts(0) - arrayBuffer_->back()->dims[1].size;
                    trig->stopOffset = bufferSizeCounts(0) - arrayBuffer_->back()->dims[1].size;
                    trig->done = true;
                    triggerQueue_->push_back(trig);
                } else {
                    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Ignoring trigger\n", driverName, functionName);
                    getIntegerParam(NDPluginReframeIgnoredCount, &triggersIgnored);
                    triggersIgnored++;
                    setIntegerParam(NDPluginReframeIgnoredCount, triggersIgnored);
                }
            } else if (containsTriggerStart<epicsType>()) {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Trigger start found at %d\n", driverName, functionName, triggerOnIndex_-1);
                if (!maxTrigs || (maxTrigs > 0 && triggerQueue_->size() < (size_t)maxTrigs)) {
                    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Adding trigger to queue\n", driverName, functionName);
                    Trigger *trig = new Trigger;
                    trig->startOffset = triggerOnIndex_-1;
                    trig->stopOffset = -1;
                    triggerQueue_->push_back(trig);
                } else {
                    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Ignoring trigger\n", driverName, functionName);
                    getIntegerParam(NDPluginReframeIgnoredCount, &triggersIgnored);
                    triggersIgnored++;
                    setIntegerParam(NDPluginReframeIgnoredCount, triggersIgnored);
                }
            }
        } else {
            // Search for trigger end
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Searching for trigger end\n", driverName, functionName);
            if (containsTriggerEnd<epicsType>()) {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Trigger end found at %d\n", driverName, functionName, triggerOffIndex_-1);
                triggerQueue_->back()->stopOffset = triggerOffIndex_-1;
                triggerQueue_->back()->done = true;
            }
        }
    }

    // Output any triggers that are now complete
    while (!triggerQueue_->empty() && triggerQueue_->front()->stopOffset >= 0 && (bufferSizeCounts(0) - triggerQueue_->front()->stopOffset >= postTrigger)) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Constructing output for trigger %d, %d\n", driverName, functionName, triggerQueue_->front()->startOffset, triggerQueue_->front()->stopOffset);
        trigCount++;
        Trigger *trig = triggerQueue_->front();
        triggerQueue_->pop_front();
        NDArray *outputArray = constructOutput<epicsType>(trig);
        delete trig;

        if (outputArray) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Outputting trigger of size %lu (%dth trigger)\n", driverName, functionName, outputArray->dims[1].size, trigCount);
            this->unlock();
            doCallbacksGenericPointer(outputArray, NDArrayData, 0);
            this->lock();
            outputArray->release();
        }

        // If we've now reached our target # of triggers, clear the trigger queue and disarm.
        if (rearmMode == Single || (rearmMode == Multiple && trigCount >= maxTrigCount)) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Target trigger # reached, clearing trigger queue (%lu pending)\n", driverName, functionName, triggerQueue_->size());
            while(!triggerQueue_->empty()) {
                Trigger *trig = triggerQueue_->front();
                triggerQueue_->pop_front();
                delete trig;
            }

            // ###TODO: Control and Mode are probably redundant now we only have 2 states, should eliminate one
            setIntegerParam(NDPluginReframeControl, 0);
            setIntegerParam(NDPluginReframeMode, Idle);
        }
    }

    // Discard any arrays that cannot contribute to the pre-buffer (either for the current oldest trigger or a future trigger if none are pending).
    while (!arrayBuffer_->empty()) {
        int unusedCounts = 0;
        if (triggerQueue_->empty())
            unusedCounts = bufferSizeCounts(0) - preTrigger;
        else
            unusedCounts = triggerQueue_->front()->startOffset - preTrigger;

        if (unusedCounts < (int)arrayBuffer_->front()->dims[1].size)
                break;

        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Pruning array buffer (size %lu)\n", driverName, functionName, arrayBuffer_->size());

        NDArray *oldArray = arrayBuffer_->front();
        size_t size = oldArray->dims[1].size;

        arrayBuffer_->pop_front();
        oldArray->release();
        triggerOnIndex_ = MAX(triggerOnIndex_ - (int)size, -1);
        triggerOffIndex_ = MAX(triggerOffIndex_ - (int)size, -1);

        // Update the trigger offsets to reflect the moved buffer start
        std::deque<Trigger *>::iterator iter;
        for (iter = triggerQueue_->begin(); iter != triggerQueue_->end(); iter++) {
            Trigger *trig = *iter;
            trig->startOffset = MAX(trig->startOffset - (int)size, -1);
            trig->stopOffset = MAX(trig->stopOffset - (int)size, -1);
        }
    }

    setIntegerParam(NDPluginReframeTriggerCount, trigCount);
    setIntegerParam(NDPluginReframeOutputCount, trigCount);
    setIntegerParam(NDPluginReframeBufferedTriggers, triggerQueue_->size());
    setIntegerParam(NDPluginReframeBufferFrames, arrayBuffer_->size());
    setIntegerParam(NDPluginReframeBufferSamples, bufferSizeCounts(0));

    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: New array handling complete\n", driverName, functionName);
}

/** Called when asyn clients call pasynInt32->write().
  * This function performs actions for some parameters.
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus NDPluginReframe::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    // ###TODO: Control needs to set up/ tear down the trigger buffer.
    // It should also clear the trigger buffer if the trigger conditions change.
    //   Should it? Can't we just handle this case?
    //   It does muck up truncation, since we need to re-test for triggers sometimes and we don't store the condition we used.
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    static const char *functionName = "writeInt32";

    if (function == NDPluginReframeControl) {
        // If the control is turned on then create our new ring buffer
        if (value == 1) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Arming plugin\n", driverName, functionName);
            if (arrayBuffer_ && triggerQueue_){
                while (arrayBuffer_->size()) {
                    NDArray *oldArray = arrayBuffer_->front();
                    arrayBuffer_->pop_front();
                    oldArray->release();
                }
                while (triggerQueue_->size()) {
                    Trigger *trig = triggerQueue_->front();
                    triggerQueue_->pop_front();
                    delete trig;
                }
            } else {
                arrayBuffer_ = new std::deque<NDArray *>;
                triggerQueue_ = new std::deque<Trigger *>;
            }
            // Set the status to buffer filling and clear any residual state current/last trigger
            setIntegerParam(NDPluginReframeSoftTrigger, 0);
            setIntegerParam(NDPluginReframeTriggerCount, 0);
            setIntegerParam(NDPluginReframeIgnoredCount, 0);
            setIntegerParam(NDPluginReframeTriggerEnded, 0);
            setIntegerParam(NDPluginReframeBufferFrames, 0);
            setIntegerParam(NDPluginReframeBufferSamples, 0);
            setStringParam(NDPluginReframeStatus, "Waiting for trigger");
            setIntegerParam(NDPluginReframeMode, Armed);

            setIntegerParam(NDPluginReframeBufferedTriggers, 0);
        } else {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Disarming plugin\n", driverName, functionName);
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
    //int traceMask = pasynTrace->getTraceMask(pasynUserSelf);
    //pasynTrace->setTraceMask(pasynUserSelf, traceMask | ASYN_TRACE_FLOW);

    const char *functionName = "NDPluginReframe";
    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s, %s: Initialising reframe plugin\n", driverName, functionName);

    arrayBuffer_ = NULL;
    triggerQueue_ = NULL;
    triggerOnIndex_ = -1;
    triggerOffIndex_ = -1;
    // General
    createParam(NDPluginReframeControlString,               asynParamInt32,   &NDPluginReframeControl);
    createParam(NDPluginReframeStatusString,                asynParamOctet,   &NDPluginReframeStatus);
    createParam(NDPluginReframeSoftTriggerString,           asynParamInt32,   &NDPluginReframeSoftTrigger);
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
    createParam(NDPluginReframeRearmModeString,             asynParamInt32,   &NDPluginReframeRearmMode);
    createParam(NDPluginReframeTriggerTotalString,          asynParamInt32,   &NDPluginReframeTriggerTotal);
    createParam(NDPluginReframeOutputCountString,           asynParamInt32,   &NDPluginReframeOutputCount);
    createParam(NDPluginReframeIgnoredCountString,          asynParamInt32,   &NDPluginReframeIgnoredCount);
    createParam(NDPluginReframeBufferFramesString,          asynParamInt32,   &NDPluginReframeBufferFrames);
    createParam(NDPluginReframeBufferSamplesString,         asynParamInt32,   &NDPluginReframeBufferSamples);
    createParam(NDPluginReframeModeString,                  asynParamInt32,   &NDPluginReframeMode);

    createParam(NDPluginReframeMaxTriggersString,           asynParamInt32,   &NDPluginReframeMaxTriggers);
    createParam(NDPluginReframeBufferedTriggersString,      asynParamInt32,   &NDPluginReframeBufferedTriggers);
    createParam(NDPluginReframeOverlappingTriggersString,   asynParamInt32,   &NDPluginReframeOverlappingTriggers);

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
    setIntegerParam(NDPluginReframeRearmMode, Continuous);

    setIntegerParam(NDPluginReframeTriggerEnded, 0);
    setIntegerParam(NDPluginReframeTriggerCount, 0);
    setIntegerParam(NDPluginReframeTriggerTotal, 0);

    setIntegerParam(NDPluginReframeOutputCount, 0);
    setIntegerParam(NDPluginReframeIgnoredCount, 0);

    setIntegerParam(NDPluginReframeBufferFrames, 0);
    setIntegerParam(NDPluginReframeBufferSamples, 0);

    setIntegerParam(NDPluginReframeMaxTriggers, 0);
    setIntegerParam(NDPluginReframeBufferedTriggers, 0);
    setIntegerParam(NDPluginReframeOverlappingTriggers, 0);

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
