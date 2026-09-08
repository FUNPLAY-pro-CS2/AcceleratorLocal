/**
 * ======================================================
 * Accelerator local
 * Written by Slynx (˙·٠● S l y n x ●٠·˙) 2026, Phoenix (˙·٠●Феникс●٠·˙) 2023-2025, Asher Baker (asherkin) 2011.
 * ======================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from 
 * the use of this software.
 */

#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

#include <ISmmPlugin.h>
#include <iserver.h>
#include "utils.hpp"

class Plugin final : public ISmmPlugin, public IMetamodListener
{
public:
	Plugin();

	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
	bool Unload(char* error, size_t maxlen);
	
private:
	const char* GetAuthor();
	const char* GetName();
	const char* GetDescription();
	const char* GetURL();
	const char* GetLicense();
	const char* GetVersion();
	const char* GetDate();
	const char* GetLogTag();

public: // Hooks
	KHook::Return<void> Hook_GameFrame(ISource2Server* pThis, bool simulating, bool bFirstTick, bool bLastTick);
	KHook::Return<void> Hook_GameServerSteamAPIActivated(ISource2Server* pThis);
	KHook::Return<void> Hook_GameServerSteamAPIDeactivated(ISource2Server* pThis);
	KHook::Return<void> Hook_StartupServer(INetworkServerService* pThis, const GameSessionConfiguration_t& config, ISource2WorldSession* pWorldSession, const char* pszMapName);

	KHook::Virtual<ISource2Server, void, bool, bool, bool>* m_hGameFrame = nullptr;
	KHook::Virtual<ISource2Server, void>* m_hGameServerSteamAPIActivated = nullptr;
	KHook::Virtual<ISource2Server, void>* m_hGameServerSteamAPIDeactivated = nullptr;
	KHook::Virtual<INetworkServerService, void, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*>* m_hStartupServer = nullptr;
};

PLUGIN_GLOBALVARS();

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
