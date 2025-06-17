#include "Common.hpp"

namespace nos::decklink
{

bool IOHandlerBaseI::OpenStream(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat)
{
	if (IsOpen)
		return false;
	if (IsStreamRunning)
		StopStream();
	if (Open(displayMode, pixelFormat))
	{
		IsOpen = true;
		return true;
	}
	return false;
}

bool IOHandlerBaseI::StartStream()
{
	if (!IsOpen)
		return false;
	if (IsStreamRunning)
		return true;
	FramesProcessed = 0;
	LastWaitedFrame = 0;
	if (Start())
	{
		IsStreamRunning = true;
		return true;
	}
	return false;
}

bool IOHandlerBaseI::StopStream()
{
	if (!IsOpen)
		return false;
	if (!IsStreamRunning)
		return true;
	if (Stop())
	{
		IsStreamRunning = false;
		return true;
	}
	return false;
}

bool IOHandlerBaseI::CloseStream()
{
	if (!IsOpen)
		return false;
	if (IsStreamRunning)
		StopStream();
	if (Close())
	{
		IsOpen = false;
		return true;
	}
	return false;
}

bool IOHandlerBaseI::WaitFrame(std::chrono::milliseconds timeout)
{
	util::Stopwatch sw;
	bool res = WaitFrameImpl(timeout);
	auto seconds = sw.Elapsed();
	char watchLogBuf[128];
	snprintf(watchLogBuf, sizeof(watchLogBuf), "DeckLink %d:%s WaitFrame", DeviceIndex, GetChannelName(Channel));
	nosEngine.WatchLog(watchLogBuf, util::Stopwatch::ElapsedString(seconds).c_str());
	return res;
}

void IOHandlerBaseI::DmaTransfer(void* buffer, size_t size)
{
	util::Stopwatch sw;
	DmaTransferImpl(buffer, size);
	char watchLogBuf[128];
	snprintf(watchLogBuf, sizeof(watchLogBuf), "DeckLink %d:%s DMAWrite", DeviceIndex, GetChannelName(Channel));
	nosEngine.WatchLog(watchLogBuf, sw.ElapsedString().c_str());
}

std::optional<nosVec2u> IOHandlerBaseI::GetDeltaSeconds() const
{
	if (!IsOpen)
		return std::nullopt;
	return nosVec2u{ (uint32_t)FrameDuration, (uint32_t)TimeScale };
}

int32_t IOHandlerBaseI::AddFrameResultCallback(nosDeckLinkFrameResultCallback callback, void* userData)
{
	return FrameResultCallbacks.Add(callback, userData);
}

void IOHandlerBaseI::RemoveFrameResultCallback(int32_t callbackId)
{
	FrameResultCallbacks.Remove(callbackId);
}

}