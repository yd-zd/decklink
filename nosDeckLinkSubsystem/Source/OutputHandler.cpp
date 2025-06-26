// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include "OutputHandler.hpp"

#include <Nodos/PluginAPI.h>
#include <nosUtil/Stopwatch.hpp>

#include "EnumConversions.hpp"
#include "VideoFrame.hpp"

namespace nos::decklink
{
	
class OutputCallback : public Object<IDeckLinkVideoOutputCallback>
{
public:
	OutputCallback(OutputHandler* outputHandler) :
		Output(outputHandler)
	{
	}

	HRESULT	STDMETHODCALLTYPE ScheduledFrameCompleted(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result) override;
	HRESULT	STDMETHODCALLTYPE ScheduledPlaybackHasStopped(void) override;

	// IUnknown needs only a dummy implementation
	HRESULT	STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) override
	{
		return E_NOINTERFACE;
	}

private:
	OutputHandler*  Output;
};
	
HRESULT	OutputCallback::ScheduledFrameCompleted(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result)
{
	Output->ScheduledFrameCompleted_DeckLinkThread(completedFrame, result);
	return S_OK;
}

HRESULT	OutputCallback::ScheduledPlaybackHasStopped(void)
{
	Output->ScheduledPlaybackHasStopped_DeckLinkThread();
	return S_OK;
}

OutputHandler::~OutputHandler()
{
	CloseStream();
	Release(Interface);
}

bool OutputHandler::Open(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat)
{
	if (pixelFormat == bmdFormatUnspecified)
		return false;
	HRESULT res;
	for (auto& frame : VideoFrames)
		Release(frame);
	for (auto& frame : VideoFrames)
	{
		// Get width and height from display mode
		long width, height;
		int rowBytes;
		{
			IDeckLinkDisplayMode* displayModeInterface = nullptr;
			res = Interface->GetDisplayMode(displayMode, &displayModeInterface);
			if (res != S_OK)
				return false;
			width = displayModeInterface->GetWidth();
			height = displayModeInterface->GetHeight();
			res = displayModeInterface->GetFrameRate(&FrameDuration, &TimeScale);
			if (res != S_OK)
				return false;
			rowBytes = 0;
			res = Interface->RowBytesForPixelFormat(pixelFormat, width, &rowBytes);
			if (res != S_OK)
				return false;
			Release(displayModeInterface);
		}
		Interface->CreateVideoFrame(width, height, rowBytes, pixelFormat, bmdFrameFlagDefault, &frame);
		if (!frame)
			return false;
	}

	res = Interface->EnableVideoOutput(displayMode, bmdVideoOutputFlagDefault);
	if (res != S_OK)
		return false;

	auto outputCallback = new OutputCallback(this);
	if (outputCallback == nullptr)
	{
		nosEngine.LogE("Could not create output callback");
		return false;
	}
	res = Interface->SetScheduledFrameCompletionCallback(outputCallback);
	Release(outputCallback);
	if (res != S_OK)
	{
		nosEngine.LogE("SubDevice: Failed to set output callback");
		Close();
		return false;
	}

	IsInterlaced = GetVideoScanType(displayMode) == NOS_MEDIAIO_VIDEO_INTERLACED_SCAN;
	return true;
}

bool OutputHandler::Start()
{
	PlaybackStopRequested = false;
	TotalFramesScheduled = 0;
	LastHardwareFrameInfo = {};
	{
		std::unique_lock lock(DMATargetMutex);
		DMATarget = {};
		NextDMATarget = {};
	}
	{
		std::unique_lock lock(PlaybackStoppedMutex);
		PlaybackStopped = false;
	}
	for (auto i = 0; i < VideoFrames.size(); ++i)
		ScheduleNextFrame(VideoFrames[i]);
	auto res = Interface->StartScheduledPlayback(0, TimeScale, 1.0);
	if (res != S_OK)
	{
		nosEngine.LogE("SubDevice: Failed to start scheduled playback");
		return false;
	}
	return true;
}

bool OutputHandler::Stop()
{
	PlaybackStopRequested = true;
	if (Interface->StopScheduledPlayback(0, nullptr, TimeScale) != S_OK)
	{
		nosEngine.LogE("Failed to stop scheduled playback");
		return false;
	}
	{
		std::unique_lock lock(PlaybackStoppedMutex);
		PlaybackStoppedCond.wait_for(lock, std::chrono::milliseconds(100), [this] { return PlaybackStopped; });
		if (!PlaybackStopped)
			nosEngine.LogE("SubDevice: Timeout waiting for playback to stop");
	}
	// DeckLink does not ACTUALLY stop calling frame completion callbacks even after it informs that playback has stopped.
	{
		std::unique_lock lock(LastHardwareFrameInfoMutex);
		FrameCompletedCV.wait_for(lock, std::chrono::milliseconds(100), [this] {
			return LastHardwareFrameInfo.FrameNumber >= TotalFramesScheduled;
		});
		if (LastHardwareFrameInfo.FrameNumber < TotalFramesScheduled)
			nosEngine.LogE("(%s) Output: Timeout waiting for all frames to be completed", GetDeviceChannelString().c_str());
	}
	return true;
}

bool OutputHandler::Close()
{
	auto res = Interface->DisableVideoOutput();
	if (res != S_OK)
	{
		nosEngine.LogE("SubDevice: Failed to disable video output");
		return false;
	}
	for (auto& frame : VideoFrames)
		Release(frame);
	Interface->SetScheduledFrameCompletionCallback(nullptr);
	return true;
}

bool OutputHandler::WaitFrameImpl(std::chrono::milliseconds timeout)
{
	std::unique_lock lock(LastHardwareFrameInfoMutex);
	bool res = FrameCompletedCV.wait_for(lock, timeout, [this] {
		return LastWaitedFrame != LastHardwareFrameInfo.FrameNumber;
	});
	LastWaitedFrame = LastHardwareFrameInfo.FrameNumber;
	if (!res)
		nosEngine.LogE("(%s) Output: Timeout waiting for frame", GetDeviceChannelString().c_str());
	return res;
}

void OutputHandler::DmaTransferImpl(void* buffer, size_t size)
{
	std::unique_lock lock(DMATargetMutex);
	if (!DMATarget.Data)
	{
		nosEngine.LogE("(%s) Output: Buffer already in use, cannot write new data", GetDeviceChannelString().c_str());
		return;
	}
	memcpy(DMATarget.Data, buffer, std::min(size, DMATarget.Size));

	uint64_t nextFrame;
	{
		std::unique_lock lock(LastHardwareFrameInfoMutex);
		nextFrame = LastHardwareFrameInfo.FrameNumber + VideoFrames.size();
	}
	if (LastProcessedFrame)
	{
		int64_t dropCount = nextFrame - *LastProcessedFrame - 1;
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
	LastProcessedFrame = nextFrame;
}

std::optional<uint64_t> OutputHandler::GetNanosecondsSinceStreamStarted()
{
	if (!IsCurrentlyRunning())
		return std::nullopt;
	auto streamTime = GetCurrentStreamTime();
	if (!streamTime)
		return std::nullopt;
	auto nsSince = TimeToNanoseconds(*streamTime, TimeScale);
	return nsSince;
}

std::optional<BMDTimeValue> OutputHandler::GetCurrentStreamTime()
{
	BMDTimeValue streamTime = 0;
	double speed = 0.0;
	auto res = Interface->GetScheduledStreamTime(TimeScale, &streamTime, &speed);
	if (res != S_OK)
	{
		nosEngine.LogE("(%s) Failed to get stream time", GetDeviceChannelString().c_str());
		return std::nullopt;
	}
	return streamTime;
}

void OutputHandler::ScheduleNextFrame(IDeckLinkVideoFrame* frameToSchedule)
{
	HRESULT result =
		Interface->ScheduleVideoFrame(frameToSchedule, TotalFramesScheduled * FrameDuration, FrameDuration, TimeScale);
	if (result != S_OK)
		nosEngine.LogE("(%s) DMA Write: Failed to schedule next frame", GetDeviceChannelString().c_str());
	else
	{
		++TotalFramesScheduled;
	}
}

void OutputHandler::ScheduledFrameCompleted_DeckLinkThread(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result)
{
	auto time = std::chrono::steady_clock::now().time_since_epoch().count();
	{
		std::unique_lock lock(DMATargetMutex);
		DMATarget = NextDMATarget;
	}
	VideoFrame output(completedFrame);
	output.StartAccess(bmdBufferAccessWrite);
	size_t actualBufferSize = completedFrame->GetRowBytes() * completedFrame->GetHeight();
	auto videoBufferBytes = output.GetBytes();
	NextDMATarget = { .Data = videoBufferBytes, .Size = actualBufferSize };
	auto streamTimeNs = GetNanosecondsSinceStreamStarted();
	auto frameTimeNs = TimeToNanoseconds(FrameDuration, TimeScale);
	output.EndAccess();
	{
		std::unique_lock lock(LastHardwareFrameInfoMutex);
		LastHardwareFrameInfo.TimestampNs = time;
		if (streamTimeNs)
		{
			LastHardwareFrameInfo.FrameNumber = *streamTimeNs / frameTimeNs;
		}
		else
		{
			LastHardwareFrameInfo.FrameNumber++;
		}
#if NOS_DECKLINK_DIAGNOSTICS
		nosEngine.LogD("(%s) Frame %llu arrived at %llu", GetDeviceChannelString().c_str(), LastHardwareFrameInfo.FrameNumber, LastHardwareFrameInfo.TimestampNs);
#endif
		LastHardwareFrameInfo.DeltaSeconds = { .x = uint32_t(FrameDuration), .y = uint32_t(TimeScale) };
	}
	FrameCompletedCV.notify_all();;
	if (PlaybackStopRequested)
	{
		nosEngine.LogW("(%s) Output: Frame complete callback invoked after stop requested, last frame: %llu", GetDeviceChannelString().c_str(), LastHardwareFrameInfo.FrameNumber);
		return;
	}
	ScheduleNextFrame(completedFrame);
}

void OutputHandler::ScheduledPlaybackHasStopped_DeckLinkThread()
{
	{
		std::unique_lock lock(PlaybackStoppedMutex);
		PlaybackStopped = true;
	}
	nosEngine.LogI("(%s) Output: Playback has stopped", GetDeviceChannelString().c_str());
	PlaybackStoppedCond.notify_all();
}
}
