/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include <Resource.h>

#ifdef GTA_FIVE
#include <ScriptHandlerMgr.h>
#include <scrThread.h>
#include <scrEngine.h>

#include <ResourceMetaDataComponent.h>
#include <ManifestVersion.h>

#include <ICoreGameInit.h>

#include <Pool.h>

struct DummyThread : public GtaThread
{
	DummyThread(fx::Resource* resource)
	{
		rage::scrThreadContext* context = GetContext();
		context->ScriptHash = HashString(resource->GetName().c_str());
		context->ThreadId = HashString(resource->GetName().c_str());

		SetScriptName(resource->GetName().c_str());
	}

	virtual void DoRun() override
	{

	}

	// zero-initialize the structure when new'd (to fix, for instance, 'can remove blips set by other scripts' defaulting to 0xCD)
	void* operator new(size_t size)
	{
		void* data = malloc(size);
		memset(data, 0, size);

		return data;
	}

	void operator delete(void* ptr)
	{
		free(ptr);
	}
};

struct MissionCleanupData
{
	rage::scriptHandler* scriptHandler;
	rage::scriptHandler* lastScriptHandler;

	DummyThread* dummyThread;
	rage::scrThread* lastThread;

	int behaviorVersion;

	MissionCleanupData()
		: scriptHandler(nullptr), lastScriptHandler(nullptr), dummyThread(nullptr), lastThread(nullptr)
	{

	}
};

GtaThread* g_resourceThread;

static void DeleteDummyThread(DummyThread** dummyThread)
{
	__try
	{
		delete *dummyThread;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{

	}

	*dummyThread = nullptr;
}

static constexpr ManifestVersion mfVer1 = "05cfa83c-a124-4cfa-a768-c24a5811d8f9";

static InitFunction initFunction([] ()
{
	fx::Resource::OnInitializeInstance.Connect([] (fx::Resource* resource)
	{
		auto data = std::make_shared<MissionCleanupData>();

		resource->OnStart.Connect([=] ()
		{
			data->scriptHandler = nullptr;
			data->dummyThread = nullptr;

			auto metaData = resource->GetComponent<fx::ResourceMetaDataComponent>();

			data->behaviorVersion = 0;

			auto result = metaData->IsManifestVersionBetween(mfVer1.guid, guid_t{ 0 });

			if (result && *result)
			{
				data->behaviorVersion = 1;
			}
		}, -10000);

		resource->OnActivate.Connect([=] ()
		{
			if (!Instance<ICoreGameInit>::Get()->GetGameLoaded())
			{
				return;
			}

			bool setScriptNow = false;

			// create the script handler if needed
			if (!data->scriptHandler)
			{
				data->dummyThread = new DummyThread(resource);

				// old behavior, removed as it will make a broken handler be left over (lastScriptHandler from AttachScript)
				if (data->behaviorVersion >= 0 && data->behaviorVersion < 1)
				{
					data->scriptHandler = new CGameScriptHandlerNetwork(data->dummyThread);

					data->dummyThread->SetScriptHandler(data->scriptHandler);
				}

				CGameScriptHandlerMgr::GetInstance()->AttachScript(data->dummyThread);

				if (data->behaviorVersion >= 1)
				{
					data->scriptHandler = data->dummyThread->GetScriptHandler();
				}

				setScriptNow = true;
			}

			// set the current script handler
			GtaThread* gtaThread = data->dummyThread;

			data->lastThread = rage::scrEngine::GetActiveThread();
			rage::scrEngine::SetActiveThread(gtaThread);

			data->lastScriptHandler = gtaThread->GetScriptHandler();
			gtaThread->SetScriptHandler(data->scriptHandler);

			if (setScriptNow)
			{
				if (data->behaviorVersion >= 1)
				{
					// make this a network script
					NativeInvoke::Invoke<0x1CA59E306ECB80A5, int>(32, false, -1);

					// get script status; this sets a flag in the CGameScriptHandlerNetComponent
					NativeInvoke::Invoke<0x57D158647A6BFABF, int>();
				}
			}
		}, -10000);

		resource->OnDeactivate.Connect([=] ()
		{
			if (!Instance<ICoreGameInit>::Get()->GetGameLoaded())
			{
				return;
			}

			// only run if we have an active thread
			if (!rage::scrEngine::GetActiveThread())
			{
				return;
			}

			// put back the last script handler
			GtaThread* gtaThread = data->dummyThread;

			// will cause breakage if combined with old behavior with unneeded script handler
			if (data->behaviorVersion >= 1)
			{
				if (gtaThread->GetScriptHandler() != data->scriptHandler)
				{
					assert(gtaThread->GetScriptHandler());

					auto handler = gtaThread->GetScriptHandler();
					data->scriptHandler = handler;
				}
			}

			gtaThread->SetScriptHandler(data->lastScriptHandler);

			// restore the last thread
			rage::scrEngine::SetActiveThread(data->lastThread);
		}, 10000);

		resource->OnStop.Connect([=] ()
		{
			if (!Instance<ICoreGameInit>::Get()->GetGameLoaded())
			{
				return;
			}

			if (data->scriptHandler)
			{
				data->scriptHandler->CleanupObjectList();
				CGameScriptHandlerMgr::GetInstance()->DetachScript(data->dummyThread);

				if (data->behaviorVersion >= 0 && data->behaviorVersion < 1)
				{
					delete data->scriptHandler;
				}

				data->scriptHandler = nullptr;
			}

			// having the function content inlined causes a compiler ICE - so we do it separately
			DeleteDummyThread(&data->dummyThread);
		}, 10000);
	});
});
#endif
