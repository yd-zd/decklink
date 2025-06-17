// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#include "OutputHandler.hpp"

#include <Nodos/Modules.h>
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
	{
		std::unique_lock lock(VideoFramesMutex);
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
	{
		std::unique_lock lock(VideoFramesMutex);
		TotalFramesScheduled = 0;
		FramePointFirstDisplayedLate = -1;
		LastFrameInfo_DeckLinkThread = {};
	}
	{
		std::unique_lock lock(BufferMutex);
		BufferToWrite = {};
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
		nosEngine.LogI("(Device %d) %s Output: Playback stop waited", DeviceIndex, GetChannelName(Channel));
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
	{
		std::unique_lock lock(VideoFramesMutex);
		for (auto& frame : VideoFrames)
			Release(frame);
	}
	Interface->SetScheduledFrameCompletionCallback(nullptr);
	return true;
}

bool OutputHandler::WaitFrameImpl(std::chrono::milliseconds timeout)
{
	std::unique_lock lock(VideoFramesMutex);
	bool res = ReadyToWrite.wait_for(lock, timeout, [this] {
		return LastWaitedFrame != LastFrameInfo_DeckLinkThread.FrameNumber;
	});
	LastWaitedFrame = LastFrameInfo_DeckLinkThread.FrameNumber;
	if (!res)
		nosEngine.LogE("(Device %d) %s Output: Timeout waiting for frame",	 DeviceIndex, GetChannelName(Channel));
	return res;
}

void OutputHandler::DmaTransferImpl(void* buffer, size_t size)
{
	std::unique_lock lock(BufferMutex);
	if (BufferToWrite.Data)
	{
		nosEngine.LogE("(Device %d) %s Output: Buffer already in use, cannot write new data", DeviceIndex, GetChannelName(Channel));
		return;
	}
	BufferToWrite.Data = buffer;
	BufferToWrite.Size = size;
	CopyCompleted.wait_for(lock, std::chrono::milliseconds(100), [this] { 
		return !BufferToWrite.Data;
	});
}

nosDeckLinkFrameTimingInfo OutputHandler::GetLastFrameInfo()
{
	std::unique_lock lock(VideoFramesMutex);
	return LastFrameInfo_DeckLinkThread;
}

std::optional<uint64_t> OutputHandler::GetNanosecondsSinceStreamStarted()
{
	if (!IsCurrentlyRunning())
		return std::nullopt;
	BMDTimeValue StreamTime = 0;
	double Speed = 0.0;
	auto res = Interface->GetScheduledStreamTime(TimeScale, &StreamTime, &Speed);
	if (res != S_OK)
	{
		nosEngine.LogE("(Device %d) %s Output: Failed to get stream time", DeviceIndex, GetChannelName(Channel));
		return std::nullopt;
	}
	auto nsSince = StreamTime * 1'000'000'000ULL / TimeScale;
	return nsSince;
}

void OutputHandler::ScheduleNextFrame(IDeckLinkVideoFrame* frameToSchedule)
{
	HRESULT result =
		Interface->ScheduleVideoFrame(frameToSchedule, TotalFramesScheduled * FrameDuration, FrameDuration, TimeScale);
	if (result != S_OK)
		nosEngine.LogE("(Device %d) %s DMA Write: Failed to schedule next frame", DeviceIndex, GetChannelName(Channel));
	else
	{
		++TotalFramesScheduled;
	}
}

void OutputHandler::ScheduledFrameCompleted_DeckLinkThread(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result)
{
	auto timestampNs =
		std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
			.count();
	if (PlaybackStopRequested)
	{
		nosEngine.LogW("(Device %d) %s Output: Frame complete callback invoked after stop requested, last frame: %llu", DeviceIndex, GetChannelName(Channel), LastFrameInfo_DeckLinkThread.FrameNumber);
		return;
	}
	{
		std::unique_lock lock(VideoFramesMutex);
		std::chrono::system_clock::duration startTime = std::chrono::duration_cast<std::chrono::system_clock::duration>(
			std::chrono::nanoseconds(timestampNs));
		LastFrameInfo_DeckLinkThread.TimestampNs = timestampNs;
		LastFrameInfo_DeckLinkThread.FrameNumber++;
		LastFrameInfo_DeckLinkThread.DeltaSeconds = { .x = uint32_t(FrameDuration), .y = uint32_t(TimeScale) };
	}
	ReadyToWrite.notify_one();
	nosDeckLinkFrameResult frameResult = NOS_DECKLINK_FRAME_COMPLETED;
	switch (result)
	{
	case bmdOutputFrameDisplayedLate:
		frameResult = NOS_DECKLINK_FRAME_DROPPED;
		break;
	case bmdOutputFrameDropped:
		frameResult = NOS_DECKLINK_FRAME_DROPPED;
		break;
	default:
		break;
	}
	OnFrameEnd(frameResult);

	{
		std::unique_lock lock(BufferMutex);
		if (BufferToWrite.Data)
		{
			VideoFrame output(completedFrame);
			output.StartAccess(bmdBufferAccessWrite);
			size_t actualBufferSize = completedFrame->GetRowBytes() * completedFrame->GetHeight();
			auto videoBufferBytes = output.GetBytes();
			if (videoBufferBytes)
			{
				if (BufferToWrite.Size != actualBufferSize)
					nosEngine.LogW("(Device %d) %s DMA Write: Buffer size does not match frame size", DeviceIndex, GetChannelName(Channel));
				size_t copySize = std::min(BufferToWrite.Size, actualBufferSize);
				std::memcpy(videoBufferBytes, BufferToWrite.Data, copySize);
			}
			output.EndAccess();
			BufferToWrite = {};
			CopyCompleted.notify_all();
		}
	}

	ScheduleNextFrame(completedFrame);
}

void OutputHandler::ScheduledPlaybackHasStopped_DeckLinkThread()
{
	{
		std::unique_lock lock(PlaybackStoppedMutex);
		PlaybackStopped = true;
	}
	nosEngine.LogI("(Device %d) %s Output: Playback has stopped", DeviceIndex, GetChannelName(Channel));
	PlaybackStoppedCond.notify_all();
}
}
