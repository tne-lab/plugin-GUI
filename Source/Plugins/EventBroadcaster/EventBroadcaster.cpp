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
	
	// Init JSON struct
    // TODO Create a procotol that has outline for every type of event
    // TODO **************** Change nested objs from &name to name.get() ********************
    DynamicObject::Ptr message = new DynamicObject();


    // deserialize the event
    EventType baseType = Event::getBaseType(msg);
    const String& identifier = channel->getIdentifier();
    
	// ****** Get Basic Info ******
    EventBasePtr baseEvent;
    const MetaDataEventObject* metaDataChannel; // for later...
    String envelope;
    switch (baseType)
    {
        uint16 index;
        // TODO Only the start of the envelope add more to it.. TTL, spike, binary... DO THIS FOR BOTH!
        // Store it for later so we can input to our function
    case SPIKE_EVENT:
        baseEvent = SpikeEvent::deserializeFromMessage(msg, static_cast<const SpikeChannel*>(channel)).release();
        index = static_cast<SpikeEvent*>(baseEvent.get())->getSortedID();
        metaDataChannel = static_cast<const MetaDataEventObject*>(static_cast<const SpikeChannel*>(channel));
        envelope = ("spike/sortedID:" + String(index) + "/type:" + identifier).toStdString();
        break;

    case PROCESSOR_EVENT:
		
        baseEvent = Event::deserializeFromMessage(msg, static_cast<const EventChannel*>(channel)).release();
        index = static_cast<Event*>(baseEvent.get())->getChannel();
        metaDataChannel = static_cast<const MetaDataEventObject*>(static_cast<const EventChannel*>(channel));
		envelope = ("event/channel:" + String(index) + "/type:" + identifier).toStdString();
        break;

    default:
        jassertfalse; // should never happen
        return;
    }

    //Send Envelope First // NO!
    const char * envelopeStr = envelope.toUTF8();
    if (-1 == zmq_send(socket, envelopeStr, strlen(envelopeStr), ZMQ_SNDMORE))
    {
        std::cout << "Error sending envelope: " << zmq_strerror(zmq_errno()) << std::endl;
    }


    ////USE JUCE JSON?? - Just a guide on how to make the obj
    DynamicObject::Ptr obj = new DynamicObject();
    obj->setProperty("foo", "bar");
    obj->setProperty("num", 123);

    DynamicObject::Ptr nestedObj = new DynamicObject();
    nestedObj->setProperty("inner", "value");
    obj->setProperty("nested", &nestedObj);

    var json(obj); // store the outer object in a var [we could have done this earlier]
    String s = JSON::toString(json);
    const char * JSONTest = s.toUTF8();
    if (-1 == zmq_send(socket, JSONTest, strlen(JSONTest), ZMQ_SNDMORE))
    {
        std::cout << "Error sending JSONTest: " << zmq_strerror(zmq_errno()) << std::endl;
    }
    ////

    // Still sending these guys as float/doubles for now. Might change in future.
    DynamicObject::Ptr timing = new DynamicObject();
    message->setProperty("timing", timing.get());
    float eventSampleRate = channel->getSampleRate();
	timing->setProperty("sample rate", eventSampleRate);
	double timestampSeconds = double(Event::getTimestamp(msg)) / eventSampleRate;
	timing->setProperty("timestamp", timestampSeconds);

    // ****** Get data payload *******
    switch (baseType)
    {
    case SPIKE_EVENT:
    {
        auto spikeChannel = static_cast<const SpikeChannel*>(channel);
        auto spike = static_cast<SpikeEvent*>(baseEvent.get());

        DynamicObject::Ptr thresholdObj = new DynamicObject();
        message->setProperty("threshold", &thresholdObj);

        // Send the thresholds
        int spikeChannels = spikeChannel->getNumChannels();
        for (int i = 0; i < spikeChannels; ++i)
        {
			float threshold = spike->getThreshold(i);
			thresholdObj->setProperty(String(i), threshold);
			const char* threshStr = String(threshold).toUTF8();
        }

        // send spike data here maybe?
		//Probably not.. We already have everything we need (we think)
    }
    break;

    case PROCESSOR_EVENT:
    {
        auto eventChannel = static_cast<const EventChannel*>(channel);
        auto event = static_cast<Event*>(baseEvent.get());

        // TODO didn't actually write the calls to send this stuff yet, since we're
        // probably going to change the format anyway.
        auto eventType = event->getEventType();
        switch (eventType)
        {
        case EventChannel::EventChannelTypes::TTL:
        {
            // data we want to send:
            bool state = static_cast<TTLEvent*>(event)->getState();
			message->setProperty("state", state);
			//Anything else?
            break;
        }
        case EventChannel::EventChannelTypes::TEXT:
        {
            // data we want to send:
            const String& text = static_cast<TextEvent*>(event)->getText();
			message->setProperty("text", text);
			//Anything else?
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
            // data we want to send:
            // I'm still unsure on how to send this data
            const void* rawData = static_cast<BinaryEvent*>(event)->getBinaryDataPointer();
            size_t rawDataSize = eventChannel->getDataSize();

            break;
        }
        }
    }
    break;

    default: // neither spike nor processor event? (wtf)
        jassertfalse;
        return;
    }

    // ******* Send metadata ********

    int numMetaData = baseEvent->getMetadataValueCount();
	
    //Iterate through all event data and output them as strings to zmq
    for (int i = 0; i < numMetaData; i++)
    {
        //Get event data descriptor
        const MetaDataDescriptor * metaDescPtr = metaDataChannel->getEventMetaDataDescriptor(i);
		String metaDesc = metaDescPtr->getName();
		
        //Get event data value
        const MetaDataValue* valuePtr = baseEvent->getMetaDataValue(i);
        //get data type returns a int corresponding to data type
        //getValue() needs an initalized variable of that type
        bool success;
		switch (valuePtr->getDataType())
		{
		case MetaDataDescriptor::MetaDataTypes::CHAR:
		{
			String sendValue;
			valuePtr->getValue(sendValue);
            message->setProperty(metaDesc, sendValue);
			success = true;
			break;
		}
        // TODO How to fix <uint16> not having a constructor???
        //  Easy fix - We make them Strings!!
        //  Hard fix - ...?
		case MetaDataDescriptor::MetaDataTypes::INT8:
			//success = sendMetaDataValue<int8>(valuePtr);
			success = appendMetaToJSON<int8>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::UINT8:
			success = appendMetaToJSON<uint8>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::INT16:
			success = appendMetaToJSON<int16>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::UINT16:
            success = appendMetaToJSON<uint16>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::INT32:
			success = appendMetaToJSON<int32>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::UINT32:
			success = appendMetaToJSON<uint32>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::INT64:
			success = appendMetaToJSON<int64>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::UINT64:
			success = appendMetaToJSON<uint64>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::FLOAT:
			success = appendMetaToJSON<float>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::DOUBLE:
			success = appendMetaToJSON<double>(valuePtr, metaDesc, message);
			break;

		default:
			std::cout << "Error: unknown metadata type" << std::endl;
			jassertfalse;
			std::cout << "Error: unknown metadata type" << std::endl;
			success = false;
		}
        if (!success) { return; }
    }

	var jsonMes(message);
    String JSONStr = JSON::toString(jsonMes);
	//Our JSON String!!
	//std::cout << JSONStr << std::endl;

    // TODO make function to send all at end... send(const char * envelope, const char * json)
	const char * JSONPtr = JSONStr.toUTF8();
	if (-1 == zmq_send(socket, JSONPtr, strlen(JSONPtr), 0))
	{
		std::cout << "Error sending json: " << zmq_strerror(zmq_errno()) << std::endl;
	}
#endif
}

template <typename T>
bool EventBroadcaster::appendMetaToJSON(const MetaDataValue* valuePtr, String metaDesc, DynamicObject::Ptr message) const
{
	Array<T> data;
	valuePtr->getValue(data);
	String valueString;
	int dataLength = data.size();
    
	if (dataLength == 1)
	{
            message->setProperty(metaDesc, data[0]);
	}
	else
	{
        DynamicObject::Ptr nestedObj = new DynamicObject();
        message->setProperty(metaDesc, &nestedObj);
		for (int i = 0; i < dataLength; ++i)
		{
			nestedObj->setProperty(String(i), data[i]);
		}
	}
	

	return true;
}

template <typename T>
bool EventBroadcaster::sendMetaDataValue(const MetaDataValue* valuePtr) const
{
    Array<T> data;
    valuePtr->getValue(data);
    String valueString;
    int dataLength = data.size();
    if (dataLength == 1)
    {
		
        valueString = String(data[0]);
    }
    else
    {
        valueString = "[";
        for (int i = 0; i < dataLength; ++i)
        {
            if (i > 0) { valueString += ", "; }
            valueString += String(data[i]);
        }
        valueString += "]";
    }

    return sendStringMetaData(valueString);
}

bool EventBroadcaster::sendStringMetaData(const String& valueString) const
{
#ifdef ZEROMQ
    void* socket = zmqSocket.get();

    const char* valueUTF8 = valueString.toUTF8();
    if (-1 == zmq_send(socket, valueUTF8, strlen(valueUTF8), ZMQ_SNDMORE))
    {
        std::cout << "Error sending metadata" << std::endl;
        return false;
    }
#endif
    return true;
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
