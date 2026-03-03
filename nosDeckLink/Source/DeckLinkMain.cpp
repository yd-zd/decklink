// Copyright MediaZ Teknoloji A.S. All Rights Reserved.

#include <Nodos/PluginHelpers.hpp>
#include <nosVulkanSubsystem/nosVulkanSubsystem.h>
#include <nosDeviceSubsystem/nosDeviceSubsystem.h>
#include <nosDeckLinkSubsystem/nosDeckLinkSubsystem.h>
#include <nosMediaIO/nosMediaIO.h>
#include <nosSync/nosSync.h>

NOS_INIT_WITH_MIN_REQUIRED_MINOR(4)
NOS_VULKAN_INIT()
NOS_DECKLINK_DEVICE_SUBSYSTEM_INIT()
NOS_MEDIAIO_SUBSYSTEM_INIT()
NOS_DEVICE_SUBSYSTEM_INIT()
NOS_SYNC_INIT()

NOS_BEGIN_IMPORT_DEPS()
	NOS_DECKLINK_DEVICE_SUBSYSTEM_IMPORT()
	NOS_VULKAN_IMPORT()
	NOS_MEDIAIO_SUBSYSTEM_IMPORT()
	NOS_DEVICE_SUBSYSTEM_IMPORT()
	NOS_SYNC_IMPORT()
NOS_END_IMPORT_DEPS()

namespace nos::decklink
{
enum class Nodes : int
{
	DMAWrite,
	DMARead,
	WaitFrame,
	Channel,
	Input,
	Output,
	Count
};

nosResult RegisterDMAWriteNode(nosNodeFunctions*);
nosResult RegisterDMAReadNode(nosNodeFunctions*);
nosResult RegisterWaitFrameNode(nosNodeFunctions*);
nosResult RegisterChannelNode(nosNodeFunctions*);

struct DeckLinkPluginFunctions : nos::PluginFunctions
{
	using PluginFunctions::PluginFunctions;

	static nosResult MigrateInOutNodes(nosFbNodePtr node, nosBuffer* outBuffer)
	{
		auto pluginVersion = node->plugin_version();
		auto isVersionLessThan = [](auto* version, int major, int minor) -> bool
		{
			if (!version)
				return true;
			if (version->major() != major)
				return version->major() < major;
			return version->minor() < minor;
		};

		bool needsNodeStatusPortalMigration = isVersionLessThan(pluginVersion, 1, 3);
		if (!needsNodeStatusPortalMigration)
			return NOS_RESULT_SUCCESS;

		fb::TNode cur;
		node->UnPackTo(&cur);

		constexpr auto key = "NodeStatusPortal";
		const bool isOutputNode = node->class_name() && node->class_name()->string_view().ends_with("decklink.Output");
		const char* value = isOutputNode ? "/Auto Resize/ShowStatus;/Channel;/WaitFrame" : "/Channel;/WaitFrame";
		bool migrated = false;

		for (auto& metadata : cur.meta_data_map)
		{
			if (!metadata || metadata->key != key)
				continue;
			if (metadata->value != value)
			{
				metadata->value = value;
				migrated = true;
			}
			break;
		}

		if (!migrated)
			return NOS_RESULT_SUCCESS;

		auto nodeBuffer = nos::EngineBuffer::CopyFrom(cur);
		*outBuffer = nodeBuffer.Release();
		return NOS_RESULT_SUCCESS;
	}

	nosResult ExportNodeFunctions(size_t& outSize, nosNodeFunctions** outList) override
	{
		outSize = static_cast<size_t>(Nodes::Count);
		if (!outList)
			return NOS_RESULT_SUCCESS;

		NOS_RETURN_ON_FAILURE(RegisterDMAWriteNode(outList[(int)Nodes::DMAWrite]))
		NOS_RETURN_ON_FAILURE(RegisterWaitFrameNode(outList[(int)Nodes::WaitFrame]))
		NOS_RETURN_ON_FAILURE(RegisterChannelNode(outList[(int)Nodes::Channel]))
		NOS_RETURN_ON_FAILURE(RegisterDMAReadNode(outList[(int)Nodes::DMARead]))

		// TODO: Remove these when migration of class-named graphs become available in Nodos.
		*outList[(int)Nodes::Input] = nosNodeFunctions{
			.ClassName = NOS_NAME("nos.decklink.Input"),
			.MigrateNode = MigrateInOutNodes
		};
		*outList[(int)Nodes::Output] = nosNodeFunctions{
			.ClassName = NOS_NAME("nos.decklink.Output"),
			.MigrateNode = MigrateInOutNodes
		};
		return NOS_RESULT_SUCCESS;
	}
};
NOS_EXPORT_PLUGIN_FUNCTIONS(DeckLinkPluginFunctions)
} // namespace nos::decklink
