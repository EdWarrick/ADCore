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

    asynInt32Client *enableCallbacks;
    asynInt32Client *blockingCallbacks;

    static int testCase;

    PluginFixture()
    {
        arrayPool = new NDArrayPool(100, 0);

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

        enableCallbacks->write(1);
        blockingCallbacks->write(1);

        testCase++;
    }
    ~PluginFixture()
    {
        delete blockingCallbacks;
        delete enableCallbacks;
        delete ds;
        delete rf;
        delete driver;
        delete arrayPool;
    }
    void rfProcess(NDArray *pArray)
    {
        rf->lock();
        rf->processCallbacks(pArray);
        rf->unlock();
    }
};

int PluginFixture::testCase = 0;

BOOST_FIXTURE_TEST_SUITE(ReframeTests, PluginFixture)

BOOST_AUTO_TEST_CASE(test1)
{
    BOOST_CHECK_EQUAL(1,1);
}

BOOST_AUTO_TEST_CASE(test2)
{
    BOOST_CHECK_EQUAL(1,0);
}

BOOST_AUTO_TEST_SUITE_END()


