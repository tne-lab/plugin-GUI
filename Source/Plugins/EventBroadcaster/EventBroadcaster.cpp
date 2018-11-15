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
	
	//Init json struct
	Json::Value message;
    // deserialize the event
    EventType baseType = Event::getBaseType(msg);
    const String& identifier = channel->getIdentifier();
    
    EventBasePtr baseEvent;
    const MetaDataEventObject* metaDataChannel; // for later...
    String envelope;
    switch (baseType)
    {
        uint16 index;
    case SPIKE_EVENT:
        baseEvent = SpikeEvent::deserializeFromMessage(msg, static_cast<const SpikeChannel*>(channel)).release();
        index = static_cast<SpikeEvent*>(baseEvent.get())->getSortedID();
        metaDataChannel = static_cast<const MetaDataEventObject*>(static_cast<const SpikeChannel*>(channel));
        message["envelope"] = ("spike/sortedID:" + String(index) + "/type:" + identifier).toStdString();
        break;

    case PROCESSOR_EVENT:
		
        baseEvent = Event::deserializeFromMessage(msg, static_cast<const EventChannel*>(channel)).release();
        index = static_cast<Event*>(baseEvent.get())->getChannel();
        metaDataChannel = static_cast<const MetaDataEventObject*>(static_cast<const EventChannel*>(channel));
		message["envelope"] = ("event/channel:" + String(index) + "/type:" + identifier).toStdString();
        break;

    default:
        jassertfalse; // should never happen
        return;
    }

    //***** Send basic info ******
	/*
    const char* envelopeStr = envelope.toUTF8();
    if (-1 == zmq_send(socket, envelopeStr, strlen(envelopeStr), ZMQ_SNDMORE))
    {
        std::cout << "Error sending envelope: " << zmq_strerror(zmq_errno()) << std::endl;
    }

	Json::Value test;
	test["subtree"]["subsubtree"] = "hello";
	test["subtree"]["subsubsubtree"] = "world";
	String JSONStr = test.toStyledString();
	const char * JSONPtr = JSONStr.toUTF8();
	if (-1 == zmq_send(socket, JSONPtr, strlen(JSONPtr), ZMQ_SNDMORE))
	{
		std::cout << "Error sending envelope: " << zmq_strerror(zmq_errno()) << std::endl;
	}
	*/
    // TODO from here down maybe we can package up into a JSON string or something similar?

    float eventSampleRate = channel->getSampleRate();
	message["timing"]["sample rate"] = eventSampleRate;
	double timestampSeconds = double(Event::getTimestamp(msg)) / eventSampleRate;
	message["timing"]["timestamp"] = timestampSeconds;
	/*
	const char* timeStr = String(timestampSeconds).toUTF8();
	
	if (-1 == zmq_send(socket, timeStr, strlen(timeStr), ZMQ_SNDMORE)) 
	{
        std::cout << "Error sending timestamp: " << zmq_strerror(zmq_errno()) << std::endl;
        return;
	}
	*/

    // ****** Send data payload *******
    switch (baseType)
    {
    case SPIKE_EVENT:
    {
        auto spikeChannel = static_cast<const SpikeChannel*>(channel);
        auto spike = static_cast<SpikeEvent*>(baseEvent.get());

        // Send the thresholds
        int spikeChannels = spikeChannel->getNumChannels();
        for (int i = 0; i < spikeChannels; ++i)
        {
			float threshold = spike->getThreshold(i);
			message["threshold"][std::to_string(i)] = threshold;
			const char* threshStr = String(threshold).toUTF8();
			/*
            const char* thresholdDesc = ("threshold " + String(i + 1)).toUTF8();
            if (-1 == zmq_send(socket, thresholdDesc, strlen(thresholdDesc), ZMQ_SNDMORE))
            {
                std::cout << "Error sending threshold desc: " << zmq_strerror(zmq_errno()) << std::endl;
                return;
            }
            if (-1 == zmq_send(socket, threshStr, strlen(threshStr), ZMQ_SNDMORE))
            {
                std::cout << "Error sending threshold: " << zmq_strerror(zmq_errno()) << std::endl;
                return;
            }
			*/
        }

        // send spike data here maybe?
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
            break;
        }
        case EventChannel::EventChannelTypes::TEXT:
        {
            // data we want to send:
            const String& text = static_cast<TextEvent*>(event)->getText();
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
        std::string metaDesc = (metaDescPtr->getName()).toStdString();
		
        //Get event data value
        const MetaDataValue* valuePtr = baseEvent->getMetaDataValue(i);
        //get data type returns a int corresponding to data type
        //getValue() needs an initalized variable of that type
        bool success = true;
		switch (valuePtr->getDataType())
		{
		case MetaDataDescriptor::MetaDataTypes::CHAR:
		{
			String value;
			valuePtr->getValue(value);
			message[metaDesc] = value.toStdString();
			break;
		}

		case MetaDataDescriptor::MetaDataTypes::INT8:
			//success = sendMetaDataValue<int8>(valuePtr);
			message = appendToJSON<int8>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::UINT8:
			message = appendToJSON<uint8>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::INT16:
			message = appendToJSON<int16>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::UINT16:
			message = appendToJSON<uint16>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::INT32:
			message = appendToJSON<int32>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::UINT32:
			message = appendToJSON<uint32>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::INT64:
			message = appendToJSON<int64>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::UINT64:
			message = appendToJSON<uint64>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::FLOAT:
			message = appendToJSON<float>(valuePtr, metaDesc, message);
			break;

		case MetaDataDescriptor::MetaDataTypes::DOUBLE:
			message = appendToJSON<double>(valuePtr, metaDesc, message);
			break;

		default:
			std::cout << "Error: unknown metadata type" << std::endl;
			jassertfalse;
			std::cout << "Error: unknown metadata type" << std::endl;
			success = false;
		}
        if (!success) { return; }
    }

	String JSONStr = message.toStyledString();
	const char * JSONPtr = JSONStr.toUTF8();
	if (-1 == zmq_send(socket, JSONPtr, strlen(JSONPtr), 0))
	{
		std::cout << "Error sending json: " << zmq_strerror(zmq_errno()) << std::endl;
	}
	
	//Send all raw data and actually flushes the multipart message out
    // TODO probably want to get rid of this eventually since it's redundant
	/*
	if (-1 == zmq_send(socket, msg.getRawData(), msg.getRawDataSize(), 0))
	{
		std::cout << "Failed to send message: " << zmq_strerror(zmq_errno()) << std::endl;
	}
	*/
#endif
}

template <typename T>
Json::Value EventBroadcaster::appendToJSON(const MetaDataValue* valuePtr, std::string metaDesc, Json::Value message) const
{
	Array<T> data;
	valuePtr->getValue(data);
	String valueString;
	int dataLength = data.size();
	if (dataLength == 1)
	{
		message[metaDesc] = data[0];
	}
	else
	{
		for (int i = 0; i < dataLength; ++i)
		{
			message[metaDesc][std::to_string(i)] = data[i];
		}
	}
	

	return message;
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
