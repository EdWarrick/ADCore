/*
 * test_NDPluginReframe.cpp
 *
 *  Created on: 11 Mar 2015
 *      Author: Edmund Warrick
 */

#include "boost/test/unit_test.hpp"
#include "boost/test/floating_point_comparison.hpp"

// AD dependencies
#include <NDPluginReframe.h>
#include <NDPluginROI.h>
#include <simDetector.h>
#include <NDArray.h>
#include <asynPortClient.h>

#include "NDPluginMock.h"

#include <string.h>
#include <stdint.h>
#include <math.h>
#include <float.h>
#include <deque>

#include <epicsTypes.h>
using namespace std;

struct ReframeFixture
{
    NDArrayPool *arrayPool;
    static simDetector *driver;
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
    asynInt32Client *triggerMax;
    asynInt32Client *triggerMode;
    asynInt32Client *triggerChannel;
    asynInt32Client *triggerCount;
    asynInt32Client *ignoredCount;
    asynOctetClient *status;
    asynInt32Client *onCond;
    asynInt32Client *offCond;
    asynFloat64Client *onThresh;
    asynFloat64Client *offThresh;
    asynInt32Client *softTrigger;
    asynInt32Client *bufferedTrigs;
    asynInt32Client *overlappingTrigs;

    asynInt32Client *counter;

    static int testCase;
    vector<NDArray *> *arrays;

    ReframeFixture()
    {
        arrayPool = new NDArrayPool(100, 0);
        arrays = new vector<NDArray *>;


        // Asyn manager doesn't like it if we try to reuse the same port name for multiple drivers (even if only one is ever instantiated at once), so
        // change it slightly for each test case.
        char simport[50], testport[50], dsport[50];
        sprintf(simport, "simPort%d", testCase);
//        // We need some upstream driver for our test plugin so that calls to connectArrayPort don't fail, but we can then ignore it and send
//        // arrays by calling processCallbacks directly.

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
        softTrigger = new asynInt32Client(testport, 0, NDPluginReframeSoftTriggerString);
        storedFrames = new asynInt32Client(testport, 0, NDPluginReframeBufferFramesString);
        storedSamples = new asynInt32Client(testport, 0, NDPluginReframeBufferSamplesString);
        triggerMax = new asynInt32Client(testport, 0, NDPluginReframeTriggerMaxString);
        triggerMode = new asynInt32Client(testport, 0, NDPluginReframeRearmModeString);
        triggerChannel = new asynInt32Client(testport, 0, NDPluginReframeTriggerChannelString);
        triggerCount = new asynInt32Client(testport, 0, NDPluginReframeTriggerCountString);
        ignoredCount = new asynInt32Client(testport, 0, NDPluginReframeIgnoredCountString);
        status = new asynOctetClient(testport, 0, NDPluginReframeStatusString);
        onCond = new asynInt32Client(testport, 0, NDPluginReframeTriggerStartConditionString);
        offCond = new asynInt32Client(testport, 0, NDPluginReframeTriggerEndConditionString);
        onThresh = new asynFloat64Client(testport, 0, NDPluginReframeTriggerStartThresholdString);
        offThresh = new asynFloat64Client(testport, 0, NDPluginReframeTriggerEndThresholdString);
        bufferedTrigs = new asynInt32Client(testport, 0, NDPluginReframeBufferedTriggersString);
        overlappingTrigs = new asynInt32Client(testport, 0, NDPluginReframeOverlappingTriggersString);


        testCase++;
    }
    ~ReframeFixture()
    {
        delete overlappingTrigs;
        delete bufferedTrigs;
        delete offThresh;
        delete onThresh;
        delete offCond;
        delete onCond;
        delete status;
        delete ignoredCount;
        delete triggerCount;
        delete triggerChannel;
        delete triggerMode;
        delete triggerMax;
        delete storedSamples;
        delete storedFrames;
        delete softTrigger;
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
    NDArray *incrementArray(int ndims, size_t *dims, double start)
    {
        NDArray *array = emptyArray(ndims, dims);
        double *pData = (double *)array->pData;
        for (size_t i = 0; i < dims[0] * dims[1]; i++) {
            // Idea is we should have an array which increments by 10 for each step in time, and increments
            // by one for each step in channel #
            *(pData + i) = start + 10 * (i/dims[0]) + i%dims[0];
        }
        return array;
    }

};

int ReframeFixture::testCase = 0;
simDetector *ReframeFixture::driver = NULL;

BOOST_AUTO_TEST_SUITE(ReframeTests)

BOOST_FIXTURE_TEST_SUITE(ReframeBufferingTests, ReframeFixture)

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

BOOST_FIXTURE_TEST_SUITE(ReframeTriggeringTests, ReframeFixture)

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

    int dscount, trigs;

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
    control->write(1);
    preTrigger->write(5);
    postTrigger->write(5);
    softTrigger->write(1);

    size_t dims[2] = {5,5};
    NDArray *testArray = emptyArray(2, dims);

    rfProcess(testArray);

    int trigs, dscount;

    dsCounter->read(&dscount);
    triggerCount->read(&trigs);

    BOOST_REQUIRE(dscount == 1);
    BOOST_REQUIRE(trigs == 1);
}

BOOST_AUTO_TEST_CASE(test_GatingTriggerHigh)
{
    control->write(1);
    preTrigger->write(5);
    postTrigger->write(5);
    onCond->write(1);
    onThresh->write(1.0);
    offCond->write(1);
    offThresh->write(3.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray = constantArray(2, dims, 1.0);
    double *pData = (double *)testArray->pData;
    *(pData+6) = 2.0;
    *(pData+15) = 4.0;

    rfProcess(testArray);

    int trigs, dscount;

    dsCounter->read(&dscount);
    triggerCount->read(&trigs);

    BOOST_REQUIRE_EQUAL(dscount, 1);
    BOOST_REQUIRE_EQUAL(trigs, 1);
}

BOOST_AUTO_TEST_CASE(test_GatingTriggerLow)
{
    control->write(1);
    preTrigger->write(5);
    postTrigger->write(5);
    onCond->write(1);
    onThresh->write(1.0);
    offCond->write(0);
    offThresh->write(-1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray = constantArray(2, dims, 1.0);
    double *pData = (double *)testArray->pData;
    *(pData+6) = 2.0;
    *(pData+15) = -2.0;

    rfProcess(testArray);

    int trigs, dscount;

    dsCounter->read(&dscount);
    triggerCount->read(&trigs);

    BOOST_REQUIRE_EQUAL(dscount, 1);
    BOOST_REQUIRE_EQUAL(trigs, 1);
}

BOOST_AUTO_TEST_CASE(test_MultiTrigger)
{
    control->write(1);
    preTrigger->write(5);
    postTrigger->write(5);
    onCond->write(1);
    onThresh->write(1.0);
    offCond->write(1);
    offThresh->write(1.0);
    triggerMax->write(5.0);
    triggerMode->write(1);

    size_t dims[2] = {3, 50};
    NDArray *testArray = emptyArray(2, dims);
    double *pData = (double *)testArray->pData;
    *(pData+15) = 2.0;
    *(pData+45) = 2.0;
    *(pData+75) = 2.0;

    rfProcess(testArray);
    rfProcess(testArray);
    // This one should be ignored
    rfProcess(testArray);

    int trigs, dscount, count;

    dsCounter->read(&dscount);
    triggerCount->read(&trigs);
    counter->read(&count);

    BOOST_CHECK_EQUAL(dscount, 5);
    BOOST_CHECK_EQUAL(trigs, 5);
    BOOST_REQUIRE_EQUAL(count, 3);
}

BOOST_AUTO_TEST_CASE(test_IndefiniteTrigger)
{
    control->write(1);
    preTrigger->write(3);
    postTrigger->write(5);
    onCond->write(1);
    onThresh->write(1.0);
    offCond->write(1);
    offThresh->write(1.0);
    triggerMax->write(0.0);


    size_t dims[2] = {4, 70};
    NDArray *testArray = emptyArray(2, dims);
    double *pData = (double *)testArray->pData;
    *(pData+16) = 2.0;
    *(pData+56) = 2.0;
    *(pData+100) = 2.0;
    *(pData+156) = 2.0;

    rfProcess(testArray);
    rfProcess(testArray);
    rfProcess(testArray);

    int trigs, dscount;

    dsCounter->read(&dscount);
    triggerCount->read(&trigs);

    BOOST_CHECK_EQUAL(dscount, 12);
    BOOST_CHECK_EQUAL(trigs, 12);
}

BOOST_AUTO_TEST_CASE(test_CanGuaranteeTriggerOn)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(1);
    onCond->write(AlwaysOn);
    offCond->write(AlwaysOn);
    triggerMode->write(Continuous);

    size_t dims[2] = {4, 5};
    NDArray *testArray = emptyArray(2, dims);
    double *pData = (double *)testArray->pData;
    pData[0] = DBL_MAX;
    pData[4*1] = DBL_MIN;
    pData[4*2] = 1.0;
    pData[4*3] = 0.0;
    pData[4*4] = -4.523e32;

    rfProcess(testArray);

    int trigs, pending, stored;

    triggerCount->read(&trigs);
    storedFrames->read(&stored);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 5);
    BOOST_CHECK_EQUAL(stored, 0);
    BOOST_CHECK_EQUAL(pending, 0);
}

BOOST_AUTO_TEST_CASE(test_CanGuaranteeTriggerOff)
{
    control->write(1);
    preTrigger->write(1);
    postTrigger->write(1);
    onCond->write(AlwaysOff);
    offCond->write(AlwaysOff);
    triggerMode->write(Continuous);

    size_t dims[2] = {4, 5};
    NDArray *testArray = emptyArray(2, dims);
    double *pData = (double *)testArray->pData;
    pData[0] = DBL_MAX;
    pData[4*1] = DBL_MIN;
    pData[4*2] = 1.0;
    pData[4*3] = 0.0;
    pData[4*4] = -4.523e32;

    rfProcess(testArray);

    int trigs, pending, stored;

    triggerCount->read(&trigs);
    storedFrames->read(&stored);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 0);
    BOOST_CHECK_EQUAL(stored, 1);
    BOOST_CHECK_EQUAL(pending, 0);
}

BOOST_AUTO_TEST_CASE(test_NonZeroTriggerChannel)
{
    control->write(1);
    preTrigger->write(30);
    postTrigger->write(67);
    triggerChannel->write(3);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {6,200};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    // Now put it in the trigger channel

    testData[6*50+3] = 2.0;

    rfProcess(testArray);

    int trigs;
    triggerCount->read(&trigs);

    BOOST_CHECK_EQUAL(trigs, 1);
}

BOOST_AUTO_TEST_CASE(test_IgnoresNonTriggerChannel)
{
    control->write(1);
    preTrigger->write(30);
    postTrigger->write(67);
    triggerChannel->write(3);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {6,200};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[6*40] = 2.0;
    testData[6*56+2] = 2.0;
    testData[6*63+4] = 2.0;
    rfProcess(testArray);

    int trigs;
    triggerCount->read(&trigs);

    BOOST_CHECK_EQUAL(trigs, 0);
}

BOOST_AUTO_TEST_CASE(test_OverlappingTriggersSkipped)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*30] = 2.0;
    testData[4*45] = 2.0;
    testData[4*65] = 2.0;

    rfProcess(testArray);

    int trigs, counts;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);

    BOOST_CHECK_EQUAL(trigs, 2);
    BOOST_CHECK_EQUAL(counts, 5);
}

BOOST_AUTO_TEST_CASE(test_IgnoredCountCorrect)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*20] = 2.0;
    testData[4*22] = 2.0;
    testData[4*35] = 2.0;
    testData[4*65] = 2.0;
    testData[4*95] = 2.0;

    rfProcess(testArray);
    rfProcess(testArray);

    int ignored;
    ignoredCount->read(&ignored);

    BOOST_CHECK_EQUAL(ignored, 4);
}

BOOST_AUTO_TEST_CASE(test_BufferedTriggersCount)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);
    overlappingTrigs->write(1);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*30] = 2.0;
    testData[4*92] = 2.0;
    testData[4*95] = 2.0;
    testData[4*97] = 2.0;

    int initial, pending, final;
    bufferedTrigs->read(&initial);

    rfProcess(testArray);

    bufferedTrigs->read(&pending);
    testArray = emptyArray(2, dims);

    rfProcess(testArray);

    bufferedTrigs->read(&final);

    BOOST_CHECK_EQUAL(initial, 0);
    BOOST_CHECK_EQUAL(pending, 3);
    BOOST_CHECK_EQUAL(final, 0);
}

// This to test we aren't missing the 1st sample in a frame if not 1st in the buffer
BOOST_AUTO_TEST_CASE(test_NarrowTrigAtFrameStart)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[0] = 2.0;

    rfProcess(testArray);
    rfProcess(testArray);

    int trigs, counts;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);

    BOOST_CHECK_EQUAL(trigs, 2);
    BOOST_CHECK_EQUAL(counts, 70);
}

// This to check we aren't double counting the last sample in a frame.
BOOST_AUTO_TEST_CASE(test_NarrowTrigAtFrameEnd)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*99] = 2.0;

    rfProcess(testArray);
    rfProcess(testArray);
    rfProcess(testArray);

    int trigs, counts, pending;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 2);
    BOOST_CHECK_EQUAL(counts, 71);
    BOOST_CHECK_EQUAL(pending, 1);
}

BOOST_AUTO_TEST_CASE(test_OverlappingTriggersPermitted)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);
    overlappingTrigs->write(1);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*10] = 2.0;
    testData[4*20] = 2.0;
    testData[4*95] = 2.0;

    rfProcess(testArray);
    rfProcess(testArray);

    int trigs, counts, pending;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 5);
    BOOST_CHECK_EQUAL(counts, 100);
    BOOST_CHECK_EQUAL(pending, 1);
}

BOOST_AUTO_TEST_CASE(test_TruncationWorksWithPending)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);
    overlappingTrigs->write(1);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*95] = 2.0;

    rfProcess(testArray);
    rfProcess(testArray);
    rfProcess(testArray);
    rfProcess(testArray);
    rfProcess(testArray);

    int trigs, counts, pending;

    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 4);
    BOOST_CHECK_EQUAL(counts, 100);
    BOOST_CHECK_EQUAL(pending, 1);
}

BOOST_AUTO_TEST_CASE(test_SoftTriggerOverridesRealTrigger)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);
    softTrigger->write(1);

    overlappingTrigs->write(1);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*10] = 2.0;
    testData[4*20] = 2.0;
    testData[4*95] = 2.0;

    rfProcess(testArray);
    rfProcess(testArray);
    rfProcess(testArray);

    int trigs, pending;

    triggerCount->read(&trigs);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 3);
    BOOST_CHECK_EQUAL(pending, 0);
}

// If the trigger off condition is also a trigger on condition, it should be
// ignored by the trigger on check (otherwise the last sample of the 1st gate would
// overlap the first sample of the next).
BOOST_AUTO_TEST_CASE(test_OverlappingTrigOffAndOnHandledCorrectly)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(3.0);
    overlappingTrigs->write(1);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*30] = 2.0;
    testData[4*45] = 4.0;
    testData[4*46] = 4.0;

    rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 2);

    BOOST_CHECK_EQUAL(arrs->at(0)->dims[1].size, 20 + 30 + 15);
    BOOST_CHECK_EQUAL(arrs->at(1)->dims[1].size, 20 + 30);
}

BOOST_AUTO_TEST_CASE(test_OverlappingGatesIgnored)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(0);
    onThresh->write(1.0);
    offThresh->write(-1.0);
    overlappingTrigs->write(1);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*30] = 2.0;
    testData[4*45] = -2.0;
    testData[4*40] = 2.0;
    testData[4*50] = -2.0;

    rfProcess(testArray);

    int pending;
    bufferedTrigs->read(&pending);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 1);

    NDArray *opArray = arrs->front();

    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 20+30+15);
    BOOST_REQUIRE_EQUAL(pending, 0);
}

BOOST_AUTO_TEST_CASE(test_StraddlingTriggerCaught)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(0);
    onThresh->write(1.0);
    offThresh->write(-1.0);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*30] = 2.0;
    testData[4*31] = -2.0;
    for (int i = 45; i < 65; i++)
        testData[4*i] = 2.0;
    testData[4*65] = -2.0;

    rfProcess(testArray);

    int trigs, counts;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);

    BOOST_CHECK_EQUAL(trigs, 2);
    BOOST_CHECK_EQUAL(counts, 5);
}

BOOST_AUTO_TEST_CASE(test_TriggersResetAfterSingleAcquisition)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(1);
    offCond->write(0);
    onThresh->write(1.0);
    offThresh->write(-1.0);
    triggerMode->write(Single);

    size_t dims[2] = {4,100};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[0] = 2.0;
    testData[4*1] = -2.0;

    rfProcess(testArray);


    control->write(1);

    int trigs, counts, dscount;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    dsCounter->read(&dscount);

    BOOST_CHECK_EQUAL(trigs, 0);
    BOOST_CHECK_EQUAL(counts, 0);
    BOOST_CHECK_EQUAL(dscount, 1);

    rfProcess(testArray);

    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    dsCounter->read(&dscount);
    BOOST_CHECK_EQUAL(trigs, 1);
    BOOST_CHECK_EQUAL(counts, 69);
    BOOST_CHECK_EQUAL(dscount, 2);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(ReframeEdgeTriggeringTests, ReframeFixture)


BOOST_AUTO_TEST_CASE(test_EdgeInitialHighDoesntTrigger)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(RisingEdge);
    offCond->write(AlwaysOn);
    onThresh->write(1.0);
    offThresh->write(-1.0);

    size_t dims[2] = {3, 200};
    NDArray *testArray = constantArray(2, dims, 2.0);

    rfProcess(testArray);

    int trigs, counts;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);

    BOOST_CHECK_EQUAL(trigs, 0);
    BOOST_CHECK_EQUAL(counts, 200);
}

BOOST_AUTO_TEST_CASE(test_SimpleRisingEdgeWorks)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(RisingEdge);
    offCond->write(AlwaysOn);
    onThresh->write(1.0);

    size_t dims[2] = {3, 200};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;

    data[3*10] = 2.0;

    rfProcess(testArray);

    int trigs, counts;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);

    BOOST_CHECK_EQUAL(trigs, 1);
    BOOST_CHECK_EQUAL(counts, 160);
}

BOOST_AUTO_TEST_CASE(test_SimpleFallingEdgeWorks)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(FallingEdge);
    offCond->write(AlwaysOn);
    onThresh->write(1.0);

    size_t dims[2] = {3, 200};
    NDArray *testArray = constantArray(2, dims, 2.0);
    double *data = (double *)testArray->pData;

    data[3*20] = 0.0;

    rfProcess(testArray);

    int trigs, counts;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);

    BOOST_CHECK_EQUAL(trigs, 1);
    BOOST_CHECK_EQUAL(counts, 150);
}

BOOST_AUTO_TEST_CASE(test_RisingFalling)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 200};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;

    for (int i = 20; i < 30; i++) {
        data[3*i] = 2.0;
    }

    rfProcess(testArray);

    int trigs, counts, ignored, pending;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    ignoredCount->read(&ignored);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 1);
    BOOST_CHECK_EQUAL(counts, 140);
    BOOST_CHECK_EQUAL(ignored, 0);
    BOOST_CHECK_EQUAL(pending, 0);
}

BOOST_AUTO_TEST_CASE(test_FallingRising)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(FallingEdge);
    offCond->write(RisingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 200};
    NDArray *testArray = constantArray(2, dims, 2.0);
    double *data = (double *)testArray->pData;

    for (int i = 20; i < 30; i++) {
        data[3*i] = 0.0;
    }

    rfProcess(testArray);

    int trigs, counts, ignored, pending;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    ignoredCount->read(&ignored);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 1);
    BOOST_CHECK_EQUAL(counts, 140);
    BOOST_CHECK_EQUAL(ignored, 0);
    BOOST_CHECK_EQUAL(pending, 0);

}

BOOST_AUTO_TEST_CASE(test_EdgeLongPulseNotDuplicated)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(30);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);
    overlappingTrigs->write(1);

    size_t dims[2] = {3, 200};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;

    for (int i = 20; i < 30; i++) {
        data[3*i] = 2.0;
    }

    rfProcess(testArray);

    int trigs, counts, ignored, pending;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    ignoredCount->read(&ignored);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 1);
    BOOST_CHECK_EQUAL(counts, 200);
    BOOST_CHECK_EQUAL(ignored, 0);
    BOOST_CHECK_EQUAL(pending, 0);
}

BOOST_AUTO_TEST_CASE(test_NarrowPulsesWork)
{
    control->write(1);
    preTrigger->write(5);
    postTrigger->write(5);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 200};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;


    data[3*20] = 2.0;
    data[3*40] = 2.0;
    data[3*80] = 2.0;


    rfProcess(testArray);

    int trigs, counts, ignored, pending;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    ignoredCount->read(&ignored);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 3);
    BOOST_CHECK_EQUAL(counts, 114);
    BOOST_CHECK_EQUAL(ignored, 0);
    BOOST_CHECK_EQUAL(pending, 0);
}

BOOST_AUTO_TEST_CASE(test_AdjacentPulsesWork)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(1);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 200};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;


    for (int i = 10; i < 15; i++) {
        data[3*2*i] = 2.0;
    }

    rfProcess(testArray);

    int trigs, counts, ignored, pending;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    ignoredCount->read(&ignored);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 5);
    BOOST_CHECK_EQUAL(counts, 0);
    BOOST_CHECK_EQUAL(ignored, 0);
    BOOST_CHECK_EQUAL(pending, 0);
}

BOOST_AUTO_TEST_CASE(test_AdjacentPulsesMultiArray)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(0);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 6};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;


    for (int i = 0; i < 3; i++) {
        data[3*(2*i+1)] = 2.0;
    }

    rfProcess(testArray);
    rfProcess(testArray);
    rfProcess(testArray);

    int trigs, counts, ignored, pending;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    ignoredCount->read(&ignored);
    bufferedTrigs->read(&pending);

    BOOST_CHECK_EQUAL(trigs, 8);
    BOOST_CHECK_EQUAL(counts, 2);
    BOOST_CHECK_EQUAL(ignored, 0);
    BOOST_CHECK_EQUAL(pending, 1);
}

BOOST_AUTO_TEST_CASE(test_MixedEdgeAndLevelWorks)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(0);
    onCond->write(RisingEdge);
    offCond->write(BelowThreshold);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 6};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;


    for (int i = 0; i < 3; i++) {
        data[3*(2*i+1)] = 2.0;
    }

    rfProcess(testArray);
    rfProcess(testArray);
    rfProcess(testArray);

    int trigs, counts, ignored, pending;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    ignoredCount->read(&ignored);
    bufferedTrigs->read(&pending);

    BOOST_WARN_EQUAL(trigs, 8);
    BOOST_WARN_EQUAL(counts, 2);
    BOOST_CHECK_EQUAL(ignored, 0);
    BOOST_CHECK_EQUAL(pending, 1);
}

BOOST_AUTO_TEST_CASE(test_EdgeWindowSizeCorrect)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(0);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray1 = emptyArray(2, dims);
    double *data = (double *)testArray1->pData;


    for (int i = 0; i < 3; i++) {
        data[3*(2*i+1)] = 2.0;
    }

    data[3*18] = 2.0;
    data[3*19] = 2.0;

    NDArray *testArray2 = constantArray(2, dims, 2.0);
    data = (double *)testArray2->pData;
    data[3*18] = 0.0;
    data[3*19] = 0.0;

    rfProcess(testArray1);
    rfProcess(testArray2);

    std::deque<NDArray *> *arrs = ds->arrays();

    BOOST_CHECK_EQUAL(arrs->size(), 4);

    NDArray *arr1 = arrs->front();
    NDArray *arr4 = arrs->back();

    BOOST_CHECK_EQUAL(arr1->dims[1].size, 1);
    BOOST_CHECK_EQUAL(arr4->dims[1].size, 20);

}

BOOST_AUTO_TEST_CASE(test_EdgeWindowWithPreAndPost)
{
    control->write(1);
    preTrigger->write(3);
    postTrigger->write(2);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray1 = emptyArray(2, dims);
    double *data = (double *)testArray1->pData;



    data[3*4] = 2.0;


    data[3*18] = 2.0;
    data[3*19] = 2.0;

    NDArray *testArray2 = constantArray(2, dims, 2.0);
    data = (double *)testArray2->pData;
    data[3*18] = 0.0;
    data[3*19] = 0.0;

    rfProcess(testArray1);
    rfProcess(testArray2);

    std::deque<NDArray *> *arrs = ds->arrays();

    BOOST_CHECK_EQUAL(arrs->size(), 2);

    NDArray *arr1 = arrs->front();
    NDArray *arr4 = arrs->back();

    BOOST_CHECK_EQUAL(arr1->dims[1].size, 6);
    BOOST_CHECK_EQUAL(arr4->dims[1].size, 25);
}

BOOST_AUTO_TEST_CASE(test_EdgeDataLayoutCorrect)
{
    control->write(1);
    preTrigger->write(3);
    postTrigger->write(2);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray1 = emptyArray(2, dims);
    double *data = (double *)testArray1->pData;

    data[3*4] = 2.0;
    data[3*18] = 2.0;
    data[3*19] = 2.0;

    NDArray *testArray2 = constantArray(2, dims, 2.0);
    data = (double *)testArray2->pData;
    data[3*18] = 0.0;
    data[3*19] = 0.0;

    rfProcess(testArray1);
    rfProcess(testArray2);

    std::deque<NDArray *> *arrs = ds->arrays();

    BOOST_CHECK_EQUAL(arrs->size(), 2);

    NDArray *arr1 = arrs->front();
    NDArray *arr4 = arrs->back();
    data = (double *)arr1->pData;

    BOOST_CHECK_LT(fabs(data[3*3] - 2.0), 0.01);

    data = (double *)arr4->pData;

    BOOST_CHECK_LT(fabs(data[3*2] - 0.0), 0.01);
    BOOST_CHECK_LT(fabs(data[3*3] - 2.0), 0.01);
    BOOST_CHECK_LT(fabs(data[3*22] - 2.0), 0.01);
    BOOST_CHECK_LT(fabs(data[3*23] - 0.0), 0.01);
}

BOOST_AUTO_TEST_CASE(test_EdgeOnArrayBoundaryWorks)
{
    control->write(1);
    preTrigger->write(3);
    postTrigger->write(2);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);
    // Needed so that the second trig will still be on boundary when we construct it.
    overlappingTrigs->write(1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;

    data[3*19] = 2.0;
    rfProcess(testArray);
    data[3*19] = 0.0;
    rfProcess(testArray);
    data[0] = 2.0;
    rfProcess(testArray);

    int trigs, counts;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);

    BOOST_CHECK_EQUAL(trigs, 2);
    BOOST_CHECK_EQUAL(counts, 20);
}

BOOST_AUTO_TEST_CASE(test_AlwaysOnArrayBoundaryWorks)
{
    control->write(1);
    preTrigger->write(3);
    postTrigger->write(2);
    onCond->write(RisingEdge);
    offCond->write(AlwaysOn);
    onThresh->write(1.0);
    offThresh->write(1.0);
    // Needed so that the second trig will still be on boundary when we construct it.
    overlappingTrigs->write(1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;

    data[3*19] = 2.0;
    rfProcess(testArray);
    data[3*19] = 0.0;
    rfProcess(testArray);
    data[0] = 2.0;
    rfProcess(testArray);

    int trigs, counts;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);

    BOOST_CHECK_EQUAL(trigs, 2);
    BOOST_CHECK_EQUAL(counts, 20);
}

BOOST_AUTO_TEST_CASE(test_EdgeOnTruncationBoundaryWorks)
{
    control->write(1);
    preTrigger->write(3);
    postTrigger->write(2);
    onCond->write(RisingEdge);
    offCond->write(FallingEdge);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray = emptyArray(2, dims);
    double *data = (double *)testArray->pData;

    data[3*5] = 2.0;
    data[3*8] = 2.0;
    data[3*11] = 2.0;
    rfProcess(testArray);


    int trigs, counts;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);

    BOOST_CHECK_EQUAL(trigs, 3);
    BOOST_CHECK_EQUAL(counts, 6);
}

// Plugin has class variable to check whether sample above threshold is
// edge or just high level. This should be reset when we change the trigger
// condition.
BOOST_AUTO_TEST_CASE(test_EdgeLatchOnTriggerChangeFixed)
{
    control->write(1);
    preTrigger->write(3);
    postTrigger->write(2);
    onCond->write(RisingEdge);
    offCond->write(AlwaysOn);
    onThresh->write(1.0);
    offThresh->write(1.0);

    size_t dims[2] = {3, 20};
    NDArray *testArray = emptyArray(2, dims);

    rfProcess(testArray);

    onCond->write(FallingEdge);
    rfProcess(testArray);

    int trigs, counts, pending, ignored;
    triggerCount->read(&trigs);
    storedSamples->read(&counts);
    ignoredCount->read(&ignored);
    bufferedTrigs->read(&ignored);

    BOOST_CHECK_EQUAL(trigs, 0);
    BOOST_CHECK_EQUAL(counts, 20);
    BOOST_CHECK_EQUAL(ignored, 0);
    BOOST_CHECK_EQUAL(pending, 0);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(ReframeReframingTests, ReframeFixture)

BOOST_AUTO_TEST_CASE(test_WindowLTFrameSize)
{
    control->write(1);
    preTrigger->write(5);
    postTrigger->write(8);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);
    triggerMax->write(0.0);


    size_t dims[2] = {2, 300};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[2*10] = 2.0;
    testData[2*72] = 2.0;
    testData[2*163] = 2.0;

    rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();
    int samples;
    storedSamples->read(&samples);

    BOOST_CHECK_EQUAL(arrs->size(), 3);

    BOOST_CHECK_EQUAL(arrs->at(0)->dims[1].size, 13);
    BOOST_CHECK_EQUAL(arrs->at(1)->dims[1].size, 13);
    BOOST_CHECK_EQUAL(arrs->at(2)->dims[1].size, 13);

    // With no gate trigger sample should be first
    // sample of post trigger
    BOOST_CHECK_EQUAL(samples, 300 - 163 - 8);
}

BOOST_AUTO_TEST_CASE(test_WindowGTFrameSize)
{
    control->write(1);
    preTrigger->write(50);
    postTrigger->write(70);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(1.0);
    offThresh->write(1.0);
    triggerMax->write(0.0);

    size_t dims[2] = {3, 5};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    for (int i = 0; i < 12; i++)
        rfProcess(testArray);

    testData[3*2] = 2.0;

    rfProcess(testArray);

    testData[3*2] = 0.0;

    for (int i = 0; i < 15; i++)
        rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();
    int samples;
    storedSamples->read(&samples);

    BOOST_CHECK_EQUAL(arrs->size(), 1);

    BOOST_CHECK_EQUAL(arrs->front()->dims[1].size, 120);

    BOOST_CHECK_EQUAL(samples, 8);
}

BOOST_AUTO_TEST_CASE(test_WindowSizeCorrect)
{
    control->write(1);
    preTrigger->write(4);
    postTrigger->write(5);
    onCond->write(1);
    offCond->write(0);
    onThresh->write(1.0);
    offThresh->write(-1.0);
    triggerMax->write(0.0);


    size_t dims[2] = {4, 30};
    NDArray *testArray = emptyArray(2, dims);
    double *testData = (double *)testArray->pData;

    testData[4*12]=2.0;
    testData[4*15]=-2.0;

    testData[4*28]=2.0;
    testData[4*2]=-2.0;

    rfProcess(testArray);
    rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_CHECK_EQUAL(arrs->size(), 3);
    BOOST_CHECK_EQUAL(arrs->front()->dims[1].size, 3+4+5);
    BOOST_CHECK_EQUAL(arrs->at(1)->dims[1].size, 4+4+5);
}

BOOST_AUTO_TEST_CASE(test_DataOrderPreserved)
{
    control->write(1);
    preTrigger->write(20);
    postTrigger->write(40);
    onCond->write(1);
    offCond->write(1);
    // Should trigger on 2nd sample of 2nd array, and trigger off on 3rd sample
    onThresh->write(1001.0);
    offThresh->write(1011.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {5, 30};

    NDArray *testArray1 = incrementArray(2, dims, 0);
    NDArray *testArray2 = incrementArray(2, dims, 1000);
    NDArray *testArray3 = incrementArray(2, dims, 2000);

    rfProcess(testArray1);
    rfProcess(testArray2);
    rfProcess(testArray3);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 1);

    NDArray *opArray = arrs->front();
    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 61);
    double *pData = (double *)opArray->pData;

    // Check 1st array written correctly
    BOOST_CHECK(fabs(pData[0] - 110.0) < 0.1);
    BOOST_CHECK(fabs(pData[2] - 112.0) < 0.1);
    BOOST_CHECK(fabs(pData[5] - 120.0) < 0.1);

    // Check 2nd array written correctly
    BOOST_CHECK(fabs(pData[20*5] - 1010.0) < 0.1);
    BOOST_CHECK(fabs(pData[19*5] - 1000.0) < 0.1);
    BOOST_CHECK(fabs(pData[48*5] - 1290.0) < 0.1);

    // Check 3rd array written correctly
    BOOST_CHECK(fabs(pData[49*5] - 2000.0) < 0.1);
    BOOST_CHECK(fabs(pData[60*5] - 2110.0) < 0.1);
}

BOOST_AUTO_TEST_CASE(test_ZeroPreTrigger)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(5);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(25.0);
    offThresh->write(25.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {2, 20};
    NDArray *testArray = incrementArray(2, dims, 0);

    rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 1);

    NDArray *opArray = arrs->front();

    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 5);

    double *pData = (double *)opArray->pData;

    BOOST_CHECK(fabs(pData[0] - 30.0) < 0.1);
}

BOOST_AUTO_TEST_CASE(test_ZeroPostTrigger)
{
    control->write(1);
    preTrigger->write(6);
    postTrigger->write(0);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(75.0);
    offThresh->write(75.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {3, 30};
    NDArray *testArray = incrementArray(2, dims, 0);

    rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 1);

    NDArray *opArray = arrs->front();

    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 6);

    double *pData = (double *)opArray->pData;

    BOOST_CHECK(fabs(pData[3*5] - 70.0) < 0.1);

}

BOOST_AUTO_TEST_CASE(test_ZeroPreAndPost)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(0);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(75.0);
    offThresh->write(75.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {3, 30};
    NDArray *testArray = incrementArray(2, dims, 0);

    rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();
    int trigs;
    triggerCount->read(&trigs);

    BOOST_CHECK_EQUAL(arrs->size(), 0);
    BOOST_CHECK_EQUAL(trigs, 1);
}

BOOST_AUTO_TEST_CASE(test_PreTriggerOnBufferStart)
{
    control->write(1);
    preTrigger->write(10);
    postTrigger->write(5);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(1900.0);
    offThresh->write(1900.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {1, 5};
    NDArray *testArray1 = incrementArray(2, dims, 0);
    NDArray *testArray2 = incrementArray(2, dims, 1000);
    NDArray *testArray3 = incrementArray(2, dims, 2000);

    rfProcess(testArray1);
    rfProcess(testArray2);
    rfProcess(testArray3);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 1);

    NDArray *opArray = arrs->front();

    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 15);

    double *pData = (double *)opArray->pData;

    BOOST_CHECK(fabs(pData[0] - 0.0) < 0.1);
    BOOST_CHECK(fabs(pData[14] - 2040.0) < 0.1);
}

BOOST_AUTO_TEST_CASE(test_WindowAlignedWithFrameBoundary)
{
    control->write(1);
    preTrigger->write(5);
    postTrigger->write(8);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(1900.0);
    offThresh->write(2010.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {1, 5};
    NDArray *testArray1 = incrementArray(2, dims, 0);
    NDArray *testArray2 = incrementArray(2, dims, 1000);
    NDArray *testArray3 = incrementArray(2, dims, 2000);
    NDArray *testArray4 = incrementArray(2, dims, 3000);
    NDArray *testArray5 = incrementArray(2, dims, 4000);

    rfProcess(testArray1);
    rfProcess(testArray2);
    rfProcess(testArray3);
    rfProcess(testArray4);
    rfProcess(testArray5);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 1);

    NDArray *opArray = arrs->front();

    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 15);

    double *pData = (double *)opArray->pData;

    BOOST_CHECK(fabs(pData[0] - 1000.0) < 0.1);
    BOOST_CHECK(fabs(pData[14] - 3040.0) < 0.1);
}

BOOST_AUTO_TEST_CASE(test_PreTriggerTruncation)
{
    control->write(1);
    preTrigger->write(50);
    postTrigger->write(2);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(5.0);
    offThresh->write(5.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {3, 10};
    NDArray *testArray1 = incrementArray(2, dims, 0);

    rfProcess(testArray1);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 1);

    NDArray *opArray = arrs->front();

    BOOST_CHECK_EQUAL(opArray->dims[1].size, 3);
}

BOOST_AUTO_TEST_CASE(test_HandlesVariableSampleSizes)
{
    control->write(1);
    preTrigger->write(10);
    postTrigger->write(10);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(3015.0);
    offThresh->write(3025.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {1, 5};
    NDArray *testArray1 = incrementArray(2, dims, 0);
    dims[1] = 30;
    NDArray *testArray2 = incrementArray(2, dims, 1000);
    dims[1] = 1;
    NDArray *testArray3 = incrementArray(2, dims, 2000);
    dims[1] = 7;
    NDArray *testArray4 = incrementArray(2, dims, 3000);
    dims[1] = 60;
    NDArray *testArray5 = incrementArray(2, dims, 4000);

    rfProcess(testArray1);
    rfProcess(testArray2);
    rfProcess(testArray3);
    rfProcess(testArray4);
    rfProcess(testArray5);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 1);

    NDArray *opArray = arrs->front();

    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 21);

    double *pData = (double *)opArray->pData;

    BOOST_CHECK(fabs(pData[0] - 1230.0) < 0.1);
    BOOST_CHECK(fabs(pData[20] - 4050.0) < 0.1);
}

BOOST_AUTO_TEST_CASE(test_OverlappingTrigsFramedCorrectly)
{
    control->write(1);
    preTrigger->write(10);
    postTrigger->write(10);
    onCond->write(0);
    offCond->write(0);
    onThresh->write(-1.0);
    offThresh->write(-1.0);
    overlappingTrigs->write(1);

    size_t dims[2] = {1, 50};
    NDArray *testArray = incrementArray(2, dims, 0);
    double *pData = (double *)testArray->pData;

    pData[7] = -2.0;
    pData[10] = -2.0;
    pData[13] = -2.0;
    pData[49] = -2.0;

    rfProcess(testArray);
    rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 7);
    double *pData0, *pData1, *pData2, *pData3, *pData4;
    pData0 = (double *)arrs->at(0)->pData;
    pData1 = (double *)arrs->at(1)->pData;
    pData2 = (double *)arrs->at(2)->pData;
    pData3 = (double *)arrs->at(3)->pData;
    pData4 = (double *)arrs->at(4)->pData;

    BOOST_CHECK(fabs(pData0[0] - pData1[0]) < 0.1);
    BOOST_CHECK(fabs(pData1[12] - pData2[9]) < 0.1);
    BOOST_CHECK(fabs(pData3[8] - pData4[0]) < 0.1);
    BOOST_CHECK(fabs(pData3[12] - pData4[4]) < 0.1);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(ReframeCarryTests, ReframeFixture)

BOOST_AUTO_TEST_CASE(test_CarryBufferCorrect)
{
    control->write(1);
    preTrigger->write(2);
    postTrigger->write(1);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(100.0);
    offThresh->write(100.0);
    triggerMax->write(0.0);

    size_t dims[2] = {1, 5};
    NDArray *testArray = incrementArray(2, dims, 0);
    double *pData = (double *)testArray->pData;
    pData[2] = 101.0;

    rfProcess(testArray);
    pData[2] = 20.0;
    pData[0] = 101.0;
    rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 2);

    NDArray *opArray = arrs->back();

    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 3);

    double *pOutData = (double *)opArray->pData;
    BOOST_CHECK(fabs(pOutData[0]-30.0)<0.1);
}

BOOST_AUTO_TEST_CASE(test_HandlesMissingCarryBuffer)
{
    control->write(1);
    preTrigger->write(2);
    postTrigger->write(3);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(100.0);
    offThresh->write(100.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {1, 5};
    NDArray *testArray = incrementArray(2, dims, 0);
    double *pData = (double *)testArray->pData;
    pData[3] = 101.0;

    int frames;
    storedFrames->read(&frames);
    BOOST_CHECK_EQUAL(frames, 0);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(ReframeAttributeTests, ReframeFixture)

BOOST_AUTO_TEST_CASE(test_AttributesFromFirstArray)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(10);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(-1.0);
    offThresh->write(-1.0);

    size_t dims[2] = {1, 3};
    NDArray *arr1 = emptyArray(2, dims);
    NDArray *arr2 = emptyArray(2, dims);
    NDArray *arr3 = emptyArray(2, dims);

    int at1 = 437;
    int at2 = 3;
    int at3 = 999999;

    arr1->pAttributeList->add("TestAttribute", "", NDAttrInt32, &at1);
    arr2->pAttributeList->add("TestAttribute", "", NDAttrInt32, &at2);
    arr3->pAttributeList->add("TestAttribute", "", NDAttrInt32, &at3);

    rfProcess(arr1);
    rfProcess(arr2);
    rfProcess(arr3);
    rfProcess(arr3);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 1);

    NDArray *opArray = arrs->back();
    int val;
    NDAttribute *opAttr = opArray->pAttributeList->find("TestAttribute");
    BOOST_REQUIRE(opAttr);
    opAttr->getValue(NDAttrInt32, &val, 1);
    BOOST_CHECK_EQUAL(val, at1);
}

BOOST_AUTO_TEST_CASE(test_AttributesFromCarryArray)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(3);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(-1.0);
    offThresh->write(-1.0);
    triggerMax->write(0.0);

    size_t dims[2] = {1, 5};
    NDArray *arr1 = emptyArray(2, dims);
    NDArray *arr2 = emptyArray(2, dims);
    NDArray *arr3 = emptyArray(2, dims);

    int at1 = 437;
    int at2 = 3;
    int at3 = 999999;

    arr1->pAttributeList->add("TestAttribute", "", NDAttrInt32, &at1);
    arr2->pAttributeList->add("TestAttribute", "", NDAttrInt32, &at2);
    arr3->pAttributeList->add("TestAttribute", "", NDAttrInt32, &at3);

    rfProcess(arr1);
    rfProcess(arr2);
    rfProcess(arr3);
    rfProcess(arr3);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 6);

    NDArray *opArray = arrs->at(3);
    int val;
    NDAttribute *opAttr = opArray->pAttributeList->find("TestAttribute");
    BOOST_REQUIRE(opAttr);
    opAttr->getValue(NDAttrInt32, &val, 1);
    BOOST_CHECK_EQUAL(val, at2);
}

BOOST_AUTO_TEST_CASE(test_TimestampCorrect)
{
    control->write(1);
    preTrigger->write(0);
    postTrigger->write(7);
    onCond->write(1);
    offCond->write(1);
    onThresh->write(-1.0);
    offThresh->write(-1.0);
    triggerMax->write(0.0);

    size_t dims[2] = {1, 5};
    NDArray *arr1 = emptyArray(2, dims);
    NDArray *arr2 = emptyArray(2, dims);
    NDArray *arr3 = emptyArray(2, dims);

    rfProcess(arr1);
    rfProcess(arr2);
    rfProcess(arr3);
    rfProcess(arr3);

    deque<NDArray *> *arrs = ds->arrays();

    BOOST_REQUIRE_EQUAL(arrs->size(), 2);

    BOOST_CHECK(fabs(arrs->front()->timeStamp - arr1->timeStamp) < 0.01);
    //BOOST_CHECK_EQUAL(arrs->front()->epicsTS, arr1->epicsTS);
    BOOST_CHECK(fabs(arrs->back()->timeStamp - arr2->timeStamp) < 0.01);
    //BOOST_CHECK_EQUAL(arrs->back()->epicsTS, arr2->epicsTS);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(ReframeErrorTests, ReframeFixture)

BOOST_AUTO_TEST_CASE(test_WrongNDims)
{
    control->write(1);
    preTrigger->write(2);
    postTrigger->write(3);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(100.0);
    offThresh->write(100.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {1, 5};
    NDArray *testArray = incrementArray(2, dims, 0);
    double *pData = (double *)testArray->pData;
    pData[3] = 101.0;

    size_t badDims[3] = {2,3,4};
    NDArray *badArray = arrayPool->alloc(3, badDims, NDFloat64, 0, NULL);

    rfProcess(testArray);
    rfProcess(badArray);

    deque<NDArray *> *arrs = ds->arrays();
    BOOST_REQUIRE_EQUAL(arrs->size(), 1);
    NDArray *opArray = arrs->back();
    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 4);
    int frames;
    storedFrames->read(&frames);
    BOOST_CHECK_EQUAL(frames, 1);
}

BOOST_AUTO_TEST_CASE(test_InconsistentChannelNum)
{
    control->write(1);
    preTrigger->write(2);
    postTrigger->write(3);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(100.0);
    offThresh->write(100.0);
    triggerMax->write(1.0);
    triggerMode->write(0);

    size_t dims[2] = {1, 5};
    NDArray *testArray = incrementArray(2, dims, 0);
    double *pData = (double *)testArray->pData;
    pData[3] = 101.0;

    size_t badDims[2] = {2, 5};
    NDArray *badArray = arrayPool->alloc(2, badDims, NDFloat64, 0, NULL);

    rfProcess(testArray);
    rfProcess(badArray);

    deque<NDArray *> *arrs = ds->arrays();
    BOOST_REQUIRE_EQUAL(arrs->size(), 1);
    NDArray *opArray = arrs->back();
    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 4);
    int frames;
    storedFrames->read(&frames);
    BOOST_CHECK_EQUAL(frames, 1);
}

BOOST_AUTO_TEST_CASE(test_NoTriggerChannel)
{
    control->write(1);
    preTrigger->write(2);
    postTrigger->write(3);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(100.0);
    offThresh->write(100.0);
    triggerMax->write(1.0);
    triggerMode->write(0);
    triggerChannel->write(5);

    size_t dims[2] = {1, 5};
    NDArray *testArray = incrementArray(2, dims, 0);
    double *pData = (double *)testArray->pData;
    pData[3] = 101.0;

    rfProcess(testArray);

    deque<NDArray *> *arrs = ds->arrays();
    BOOST_REQUIRE_EQUAL(arrs->size(), 0);
    int frames;
    storedFrames->read(&frames);
    BOOST_CHECK_EQUAL(frames, 0);
}

BOOST_AUTO_TEST_CASE(test_WrongDataType)
{
    control->write(1);
    preTrigger->write(2);
    postTrigger->write(3);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(100.0);
    offThresh->write(100.0);
    triggerMax->write(1.0);
    triggerMode->write(0);


    size_t dims[2] = {1, 5};
    NDArray *testArray = incrementArray(2, dims, 0);
    double *pData = (double *)testArray->pData;
    pData[3] = 101.0;

    NDArray *badArray = arrayPool->alloc(2, dims, NDUInt8, 0, NULL);

    rfProcess(testArray);
    rfProcess(badArray);

    badArray->release();
    deque<NDArray *> *arrs = ds->arrays();
    BOOST_REQUIRE_EQUAL(arrs->size(), 1);
    NDArray *opArray = arrs->back();
    BOOST_REQUIRE_EQUAL(opArray->dims[1].size, 4);
    int frames;
    storedFrames->read(&frames);
    BOOST_CHECK_EQUAL(frames, 1);
}

BOOST_AUTO_TEST_CASE(test_HandlesUInt8)
{
    control->write(1);
    preTrigger->write(2);
    postTrigger->write(3);
    onCond->write(1.0);
    offCond->write(1.0);
    onThresh->write(100.0);
    offThresh->write(100.0);
    triggerMax->write(1.0);
    triggerMode->write(0);


    size_t dims[2] = {3,20};
    NDArray *testArray = arrayPool->alloc(2, dims, NDUInt8, 0, NULL);
    uint8_t *pData = (uint8_t *)testArray->pData;
    pData[3*5] = 101;

    rfProcess(testArray);
    testArray->release();

    deque<NDArray *> *arrs = ds->arrays();
    BOOST_REQUIRE_EQUAL(arrs->size(), 1);
    NDArray *opArray = arrs->back();
    BOOST_CHECK_EQUAL(opArray->dims[1].size, 5);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
