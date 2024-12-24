// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include "Common.hpp"
#include "nosDeckLinkSubsystem/nosDeckLinkSubsystem.h"
#include "VideoFrame.hpp"

namespace nos::decklink
{

struct InputHandler : IOHandlerBase<IDeckLinkInput>
{
	~InputHandler() override;

	std::condition_variable FrameAvailableCond;
	std::deque<std::unique_ptr<VideoFrame>> ReadFrames;
	std::mutex ReadFramesMutex;

	bool ResetReadFrames();
	bool Flush();
	bool WaitFrame(std::chrono::milliseconds timeout, nosMediaIOInterlacedFieldType optFieldType) override;
	void DmaTransfer(void* buffer, size_t size) override;

	void OnInputFrameArrived_DeckLinkThread(IDeckLinkVideoInputFrame* frame);
	void OnInputVideoFormatChanged_DeckLinkThread(BMDDisplayMode newDisplayMode, BMDPixelFormat pixelFormat);

	int32_t AddInputVideoFormatChangeCallback(nosDeckLinkInputVideoFormatChangeCallback callback, void* userData);
	void RemoveInputVideoFormatChangeCallback(int32_t callbackId);
	
	std::mutex CallbacksMutex;
	std::unordered_map<int32_t, std::pair<nosDeckLinkInputVideoFormatChangeCallback, void*>> VideoFormatChangeCallbacks;
	int32_t NextCallbackId = 0;

	
	
protected:
	bool Open(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat) override;
	bool Start() override;
	bool Stop() override;
	bool Close() override;
};

}
