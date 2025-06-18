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

	std::mutex ReadFrameMutex;
	//std::unique_ptr<VideoFrame> ReadFrame = nullptr;
	//std::condition_variable ReadFrameReadyCond;
	nosBuffer ReadFrameBuffer;

	std::mutex LastFrameInfoMutex;
	std::condition_variable FrameArrivedCond;

	void FreeReadFrame(std::unique_lock<std::mutex>& readFrameLock);
	bool Flush();
	bool WaitFrameImpl(std::chrono::milliseconds timeout) override;
	void DmaTransferImpl(void* buffer, size_t size) override;
	nosDeckLinkFrameTimingInfo GetLastFrameInfo() override;
	bool UpdateFrameRate(BMDDisplayMode displayMode);

	void OnInputFrameArrived_DeckLinkThread(IDeckLinkVideoInputFrame* frame);
	void OnInputVideoFormatChanged_DeckLinkThread(BMDDisplayMode newDisplayMode, BMDPixelFormat pixelFormat);

	int32_t AddInputVideoFormatChangeCallback(nosDeckLinkInputVideoFormatChangeCallback callback, void* userData);
	void RemoveInputVideoFormatChangeCallback(int32_t callbackId);
	
	std::mutex CallbacksMutex;
	Callbacks<nosDeckLinkInputVideoFormatChangeCallback> VideoFormatChangeCallbacks;

protected:
	bool Open(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat) override;
	bool Start() override;
	bool Stop() override;
	bool Close() override;
};

}
