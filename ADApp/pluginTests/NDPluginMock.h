/*
 * NDPluginMock.h
 *
 *  Created on: 11 Mar 2015
 *      Author: qvr31998
 */

#ifndef ADAPP_PLUGINTESTS_NDPLUGINMOCK_H_
#define ADAPP_PLUGINTESTS_NDPLUGINMOCK_H_

#include "NDPluginDriver.h"

#include <deque>
using namespace std;



// Mock NDPlugin; simply stores all received NDArrays and provides them to a client on request.
class NDPluginMock : public NDPluginDriver {
public:
    NDPluginMock (const char *portName, int queueSize, int blockingCallbacks,
                                  const char *NDArrayPort, int NDArrayAddr,
                                  int maxBuffers, size_t maxMemory,
                                  int priority, int stackSize);
    ~NDPluginMock();
    void processCallbacks(NDArray *pArray);
    deque<NDArray *> *arrays();
private:
    deque<NDArray *> *arrays_;
};



#endif /* ADAPP_PLUGINTESTS_NDPLUGINMOCK_H_ */
