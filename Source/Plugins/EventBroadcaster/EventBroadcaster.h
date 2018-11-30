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
//#include "json.h"
//#include "json-forwards.h"

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

    void sendEvent(const InfoObjectCommon* channel, const MidiMessage& msg) const;

    // add metadata from an event to a DynamicObject
    static void populateMetaData(const MetaDataEventObject* channel,
        const EventBasePtr event, DynamicObject::Ptr dest);

    // get metadata in a form we can add to the JSON object
    template <typename T>
    static var metaDataValueToVar(const MetaDataValue* valuePtr);

    // specialization for strings
    template <>
    static var metaDataValueToVar<char>(const MetaDataValue* valuePtr);

    // send our envelope and JSON obj on the socket
    static bool sendPackage(void* socket, const char* envelopeStr, const char* jsonStr);

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
