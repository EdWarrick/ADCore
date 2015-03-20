#ifndef NDPluginReframe_H
#define NDPluginReframe_H

#include <deque>

#include <epicsTypes.h>
#include <asynStandardInterfaces.h>

#include "NDPluginDriver.h"

typedef enum NDPluginReframeMode {
    Idle,
    Armed,
    Gating,
    Acquiring
} NDPluginReframeMode;

/* Param definitions */
#define NDPluginReframeControlString               "REFRAME_CONTROL"       /* (asynInt32,        r/w) Arm plugin */
#define NDPluginReframeStatusString                "REFRAME_STATUS"        /* (asynOctetRead,    r/o) Status */
#define NDPluginReframeSoftTriggerString           "REFRAME_SOFT_TRIGGER"  /* (asynInt32,        r/w) Force a soft trigger */
// ###TODO: Don't think this does anything. Replace with mode variable and implement some useful readback params.
#define NDPluginReframeTriggeredString             "REFRAME_TRIGGERED"     /* (asynInt32,        r/o) Have we had a trigger event */
#define NDPluginReframeTriggerDimensionString      "REFRAME_TRIGGER_DIM"   /* (asynInt32,        r/o) Dimension to use as time dimension */
#define NDPluginReframeTriggerChannelString        "REFRAME_TRIGGER_CHAN"  /* (asynInt32,        r/w) Channel to use as trigger channel */
#define NDPluginReframePreTriggerSamplesString     "REFRAME_PRE_SAMPLES"   /* (asynInt32,        r/w) Number of pre-trigger counts to output */
#define NDPluginReframePostTriggerSamplesString    "REFRAME_POST_SAMPLES"  /* (asynInt32,        r/w) Number of post-trigger counts to output */
#define NDPluginReframeTriggerStartConditionString "REFRAME_START_COND"    /* (asynInt32,        r/w) Condition to check to determine start of trigger */
#define NDPluginReframeTriggerEndConditionString   "REFRAME_END_COND"      /* (asynInt32,        r/w) Condition to check to determine end of trigger */
#define NDPluginReframeTriggerStartThresholdString "REFRAME_START_THRESH"  /* (asynFloat64,      r/w) Threshold for trigger start */
#define NDPluginReframeTriggerEndThresholdString   "REFRAME_END_THRESH"    /* (asynFloat64,      r/w) Threshold for trigger end */
#define NDPluginReframeTriggerMaxString            "REFRAME_TRIGGER_MAX"   /* (asynInt32,        r/w) Number of triggers/gates. ADC will disarm once
                                                                                                              reached. Set to 0 for continuous re-arm */
#define NDPluginReframeTriggerCountString          "REFRAME_TRIGGER_COUNT" /* (asynInt32,        r/o) Triggers detected so far */
#define NDPluginReframeTriggerTotalString          "REFRAME_TRIGGER_TOTAL" /* (asynInt32,        r/o) Total number of triggers output. Used to generate the
                                                                                                              uniqueID for the output frames */
#define NDPluginReframeTriggerEndedString          "REFRAME_TRIGGER_ENDED" /* (asynInt32,        r/o) Has end of gate been seen? Counterpart to
                                                                                                              NDPluginReframeTriggered */
#define NDPluginReframeBufferFramesString          "REFRAME_BUFFER_FRAMES" /* (asynInt32,        r/o) Number of arrays stored in buffer */
#define NDPluginReframeBufferSamplesString         "REFRAME_BUFFER_SAMPLES"/* (asynInt32,        r/o) Number of samples stored in buffer */

class epicsShareClass NDPluginReframe : public NDPluginDriver {
public:
    NDPluginReframe(const char *portName, int queueSize, int blockingCallbacks,
                 const char *NDArrayPort, int NDArrayAddr,
                 int maxBuffers, size_t maxMemory,
                 int priority, int stackSize);
    /* These methods override the virtual methods in the base class */
    void processCallbacks(NDArray *pArray);
    asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

protected:
    /* Both modes */
    int NDPluginReframeControl;
    #define FIRST_NDPLUGIN_REFRAME_PARAM NDPluginReframeControl
    int NDPluginReframeSoftTrigger;
    int NDPluginReframeTriggered;
    int NDPluginReframeStatus;

    int NDPluginReframeTriggerDimension; // Hard-code to 1 for now
    int NDPluginReframeTriggerChannel;
    int NDPluginReframePreTriggerSamples;
    int NDPluginReframePostTriggerSamples;
    int NDPluginReframeTriggerStartCondition;
    int NDPluginReframeTriggerEndCondition;
    int NDPluginReframeTriggerStartThreshold;
    int NDPluginReframeTriggerEndThreshold;
    int NDPluginReframeTriggerMax;
    int NDPluginReframeTriggerEnded;
    int NDPluginReframeTriggerCount;
    int NDPluginReframeTriggerTotal;
    int NDPluginReframeBufferFrames;
    int NDPluginReframeBufferSamples;
    #define LAST_NDPLUGIN_REFRAME_PARAM NDPluginReframeBufferSamples

private:
    // Methods
    int containsTriggerStart();                    // Search for gate start in current buffer and return true if found. Sets gateStartOffset_.
    int containsTriggerEnd();                      // Search for gate end in current buffer and return true if found. Sets gateEndOffset_.
    NDArray *constructOutput();               // Create a single output NDArray from the arrays stored in preBuffer_.
    int bufferSizeCounts(int start);          // Utility function; walks the NDArray buffer from the given start array to the end and returns the total size in counts.
    int arrayIsValid(NDArray *pArray);         // Checks that input arrays have the expected number of dimensions & the number of channels is consistent.

    // Data
    std::deque<NDArray *> *arrayBuffer_;
    int triggerStartOffset_;
    int triggerEndOffset_;
    NDPluginReframeMode mode_;
};
#define NUM_NDPLUGIN_REFRAME_PARAMS ((int)(&LAST_NDPLUGIN_REFRAME_PARAM - &FIRST_NDPLUGIN_REFRAME_PARAM + 1))

#endif
