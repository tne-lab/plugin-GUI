
#ifndef __SPIKEDETECTORDYNAMIC_H_3F920F95__
#define __SPIKEDETECTORDYNAMIC_H_3F920F95__

#include <ProcessorHeaders.h>
#include "SpikeDetectorDynamicEditor.h"
#include <ProcessorHeaders.h>

struct SimpleElectrode
{
    String name;

    int numChannels;
    int prePeakSamples, postPeakSamples;
    int lastBufferIndex;
    bool isMonitored;
    int electrodeID;
    int sourceNodeId;

    HeapBlock<int> channels;
    HeapBlock<double> thresholds;
    HeapBlock<bool> isActive;

};

class SpikeDetectorDynamicEditor;

/**
  Detects spikes in a continuous signal using dynamic thresholds and outputs events containing the spike data.
*/

class SpikeDetectorDynamic : public GenericProcessor
{
public:
    // CONSTRUCTOR AND DESTRUCTOR //
    /** constructor */
	SpikeDetectorDynamic();
    /** destructor */
	~SpikeDetectorDynamic();

    // PROCESSOR METHODS //

    /** Processes an incoming continuous buffer and places new
        spikes into the event buffer. */
    void process(AudioSampleBuffer& buffer, MidiBuffer& events);

    /** Used to alter parameters of data acquisition. */
    void setParameter(int parameterIndex, float newValue);

    /** Called whenever the signal chain is altered. */
    void updateSettings();

    /** Called prior to start of acquisition. */
    bool enable();

    /** Called after acquisition is finished. */
    bool disable();

    /** Creates the SpikeDetectorEditor. */
    AudioProcessorEditor* createEditor();


    // INTERNAL BUFFERS //

    /** Extra samples are placed in this buffer to allow seamless
        transitions between callbacks. */
    AudioSampleBuffer overflowBuffer;

    // CREATE AND DELETE ELECTRODES //

    /** Adds an electrode with n channels to be processed. */
    bool addElectrode(int nChans, int electrodeID = 0);

    /** Removes an electrode with a given index. */
    bool removeElectrode(int index);

    // EDIT AND QUERY ELECTRODE SETTINGS //

    /** Returns the number of channels for a given electrode. */
    int getNumChannels(int index);

    /** Edits the mapping between input channels and electrode channels. */
    void setChannel(int electrodeIndex, int channelNum, int newChannel);

    /** Returns the continuous channel that maps to a given
    	electrode channel. */
    int getChannel(int index, int chan);

    /** Sets the name of a given electrode. */
    void setElectrodeName(int index, String newName);

    /** */
    void setChannelActive(int electrodeIndex, int channelNum, bool active);

    /** */
    bool isChannelActive(int electrodeIndex, int channelNum);

    // RETURN STRING ARRAYS //

    /** Returns a StringArray containing the names of all electrodes */
    StringArray getElectrodeNames();

    /** Returns array of electrodes. */
	void getElectrodes(Array<SimpleElectrode*>& electrodeArray);

    /** Returns array of electrodes. */
    SimpleElectrode* getActiveElectrode();

    /** Sets the current electrode index */
    SimpleElectrode* setCurrentElectrodeIndex(int);

    /** Returns a list of possible electrode types (e.g., stereotrode, tetrode). */
    StringArray electrodeTypes;

    void setChannelThreshold(int electrodeNum, int channelNum, float threshold);

    double getChannelThreshold(int electrodeNum, int channelNum);

    void saveCustomParametersToXml(XmlElement* parentElement);
    void loadCustomParametersFromXml();

private:
    /** Pointer to a continuous buffer. */
    AudioSampleBuffer* dataBuffer;

    float getDefaultThreshold();

    int overflowBufferSize;

    int sampleIndex;

    Array<int> electrodeCounter;

    float getNextSample(int& chan);
    float getCurrentSample(int& chan);
    bool samplesAvailable(int nSamples);

    Array<bool> useOverflowBuffer;

    int currentElectrode;
    int currentChannelIndex;
    int currentIndex;

	HeapBlock<uint8_t> spikeBuffer;
    int64 timestamp;

    OwnedArray<SimpleElectrode> electrodes;
    int uniqueID;

    void handleEvent(int eventType, MidiMessage& event, int sampleNum);

    void addSpikeEvent(SpikeObject* s, MidiBuffer& eventBuffer, int peakIndex);
    void addWaveformToSpikeObject(SpikeObject* s,
                                  int& peakIndex,
                                  int& electrodeNumber,
                                  int& currentChannel,
								  int dyn_threshold);

    void resetElectrode(SimpleElectrode*);
    
    uint16_t sampleRateForElectrode;
	int window_size;
	float scalar = 0.6745f;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpikeDetectorDynamic);
};



#endif  // __SPIKEDETECTORDYNAMIC_H_3F920F95__
