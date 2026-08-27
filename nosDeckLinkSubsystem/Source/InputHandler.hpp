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
	nosBuffer ReadFrameBuffer;

	std::condition_variable FrameArrivedCV;

	bool Flush();
	bool WaitFrameImpl(std::chrono::milliseconds timeout) override;
	bool DmaTransferImpl(void* buffer, size_t size) override;
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

	BMDPixelFormat PixelFormat = bmdFormat8BitYUV;
	BMDDisplayMode DisplayMode = bmdModeUnknown;
};

}
