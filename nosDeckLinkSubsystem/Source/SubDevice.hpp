// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include "Common.hpp"

#include "OutputHandler.hpp"
#include "InputHandler.hpp"

#include <nosMediaIO/nosMediaIO.h>

namespace nos::decklink
{
class SubDevice
{
public:
	friend class OutputCallback;
	SubDevice(IDeckLink* deviceInterface);
	~SubDevice();
	bool IsBusyWith(nosMediaIODirection mode);
	bool IsBusy();
	std::map<nosMediaIOFrameGeometry, std::set<nosMediaIOFrameRate>> GetSupportedOutputFrameGeometryAndFrameRates(nosMediaIOVideoScanType scanType, std::unordered_set<nosMediaIOPixelFormat> const& pixelFormats);
	std::map<nosMediaIOFrameGeometry, std::map<nosMediaIOFrameRate, std::set<nosMediaIOPixelFormat>>> GetSupportedOutputVideoFormats(nosMediaIOVideoScanType scanType);
	int32_t AddInputVideoFormatChangeCallback(nosDeckLinkInputVideoFormatChangeCallback callback, void* userData);
	void RemoveInputVideoFormatChangeCallback(uint32_t callbackId);
	int32_t AddFrameResultCallback(nosMediaIODirection dir, nosDeckLinkFrameResultCallback callback, void* user_data);
	void RemoveFrameResultCallback(nosMediaIODirection dir, uint32_t callbackId);

	std::string ModelName;
	int64_t SubDeviceIndex = -1;
	int64_t PersistentId = -1;
	int64_t ProfileId = -1;
	int64_t DeviceGroupId = -1;
	int64_t TopologicalId = -1;
	std::string Handle;

	// Output
	bool DoesSupportOutputVideoMode(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat);
	bool OpenOutput(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat);
	bool CloseOutput();
	bool WaitFrame(nosMediaIODirection dir, std::chrono::milliseconds timeout, nosMediaIOInterlacedFieldType optFieldType);
	void DmaTransfer(nosMediaIODirection dir, void* buffer, size_t size);
	std::optional<nosVec2u> GetDeltaSeconds(nosMediaIODirection dir);

	// Input
	bool OpenInput(BMDPixelFormat pixelFormat);
	bool CloseInput();
	bool ResetInputFrames();

	// Input/Output
	bool StartStream(nosMediaIODirection mode);
	bool StopStream(nosMediaIODirection mode);

	void TagChannel(nosMediaIODirection dir, nosDeckLinkChannel channel);
	void TagDevice(uint32_t deviceIndex);

	constexpr IOHandlerBaseI& GetIO(nosMediaIODirection dir);

	IDeckLinkProfileManager* ProfileManager = nullptr;
	IDeckLink* DLDevice = nullptr;
protected:
	IDeckLinkProfileAttributes* ProfileAttributes = nullptr;

	OutputHandler Output;
	InputHandler Input;
};
}