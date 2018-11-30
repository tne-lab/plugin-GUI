/*
  ==============================================================================

    EventBroadcaster.cpp
    Created: 22 May 2015 3:31:50pm
    Author:  Christopher Stawarz

  ==============================================================================
*/

#include "EventBroadcaster.h"
#include "EventBroadcasterEditor.h"
#include <string>

EventBroadcaster::ZMQContext* EventBroadcaster::sharedContext = nullptr;
CriticalSection EventBroadcaster::sharedContextLock{};

EventBroadcaster::ZMQContext::ZMQContext(const ScopedLock& lock)
#ifdef ZEROMQ
    : context(zmq_ctx_new())
#endif
{
    sharedContext = this;
}

// ZMQContext is a ReferenceCountedObject with a pointer in each instance's 
// socket pointer, so this only happens when the last instance is destroyed.
EventBroadcaster::ZMQContext::~ZMQContext()
{
    ScopedLock lock(sharedContextLock);
    sharedContext = nullptr;
#ifdef ZEROMQ
    zmq_ctx_destroy(context);
#endif
}

void* EventBroadcaster::ZMQContext::createZMQSocket()
{
#ifdef ZEROMQ
    jassert(context != nullptr);
    return zmq_socket(context, ZMQ_PUB);
#else
    jassertfalse; // should never be called in this case
    return nullptr;
#endif
}

EventBroadcaster::ZMQSocketPtr::ZMQSocketPtr()
    : std::unique_ptr<void, decltype(&closeZMQSocket)>(nullptr, &closeZMQSocket)
{
    ScopedLock lock(sharedContextLock);
    if (sharedContext == nullptr)
    {
        // first one, create the context
        context = new ZMQContext(lock);
    }
    else
    {
        // use already-created context
        context = sharedContext;
    }

#ifdef ZEROMQ
    reset(context->createZMQSocket());
#endif
}

EventBroadcaster::ZMQSocketPtr::~ZMQSocketPtr()
{
    // close the socket before the context might get destroyed.
    reset(nullptr);
}

int EventBroadcaster::unbindZMQSocket()
{
#ifdef ZEROMQ
    void* socket = zmqSocket.get();
    if (socket != nullptr && listeningPort != 0)
    {
        return zmq_unbind(socket, getEndpoint(listeningPort).toRawUTF8());
    }
#endif
    return 0;
}

int EventBroadcaster::rebindZMQSocket()
{
#ifdef ZEROMQ
    void* socket = zmqSocket.get();
    if (socket != nullptr && listeningPort != 0)
    {
        return zmq_bind(socket, getEndpoint(listeningPort).toRawUTF8());
    }
#endif
    return 0;
}

void EventBroadcaster::closeZMQSocket(void* socket)
{
#ifdef ZEROMQ
    zmq_close(socket);
#endif
}

String EventBroadcaster::getEndpoint(int port)
{
    return String("tcp://*:") + String(port);
}

void EventBroadcaster::reportActualListeningPort(int port)
{
    listeningPort = port;
    auto editor = static_cast<EventBroadcasterEditor*>(getEditor());
    if (editor)
    {
        editor->setDisplayedPort(port);
    }
}

EventBroadcaster::EventBroadcaster()
    : GenericProcessor  ("Event Broadcaster")
    , listeningPort     (0)
{
    setProcessorType (PROCESSOR_TYPE_SINK);

    int portToTry = 5557;
    while (setListeningPort(portToTry) == EADDRINUSE)
    {
        // try the next port, looking for one not in use
        portToTry++;
    }
}


AudioProcessorEditor* EventBroadcaster::createEditor()
{
    editor = new EventBroadcasterEditor(this, true);
    return editor;
}


int EventBroadcaster::getListeningPort() const
{
    return listeningPort;
}


int EventBroadcaster::setListeningPort(int port, bool forceRestart)
{
    if (listeningPort == port && !forceRestart)
    {
        return 0;
    }

#ifdef ZEROMQ
    // unbind current socket (if any) to free up port
    unbindZMQSocket();
    ZMQSocketPtr newSocket;
    auto editor = static_cast<EventBroadcasterEditor*>(getEditor());
    int status = 0;

    if (!newSocket.get())
    {
        status = zmq_errno();
        std::cout << "Failed to create socket: " << zmq_strerror(status) << std::endl;
    }
    else
    {
        if (0 != zmq_bind(newSocket.get(), getEndpoint(port).toRawUTF8()))
        {
            status = zmq_errno();
            std::cout << "Failed to open socket: " << zmq_strerror(status) << std::endl;
        }
        else
        {
            // success
            zmqSocket.swap(newSocket);
            reportActualListeningPort(port);
            return status;
        }
    }

    // failure, try to rebind current socket to previous port
    if (0 == rebindZMQSocket())
    {
        reportActualListeningPort(listeningPort);
    }
    else
    {
        reportActualListeningPort(0);
    }
    return status;

#else
    reportActualListeningPort(port);
    return 0;
#endif
}


void EventBroadcaster::process(AudioSampleBuffer& continuousBuffer)
{
    checkForEvents(true);
}

void EventBroadcaster::sendEvent(const InfoObjectCommon* channel, const MidiMessage& msg) const
{
#ifdef ZEROMQ
    void* socket = zmqSocket.get();

    // TODO Create a procotol that has outline for every type of event

    // common info that isn't type-specific
    EventType baseType = Event::getBaseType(msg);
    const String& identifier = channel->getIdentifier();
    float sampleRate = channel->getSampleRate();
    int64 timestamp = Event::getTimestamp(msg);
    
    // info to be assigned depending on the event type
    String envelope;
    DynamicObject::Ptr message = new DynamicObject();
    EventBasePtr baseEvent;
    const MetaDataEventObject* metaDataChannel;

    // Add common info to JSON
    // Still sending these guys as float/doubles for now. Might change in future.
    DynamicObject::Ptr timing = new DynamicObject();
    timing->setProperty("sampleRate", sampleRate);
    timing->setProperty("timestamp", timestamp);
    message->setProperty("timing", timing.get());

    message->setProperty("identifier", identifier);
    message->setProperty("name", channel->getName());
    
    // deserialize event and get type-specific information
    switch (baseType)
    {
    case SPIKE_EVENT:
    {
        auto spikeChannel = static_cast<const SpikeChannel*>(channel);
        metaDataChannel = static_cast<const MetaDataEventObject*>(spikeChannel);

        baseEvent = SpikeEvent::deserializeFromMessage(msg, spikeChannel).release();
        auto spike = static_cast<SpikeEvent*>(baseEvent.get());

        // create envelope
        uint16 sortedID = spike->getSortedID();
        envelope = "spike/sortedid:" + String(sortedID) + "/id:" + identifier + "/ts:" + String(timestamp);

        // add info to JSON
        message->setProperty("type", "spike");
        message->setProperty("sortedID", sortedID);

        int spikeChannels = spikeChannel->getNumChannels();
        message->setProperty("numChannels", spikeChannels);

        Array<var> thresholds;
        for (int i = 0; i < spikeChannels; ++i)
        {
            thresholds.add(spike->getThreshold(i));
        }
        message->setProperty("threshold", thresholds);

        break;  // case SPIKE_EVENT
    }

    case PROCESSOR_EVENT:
    {
        auto eventChannel = static_cast<const EventChannel*>(channel);
        metaDataChannel = static_cast<const MetaDataEventObject*>(eventChannel);

        baseEvent = Event::deserializeFromMessage(msg, eventChannel).release();
        auto event = static_cast<Event*>(baseEvent.get());
        
        uint16 channel = event->getChannel();
        message->setProperty("channel", channel);

        auto eventType = event->getEventType();
        switch (eventType)
        {
        case EventChannel::EventChannelTypes::TTL:
        {
            bool state = static_cast<TTLEvent*>(event)->getState();

            envelope = "ttl/channel:" + String(channel) + "/state:" + (state ? "1" : "0") +
                "/id:" + identifier + "/ts:" + String(timestamp);

            message->setProperty("type", "ttl");
            message->setProperty("data", state);
            break;
        }

        case EventChannel::EventChannelTypes::TEXT:
        {
            const String& text = static_cast<TextEvent*>(event)->getText();

            envelope = "text/channel:" + String(channel) + "/id:" + identifier +
                "/text:" + text + "/ts:" + String(timestamp);

            message->setProperty("type", "text");
            message->setProperty("data", text);
            break;
        }

        default:
        {
            if (eventType < EventChannel::EventChannelTypes::BINARY_BASE_VALUE ||
                eventType >= EventChannel::EventChannelTypes::INVALID)
            {
                jassertfalse;
                return;
            }

            // must have binary event

            envelope = "binary/channel:" + String(channel) + "/id:" + identifier +
                "/ts:" + String(timestamp);

            message->setProperty("type", "binary");
            // TODO: get the binary data as a string.
            //const void* rawData = static_cast<BinaryEvent*>(event)->getBinaryDataPointer();
            //unsigned int length = eventChannel->getLength();
            break;
        }
        } // end switch(eventType)

        break; // case PROCESSOR_EVENT
    }

    default:
        jassertfalse; // should never happen
        return;

    } // end switch(baseType)

    // Add metadata
    DynamicObject::Ptr metaDataObj = new DynamicObject();
    populateMetaData(metaDataChannel, baseEvent, metaDataObj);
    message->setProperty("metaData", metaDataObj.get());

    // Finally, send everything
    var jsonMes(message);
    const char* jsonStr = JSON::toString(jsonMes).toUTF8();
    const char* envelopeStr = envelope.toUTF8();
    
    if (!sendPackage(socket, envelopeStr, jsonStr))
    {
        std::cout << "Error sending Package" << std::endl;
    }
#endif
}

void EventBroadcaster::populateMetaData(const MetaDataEventObject* channel,
    const EventBasePtr event, DynamicObject::Ptr dest)
{
    //Iterate through all event data and add to metadata object
    int numMetaData = event->getMetadataValueCount();
    for (int i = 0; i < numMetaData; i++)
    {
        //Get event data descriptor
        const MetaDataDescriptor * metaDescPtr = channel->getEventMetaDataDescriptor(i);
        String metaDataName = metaDescPtr->getName();

        //Get event data value
        const MetaDataValue* valuePtr = event->getMetaDataValue(i);
        //get data type returns a int corresponding to data type
        //getValue() needs an initalized variable of that type
        switch (valuePtr->getDataType())
        {

            // TODO How to fix <uint16> not having a constructor???
            //  Easy fix - We make them Strings!! Current fix.
            //  Hard fix - ...?
#define SET_METADATA_OF_TYPE(type) dest->setProperty(metaDataName, metaDataValueToVar<type>(valuePtr))

        case MetaDataDescriptor::MetaDataTypes::CHAR:
            SET_METADATA_OF_TYPE(char);
            break;
        case MetaDataDescriptor::MetaDataTypes::INT8:
            SET_METADATA_OF_TYPE(int8);
            break;
        case MetaDataDescriptor::MetaDataTypes::UINT8:
            SET_METADATA_OF_TYPE(uint8);
            break;
        case MetaDataDescriptor::MetaDataTypes::INT16:
            SET_METADATA_OF_TYPE(int16);
            break;
        case MetaDataDescriptor::MetaDataTypes::UINT16:
            SET_METADATA_OF_TYPE(uint16);
            break;
        case MetaDataDescriptor::MetaDataTypes::INT32:
            SET_METADATA_OF_TYPE(int32);
            break;
        case MetaDataDescriptor::MetaDataTypes::UINT32:
            SET_METADATA_OF_TYPE(uint32);
            break;
        case MetaDataDescriptor::MetaDataTypes::INT64:
            SET_METADATA_OF_TYPE(int64);
            break;
        case MetaDataDescriptor::MetaDataTypes::UINT64:
            SET_METADATA_OF_TYPE(uint64);
            break;
        case MetaDataDescriptor::MetaDataTypes::FLOAT:
            SET_METADATA_OF_TYPE(float);
            break;
        case MetaDataDescriptor::MetaDataTypes::DOUBLE:
            SET_METADATA_OF_TYPE(double);
            break;

#undef SET_METADATA_OF_TYPE

        default:
            std::cout << "Error: unknown metadata type" << std::endl;
            jassertfalse;
            return;
        }
    }
}

template <typename T>
var EventBroadcaster::metaDataValueToVar(const MetaDataValue* valuePtr)
{
    Array<T> data;
    valuePtr->getValue(data);
    String valueString;
    int dataLength = data.size();
    
    if (dataLength == 1)
    {
        return String(data.getUnchecked(0));
    }
    else
    {
        Array<var> metaDataArray;
        for (int i = 0; i < dataLength; ++i)
        {
            metaDataArray.add(String(data.getUnchecked(i)));
        }
        return metaDataArray;
    }
}

template <>
var EventBroadcaster::metaDataValueToVar<char>(const MetaDataValue* valuePtr)
{
    String value;
    valuePtr->getValue(value);
    return value;
}


bool EventBroadcaster::sendPackage(void* socket, const char* envelopeStr, const char* jsonStr)
{
#ifdef ZEROMQ
    if (-1 == zmq_send(socket, envelopeStr, strlen(envelopeStr), ZMQ_SNDMORE))
    {
        std::cout << "Error sending envelope: " << zmq_strerror(zmq_errno()) << std::endl;
        return false;
    }

    if (-1 == zmq_send(socket, jsonStr, strlen(jsonStr), 0))
    {
        std::cout << "Error sending json: " << zmq_strerror(zmq_errno()) << std::endl;
        return false;
    }
    return true;
#else
    std::cout << "No ZEROMQ" << std::endl;
    return false;
#endif
}

void EventBroadcaster::handleEvent(const EventChannel* channelInfo, const MidiMessage& event, int samplePosition)
{
    sendEvent(channelInfo, event);
}

void EventBroadcaster::handleSpike(const SpikeChannel* channelInfo, const MidiMessage& event, int samplePosition)
{
    sendEvent(channelInfo, event);
}

void EventBroadcaster::saveCustomParametersToXml(XmlElement* parentElement)
{
    XmlElement* mainNode = parentElement->createNewChildElement("EVENTBROADCASTER");
    mainNode->setAttribute("port", listeningPort);
}


void EventBroadcaster::loadCustomParametersFromXml()
{
    if (parametersAsXml)
    {
        forEachXmlChildElement(*parametersAsXml, mainNode)
        {
            if (mainNode->hasTagName("EVENTBROADCASTER"))
            {
                setListeningPort(mainNode->getIntAttribute("port"));
            }
        }
    }
}
