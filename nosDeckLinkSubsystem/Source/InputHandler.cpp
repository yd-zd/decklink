// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include "InputHandler.hpp"

#include <Nodos/PluginAPI.h>
#include <nosUtil/Stopwatch.hpp>

#include "EnumConversions.hpp"
#include "VideoFrame.hpp"

namespace nos::decklink
{
	
// The input callback class
class InputCallback : public Object<IDeckLinkInputCallback>
{
	
public:
	InputHandler* Input;
	
	InputCallback(InputHandler *input) : Input(input)
	{
	}
	
	// The callback that is called when a property of the video input stream has changed.
	HRESULT		STDMETHODCALLTYPE VideoInputFormatChanged (/* in */ BMDVideoInputFormatChangedEvents notificationEvents, /* in */ IDeckLinkDisplayMode *newDisplayMode, /* in */ BMDDetectedVideoInputFormatFlags detectedSignalFlags)
	{
		BMDPixelFormat      pixelFormat = bmdFormat10BitYUV;
		BMDVideoInputFlags  videoInputFlags = bmdVideoInputEnableFormatDetection;
		
		// // Check for video field changes
		if (notificationEvents & bmdVideoInputFieldDominanceChanged)
		{
			BMDFieldDominance fieldDominance = newDisplayMode->GetFieldDominance();
		}
		
		// Check if the pixel format has changed
		if (notificationEvents & bmdVideoInputColorspaceChanged)
		{
			if (detectedSignalFlags & bmdDetectedVideoInputYCbCr422)
			{
				if (detectedSignalFlags & bmdDetectedVideoInput8BitDepth)
					pixelFormat = bmdFormat8BitYUV;
				else if (detectedSignalFlags & bmdDetectedVideoInput10BitDepth)
					pixelFormat = bmdFormat10BitYUV;
				else
					return E_FAIL;
			}
			else if (detectedSignalFlags & bmdDetectedVideoInputRGB444)
			{
				if (detectedSignalFlags & bmdDetectedVideoInput8BitDepth)
					pixelFormat = bmdFormat8BitARGB;
				else if (detectedSignalFlags & bmdDetectedVideoInput10BitDepth)
					pixelFormat = bmdFormat10BitRGB;
				else if (detectedSignalFlags & bmdDetectedVideoInput12BitDepth)
					pixelFormat = bmdFormat12BitRGB;
				else
				{
					return E_FAIL;
				}
			}
		}
		
		// Check if the video mode has changed
		if (notificationEvents & bmdVideoInputDisplayModeChanged)
		{
			// Obtain the name of the video mode
			dlstring_t displayModeString;
			if (newDisplayMode->GetName(&displayModeString) == S_OK)
			{
				std::string modeName = DlToStdString(displayModeString);
				if (detectedSignalFlags & bmdDetectedVideoInputDualStream3D)
					videoInputFlags |= bmdVideoInputDualStream3D;
				// Release the video mode name string
				DeleteString(displayModeString);
			}
		}
		
		if (notificationEvents & (bmdVideoInputDisplayModeChanged | bmdVideoInputColorspaceChanged))
		{
			Input->OnInputVideoFormatChanged_DeckLinkThread(newDisplayMode->GetDisplayMode(), pixelFormat);
		}
		
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE VideoInputFrameArrived (/* in */ IDeckLinkVideoInputFrame* videoFrame, /* in */ IDeckLinkAudioInputPacket* audioPacket)
	{
		Input->OnInputFrameArrived_DeckLinkThread(videoFrame);
		return S_OK;
	}

private:
	virtual ~InputCallback(void) {}

};

InputHandler::~InputHandler()
{
	CloseStream();
	Release(Interface);
}

void InputHandler::OnInputFrameArrived_DeckLinkThread(IDeckLinkVideoInputFrame* frame)
{
	BMDTimeValue frameTime, frameDuration;
	auto res = frame->GetStreamTime(&frameTime, &frameDuration, TimeScale);
	if (res != S_OK)
		return;
	// TODO: Additionally check for frameTime and frameDuration for drops
	{
		std::unique_lock lock(ReadFramesMutex);
		if (ReadFrames.size() > 1)
		{
			OnFrameEnd(NOS_DECKLINK_FRAME_DROPPED);
			return;
		}
	}
	OnFrameEnd(NOS_DECKLINK_FRAME_COMPLETED);
	auto inputFrame = std::make_unique<VideoFrame>(frame);
	inputFrame->StartAccess(bmdBufferAccessRead);
	{
		std::unique_lock lock(ReadFramesMutex);
		ReadFrames.push_back(std::move(inputFrame));
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "DeckLink %d:%s Input Queue Size", DeviceIndex, GetChannelName(Channel));
		nosEngine.WatchLog(buffer, std::to_string(ReadFrames.size()).c_str());
	}
	FrameAvailableCond.notify_one();
}

bool InputHandler::ResetReadFrames()
{
	std::unique_lock lock(ReadFramesMutex);
	ReadFrames.clear();
	return true;
}

bool InputHandler::Flush()
{
	auto res = Interface->PauseStreams();
	if (S_OK != res)
		return false;
	
	res = Interface->FlushStreams();
	if (S_OK != res)
		return false;

	std::unique_lock lock(ReadFramesMutex);
	ReadFrames.clear();

	return true;
}

bool InputHandler::Open(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat)
{
	IsInterlaced = GetVideoScanType(displayMode) == NOS_MEDIAIO_VIDEO_INTERLACED_SCAN;

	// Create an instance of notification callback
	auto callback = new InputCallback(this);
	if (callback == nullptr)
	{
		nosEngine.LogE("Could not create input callback object");
		return false;
	}
	
	// Set the callback object to the DeckLink device's input interface
	auto res = Interface->SetCallback(callback);
	if (res != S_OK)
	{
		nosEngine.LogE("Could not set callback - result = %08x", res);
		return false;
	}
	Release(callback);
	res = Interface->EnableVideoInput(displayMode, pixelFormat, bmdVideoInputEnableFormatDetection);
	if (res != S_OK)
	{
		nosEngine.LogE("Could not enable video input - result = %08x", res);
		return false;
	}
	{
		IDeckLinkDisplayMode* displayModeInterface = nullptr;
		res = Interface->GetDisplayMode(displayMode, &displayModeInterface);
		if (res != S_OK)
			return false;
		res = displayModeInterface->GetFrameRate(&FrameDuration, &TimeScale);
		if (res != S_OK)
			return false;
		Release(displayModeInterface);
	}
	return true;
}

bool InputHandler::Start()
{
	if (S_OK != Interface->StartStreams())
		return false;
	return true;
}

bool InputHandler::Stop()
{
	Flush();
	if (S_OK != Interface->StopStreams())
		return false;
	return true;
}

bool InputHandler::Close()
{
	if (S_OK != Interface->DisableVideoInput())
		return false;
	return true;
}

bool InputHandler::WaitFrameImpl(std::chrono::milliseconds timeout)
{
	std::unique_lock lock(ReadFramesMutex);
	bool res = FrameAvailableCond.wait_for(lock, timeout, [this]{
		return !ReadFrames.empty();
	});
	if (!res)
	{
		nosEngine.LogE("(Device %d) %s Input: Timeout waiting for frame", DeviceIndex, GetChannelName(Channel));
		return false;
	}
	return res;
}

void InputHandler::DmaTransferImpl(void* buffer, size_t size)
{
	std::unique_lock lock(ReadFramesMutex);
	if (ReadFrames.empty())
	{
		nosEngine.LogE("(Device %d) %s DMA Read: No frame available to read", DeviceIndex, GetChannelName(Channel));
		return;
	}
	auto readFrame = std::move(ReadFrames.front());
	ReadFrames.pop_front();
	size_t actualSize = readFrame->Size;
	if (!actualSize)
		return;
	if (size != actualSize)
	{
		nosEngine.LogW("(Device %d) %s DMA Read: Buffer size does not match frame size", DeviceIndex, GetChannelName(Channel));
	}
	auto copySize = std::min(actualSize, size);
	std::memcpy(buffer, readFrame->GetBytes(), copySize);
	readFrame->EndAccess();
}

void InputHandler::OnInputVideoFormatChanged_DeckLinkThread(BMDDisplayMode newDisplayMode, BMDPixelFormat pixelFormat)
{
	IsInterlaced = GetVideoScanType(newDisplayMode) == NOS_MEDIAIO_VIDEO_INTERLACED_SCAN;

	// Pause video capture
	Interface->PauseStreams();
			
	// Enable video input with the properties of the new video stream
	Interface->EnableVideoInput(newDisplayMode, pixelFormat, bmdVideoInputEnableFormatDetection);

	// Flush any queued video frames
	Interface->FlushStreams();

	// Start video capture
	Interface->StartStreams();

	auto [frameGeometry, frameRate] = GetFrameGeometryAndRatePairFromDeckLinkDisplayMode(newDisplayMode);
	{
		std::unique_lock lock(CallbacksMutex);
		for (auto& [_, pair] : VideoFormatChangeCallbacks)
		{
			auto& [callback, userData] = pair;
			callback(userData, GetVideoScanType(newDisplayMode), frameGeometry, frameRate, GetPixelFormatFromDeckLink(pixelFormat));
		}
	}
}

int32_t InputHandler::AddInputVideoFormatChangeCallback(nosDeckLinkInputVideoFormatChangeCallback callback, void* userData)
{
	std::unique_lock lock(CallbacksMutex);
	return VideoFormatChangeCallbacks.Add(callback, userData);
}

void InputHandler::RemoveInputVideoFormatChangeCallback(int32_t callbackId)
{
	std::unique_lock lock(CallbacksMutex);
	VideoFormatChangeCallbacks.Remove(callbackId);
}
}
