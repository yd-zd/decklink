// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include "Common.hpp"

namespace nos::decklink
{
struct OutputHandler : IOHandlerBase<IDeckLinkOutput>
{
	std::array<IDeckLinkMutableVideoFrame*, 2> VideoFrames{};
	
	std::atomic_uint32_t TotalFramesScheduled = 0;

	~OutputHandler() override;

	bool WaitFrameImpl(std::chrono::milliseconds timeout) override;
	void DmaTransferImpl(void* buffer, size_t size) override;
	std::optional<uint64_t> GetNanosecondsSinceStreamStarted();
	std::optional<BMDTimeValue> GetCurrentStreamTime();
	
	void ScheduleNextFrame(IDeckLinkVideoFrame* frameToSchedule);
	void ScheduledFrameCompleted_DeckLinkThread(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result);
	void ScheduledPlaybackHasStopped_DeckLinkThread();
protected:
	bool Open(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat) override;
	bool Start() override;
	bool Stop() override;
	bool Close() override;

	std::mutex PlaybackStoppedMutex;
	std::condition_variable PlaybackStoppedCond;
	bool PlaybackStopped = true;
	std::atomic_bool PlaybackStopRequested = false;
	
	std::condition_variable FrameCompletedCV;
	std::mutex DMATargetMutex;
	nosBuffer DMATarget{};
	nosBuffer NextDMATarget{};
};
}