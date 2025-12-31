// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include "Common.hpp"
#include "nosSysDecklink/nosDeckLinkSubsystem.h"
#include "VideoFrame.hpp"
#include <vector>

namespace nos::decklink
{

struct InputHandler : IOHandlerBase<IDeckLinkInput>
{
	~InputHandler() override;

	std::mutex ReadFrameMutex;
	nosBuffer ReadFrameBuffer;

	std::mutex ReadAudioMutex;
	nosBuffer ReadAudioBuffer;
	std::vector<uint8_t> AudioBufferStore;
	BMDAudioSampleRate AudioSampleRate = bmdAudioSampleRate48kHz;
	BMDAudioSampleType AudioSampleType = bmdAudioSampleType16bitInteger;
	uint32_t AudioChannelCount = 2;

	std::condition_variable FrameArrivedCV;

	bool Flush();
	bool WaitFrameImpl(std::chrono::milliseconds timeout) override;
	void DmaTransferImpl(void* buffer, size_t size) override;
	bool UpdateFrameRate(BMDDisplayMode displayMode);

	void OnInputFrameArrived_DeckLinkThread(IDeckLinkVideoInputFrame* frame);
	void OnInputAudioArrived_DeckLinkThread(IDeckLinkAudioInputPacket* audioPacket);
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
