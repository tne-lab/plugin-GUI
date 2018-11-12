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

std::shared_ptr<void> EventBroadcaster::getZMQContext() {
    // Note: C++11 guarantees that initialization of static local variables occurs exactly once, even
    // if multiple threads attempt to initialize the same static local variable concurrently.
#ifdef ZEROMQ
    static const std::shared_ptr<void> ctx(zmq_ctx_new(), zmq_ctx_destroy);
#else
    static const std::shared_ptr<void> ctx;
#endif
    return ctx;
}


void EventBroadcaster::closeZMQSocket(void* socket)
{
#ifdef ZEROMQ
    zmq_close(socket);
#endif
}


EventBroadcaster::EventBroadcaster()
    : GenericProcessor  ("Event Broadcaster")
    , zmqContext        (getZMQContext())
    , zmqSocket         (nullptr, &closeZMQSocket)
    , listeningPort     (0)
{
    setProcessorType (PROCESSOR_TYPE_SINK);

    setListeningPort(5557);
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


void EventBroadcaster::setListeningPort(int port, bool forceRestart)
{
    if ((listeningPort != port) || forceRestart)
    {
#ifdef ZEROMQ
        zmqSocket.reset(zmq_socket(zmqContext.get(), ZMQ_PUB));
        if (!zmqSocket)
        {
            std::cout << "Failed to create socket: " << zmq_strerror(zmq_errno()) << std::endl;
            return;
        }

        String url = String("tcp://*:") + String(port);
        if (0 != zmq_bind(zmqSocket.get(), url.toRawUTF8()))
        {
            std::cout << "Failed to open socket: " << zmq_strerror(zmq_errno()) << std::endl;
            return;
        }
#endif

        listeningPort = port;
    }
}


void EventBroadcaster::process(AudioSampleBuffer& continuousBuffer)
{
    checkForEvents(true);
}


//IMPORTANT: The structure of the event buffers has changed drastically, so we need to find a better way of doing this
void EventBroadcaster::sendEvent(const MidiMessage& event, float eventSampleRate) const
{
	
	
#ifdef ZEROMQ
	//Send Event
	uint16 type = Event::getBaseType(event);
	if (-1 == zmq_send(zmqSocket.get(), &type, sizeof(type), ZMQ_SNDMORE)) 
	{
		fprintf(stdout, "Error sending event \n");
	}
	//Send timestamp
	double timestampSeconds = double(Event::getTimestamp(event)) / eventSampleRate;
	juce::String newTime = juce::String(timestampSeconds);
	const char * finalTime = newTime.toUTF8();
	
	if (-1 == zmq_send(zmqSocket.get(), finalTime, strlen(finalTime), ZMQ_SNDMORE)) 
	{
		fprintf(stdout, "Error sending timestamp \n");
	}

	//Doesn't line up with anything(that I know of), but might be useful for someone.
	int channelNumber = event.getChannel();
	// TODO !!! Find the actual channel number that corresponds to spike/events not just the first one.
	
	//Is it a spike event?
	if (getTotalSpikeChannels() > 0) {
		//Only one channel of spikes, so grab the first one
		const SpikeChannel * spikeChannelPtr = getSpikeChannel(0);

		//Gets the data from the event
		SpikeEventPtr spikeEventPtr = SpikeEvent::deserializeFromMessage(event, spikeChannelPtr);
		SpikeEvent * spike = spikeEventPtr.get();
		
		/*
		//Using spike sorter this gets the color. Awkward to use that info unless 
		//we find a way to send box information as well.
		//Send event data descriptor
		const MetaDataDescriptor * metaDescPtr = eventChan->getEventMetaDataDescriptor(0);
		juce::String metaDesc = metaDescPtr->getName();
		const char * metaString = metaDesc.toUTF8();
		if (-1 == zmq_send(zmqSocket.get(), metaString, strlen(metaString), ZMQ_SNDMORE))
		{
			fprintf(stdout, "Error sending description \n");
		
		}

		//Send meta data
		const MetaDataValue * metaSpikePtr = spike->getMetaDataValue(0);
		///HARD CODED FOR SPIKE SORTER (uint8)
		uint8 spikeData;
		metaSpikePtr->getValue(spikeData);
		juce::String metaValue = juce::String(spikeData);
		const char * metaValueStr = metaValue.toUTF8();
		if (-1 == zmq_send(zmqSocket.get(), metaValueStr, strlen(metaValueStr), ZMQ_SNDMORE))
		{
		fprintf(stdout, "Error sending description \n");

		}
		*/

		//Send the threshold
		const char * thresholdDesc = "threshold";
		if (-1 == zmq_send(zmqSocket.get(), thresholdDesc, strlen(thresholdDesc), ZMQ_SNDMORE))
		{
			fprintf(stdout, "Error sending threshold desc \n");
		}
		

		float threshold = spike->getThreshold(0);
		juce::String threshStr = juce::String(threshold);
		const char * threshStrPtr = threshStr.toUTF8();

		if (-1 == zmq_send(zmqSocket.get(), threshStrPtr, strlen(threshStrPtr), ZMQ_SNDMORE))
		{
			fprintf(stdout, "Error sending threshold \n");
		}
	}
	//Or event...? 
	else if (getTotalEventChannels() > 0)
	{
		//Get first channel, should only be one unless sending multiple types of events
		const EventChannel * eventChannelPtr = getEventChannel(0);
		if (eventChannelPtr->getInfoObjectType() == 1)
		{
			//Parse message
			TTLEventPtr metaEventPtr = TTLEvent::deserializeFromMessage(event, eventChannelPtr);

			TTLEvent *meta = metaEventPtr.get();
			int numMetaData = meta->getMetadataValueCount();
			const EventChannel * eventChan = metaEventPtr->getChannelInfo();

			//Iterate through all event data and output them as strings to zmq
			for (int i = 0; i < numMetaData; i++)
			{

				//Send event data descriptor
				const MetaDataDescriptor * metaDescPtr = eventChan->getEventMetaDataDescriptor(i);
				if (metaDescPtr != nullptr)
				{
					juce::String metaDesc = metaDescPtr->getName();

					if (metaDesc.isNotEmpty())
					{
						const char * metaString = metaDesc.toUTF8();
						if (-1 == zmq_send(zmqSocket.get(), metaString, strlen(metaString), ZMQ_SNDMORE))
						{
							fprintf(stdout, "Error sending description \n");
						}
					}
					else
					{
						fprintf(stdout, "Empty meta descriptor \n");
					}
				}

				//Send event data value
				const MetaDataValue * valuePtr = meta->getMetaDataValue(i);
				//get data type returns a int corresponding to data type
				//getValue() needs an initalized variable of that type
				//Might be able to malloc with a void * but I found this to be the easiest method
				switch (valuePtr->getDataType())
				{
				case 0: {
					//Get the Value
					CHAR sendValue;
					valuePtr->getValue(sendValue);
					//Change to UTF8 encoded string so python doesn't need to worry about anything
					juce::String valueString = juce::String(sendValue);
					const char * charValue = valueString.toUTF8();
					//Send string
					if (-1 == zmq_send(zmqSocket.get(), charValue, strlen(charValue), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending char \n");
					}
					break;
				}
				case 1: {
					int8 sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * int8Value = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), int8Value, strlen(int8Value), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending int8 \n");
					}
					break;
				}
				case 2: {
					uint8 sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * uint8Value = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), uint8Value, strlen(uint8Value), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending uint8 \n");
					}
					break;
				}
				case 3: {
					int16 sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * int16Value = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), int16Value, strlen(int16Value), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending int16 \n");
					}
					break;
				}
				case 4:
				{
					uint16 sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * uint16Value = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), uint16Value, strlen(uint16Value), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending uint8 \n");
					}
					break;
				}
				case 5:
				{
					int32 sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * int32Value = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), int32Value, strlen(int32Value), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending int32 \n");
					}
					break;
				}
				case 6:
				{
					uint32 sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * uint32Value = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), uint32Value, strlen(uint32Value), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending uint32 \n");
					}
					break;
				}
				case 7:
				{
					int64 sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * int64Value = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), int64Value, strlen(int64Value), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending int64 \n");
					}
					break;
				}
				case 8:
				{
					uint64 sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * uint64Value = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), uint64Value, strlen(uint64Value), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending uint64 \n");
					}
					break;
				}
				case 9:
				{
					float sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * floatValue = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), floatValue, strlen(floatValue), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending float \n");
					}
					break;
				}
				case 10:
				{
					double sendValue;
					valuePtr->getValue(sendValue);
					juce::String valueString = juce::String(sendValue);
					const char * doubleValue = valueString.toUTF8();
					if (-1 == zmq_send(zmqSocket.get(), doubleValue, strlen(doubleValue), ZMQ_SNDMORE))
					{
						fprintf(stdout, "Error sending double \n");
					}
					break;
				}
				default:
					fprintf(stdout, "error \n");
					return;
				}

			}
		}
	}

	//Maybe other types of events?
	
	//Send all raw data and actually flushes the multipart message out
	if (-1 == zmq_send(zmqSocket.get(), event.getRawData(), event.getRawDataSize(), 0))
	{
		std::cout << "Failed to send message: " << zmq_strerror(zmq_errno()) << std::endl;
	}
#endif
}

void EventBroadcaster::handleEvent(const EventChannel* channelInfo, const MidiMessage& event, int samplePosition)
{
	sendEvent(event, channelInfo->getSampleRate());
}

void EventBroadcaster::handleSpike(const SpikeChannel* channelInfo, const MidiMessage& event, int samplePosition)
{
	sendEvent(event, channelInfo->getSampleRate());
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
