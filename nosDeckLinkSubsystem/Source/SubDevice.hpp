// Copyright MediaZ Teknoloji A.S. All Rights Reserved.
#pragma once

#include "Common.hpp"

#include "OutputHandler.hpp"
#include "InputHandler.hpp"

#include <nosMediaIO/nosMediaIO.h>
#include <array>
#include <utility>

namespace nos::decklink
{
class SubDevice
{
public:
	friend class OutputCallback;
	SubDevice(IDeckLink* deviceInterface);
	~SubDevice();
	bool IsValid() const { return Valid; }
	bool IsBusyWith(nosMediaIODirection mode);
	std::map<nosMediaIOFrameGeometry, std::set<nosMediaIOFrameRate>> GetSupportedOutputFrameGeometryAndFrameRates(BMDVideoConnection connection, nosMediaIOVideoScanType scanType, std::unordered_set<nosMediaIOPixelFormat> const& pixelFormats);
	std::map<nosMediaIOFrameGeometry, std::map<nosMediaIOFrameRate, std::set<nosMediaIOPixelFormat>>> GetSupportedOutputVideoFormats(BMDVideoConnection connection, nosMediaIOVideoScanType scanType);
	int32_t AddInputVideoFormatChangeCallback(nosDeckLinkInputVideoFormatChangeCallback callback, void* userData);
	void RemoveInputVideoFormatChangeCallback(uint32_t callbackId);
	int32_t AddFrameResultCallback(nosMediaIODirection dir, nosDeckLinkFrameResultCallback callback, void* userData);
	void RemoveFrameResultCallback(nosMediaIODirection dir, uint32_t callbackId);

	std::string ModelName;
	int64_t SubDeviceIndex = -1;
	int64_t PersistentId = -1;
	int64_t ProfileId = -1;
	int64_t DeviceGroupId = -1;
	int64_t TopologicalId = -1;
	std::string SerialNumber;
	std::string Handle;

	// Output
	bool DoesSupportOutputVideoMode(BMDVideoConnection connection, BMDDisplayMode displayMode, BMDPixelFormat pixelFormat);
	bool OpenOutput(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat, bool useIP);
	bool CloseOutput();
	std::optional<nosDeckLinkReferenceStatus> GetOutputReferenceStatus();

	// Input
	bool OpenInput(BMDPixelFormat pixelFormat, bool useIP);
	bool CloseInput();
	bool ResetInputFrames();

	bool IsIPCapable() const { return IPExtensions != nullptr && !IPFlows.empty(); }
	std::optional<BMDVideoConnection> GetIPVideoConnection(nosMediaIODirection direction) const;
	std::optional<std::string> GetIPFlowSDP(nosDeckLinkIPFlowType flowType) const;

	// Input/Output
	bool StartStream(nosMediaIODirection mode);
	bool WaitFrame(nosMediaIODirection dir, std::chrono::milliseconds timeout);
	void DmaTransfer(nosMediaIODirection dir, void* buffer, size_t size);
	std::optional<nosVec2u> GetDeltaSeconds(nosMediaIODirection dir);
	bool StopStream(nosMediaIODirection mode);

	void TagChannel(nosMediaIODirection dir, nosDeckLinkChannel channel);
	void TagDevice(uint32_t deviceIndex);

	IOHandlerBaseI& GetIO(nosMediaIODirection dir)
	{
		if (dir == NOS_MEDIAIO_DIRECTION_INPUT)
			return Input;
		return Output;
	}

	IDeckLinkProfileManager* ProfileManager = nullptr;
	IDeckLink* DLDevice = nullptr;
	IDeckLinkProfileAttributes* ProfileAttributes = nullptr;
	bool Valid = true;
protected:
	struct IPFlowInfo
	{
		IDeckLinkIPFlow* Flow = nullptr;
		IDeckLinkIPFlowAttributes* Attributes = nullptr;
		int64_t Direction = -1;
		int64_t Type = -1;
		bool Enabled = false;
		bool Receiver = false;
		bool Sender = false;
		std::string SDP;

		IPFlowInfo() = default;
		IPFlowInfo(const IPFlowInfo&) = delete;
		IPFlowInfo& operator=(const IPFlowInfo&) = delete;
		IPFlowInfo(IPFlowInfo&& other) noexcept
			: Flow(other.Flow), Attributes(other.Attributes), Direction(other.Direction), Type(other.Type), Enabled(other.Enabled), Receiver(other.Receiver), Sender(other.Sender), SDP(std::move(other.SDP))
		{
			other.Flow = nullptr;
			other.Attributes = nullptr;
		}
		IPFlowInfo& operator=(IPFlowInfo&& other) noexcept
		{
			if (this == &other)
				return *this;
			Release(Attributes);
			Release(Flow);
			Flow = other.Flow;
			Attributes = other.Attributes;
			Direction = other.Direction;
			Type = other.Type;
			Enabled = other.Enabled;
			Receiver = other.Receiver;
			Sender = other.Sender;
			SDP = std::move(other.SDP);
			other.Flow = nullptr;
			other.Attributes = nullptr;
			return *this;
		}
		~IPFlowInfo()
		{
			Release(Attributes);
			Release(Flow);
		}
	};

	void EnumerateIPFlows();
	void ApplyIPConfiguration();
	bool EnableIPReceiverFlows();
	bool EnableIPSenderFlows();
	void DisableIPFlows();

	IDeckLinkIPExtensions* IPExtensions = nullptr;
	std::vector<IPFlowInfo> IPFlows;
	std::array<std::string, 3> SenderSDPs{};
	bool MissingIPReceiverSDPReported = false;
	IDeckLinkConfiguration* Configuration = nullptr;

	OutputHandler Output{};
	InputHandler Input{};
};
}
