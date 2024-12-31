// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include "Common.hpp"

namespace nos::decklink
{
struct OutputHandler : IOHandlerBase<IDeckLinkOutput>
{
	std::array<IDeckLinkMutableVideoFrame*, 2> VideoFrames{};
	
	std::atomic_uint32_t TotalFramesScheduled = 0;

	std::mutex VideoFramesMutex;
	std::condition_variable WriteCond;
	std::deque<IDeckLinkVideoFrame*> WriteQueue;

	~OutputHandler() override;

	bool WaitFrameImpl(std::chrono::milliseconds timeout) override;
	void DmaTransferImpl(void* buffer, size_t size) override;
	
	void ScheduleNextFrame();
	void ScheduledFrameCompleted_DeckLinkThread(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result);
	void ScheduledPlaybackHasStopped_DeckLinkThread();
protected:
	bool Open(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat) override;
	bool Start() override;
	bool Stop() override;
	bool Close() override;

	int64_t FramePointFirstDisplayedLate = -1;

	std::mutex PlaybackStoppedMutex;
	std::condition_variable PlaybackStoppedCond;
	bool Closed = true;
	std::optional<BMDReferenceStatus> ReferenceStatus = std::nullopt;
};
}