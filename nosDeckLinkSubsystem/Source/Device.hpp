// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>

#include "Common.hpp"
#include "SubDevice.hpp"

// Nodos
#include <Nodos/Types.h>
#include <Nodos/Modules.h>

#include "nosDeckLinkSubsystem/nosDeckLinkSubsystem.h"

#include "DeckLink_generated.h"

namespace nos::decklink
{

std::vector<std::unique_ptr<class Device>> CreateDevices(std::optional<uint32_t> optGroupId = std::nullopt);

class Device
{
public:
	void SetupFromMainSubDevice();
	Device(uint32_t index, std::vector<std::unique_ptr<SubDevice>>&& subDevices);

	void Destroy();
	void Reinit(uint32_t groupId);

	std::string GetUniqueDisplayName() const;

	std::vector<nosDeckLinkChannel> GetAvailableChannels(nosMediaIODirection mode);

	bool CanOpenChannel(nosMediaIODirection dir, nosDeckLinkChannel channel, SubDevice** outSubDevice = nullptr) const;

	IDeckLinkProfileManager* GetProfileManager() const;

	std::optional<BMDProfileID> GetActiveProfile() const;
	
	/// Once used, this device should be recreated.
	void UpdateProfile(BMDProfileID profileId);

	SubDevice* GetSubDeviceOfChannel(nosMediaIODirection dir, nosDeckLinkChannel channel) const;
	std::pair<SubDevice*, nosMediaIODirection> GetSubDeviceOfOpenChannel(nosDeckLinkChannel channel) const;
	SubDevice* GetSubDevice(int64_t index) const;
	SubDevice* GetMainSubDevice() const;

	// Channels
	bool OpenOutput(nosDeckLinkChannel channel, BMDDisplayMode displayMode, BMDPixelFormat pixelFormat);
	bool OpenInput(nosDeckLinkChannel channel, BMDPixelFormat pixelFormat);
	bool StartStream(nosDeckLinkChannel channel);
	bool StopStream(nosDeckLinkChannel channel);
	bool CloseChannel(nosDeckLinkChannel channel);
	bool ResetInputFrames(nosDeckLinkChannel channel);
	std::optional<nosVec2u> GetCurrentDeltaSecondsOfChannel(nosDeckLinkChannel channel);

	bool WaitFrame(nosDeckLinkChannel channel, std::chrono::milliseconds timeout);
	bool DmaTransfer(nosDeckLinkChannel channel, void* buffer, size_t size);

	void ClearSubDevices();

	int32_t AddDeviceInvalidatedCallback(nosDeckLinkDeviceInvalidatedCallback callback, void* userData);
	void RemoveDeviceInvalidatedCallback(int32_t callbackId);

	uint32_t Index = -1;
	int64_t GroupId = -1;
	std::string ModelName;
protected:
	IDeckLinkStatus* Status = nullptr;
	IDeckLinkNotification* Notification = nullptr;
	class NotificationCallback* NotifCallback = nullptr;
	
	std::vector<std::unique_ptr<SubDevice>> SubDevices;
	std::unordered_map<nosMediaIODirection, std::unordered_map<nosDeckLinkChannel, SubDevice*>> Channel2SubDevice;
	std::unordered_map<nosDeckLinkChannel, std::pair<SubDevice*, nosMediaIODirection>> OpenChannels;

	int32_t NextDeviceInvalidatedCallbackId = 0;
	std::unordered_map<int32_t, std::pair<nosDeckLinkDeviceInvalidatedCallback, void*>> DeviceInvalidatedCallbacks;
	std::unique_ptr<std::mutex> DeviceInvalidatedCallbacksMutex;
};
	
}
