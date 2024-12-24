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
			WriteQueue.push_back(frame);
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
	{
		std::unique_lock lock(PlaybackStoppedMutex);
		Closed = false;
	}

	IsInterlaced = GetVideoScanType(displayMode) == NOS_MEDIAIO_VIDEO_INTERLACED_SCAN;
	return true;
}

bool OutputHandler::Start()
{
	LastWaitedFieldType = NOS_MEDIAIO_INTERLACED_FIELD_TYPE_INVALID;
	{
		std::unique_lock lock(VideoFramesMutex);
		TotalFramesScheduled = 0;
		FramePointFirstDisplayedLate = -1;
		WriteQueue.clear();
		for (auto& frame : VideoFrames)
			WriteQueue.push_back(frame);
	}
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
	if (Interface->StopScheduledPlayback(0, nullptr, TimeScale) != S_OK)
	{
		nosEngine.LogE("Failed to stop scheduled playback");
		return false;
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
	{
		std::unique_lock lock(VideoFramesMutex);
		for (auto& frame : VideoFrames)
			Release(frame);
		WriteQueue.clear();
	}
	{
		std::unique_lock lock(PlaybackStoppedMutex);
		PlaybackStoppedCond.wait_for(lock, std::chrono::milliseconds(100), [this]{ return Closed; });
		if (!Closed)
		{
			nosEngine.LogE("SubDevice: Timeout waiting for playback to stop");
			NOS_SOFT_CHECK(false)
		}
	}
	Interface->SetScheduledFrameCompletionCallback(nullptr);
	return true;
}

bool OutputHandler::WaitFrame(std::chrono::milliseconds timeout, nosMediaIOInterlacedFieldType optFieldType)
{
	if (IsInterlaced)
	{
		LastWaitedFieldType = optFieldType;
		if (optFieldType == NOS_MEDIAIO_INTERLACED_EVEN_FIELD)
			return true;
	}
	util::Stopwatch sw;
	bool res;
	{
		std::unique_lock lock(VideoFramesMutex);
		res = WriteCond.wait_for(lock, timeout, [this] {
			return !WriteQueue.empty();
		});
		if (!res)
		{
			nosEngine.LogE("(Device %d) %s Output: Timeout waiting for frame", DeviceIndex, GetChannelName(Channel));
			return false;
		}
	}
	auto seconds = sw.Elapsed();
	char watchLogBuf[128];
	snprintf(watchLogBuf, sizeof(watchLogBuf), "DeckLink %d:%s WaitFrame", DeviceIndex, GetChannelName(Channel));
	nosEngine.WatchLog(watchLogBuf, util::Stopwatch::ElapsedString(seconds).c_str());
	return res;
}

void OutputHandler::DmaTransfer(void* buffer, size_t size)
{
	if (IsInterlaced)
	{
		if (LastWaitedFieldType == NOS_MEDIAIO_INTERLACED_EVEN_FIELD)
		{
			// Skip writing for this half-frame.
			return;
		}
	}
	util::Stopwatch sw;
	IDeckLinkVideoFrame* frame;
	{
		std::unique_lock lock(VideoFramesMutex);
		if (WriteQueue.empty())
		{
			nosEngine.LogE("(Device %d) %s DMA Write: No frame available to write", DeviceIndex, GetChannelName(Channel));
			return;
		}
		frame = WriteQueue.front();
	}
	{
		VideoFrame output(frame);
		output.StartAccess(bmdBufferAccessWrite);
		size_t actualBufferSize = frame->GetRowBytes() * frame->GetHeight();
		auto videoBufferBytes = output.GetBytes();
		if (videoBufferBytes && buffer)
		{
			if (size != actualBufferSize)
			{
				nosEngine.LogW("(Device %d) %s DMA Write: Buffer size does not match frame size", DeviceIndex, GetChannelName(Channel));
			}
			size_t copySize = std::min(size, actualBufferSize);
			std::memcpy(videoBufferBytes, buffer, copySize);
		}
		output.EndAccess();
	}
	char watchLogBuf[128];
	snprintf(watchLogBuf, sizeof(watchLogBuf), "DeckLink %d:%s DMAWrite", DeviceIndex, GetChannelName(Channel));
	nosEngine.WatchLog(watchLogBuf, sw.ElapsedString().c_str());
	ScheduleNextFrame();
}

void OutputHandler::ScheduleNextFrame()
{
	if (!IsCurrentlyRunning())
		return;
	IDeckLinkVideoFrame* frame;
	{
		std::unique_lock lock(VideoFramesMutex);
		if (WriteQueue.empty())
		{
			nosEngine.LogE("(Device %d) %s DMA Write: No frame available to schedule next", DeviceIndex, GetChannelName(Channel));
			return;
		}
		frame = WriteQueue.front();
		WriteQueue.pop_front();
	}

	HRESULT result = Interface->ScheduleVideoFrame(frame, TotalFramesScheduled * FrameDuration, FrameDuration, TimeScale);
	if (result != S_OK)
		nosEngine.LogE("(Device %d) %s DMA Write: Failed to schedule next frame", DeviceIndex, GetChannelName(Channel));
	else
		++TotalFramesScheduled;
}

void OutputHandler::ScheduledFrameCompleted_DeckLinkThread(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result)
{
	{
		std::unique_lock lock(VideoFramesMutex);
		WriteQueue.push_back(completedFrame);
		char buffer[128];
		snprintf(buffer, sizeof(buffer), "DeckLink %d:%s Output Queue Size", DeviceIndex, GetChannelName(Channel));
		nosEngine.WatchLog(buffer, std::to_string(WriteQueue.size()).c_str());
	}
	WriteCond.notify_one();
	nosDeckLinkFrameResult frameResult = NOS_DECKLINK_FRAME_COMPLETED;
	switch (result)
	{
	case bmdOutputFrameCompleted:
	case bmdOutputFrameFlushed:
		return;
	case bmdOutputFrameDisplayedLate:
		if (FramePointFirstDisplayedLate == -1)
		{
			FramePointFirstDisplayedLate = TotalFramesScheduled;
			frameResult = NOS_DECKLINK_FRAME_DROPPED;
		}
		break;
	case bmdOutputFrameDropped:
		frameResult = NOS_DECKLINK_FRAME_DROPPED;
		break;
	}
	OnFrameEnd(frameResult);
}

void OutputHandler::ScheduledPlaybackHasStopped_DeckLinkThread()
{
	{
		std::unique_lock lock(PlaybackStoppedMutex);
		Closed = true;
	}
	PlaybackStoppedCond.notify_all();
}
}
