#ifndef NDPluginCircularBuff_H
#define NDPluginCircularBuff_H

#include <deque>

#include <epicsTypes.h>
#include <asynStandardInterfaces.h>

#include "NDPluginDriver.h"

/* Param definitions */
#define NDPluginCircularBuffControlString               "CIRCULAR_BUFF_CONTROL"       /* (asynInt32,        r/w) Run scope? */
#define NDPluginCircularBuffStatusString                "CIRCULAR_BUFF_STATUS"        /* (asynOctetRead,    r/o) Scope status */
#define NDPluginCircularBuffPreTriggerString            "CIRCULAR_BUFF_PRE_TRIGGER"   /* (asynInt32,        r/w) Number of pre-trigger images */
#define NDPluginCircularBuffPostTriggerString           "CIRCULAR_BUFF_POST_TRIGGER"  /* (asynInt32,        r/w) Number of post-trigger images */
#define NDPluginCircularBuffCurrentImageString          "CIRCULAR_BUFF_CURRENT_IMAGE" /* (asynInt32,        r/o) Number of the current image */
#define NDPluginCircularBuffPostCountString             "CIRCULAR_BUFF_POST_COUNT"    /* (asynInt32,        r/o) Number of the current post count image */
#define NDPluginCircularBuffSoftTriggerString           "CIRCULAR_BUFF_SOFT_TRIGGER"  /* (asynInt32,        r/w) Force a soft trigger */
#define NDPluginCircularBuffTriggeredString             "CIRCULAR_BUFF_TRIGGERED"     /* (asynInt32,        r/o) Have we had a trigger event */
#define NDPluginCircularBuffADCModeString               "CIRCULAR_BUFF_ADC_MODE"      /* (asynInt32,        r/w) Camera mode (0) or ADC mode (1) */
#define NDPluginCircularBuffTriggerDimensionString      "CIRCULAR_BUFF_TRIGGER_DIM"   /* (asynInt32,        r/o) Dimension to use as time dimension */
#define NDPluginCircularBuffTriggerChannelString        "CIRCULAR_BUFF_TRIGGER_CHAN"  /* (asynInt32,        r/w) Channel to use as trigger channel */
#define NDPluginCircularBuffPreTriggerSamplesString     "CIRCULAR_BUFF_PRE_SAMPLES"   /* (asynInt32,        r/w) Number of pre-trigger counts to output */
#define NDPluginCircularBuffPostTriggerSamplesString    "CIRCULAR_BUFF_POST_SAMPLES"  /* (asynInt32,        r/w) Number of post-trigger counts to output */
#define NDPluginCircularBuffTriggerStartConditionString "CIRCULAR_BUFF_START_COND"    /* (asynInt32,        r/w) Condition to check to determine start of trigger */
#define NDPluginCircularBuffTriggerEndConditionString   "CIRCULAR_BUFF_END_COND"      /* (asynInt32,        r/w) Condition to check to determine end of trigger */
#define NDPluginCircularBuffTriggerStartThresholdString "CIRCULAR_BUFF_START_THRESH"  /* (asynFloat64,      r/w) Threshold for trigger start */
#define NDPluginCircularBuffTriggerEndThresholdString   "CIRCULAR_BUFF_END_THRESH"    /* (asynFloat64,      r/w) Threshold for trigger end */
#define NDPluginCircularBuffTriggerMaxString            "CIRCULAR_BUFF_TRIGGER_MAX"   /* (asynInt32,        r/w) Number of triggers/gates. ADC will disarm once
													      reached. Set to 0 for continuous re-arm */
#define NDPluginCircularBuffTriggerEndedString          "CIRCULAR_BUFF_TRIGGER_ENDED" /* (asynInt32,        r/o) Has end of gate been seen? Counterpart to
													      NDPluginCircularBuffTriggered */
#define NDPluginCircularBuffTriggerCountString          "CIRCULAR_BUFF_TRIGGER_COUNT" /* (asynInt32,        r/o) Triggers detected so far */

#define NDPluginCircularBuffTriggeredAttribute       "ExternalTrigger"

/** Performs a scope like capture.  Records a quantity
  * of pre-trigger and post-trigger images
  */
class epicsShareClass NDPluginCircularBuff : public NDPluginDriver {
public:
    NDPluginCircularBuff(const char *portName, int queueSize, int blockingCallbacks,
                 const char *NDArrayPort, int NDArrayAddr,
                 int maxBuffers, size_t maxMemory,
                 int priority, int stackSize);
    /* These methods override the virtual methods in the base class */
    void processCallbacks(NDArray *pArray);
    asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    
    //template <typename epicsType> asynStatus doProcessCircularBuffT(NDArray *pArray);
    //asynStatus doProcessCircularBuff(NDArray *pArray);
   
protected:
    /* Both modes */
    int NDPluginCircularBuffControl;
    #define FIRST_NDPLUGIN_CIRCULAR_BUFF_PARAM NDPluginCircularBuffControl
    int NDPluginCircularBuffSoftTrigger;
    int NDPluginCircularBuffTriggered;
    int NDPluginCircularBuffStatus;
    int NDPluginCircularBuffADCMode;

    /* Scope only */
    int NDPluginCircularBuffPreTrigger;
    int NDPluginCircularBuffPostTrigger;
    int NDPluginCircularBuffCurrentImage;
    int NDPluginCircularBuffPostCount;

    /* ADC only */
    int NDPluginCircularBuffTriggerDimension; // Hard-code to 1 for now
    int NDPluginCircularBuffTriggerChannel;
    int NDPluginCircularBuffPreTriggerSamples;
    int NDPluginCircularBuffPostTriggerSamples;
    int NDPluginCircularBuffTriggerStartCondition;
    int NDPluginCircularBuffTriggerEndCondition;
    int NDPluginCircularBuffTriggerStartThreshold;
    int NDPluginCircularBuffTriggerEndThreshold;
    int NDPluginCircularBuffTriggerMax;
    int NDPluginCircularBuffTriggerEnded;
    int NDPluginCircularBuffTriggerCount;
    #define LAST_NDPLUGIN_CIRCULAR_BUFF_PARAM NDPluginCircularBuffTriggerCount
                                
private:
    int containsTriggerStart();                    // Search for gate start in current buffer and return true if found. Sets gateStartOffset_.
    int containsTriggerEnd();                      // Search for gate end in current buffer and return true if found. Sets gateEndOffset_.
    NDArray *constructOutput();               // Create a single output NDArray from the arrays stored in preBuffer_.
    int bufferSizeCounts(int start); // Utility function; walks the NDArray buffer from the given start array to the end and returns the total size in counts.

    std::deque<NDArray *> *arrayBuffer_;
    NDArray *pOldArray_;
    int previousTrigger_;
    int maxBuffers_;

    int triggerStartOffset_;
    int triggerEndOffset_;
};
#define NUM_NDPLUGIN_CIRCULAR_BUFF_PARAMS ((int)(&LAST_NDPLUGIN_CIRCULAR_BUFF_PARAM - &FIRST_NDPLUGIN_CIRCULAR_BUFF_PARAM + 1))
    
#endif

