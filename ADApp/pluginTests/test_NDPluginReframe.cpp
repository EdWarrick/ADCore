/*
 * test_NDPluginReframe.cpp
 *
 *  Created on: 11 Mar 2015
 *      Author: Edmund Warrick
 */

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE "NDPluginReframe Tests"
#include "boost/test/unit_test.hpp"

// AD dependencies
#include <NDPluginReframe.h>
#include <simDetector.h>
#include <NDArray.h>
#include <asynPortClient.h>

#include "NDPluginMock.h"

#include <string.h>
#include <stdint.h>

struct PluginFixture
{
    NDArrayPool *arrayPool;
    simDetector *driver;
    NDPluginReframe *rf;
    NDPluginMock *ds;

    // Mock downstream params
    asynInt32Client *enableCallbacks;
    asynInt32Client *blockingCallbacks;
    asynInt32Client *dsCounter;

    // Reframe params
    asynInt32Client *control;
    asynInt32Client *preTrigger;
    asynInt32Client *postTrigger;
    asynInt32Client *storedFrames;
    asynInt32Client *storedSamples;
    asynInt32Client *triggerCount;
    asynOctetClient *status;
    asynInt32Client *onCond;
    asynInt32Client *offCond;
    asynFloat64Client *onThresh;
    asynFloat64Client *offThresh;

    asynInt32Client *counter;

    static int testCase;
    vector<NDArray *> *arrays;

    PluginFixture()
    {
        arrayPool = new NDArrayPool(100, 0);
        arrays = new vector<NDArray *>;

        // Asyn manager doesn't like it if we try to reuse the same port name for multiple drivers (even if only one is ever instantiated at once), so
        // change it slightly for each test case.
        char simport[50], testport[50], dsport[50];
        sprintf(simport, "simPort%d", testCase);
        // We need some upstream driver for our test plugin so that calls to connectArrayPort don't fail, but we can then ignore it and send
        // arrays by calling processCallbacks directly.
        driver = new simDetector(simport, 800, 500, NDFloat64, 50, 0, 0, 2000000);

        // This is the plugin under test
        sprintf(testport, "testPort%d", testCase);
        rf = new NDPluginReframe(testport, 50, 0, simport, 0, 1000, -1, 0, 2000000);

        // This is the mock downstream plugin
        sprintf(dsport, "dsPort%d", testCase);
        ds = new NDPluginMock(dsport, 16, 1, testport, 0, 50, -1, 0, 2000000);

        enableCallbacks = new asynInt32Client(dsport, 0, NDPluginDriverEnableCallbacksString);
        blockingCallbacks = new asynInt32Client(dsport, 0, NDPluginDriverBlockingCallbacksString);
        dsCounter = new asynInt32Client(dsport, 0, NDArrayCounterString);

        enableCallbacks->write(1);
        blockingCallbacks->write(1);

        counter = new asynInt32Client(testport, 0, NDArrayCounterString);

        control = new asynInt32Client(testport, 0, NDPluginReframeControlString);
        preTrigger = new asynInt32Client(testport, 0, NDPluginReframePreTriggerSamplesString);
        postTrigger = new asynInt32Client(testport, 0, NDPluginReframePostTriggerSamplesString);
        storedFrames = new asynInt32Client(testport, 0, NDPluginReframeBufferFramesString);
        storedSamples = new asynInt32Client(testport, 0, NDPluginReframeBufferSamplesString);
        triggerCount = new asynInt32Client(testport, 0, NDPluginReframeTriggerCountString);
        status = new asynOctetClient(testport, 0, NDPluginReframeStatusString);
        onCond = new asynInt32Client(testport, 0, NDPluginReframeTriggerStartConditionString);
        offCond = new asynInt32Client(testport, 0, NDPluginReframeTriggerEndConditionString);
        onThresh = new asynFloat64Client(testport, 0, NDPluginReframeTriggerStartThresholdString);
        offThresh = new asynFloat64Client(testport, 0, NDPluginReframeTriggerEndThresholdString);

        testCase++;
    }
    ~PluginFixture()
    {
        delete offThresh;
        delete onThresh;
        delete offCond;
        delete onCond;
        delete status;
        delete triggerCount;
        delete storedSamples;
        delete storedFrames;
        delete postTrigger;
        delete preTrigger;
        delete control;
        delete counter;
        delete dsCounter;
        delete blockingCallbacks;
        delete enableCallbacks;
        delete ds;
        delete rf;
        delete driver;

        for (vector<NDArray *>::iterator iter = arrays->begin(); iter != arrays->end(); ++iter)
            (*iter)->release();

        delete arrays;
        delete arrayPool;
    }
    void rfProcess(NDArray *pArray)
    {
        rf->lock();
        rf->processCallbacks(pArray);
        rf->unlock();
    }
    // Convenience methods to create arrays, fill them with data, and handle memory management
    NDArray *arrayAlloc(int ndims, size_t *dims, NDDataType_t dataType, size_t dataSize, void *pData)
    {
        NDArray *array = arrayPool->alloc(ndims, dims, dataType, dataSize, pData);
        arrays->push_back(array);
        return array;
    }
    NDArray *constantArray(int ndims, size_t *dims, double val)
    {
        NDArray *array = arrayAlloc(ndims, dims, NDFloat64, 0, NULL);
        double *pData = (double *)array->pData;
        for (size_t i = 0; i < dims[0] * dims[1]; i++) {
            pData[i] = val;
        }
        return array;
    }
    NDArray *emptyArray(int ndims, size_t *dims)
    {
        return constantArray(ndims, dims, 0.0);
    }

};

int PluginFixture::testCase = 0;

BOOST_FIXTURE_TEST_SUITE(ReframeBufferingTests, PluginFixture)

// Verify that plugin starts off in idle mode and ignores frames sent to it (doesn't check them for triggers or add them to the buffer).
BOOST_AUTO_TEST_CASE(test_IdleModeIgnoresFrames)
{
    int count, triggers, frames, samples, eom;
    char state[50] = {0};
    size_t bytes;

    size_t dims = 3;
    NDArray *array = arrayPool->alloc(1, &dims, NDFloat64, 0, NULL);

    for (int i = 0; i < 5; i++)
        rfProcess(array);

    array->release();
    counter->read(&count);
    triggerCount->read(&triggers);
    storedFrames->read(&frames);
    storedSamples->read(&samples);
    status->read(state, 50, &bytes, &eom);

    BOOST_REQUIRE_EQUAL(count, 5);
    BOOST_REQUIRE_EQUAL(triggers, 0);
    BOOST_REQUIRE_EQUAL(frames, 0);
    BOOST_REQUIRE_EQUAL(samples, 0);
    BOOST_REQUIRE_EQUAL(strcmp(state, "Idle"), 0);
}

// Verify that when in armed mode, frames are correctly stored in the buffer if they don't contain triggers.
BOOST_AUTO_TEST_CASE(test_FrameBufferingWorks)
{
    // Set up the plugin
    preTrigger->write(5000);
    postTrigger->write(5000);
    control->write(1);

    // Triggering is a bit icky. We need to guarantee we won't trigger. With generic triggering this is easy, but with just high/low it's a bit
    // harder. (Or we could not worry about it and just rely on the post trigger; but probably best to guarantee we won't trigger so we're not
    // failing to test the pre-trigger)
    // Trigger on high level
    onCond->write(1);
    onThresh->write(1.0);

    // Set up the array
    size_t dims[2] = {3,3};
    NDArray *array = arrayPool->alloc(2, dims, NDFloat64, 0, NULL);
    double *pData = (double *)array->pData;
    for (int i = 0; i < 9; i++) {
        pData[i] = 0.0;
    }

    // Pass the array to the plugin
    for (int i = 0; i < 7; i++) {
        rfProcess(array);
    }
    array->release();

    // Get the params
    int triggers, frames, samples;

    storedFrames->read(&frames);
    storedSamples->read(&samples);
    triggerCount->read(&triggers);

    BOOST_REQUIRE_EQUAL(triggers, 0);
    BOOST_REQUIRE_EQUAL(frames, 7);
    BOOST_REQUIRE_EQUAL(samples, 3*7);
}

// Verify that the buffer wraps correctly, i.e. that it is pruned once the pre-buffer is full
// The pruning condition is that any arrays that are old enough that they would contribute no data to
// the pre-buffer if a trigger arrives on the next frame, should be pruned.
// This means both the number of stored frames and the stored samples can vary.
// This is probably complex enough to split into several test cases.
BOOST_AUTO_TEST_CASE(test_BufferWrappingWorks)
{
    // Set up the plugin
    preTrigger->write(100);
    control->write(1);

    // Set to never trigger (so long as we send arrays of all 0)
    onCond->write(1);
    onThresh->write(1.0);

    // Set up array
    size_t dims[2] = {2, 30};
    NDArray *array = arrayPool->alloc(2, dims, NDFloat64, 0, NULL);
    double *pData = (double *)array->pData;
    for (int i = 0; i < 60; i++)
    {
        pData[i] = 0.0;
    }

    // Pass to plugin
    for (int i = 0; i < 4; i++)
    {
        rfProcess(array);
    }

    // Verify wrapping doesn't start prematurely

    int frames, samples;
    storedFrames->read(&frames);
    storedSamples->read(&samples);

    BOOST_REQUIRE_EQUAL(frames, 4);
    BOOST_REQUIRE_EQUAL(samples, 120);

    // Start wrapping
    rfProcess(array);

    storedFrames->read(&frames);
    storedSamples->read(&samples);

    BOOST_REQUIRE_EQUAL(frames, 4);
    BOOST_REQUIRE_EQUAL(samples, 120);

    // More wrapping

    for (int i = 0; i < 100; i++) {
        rfProcess(array);
    }

    storedFrames->read(&frames);
    storedSamples->read(&samples);

    BOOST_REQUIRE_EQUAL(frames, 4);
    BOOST_REQUIRE_EQUAL(samples, 120);

    array->release();

    // Now test we can prune multiple arrays:

    size_t dims2[2] = {2, 80};
    array = arrayPool->alloc(2, dims2, NDFloat64, 0, NULL);
    pData = (double *)array->pData;
    for (int i = 0; i < 2*80; i++) {
        pData[i] = 0.0;
    }

    rfProcess(array);

    storedFrames->read(&frames);
    storedSamples->read(&samples);

    BOOST_REQUIRE_EQUAL(frames, 2);
    BOOST_REQUIRE_EQUAL(samples, 110);

    array->release();

    // Now test we don't always prune on new data arriving, if the oldest array would still contribute
    // to the pre-buffer.

    size_t dims3[2] = {2, 5};
    array = arrayPool->alloc(2, dims3, NDFloat64, 0, NULL);
    pData = (double *)array->pData;
    for (int i = 0; i < 2*5; i++) {
        pData[i] = 0.0;
    }

    rfProcess(array);

    storedFrames->read(&frames);
    storedSamples->read(&samples);

    BOOST_REQUIRE_EQUAL(frames, 3);
    BOOST_REQUIRE_EQUAL(samples, 115);

    array->release();
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(ReframeTriggeringTests, PluginFixture)

BOOST_AUTO_TEST_CASE(test_SimpleTriggerHigh)
{
    control->write(1);
    preTrigger->write(5);
    postTrigger->write(5);
    onCond->write(1);
    onThresh->write(3.0);
    offCond->write(1);
    offThresh->write(-1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray = emptyArray(2, dims);
    double *pData = (double *)testArray->pData;
    *(pData+9) = 4.0;

    rfProcess(testArray);

    int dscount, trigs;

    dsCounter->read(&dscount);
    triggerCount->read(&trigs);

    BOOST_REQUIRE(dscount == 1);
    BOOST_REQUIRE(trigs == 1);

    // Now test that equal to trigger is ignored (i.e. use >, not >=)

    control->write(1);

    *(pData+9) = 3.0;

    rfProcess(testArray);

    dsCounter->read(&dscount);
    triggerCount->read(&trigs);

    BOOST_REQUIRE(dscount == 1);
    BOOST_REQUIRE(trigs == 0);
}

BOOST_AUTO_TEST_CASE(test_SimpleTriggerLow)
{
    control->write(1);
    preTrigger->write(5);
    postTrigger->write(5);
    onCond->write(0);
    onThresh->write(1.0);
    offCond->write(1);
    offThresh->write(-1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray = constantArray(2, dims, 1.0);
    double *pData = (double *)testArray->pData;
    *(pData+9) = 0.0;

    rfProcess(testArray);

    int dscount, samples, trigs;

    dsCounter->read(&dscount);
    triggerCount->read(&trigs);

    BOOST_REQUIRE(dscount == 1);
    BOOST_REQUIRE(trigs == 1);

    // Now test that equal to trigger is ignored (i.e. use <, not <=)

    control->write(1);

    *(pData+9) = 1.0;

    rfProcess(testArray);

    dsCounter->read(&dscount);
    triggerCount->read(&trigs);

    BOOST_REQUIRE(dscount == 1);
    BOOST_REQUIRE(trigs == 0);

}

BOOST_AUTO_TEST_CASE(test_SoftTrigger)
{

}

BOOST_AUTO_TEST_CASE(test_GatingTriggerHigh)
{

}

BOOST_AUTO_TEST_CASE(test_GatingTriggerLow)
{

}

BOOST_AUTO_TEST_CASE(test_MultiTrigger)
{

}

BOOST_AUTO_TEST_CASE(test_IndefiniteTrigger)
{

}

BOOST_AUTO_TEST_CASE(test_CanGuaranteeTriggerOn)
{

}

BOOST_AUTO_TEST_CASE(test_CanGuaranteeTriggerOff)
{


}

BOOST_AUTO_TEST_CASE(test_NonZeroTriggerChannel)
{

}

BOOST_AUTO_TEST_CASE(test_IgnoresNonTriggerChannel)
{

}

BOOST_AUTO_TEST_SUITE_END()


