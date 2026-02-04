// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <Nodos/Modules.h>

#include <DeckLinkAPIVersion.h>

#include "DeckLink_generated.h"
#include "nosDeckLinkSubsystem/nosDeckLinkSubsystem.h"

#define NOS_DECKLINK_USED_DRIVER_API_VERSION_MAJOR ((BLACKMAGIC_DECKLINK_API_VERSION >> 24) & 0xFF)
#define NOS_DECKLINK_USED_DRIVER_API_VERSION_MINOR ((BLACKMAGIC_DECKLINK_API_VERSION >> 16) & 0xFF)

#define __NOS_DECKLINK_INTERNAL_STR_IMPL(x) #x
#define __NOS_DECKLINK_INTERNAL_STR(x) __NOS_DECKLINK_INTERNAL_STR_IMPL(x)

#define NOS_DECKLINK_USED_DRIVER_API_VERSION BLACKMAGIC_DECKLINK_API_VERSION_STRING

namespace nos::decklink
{
class Device;

class DeviceManager
{
public:
	void ClearDeviceList();
	Device* GetDevice(int64_t groupId);
	Device* GetDevice(uint32_t deviceIndex);
	const std::vector<std::unique_ptr<Device>>& GetDevices() { return Devices; }
	void LockDevice(uint32_t deviceIndex, bool shared = true);
	void UnlockDevice(uint32_t deviceIndex, bool shared = true);
	static DeviceManager* Instance();
	static void Destroy();
	~DeviceManager();
	sys::decklink::TSettings Settings;
	void LoadDefaultSettings();
	void LoadSettings(sys::decklink::Settings const& settings);
	bool ValidatePortMappings();
	void InitializeDeviceList();
	void FetchApiVersion();
	std::optional<std::string> GetPortMappedChannelName(uint32_t deviceIndex, nosDeckLinkChannel channel);
	nosDeckLinkChannel GetChannelFromPortMappedName(uint32_t deviceIndex, std::string_view portMappedName);
	std::optional<std::array<int, 2>> ApiVersion;
protected:
	std::unordered_map<uint32_t, std::unique_ptr<std::shared_mutex>> DeviceMutexes;
	std::vector<std::unique_ptr<Device>> Devices;
	struct RecursiveMutex
	{
		uint32_t LockCount = 0;
		bool IsSharedLock = false;
	};
	static thread_local std::unordered_map<uint32_t, RecursiveMutex> LockedByThisThread; // deviceIndex, <locked, shared>
private:
	DeviceManager();
	static DeviceManager* SingleInstance;
};

struct DeviceLock
{
	DeviceLock(uint32_t deviceIndex, bool shared = true)
		: DeviceIndex(deviceIndex), SharedLock(shared)
	{
		DeviceManager::Instance()->LockDevice(deviceIndex, SharedLock);
	}
	~DeviceLock()
	{
		DeviceManager::Instance()->UnlockDevice(DeviceIndex, SharedLock);
	}

	DeviceLock(const DeviceLock&) = delete;
	DeviceLock& operator=(const DeviceLock&) = delete;
	DeviceLock(DeviceLock&&) = delete;
protected:
	uint32_t DeviceIndex;
	bool SharedLock;
};
}
