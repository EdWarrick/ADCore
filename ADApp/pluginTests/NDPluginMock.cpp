/*
 * NDPluginMock.cpp
 *
 *  Created on: 11 Mar 2015
 *      Author: Edmund Warrick
 */

#include "NDPluginMock.h"

NDPluginMock::NDPluginMock (const char *portName, int queueSize, int blockingCallbacks,
                              const char *NDArrayPort, int NDArrayAddr,
                              int maxBuffers, size_t maxMemory,
                              int priority, int stackSize)
         /* Invoke the base class constructor */
         : NDPluginDriver(portName, queueSize, blockingCallbacks,
                        NDArrayPort, NDArrayAddr, 1, 0, maxBuffers, maxMemory,
                        asynInt32ArrayMask | asynFloat64ArrayMask | asynGenericPointerMask,
                        asynInt32ArrayMask | asynFloat64ArrayMask | asynGenericPointerMask,
                        0, 1, priority, stackSize)
{
    arrays_ = new deque<NDArray *>();

    connectToArrayPort();
}

NDPluginMock::~NDPluginMock()
{
    while(arrays_->front()) {
        arrays_->front()->release();
        arrays_->pop_front();
    }
    delete arrays_;
}

std::deque<NDArray *> *NDPluginMock::arrays()
{
    return arrays_;
}

void NDPluginMock::processCallbacks(NDArray *pArray)
{
    NDPluginDriver::processCallbacks(pArray);
    NDArray *pArrayCpy = this->pNDArrayPool->copy(pArray, NULL, 1);
    if (pArrayCpy) {
        arrays_->push_back(pArrayCpy);
    }
}


