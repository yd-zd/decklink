// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

// DeckLink
#if _WIN32
#include <comdef.h>
#endif
#include <DeckLinkAPI.h>

// Nodos
#include <Nodos/Types.h>

// stl
#include <functional>
#include <optional>
#include <atomic>
#include <chrono>
#include <string>

#include "nosDeckLinkSubsystem/nosDeckLinkSubsystem.h"

#include <Nodos/PluginAPI.h>
#include <nosUtil/Stopwatch.hpp>

#include "EnumConversions.hpp"

namespace nos::decklink
{
template <typename T>
auto Release(T& obj)
{
	if (obj)
	{
		auto refCount = obj->Release();
		obj = nullptr;
		return refCount;
	}
	return 0ul;
}

#if _WIN32
#define dlbool_t	BOOL
#define dlstring_t	BSTR
inline const std::function<void(dlstring_t)> DeleteString = SysFreeString;
inline const std::function<std::string(dlstring_t)> DlToStdString = [](dlstring_t dl_str) -> std::string
{
	int wlen = ::SysStringLen(dl_str);
	int mblen = ::WideCharToMultiByte(CP_ACP, 0, (wchar_t*)dl_str, wlen, NULL, 0, NULL, NULL);

	std::string ret_str(mblen, '\0');
	mblen = ::WideCharToMultiByte(CP_ACP, 0, (wchar_t*)dl_str, wlen, &ret_str[0], mblen, NULL, NULL);

	return ret_str;
};
inline const std::function<dlstring_t(std::string)> StdToDlString = [](std::string std_str) -> dlstring_t
{
	int wlen = ::MultiByteToWideChar(CP_ACP, 0, std_str.data(), (int)std_str.length(), NULL, 0);

	dlstring_t ret_str = ::SysAllocStringLen(NULL, wlen);
	::MultiByteToWideChar(CP_ACP, 0, std_str.data(), (int)std_str.length(), ret_str, wlen);

	return ret_str;
};
inline HRESULT GetDeckLinkIterator(IDeckLinkIterator **deckLinkIterator)
{
	HRESULT result = S_OK;

	// Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
	result = CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)deckLinkIterator);
	if (FAILED(result))
		nosEngine.LogE("A DeckLink iterator could not be created. The DeckLink drivers may not be installed.");

	return result;
}
#else
#define dlbool_t	bool
#define dlstring_t	const char*
#define BOOL bool
const auto DeleteString = [](dlstring_t dl_str) { free((void*)dl_str); };
const auto DlToStdString = [](dlstring_t dl_str) -> std::string { return dl_str; };
inline HRESULT GetDeckLinkIterator(IDeckLinkIterator **deckLinkIterator)
{
	HRESULT result = S_OK;

	// Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
	*deckLinkIterator = CreateDeckLinkIteratorInstance();
	if (*deckLinkIterator == NULL)
	{
		result = E_FAIL;
	}

	if (FAILED(result))
		nosEngine.LogE("A DeckLink iterator could not be created. The DeckLink drivers may not be installed.");

	return result;
}
#endif


template <typename T>
class Object : public T
{
public:
	Object() :
		RefCount(1)
	{
	}

	virtual ~Object() = default;

	// IUnknown needs only a dummy implementation
	HRESULT	STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) override
	{
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++RefCount;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG newRefValue = --RefCount;

		if (newRefValue == 0)
			delete this;

		return newRefValue;
	}

private:
	std::atomic<int32_t> RefCount;
};

template <typename T>
struct Callbacks
{
	std::unordered_map<int32_t, std::pair<T, void*>> Map;
	int32_t NextId = 0;
	int32_t Add(T callback, void* userData)
	{
		Map[NextId] = { callback, userData };
		return NextId++;
	}
	void Remove(int32_t callbackId)
	{
		Map.erase(callbackId);
	}
	auto begin()
	{
		return Map.begin();
	}
	auto end()
	{
		return Map.end();
	}
};

struct IOHandlerBaseI
{
	virtual ~IOHandlerBaseI() = default;
	// Info
	nosDeckLinkChannel Channel = NOS_DECKLINK_CHANNEL_INVALID;
	uint32_t DeviceIndex = -1;

	BMDTimeValue FrameDuration = 0;
	BMDTimeScale TimeScale = 0;
	
	uint32_t FramesProcessed = 0;

	bool IsInterlaced = false;

	virtual bool Open(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat) = 0;
	virtual bool Close() = 0;

	bool OpenStream(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat);
	bool StartStream();
	bool StopStream();
	bool CloseStream();

	bool WaitFrame(std::chrono::milliseconds timeout);
	void DmaTransfer(void* buffer, size_t size);
	std::optional<nosVec2u> GetDeltaSeconds() const;
	int32_t AddFrameResultCallback(nosDeckLinkFrameResultCallback callback, void* userData);
	void RemoveFrameResultCallback(int32_t callbackId);

protected:
	virtual bool Start() = 0;
	virtual bool WaitFrameImpl(std::chrono::milliseconds timeout) = 0;
	virtual void DmaTransferImpl(void* buffer, size_t size) = 0;
	virtual bool Stop() = 0;
	void OnFrameEnd(nosDeckLinkFrameResult result)
	{
		++FramesProcessed;
		for (auto& [callbackId, pair] : FrameResultCallbacks)
		{
			auto& [callback, userData] = pair;
			callback(userData, result, FramesProcessed);
		}
	}
	Callbacks<nosDeckLinkFrameResultCallback> FrameResultCallbacks;
private:
	std::atomic_bool IsOpen = false;
	std::atomic_bool IsStreamRunning = false;
public:
	bool IsCurrentlyOpen() const
	{
		return IsOpen;
	}
	bool IsCurrentlyRunning() const
	{
		return IsStreamRunning;
	}
};
	
template <typename T>
struct IOHandlerBase : IOHandlerBaseI
{
	T* Interface = nullptr;
	~IOHandlerBase() override = default;
	operator bool() const
	{
		return Interface != nullptr;
	}
	T* operator->() const
	{
		return Interface;
	}
};

inline bool IOHandlerBaseI::OpenStream(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat)
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

inline bool IOHandlerBaseI::StartStream()
{
	if (!IsOpen)
		return false;
	if (IsStreamRunning)
		return true;
	FramesProcessed = 0;
	if (Start())
	{
		IsStreamRunning = true;
		return true;
	}
	return false;
}

inline bool IOHandlerBaseI::StopStream()
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

inline bool IOHandlerBaseI::CloseStream()
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

inline bool IOHandlerBaseI::WaitFrame(std::chrono::milliseconds timeout)
{
	util::Stopwatch sw;
	bool res = WaitFrameImpl(timeout);
	auto seconds = sw.Elapsed();
	char watchLogBuf[128];
	snprintf(watchLogBuf, sizeof(watchLogBuf), "DeckLink %d:%s WaitFrame", DeviceIndex, GetChannelName(Channel));
	nosEngine.WatchLog(watchLogBuf, util::Stopwatch::ElapsedString(seconds).c_str());
	return res;
}

inline void IOHandlerBaseI::DmaTransfer(void* buffer, size_t size)
{
	util::Stopwatch sw;
	DmaTransferImpl(buffer, size);
	char watchLogBuf[128];
	snprintf(watchLogBuf, sizeof(watchLogBuf), "DeckLink %d:%s DMAWrite", DeviceIndex, GetChannelName(Channel));
	nosEngine.WatchLog(watchLogBuf, sw.ElapsedString().c_str());
}

inline std::optional<nosVec2u> IOHandlerBaseI::GetDeltaSeconds() const
{
	if (!IsOpen)
		return std::nullopt;
	return nosVec2u{ (uint32_t)FrameDuration, (uint32_t)TimeScale };
}

inline int32_t IOHandlerBaseI::AddFrameResultCallback(nosDeckLinkFrameResultCallback callback, void* userData)
{
	return FrameResultCallbacks.Add(callback, userData);
}

inline void IOHandlerBaseI::RemoveFrameResultCallback(int32_t callbackId)
{
	FrameResultCallbacks.Remove(callbackId);
}
}
