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
	LastProcessedFrame = std::nullopt;
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
	snprintf(watchLogBuf, sizeof(watchLogBuf), "(%s) Wait Frame", GetDeviceChannelString().c_str());
	nosEngine.WatchLog(watchLogBuf, util::Stopwatch::ElapsedString(seconds).c_str());
	return res;
}

void IOHandlerBaseI::DmaTransfer(void* buffer, size_t size)
{
	util::Stopwatch sw;
	DmaTransferImpl(buffer, size);
	char watchLogBuf[128];
	snprintf(watchLogBuf, sizeof(watchLogBuf), "(%s) DMA", GetDeviceChannelString().c_str());
	nosEngine.WatchLog(watchLogBuf, sw.ElapsedString().c_str());
#if NOS_DECKLINK_DIAGNOSTICS
	char title[128], value[128];
	snprintf(title, sizeof(title), "(%s) Last Processed Frame", GetDeviceChannelString().c_str());
	snprintf(value, sizeof(value), "%llu", *LastProcessedFrame);
	nosEngine.WatchLog(title, value);
#endif
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

nosDeckLinkFrameTimingInfo IOHandlerBaseI::GetLastFrameInfo()
{
	std::unique_lock lock(LastHardwareFrameInfoMutex);
	return LastHardwareFrameInfo;
}

std::string IOHandlerBaseI::GetDeviceChannelString() const
{
	std::string str(128, '\0');
	snprintf(str.data(), str.capacity(), "DeckLink Device %d, %s", DeviceIndex, GetChannelName(Channel));
	return str;
}

uint64_t IOHandlerBaseI::TimeToNanoseconds(BMDTimeValue time, BMDTimeScale scale)
{
	return time * 1'000'000'000ULL / scale;
}

void IOHandlerBaseI::OnFrameEnd(nosDeckLinkFrameResult result)
{
	++FramesProcessed;
#if NOS_DECKLINK_DIAGNOSTICS
	char title[128];
	snprintf(title, sizeof(title), "(%s) Frames Processed", GetDeviceChannelString().c_str());
	char value[128];
	snprintf(value, sizeof(value), "%llu", FramesProcessed);
	nosEngine.WatchLog(title, value);
	nosEngine.LogD("(%s) Frame %d ended with result %d", GetDeviceChannelString().c_str(), FramesProcessed, result);
#endif
	for (auto& [callbackId, pair] : FrameResultCallbacks)
	{
		auto& [callback, userData] = pair;
		callback(userData, result, FramesProcessed);
	}
}

}