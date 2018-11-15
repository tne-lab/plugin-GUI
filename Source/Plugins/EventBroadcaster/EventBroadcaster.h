/*
  ==============================================================================

    EventBroadcaster.h
    Created: 22 May 2015 3:31:50pm
    Author:  Christopher Stawarz

  ==============================================================================
*/

#ifndef EVENTBROADCASTER_H_INCLUDED
#define EVENTBROADCASTER_H_INCLUDED

#include <ProcessorHeaders.h>

#ifdef ZEROMQ
    #ifdef WIN32
        #include <zmq.h>
        #include <zmq_utils.h>
    #else
        #include <zmq.h>
    #endif
#endif

#include <memory>

class EventBroadcaster : public GenericProcessor
{
public:
    EventBroadcaster();

    AudioProcessorEditor* createEditor() override;

    int getListeningPort() const;
    // returns 0 on success, else the errno value for the error that occurred.
    int setListeningPort (int port, bool forceRestart = false);

    void process (AudioSampleBuffer& continuousBuffer) override;
    void handleEvent (const EventChannel* channelInfo, const MidiMessage& event, int samplePosition = 0) override;
	void handleSpike(const SpikeChannel* channelInfo, const MidiMessage& event, int samplePosition = 0) override;

    void saveCustomParametersToXml (XmlElement* parentElement) override;
    void loadCustomParametersFromXml() override;

private:
    class ZMQContext : public ReferenceCountedObject
    {
    public:
        ZMQContext(const ScopedLock& lock);
        ~ZMQContext() override;
        void* createZMQSocket();
    private:
        void* context;
    };

    static void closeZMQSocket(void* socket);

    class ZMQSocketPtr : public std::unique_ptr<void, decltype(&closeZMQSocket)>
    {
    public:
        ZMQSocketPtr();
        ~ZMQSocketPtr();
    private:
        ReferenceCountedObjectPtr<ZMQContext> context;
    };
    
    int unbindZMQSocket();
    int rebindZMQSocket();

    /*
    * First part of message("envelope") is important b/c subscribers can't use any later parts for filtering.
    * Should include information relevant for subscribers to decide whether they need this event.
    * See: http://zguide.zeromq.org/page:all#Pub-Sub-Message-Envelopes
    * 
    * Structure (as a *packed* byte array, i.e. no alignment):
    * - baseType   (uint8)        - either PROCESSOR_EVENT (1) or SPIKE_EVENT (2)).
    * - index      (uint16)       - corresponds to TTL channel for TTL events, whatever was assigned
    *                               to "channel" for other processor events, and the sorted ID for spikes.
    * - identifier (utf-8 string) - the channel's "machine-readable data identifier" (no null terminator).
    */
    class Envelope : public MemoryBlock
    {
    public:
        Envelope(uint8 baseType, uint16 index, const String& identifier);
    };

	void sendEvent(const InfoObjectCommon* channel, const MidiMessage& event) const;
    
    // returns true on success, false on failure
    template <typename T>
    bool sendMetaDataValue(const MetaDataValue* valuePtr) const;

    // special specialization for strings
    bool sendStringMetaData(const String& valueString) const;

    static String getEndpoint(int port);
    // called from getListeningPort() depending on success/failure of ZMQ operations
    void reportActualListeningPort(int port);

    // share a "dumb" pointer that doesn't take part in reference counting.
    // want the context to be terminated by the time the static members are
    // destroyed (see: https://github.com/zeromq/libzmq/issues/1708)
    static ZMQContext* sharedContext;
    static CriticalSection sharedContextLock;
    ZMQSocketPtr zmqSocket;
    int listeningPort;
};


#endif  // EVENTBROADCASTER_H_INCLUDED
