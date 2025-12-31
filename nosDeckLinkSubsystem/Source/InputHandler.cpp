// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include "InputHandler.hpp"

#include <Nodos/Utils/Stopwatch.hpp>

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
		BMDPixelFormat      detectedPixelFormat = bmdFormat10BitYUV;
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
					detectedPixelFormat = bmdFormat8BitYUV;
				else if (detectedSignalFlags & bmdDetectedVideoInput10BitDepth)
					detectedPixelFormat = bmdFormat10BitYUV;
				else
					return E_FAIL;
			}
			else if (detectedSignalFlags & bmdDetectedVideoInputRGB444)
			{
				if (detectedSignalFlags & bmdDetectedVideoInput8BitDepth)
					detectedPixelFormat = bmdFormat8BitARGB;
				else if (detectedSignalFlags & bmdDetectedVideoInput10BitDepth)
					detectedPixelFormat = bmdFormat10BitRGB;
				else if (detectedSignalFlags & bmdDetectedVideoInput12BitDepth)
					detectedPixelFormat = bmdFormat12BitRGB;
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
			Input->OnInputVideoFormatChanged_DeckLinkThread(newDisplayMode->GetDisplayMode(), detectedPixelFormat);
		}
		
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE VideoInputFrameArrived (/* in */ IDeckLinkVideoInputFrame* videoFrame, /* in */ IDeckLinkAudioInputPacket* audioPacket)
	{
		Input->OnInputFrameArrived_DeckLinkThread(videoFrame);
		if (audioPacket)
			Input->OnInputAudioArrived_DeckLinkThread(audioPacket);
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
	auto time = std::chrono::steady_clock::now().time_since_epoch().count();
	if (res != S_OK)
		return;
	auto inputFrame = std::make_unique<VideoFrame>(frame);
	inputFrame->StartAccess(bmdBufferAccessRead);
	{
		std::unique_lock lock(ReadFrameMutex);
		ReadFrameBuffer = {.Data = inputFrame->GetBytes(), .Size = inputFrame->Size};
	}
	inputFrame->EndAccess();
	{
		std::unique_lock lock(ReadFrameMutex);
		auto frameTimeNs = TimeToNanoseconds(FrameDuration, TimeScale);
		auto streamTimeNs = TimeToNanoseconds(frameTime, TimeScale);
		LastHardwareFrameInfo.TimestampNs = time;
		LastHardwareFrameInfo.FrameNumber = streamTimeNs / frameTimeNs;
		LastHardwareFrameInfo.DeltaSeconds = {.x = uint32_t(FrameDuration), .y = uint32_t(TimeScale)};
	}
	FrameArrivedCV.notify_one();
}

bool InputHandler::Flush()
{
	auto res = Interface->PauseStreams();
	if (S_OK != res)
		return false;
	
	res = Interface->FlushStreams();
	if (S_OK != res)
		return false;

	{
		std::unique_lock lock(ReadFrameMutex);
		ReadFrameBuffer = {};
	}

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
	PixelFormat = pixelFormat;
	DisplayMode = bmdModeUnknown;
	res = Interface->EnableVideoInput(displayMode, PixelFormat, bmdVideoInputEnableFormatDetection);
	if (res != S_OK)
	{
		nosEngine.LogE("Could not enable video input - result = %08x", res);
		return false;
	}
	res = Interface->EnableAudioInput(AudioSampleRate, AudioSampleType, AudioChannelCount);
	if (res != S_OK)
	{
		nosEngine.LogE("Could not enable audio input - result = %08x", res);
		return false;
	}
	if (!UpdateFrameRate(displayMode))
		return false;
	return true;
}

bool InputHandler::Start()
{
	{
		std::unique_lock lock(LastHardwareFrameInfoMutex);
		LastHardwareFrameInfo = {};
		LastWaitedFrame = 0;
	}
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
	if (S_OK != Interface->DisableAudioInput())
		return false;
	return true;
}

bool InputHandler::WaitFrameImpl(std::chrono::milliseconds timeout)
{
	std::unique_lock lock(LastHardwareFrameInfoMutex);
	bool res = FrameArrivedCV.wait_for(lock, timeout, [this]{
		return LastWaitedFrame != LastHardwareFrameInfo.FrameNumber;
	});
	LastWaitedFrame = LastHardwareFrameInfo.FrameNumber;
	if (!res)
		nosEngine.LogW("(Device %d) %s Input: Timeout waiting for frame", DeviceIndex, GetChannelName(Channel));
	return res;
}

void InputHandler::DmaTransferImpl(void* buffer, size_t size)
{
	nosBuffer readBuffer{};
	{
		std::unique_lock lock(ReadFrameMutex);
		if (!ReadFrameBuffer.Size)
		{
			nosEngine.LogE("(%s) DMA Read: No frame available to read", GetDeviceChannelString().c_str());
			return;
		}
		readBuffer = ReadFrameBuffer;
	}
	size_t actualSize = readBuffer.Size;
	if (!actualSize)
		return;
	if (size != actualSize)
	{
		nosEngine.LogW("(%s) DMA Read: Buffer size does not match frame size", GetDeviceChannelString().c_str());
	}
	auto copySize = std::min(actualSize, size);
	std::memcpy(buffer, readBuffer.Data, copySize);
	uint64_t lastFrame;
	{
		std::unique_lock lock(LastHardwareFrameInfoMutex);
		lastFrame = LastHardwareFrameInfo.FrameNumber;
	}
	if (LastProcessedFrame)
	{
		int64_t dropCount = lastFrame - *LastProcessedFrame - 1;
		if (dropCount > 0)
		{
			for (int i = 0; i < dropCount; i++)
				OnFrameEnd(NOS_DECKLINK_FRAME_DROPPED);
		}
		else
		{
			OnFrameEnd(NOS_DECKLINK_FRAME_COMPLETED);
		}
	}
	LastProcessedFrame = lastFrame;
}

bool InputHandler::UpdateFrameRate(BMDDisplayMode displayMode)
{
	IDeckLinkDisplayMode* displayModeInterface = nullptr;
	auto res = Interface->GetDisplayMode(displayMode, &displayModeInterface);
	if (res != S_OK)
		return false;
	res = displayModeInterface->GetFrameRate(&FrameDuration, &TimeScale);
	if (res != S_OK)
		return false;
	Release(displayModeInterface);
	return true;
}

void InputHandler::OnInputVideoFormatChanged_DeckLinkThread(BMDDisplayMode newDisplayMode, BMDPixelFormat detectedPixelFormat)
{
	if (DisplayMode == newDisplayMode)
		return;
	IsInterlaced = GetVideoScanType(newDisplayMode) == NOS_MEDIAIO_VIDEO_INTERLACED_SCAN;

	// Pause video capture
	Interface->PauseStreams();

	// Enable video input with the properties of the new video stream
	Interface->EnableVideoInput(newDisplayMode, PixelFormat, bmdVideoInputEnableFormatDetection);

	UpdateFrameRate(newDisplayMode);

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
			callback(userData, GetVideoScanType(newDisplayMode), frameGeometry, frameRate, GetPixelFormatFromDeckLink(PixelFormat));
		}
	}
	DisplayMode = newDisplayMode;
}

void InputHandler::OnInputAudioArrived_DeckLinkThread(IDeckLinkAudioInputPacket* audioPacket)
{
	long sampleFrames = audioPacket->GetSampleFrameCount();
	void* bytes = nullptr;
	if (audioPacket->GetBytes(&bytes) != S_OK || bytes == nullptr || sampleFrames <= 0)
		return;
	uint32_t bytesPerSample = (AudioSampleType == bmdAudioSampleType16bitInteger) ? 2u : 4u;
	uint64_t totalBytes = uint64_t(sampleFrames) * bytesPerSample * AudioChannelCount;
	AudioBufferStore.resize(size_t(totalBytes));
	std::memcpy(AudioBufferStore.data(), bytes, size_t(totalBytes));
	{
		std::unique_lock lock(ReadAudioMutex);
		ReadAudioBuffer = { .Data = AudioBufferStore.data(), .Size = AudioBufferStore.size() };
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
