#include "stdafx.h"
#include <time.h>
#include <cstdarg>
#include "GameSocket.h"
#include "Npc.h"
#include "User.h"
#include "NpcThread.h"
#include "../GameServer/MagicProcess.h"

#include "../shared/database/OdbcRecordset.h"
#include "../shared/database/MagicTableSet.h"
#include "../shared/database/MagicType1Set.h"
#include "../shared/database/MagicType2Set.h"
#include "../shared/database/MagicType4Set.h"
#include "../shared/database/NpcPosSet.h"
#include "../shared/database/ZoneInfoSet.h"
#include "../shared/database/NpcItemSet.h"
#include "../shared/database/MakeItemGroupSet.h"
#include "../shared/database/NpcTableSet.h"
#include "../shared/database/MakeWeaponTableSet.h"
#include "../shared/database/MakeDefensiveTableSet.h"
#include "../shared/database/MakeGradeItemTableSet.h"
#include "../shared/database/MakeLareItemTableSet.h"
#include "../shared/database/ServerResourceSet.h"
#include "Region.h"
#include "../shared/Ini.h"
#include "../shared/packets.h"
#include "../shared/DateTime.h"

using namespace std;

bool g_bNpcExit	= false;
ZoneArray			g_arZone;

std::vector<Thread *> g_timerThreads;

CServerDlg::CServerDlg()
{
	m_iYear = 0; 
	m_iMonth = 0;
	m_iDate = 0;
	m_iHour = 0;
	m_iMin = 0;
	m_iWeather = 0;
	m_iAmount = 0;
	m_bIsNight = false;
	m_byBattleEvent = BATTLEZONE_CLOSE;
	m_sKillKarusNpc = 0;
	m_sKillElmoNpc = 0;
}

bool CServerDlg::Startup()
{
	g_timerThreads.push_back(new Thread(Timer_CheckAliveTest));

	m_sMapEventNpc = 0;
	m_bFirstServerFlag = false;			

	// Server Start
	DateTime time;
	printf("GameServer iniciado el %02d-%02d-%04d a las %02d:%02d.\n\n", time.GetDay(), time.GetMonth(), time.GetYear(), time.GetHour(), time.GetMinute());

	//----------------------------------------------------------------------
	//	DB part initialize
	//----------------------------------------------------------------------
	GetServerInfoIni();

	if (!m_GameDB.Connect(m_strGameDSN, m_strGameUID, m_strGamePWD, false))
	{
		OdbcError *pError = m_GameDB.GetError();
		printf("ERROR: No se puede conectar a la base de datos, se ha recibido el error:\n%s\n", 
			pError->ErrorMessage.c_str());
		delete pError;
		return false;
	}

	//----------------------------------------------------------------------
	//	Communication Part Initialize ...
	//----------------------------------------------------------------------
	if (!m_socketMgr.Listen(AI_SERVER_PORT, MAX_SOCKET))
		return false;

	//----------------------------------------------------------------------
	//	Load tables
	//----------------------------------------------------------------------
	if (!GetMagicTableData()
		|| !GetMagicType1Data()
		|| !GetMagicType2Data()
		|| !GetMagicType4Data()
		|| !GetNpcItemTable()
		|| !GetMakeItemGroupTable()
		|| !GetMakeWeaponItemTableData()
		|| !GetMakeDefensiveItemTableData()
		|| !GetMakeGradeItemTableData()
		|| !GetMakeLareItemTableData()
		|| !GetServerResourceTable()
		|| !GetNpcTableData(false)
		|| !GetNpcTableData(true)
		// Load maps
		|| !MapFileLoad()
		// Spawn NPC threads
		|| !CreateNpcThread())
		return false;

	//----------------------------------------------------------------------
	//	Start NPC THREAD
	//----------------------------------------------------------------------
	ResumeAI();
	return true; 
}

bool CServerDlg::GetMagicTableData()
{
	LOAD_TABLE(CMagicTableSet, &m_GameDB, &m_MagictableArray, false);
}

bool CServerDlg::GetMagicType1Data()
{
	LOAD_TABLE(CMagicType1Set, &m_GameDB, &m_Magictype1Array, false);
}

bool CServerDlg::GetMagicType2Data()
{
	LOAD_TABLE(CMagicType2Set, &m_GameDB, &m_Magictype2Array, false);
}

bool CServerDlg::GetMagicType4Data()
{
	LOAD_TABLE(CMagicType4Set, &m_GameDB, &m_Magictype4Array, false);
}

bool CServerDlg::GetMakeWeaponItemTableData()
{
	LOAD_TABLE(CMakeWeaponTableSet, &m_GameDB, &m_MakeWeaponItemArray, true);
}

bool CServerDlg::GetMakeDefensiveItemTableData()
{
	LOAD_TABLE(CMakeDefensiveTableSet, &m_GameDB, &m_MakeDefensiveItemArray, true);
}

bool CServerDlg::GetMakeGradeItemTableData()
{
	LOAD_TABLE(CMakeGradeItemTableSet, &m_GameDB, &m_MakeGradeItemArray, false);
}

bool CServerDlg::GetMakeLareItemTableData()
{
	LOAD_TABLE(CMakeLareItemTableSet, &m_GameDB, &m_MakeLareItemArray, false);
}

bool CServerDlg::GetServerResourceTable()
{
	LOAD_TABLE(CServerResourceSet, &m_GameDB, &m_ServerResourceArray, false);
}

bool CServerDlg::GetNpcItemTable()
{
	LOAD_TABLE(CNpcItemSet, &m_GameDB, &m_NpcItemArray, false);
}

bool CServerDlg::GetMakeItemGroupTable()
{
	LOAD_TABLE(CMakeItemGroupSet, &m_GameDB, &m_MakeItemGroupArray, false);
}

bool CServerDlg::GetNpcTableData(bool bNpcData /*= true*/)
{
	if (bNpcData)	{ LOAD_TABLE(CNpcTableSet, &m_GameDB, &m_arNpcTable, false); }
	else			{ LOAD_TABLE(CMonTableSet, &m_GameDB, &m_arMonTable, false); }
}

bool CServerDlg::CreateNpcThread()
{
	m_TotalNPC = m_sMapEventNpc;
	m_CurrentNPC = 0;

	LOAD_TABLE_ERROR_ONLY(CNpcPosSet, &m_GameDB, nullptr, false);

	FastGuard lock(m_eventThreadLock);
	foreach_stlmap (itr, g_arZone)
	{
		CNpcThread * pNpcThread = new CNpcThread();
		m_arNpcThread.insert(make_pair(itr->first, pNpcThread));
		m_arEventNpcThread.insert(make_pair(itr->first, new CNpcThread()));

		foreach_stlmap (npcItr, m_arNpc)
		{
			if (npcItr->second->GetZoneID() != itr->first)
				continue;

			CNpc * pNpc = npcItr->second;
			pNpc->Init();
			pNpcThread->m_pNpcs.insert(pNpc);
		}
	}

	printf("[Criaturas Inicio - %d, threads=%lld]\n", (uint16) m_TotalNPC, (long long) m_arNpcThread.size());
	return true;
}

bool CServerDlg::LoadSpawnCallback(OdbcCommand *dbCommand)
{
	// Avoid allocating stack space for these.
	// This method will only ever run in the same thread.
	static int nRandom = 0;
	static double dbSpeed = 0;
	static CNpcTable * pNpcTable = nullptr;
	static CRoomEvent* pRoom = nullptr;
	static char szPath[500];
	static float fRandom_X = 0.0f, fRandom_Z = 0.0f;

	// Unfortunately we cannot simply read what we need directly
	// into the CNpc instance. We have to resort to creating
	// copies of the data to allow for the way they handle multiple spawns...
	// Best we can do, I think, is to avoid allocating it on the stack.
	static uint8	bNumNpc, bZoneID, bActType, bRegenType, bDungeonFamily, bSpecialType,
		bTrapNumber, bDirection, bDotCnt;	
	static uint16	sSid, sRegTime;
	static uint32	nServerNum;
	static int32	iLeftX, iTopZ, iRightX, iBottomZ,
		iLimitMinX, iLimitMinZ, iLimitMaxX, iLimitMaxZ;

	dbCommand->FetchByte(1, bZoneID);
	dbCommand->FetchUInt16(2, sSid);
	dbCommand->FetchByte(3, bActType);
	dbCommand->FetchByte(4, bRegenType);
	dbCommand->FetchByte(5, bDungeonFamily);
	dbCommand->FetchByte(6, bSpecialType);
	dbCommand->FetchByte(7, bTrapNumber);
	dbCommand->FetchInt32(8, iLeftX);
	dbCommand->FetchInt32(9, iTopZ);
	dbCommand->FetchInt32(10, iRightX);
	dbCommand->FetchInt32(11, iBottomZ);
	dbCommand->FetchInt32(12, iLimitMinZ);
	dbCommand->FetchInt32(13, iLimitMinX);
	dbCommand->FetchInt32(14, iLimitMaxX);
	dbCommand->FetchInt32(15, iLimitMaxZ);
	dbCommand->FetchByte(16, bNumNpc);
	dbCommand->FetchUInt16(17, sRegTime);
	dbCommand->FetchByte(18, bDirection);
	dbCommand->FetchByte(19, bDotCnt);
	dbCommand->FetchString(20, szPath, sizeof(szPath));

	uint8 bPathSerial = 1;
	for (uint8 j = 0; j < bNumNpc; j++)
	{
		CNpc * pNpc = new CNpc();

		pNpc->m_byMoveType = bActType;
		pNpc->m_byInitMoveType = bActType;

		bool bMonster = (bActType < 100);
		if (bMonster)
		{
			pNpcTable = m_arMonTable.GetData(sSid);
		}
		else 
		{
			pNpc->m_byMoveType = bActType - 100;
			pNpcTable = m_arNpcTable.GetData(sSid);
		}

		if (pNpcTable == nullptr)
		{
			printf("NPC %d no encontrado en la tabla %s.\n", sSid, bMonster ? "K_MONSTER" : "K_NPC");
			delete pNpc;
			return false;
		}

		pNpc->Load(++m_TotalNPC, pNpcTable, bMonster);
		pNpc->m_byBattlePos = 0;

		if (pNpc->m_byMoveType >= 2)
		{
			pNpc->m_byBattlePos = myrand(1, 3);
			pNpc->m_byPathCount = bPathSerial++;
		}

		pNpc->InitPos();

		pNpc->m_bZone = bZoneID;

		nRandom = abs(iLeftX - iRightX);
		if (nRandom <= 1)
			fRandom_X = (float)iLeftX;
		else
		{
			if (iLeftX < iRightX)
				fRandom_X = (float)myrand(iLeftX, iRightX);
			else
				fRandom_X = (float)myrand(iRightX, iLeftX);
		}

		nRandom = abs(iTopZ - iBottomZ);
		if (nRandom <= 1)
			fRandom_Z = (float)iTopZ;
		else
		{
			if (iTopZ < iBottomZ)
				fRandom_Z = (float)myrand(iTopZ, iBottomZ);
			else
				fRandom_Z = (float)myrand(iBottomZ, iTopZ);
		}

		pNpc->SetPosition(fRandom_X, 0.0f, fRandom_Z);

		pNpc->m_sRegenTime		= sRegTime * SECOND;
		pNpc->m_byDirection		= bDirection;
		pNpc->m_sMaxPathCount	= bDotCnt;

		if ((pNpc->m_byMoveType == 2 || pNpc->m_byMoveType == 3) && bDotCnt == 0)
		{
			pNpc->m_byMoveType = 1;
			TRACE("##### ServerDlg:CreateNpcThread - Path type Error :  nid=%d, sid=%d, name=%s, acttype=%d, path=%d #####\n", 
				pNpc->GetID(), pNpc->GetProtoID(), pNpc->GetName().c_str(), pNpc->m_byMoveType, pNpc->m_sMaxPathCount);
		}

		if (bDotCnt > 0)
		{
			int index = 0;
			for (int l = 0; l < bDotCnt; l++)
			{
				static char szX[5], szZ[5];

				memset(szX, 0, sizeof(szX));
				memset(szZ, 0, sizeof(szZ));

				memcpy(szX, szPath + index, 4);
				index += 4;

				memcpy(szZ, szPath + index, 4);
				index += 4;

				pNpc->m_PathList.pPattenPos[l].x = atoi(szX);
				pNpc->m_PathList.pPattenPos[l].z = atoi(szZ);
			}
		}

		pNpc->m_nInitMinX = pNpc->m_nLimitMinX		= iLeftX;
		pNpc->m_nInitMinY = pNpc->m_nLimitMinZ		= iTopZ;
		pNpc->m_nInitMaxX = pNpc->m_nLimitMaxX		= iRightX;
		pNpc->m_nInitMaxY = pNpc->m_nLimitMaxZ		= iBottomZ;

		// dungeon work
		pNpc->m_byDungeonFamily	= bDungeonFamily;
		pNpc->m_bySpecialType	= (NpcSpecialType) bSpecialType;
		pNpc->m_byRegenType		= bRegenType;
		pNpc->m_byTrapNumber    = bTrapNumber;

		if (pNpc->m_byDungeonFamily > 0)
		{
			pNpc->m_nLimitMinX = iLimitMinX;
			pNpc->m_nLimitMinZ = iLimitMinZ;
			pNpc->m_nLimitMaxX = iLimitMaxX;
			pNpc->m_nLimitMaxZ = iLimitMaxZ;
		}	

		pNpc->m_pMap = GetZoneByID(pNpc->GetZoneID());
		if (pNpc->GetMap() == nullptr)
		{
			printf(_T("Error: NPC %d en la zona %d que no existe."), sSid, bZoneID);
			delete pNpc;
			return false;
		}

		if (!m_arNpc.PutData(pNpc->GetID(), pNpc))
		{
			--m_TotalNPC;
			TRACE("Npc PutData Fail - %d\n", pNpc->GetID());
			delete pNpc;
			continue;
		}

		if (pNpc->GetMap()->m_byRoomEvent > 0 && pNpc->m_byDungeonFamily > 0)
		{
			pRoom = pNpc->GetMap()->m_arRoomEventArray.GetData(pNpc->m_byDungeonFamily);
			if (pRoom == nullptr)
			{
				printf("Error : CServerDlg,, Map Room Npc Fail!!\n");
				delete pNpc;
				return false;
			}

			// this is why their CSTLMap class sucks.
			int *pInt = new int;
			*pInt = pNpc->GetID();
			if (!pRoom->m_mapRoomNpcArray.PutData(*pInt, pInt))
			{
				delete pInt;
				TRACE("### Map - Room Array MonsterNid Fail : nid=%d, sid=%d ###\n", 
					pNpc->GetID(), pNpc->GetProtoID());
			}
		}
	}

	return true;
}

void CServerDlg::ResumeAI()
{
	foreach (itr, m_arNpcThread)
		itr->second->m_thread.start(NpcThreadProc, itr->second);

	FastGuard lock(m_eventThreadLock);
	foreach (itr, m_arEventNpcThread)
		itr->second->m_thread.start(NpcThreadProc, itr->second);

	m_zoneEventThread.start(ZoneEventThreadProc, this);
}

bool CServerDlg::MapFileLoad()
{
	ZoneInfoMap zoneMap;

	m_sTotalMap = 0;
	LOAD_TABLE_ERROR_ONLY(CZoneInfoSet, &m_GameDB, &zoneMap, false); 

	foreach (itr, zoneMap)
	{
		_ZONE_INFO *pZone = itr->second;

		MAP *pMap = new MAP();
		if (!pMap->Initialize(pZone))
		{
			printf("ERROR: No se puede cargar el fichero SMD - %s\n", pZone->m_MapName.c_str());
			delete pZone;
			delete pMap;
			g_arZone.DeleteAllData();
			m_sTotalMap = 0;
			return false;
		}

		delete pZone;
		g_arZone.PutData(pMap->m_nZoneNumber, pMap);
		m_sTotalMap++;
	}

	return true;
}

/**
* @brief	Gets & formats a cached server resource (_SERVER_RESOURCE entry).
*
* @param	nResourceID	Identifier for the resource.
* @param	result	   	The string to store the formatted result in.
*/
void CServerDlg::GetServerResource(int nResourceID, string * result, ...)
{
	_SERVER_RESOURCE *pResource = m_ServerResourceArray.GetData(nResourceID);
	if (pResource == nullptr)
	{
		*result = nResourceID;
		return;
	}

	va_list args;
	va_start(args, result);
	_string_format(pResource->strResource, result, args);
	va_end(args);
}

// game server에 모든 npc정보를 전송..
void CServerDlg::AllNpcInfo()
{
	Packet result(NPC_INFO_ALL);
	result.SByte();
	foreach_stlmap (itr, g_arZone)
	{
		uint32 nZone = itr->first;
		uint8 bCount = 0;

		result.clear();
		result << bCount;

		foreach_stlmap (itr, m_arNpc)
		{
			CNpc *pNpc = itr->second;
			if (pNpc == nullptr
				|| pNpc->GetZoneID() != nZone)	
				continue;

			pNpc->FillNpcInfo(result);
			if (++bCount == NPC_NUM)
			{
				result.put(0, bCount);
				m_socketMgr.SendAllCompressed(&result);

				// Reset packet buffer
				bCount = 0;
				result.clear();
				result << bCount;
			}
		}	

		if (bCount != 0 && bCount < NPC_NUM)
		{
			result.put(0, bCount);
			m_socketMgr.SendAllCompressed(&result);
		}

		Packet serverInfo(AG_SERVER_INFO, uint8(nZone));
		serverInfo << uint16(m_TotalNPC);
		m_socketMgr.SendAll(&serverInfo);
	}
}

Unit * CServerDlg::GetUnitPtr(uint16 id)
{
	if (id < NPC_BAND)
		return GetUserPtr(id);

	return GetNpcPtr(id);
}

CNpc * CServerDlg::GetNpcPtr(uint16 npcId)
{
	return m_arNpc.GetData(npcId);
}

CUser* CServerDlg::GetUserPtr(uint16 sessionId)
{
	FastGuard lock(m_userLock);
	auto itr = m_pUser.find(sessionId);
	if (itr == m_pUser.end())
		return nullptr;

	return itr->second;
}

bool CServerDlg::SetUserPtr(uint16 sessionId, CUser * pUser)
{
	if (sessionId >= MAX_USER)
		return false;

	FastGuard lock(m_userLock);
	auto itr = m_pUser.find(sessionId);
	if (itr != m_pUser.end())
	{
		TRACE("Warning: El usuario %u no ha sido eliminado del mapa de sesiones.\n", sessionId);
		return false; 
	}

	m_pUser[sessionId] = pUser;
	return true;
}

void CServerDlg::DeleteUserPtr(uint16 sessionId)
{
	FastGuard lock(m_userLock);
	auto itr = m_pUser.find(sessionId);
	if (itr != m_pUser.end())
	{
		delete itr->second;
		m_pUser.erase(itr);
	}
}

uint32 THREADCALL CServerDlg::Timer_CheckAliveTest(void * lpParam)
{
	while (g_bRunning)
	{
		g_pMain->CheckAliveTest();
		sleep(10 * SECOND);
	}
	return 0;
}

void CServerDlg::CheckAliveTest()
{
	Packet result(AG_CHECK_ALIVE_REQ);
	SessionMap & sessMap = m_socketMgr.GetActiveSessionMap();
	uint32 count = 0, sessCount = sessMap.size();
	foreach (itr, sessMap)
	{
		if (itr->second->Send(&result))
			count++;
	}
	m_socketMgr.ReleaseLock();

	if (sessCount > 0 && count == 0)
		DeleteAllUserList();
}

void CServerDlg::DeleteAllUserList(CGameSocket *pSock)
{
	// If a server disconnected, show it...
	if (pSock != nullptr)
	{
		printf("[GameServer desconectado = %s]\n", pSock->GetRemoteIP().c_str());
		return;
	}

	// Server didn't disconnect? 
	if (!m_bFirstServerFlag)
		return;

	// If there's no servers even connected, cleanup.
	TRACE("*** DeleteAllUserList - Start *** \n");
	foreach_stlmap (itr, g_arZone)
	{
		MAP * pMap = itr->second;
		if (pMap == nullptr)	
			continue;
		for (int i = 0; i < pMap->GetXRegionMax(); i++)
		{
			for (int j = 0; j < pMap->GetZRegionMax(); j++)
				pMap->m_ppRegion[i][j].m_RegionUserArray.DeleteAllData();
		}
	}

	FastGuard lock(m_userLock);
	foreach (itr, m_pUser)
	{
		if (itr->second == nullptr)  
			continue;

		delete itr->second;
	}
	m_pUser.clear();

	// Party Array Delete 
	m_arParty.DeleteAllData();

	m_bFirstServerFlag = false;
	TRACE("*** DeleteAllUserList - End *** \n");

	printf("[ ELIMINAR Listado de usuarios completo ]\n");
}

void CServerDlg::Send(Packet * pkt)
{
	m_socketMgr.SendAll(pkt);
}

void CServerDlg::GameServerAcceptThread()
{
	m_socketMgr.RunServer();
}

bool CServerDlg::AddObjectEventNpc(_OBJECT_EVENT* pEvent, MAP * pMap)
{
	int sSid = (pEvent->sType == OBJECT_ANVIL || pEvent->sType == OBJECT_ARTIFACT 
		? pEvent->sIndex : pEvent->sControlNpcID);
	if (sSid <= 0)
		return false;

	CNpcTable * pNpcTable = m_arNpcTable.GetData(sSid);
	if(pNpcTable == nullptr)	{
		// TRACE("#### AddObjectEventNpc Fail : [sid = %d], zone=%d #####\n", pEvent->sIndex, zone_number);
		return false;
	}

	CNpc *pNpc = new CNpc();

	pNpc->m_byMoveType = 0;
	pNpc->m_byInitMoveType = 0;

	pNpc->m_byBattlePos = 0;

	pNpc->m_byObjectType = SPECIAL_OBJECT;
	pNpc->m_byGateOpen	= (pEvent->sStatus == 1);

	pNpc->m_bZone	= pMap->m_nZoneNumber;
	pNpc->SetPosition(pEvent->fPosX, pEvent->fPosY, pEvent->fPosZ);

	pNpc->m_nInitMinX	= (int)pEvent->fPosX-1;
	pNpc->m_nInitMinY	= (int)pEvent->fPosZ-1;
	pNpc->m_nInitMaxX	= (int)pEvent->fPosX+1;
	pNpc->m_nInitMaxY	= (int)pEvent->fPosZ+1;	

	pNpc->Load(m_sMapEventNpc++, pNpcTable, false);
	pNpc->m_pMap = pMap;

	if (pNpc->GetMap() == nullptr
		|| !m_arNpc.PutData(pNpc->GetID(), pNpc))
	{
		m_sMapEventNpc--;
		TRACE("Npc PutData Fail - %d\n", pNpc->GetID());
		delete pNpc;
		return false;
	}

	m_TotalNPC = m_sMapEventNpc;
	return true;
}

CNpc * CServerDlg::SpawnEventNpc(uint16 sSid, bool bIsMonster, uint8 byZone, float fX, float fY, float fZ)
{
	CNpcTable * proto = nullptr;
	MAP * pZone = GetZoneByID(byZone);

	if (pZone == nullptr)
		return nullptr;

	if (bIsMonster)
		proto = m_arMonTable.GetData(sSid);
	else
		proto = m_arNpcTable.GetData(sSid);

	if (proto == nullptr)
		return nullptr;

	FastGuard lock(m_eventThreadLock);
	auto itr = m_arEventNpcThread.find(byZone);
	if (itr == m_arEventNpcThread.end())
		return false;

	CNpc * pNpc = new CNpc();

	pNpc->m_bIsEventNpc = true;
	pNpc->m_byMoveType = (bIsMonster ? 1 : 0);
	pNpc->m_byInitMoveType = 1;
	pNpc->m_byBattlePos = 0;

	pNpc->m_bZone = byZone;
	pNpc->SetPosition(fX, fY, fZ);
	pNpc->m_pMap = pZone;

	pNpc->Load(++m_TotalNPC, proto, bIsMonster);
	pNpc->InitPos();

	itr->second->AddNPC(pNpc);
	m_arNpc.PutData(pNpc->GetID(), pNpc);

	return pNpc;
}

void CServerDlg::RemoveEventNPC(CNpc * pNpc)
{
	FastGuard lock(m_eventThreadLock);
	auto itr = m_arEventNpcThread.find(pNpc->GetZoneID());
	if (itr == m_arEventNpcThread.end())
		return;

	itr->second->RemoveNPC(pNpc);
}

void CServerDlg::NpcPropertiesUpdate(uint16 sSid, bool bIsMonster, uint8 byGroup, uint16 sPid)
{
	CNpcTable * proto = nullptr;

	if (bIsMonster)
		proto = m_arMonTable.GetData(sSid);
	else
		proto = m_arNpcTable.GetData(sSid);

	if (proto == nullptr)
		return;

	if (byGroup > 0)
		proto->m_byGroupSpecial = byGroup;

	if (sPid > 0)
		proto->m_sPid = sPid;
}

MAP * CServerDlg::GetZoneByID(int zonenumber)
{
	return g_arZone.GetData(zonenumber);
}

void CServerDlg::GetServerInfoIni()
{
	CIni ini("./AIServer.ini");
	ini.GetString("ODBC", "GAME_DSN", "KN_online", m_strGameDSN, false);
	ini.GetString("ODBC", "GAME_UID", "knight", m_strGameUID, false);
	ini.GetString("ODBC", "GAME_PWD", "knight", m_strGamePWD, false);
}

void CServerDlg::SendSystemMsg(std::string & pMsg, int type)
{
	Packet result(AG_SYSTEM_MSG, uint8(type));
	result << pMsg;
	Send(&result);
}

void CServerDlg::ResetBattleZone()
{
	TRACE("ServerDlg - ResetBattleZone() : start \n");
	foreach_stlmap (itr, g_arZone)
	{
		MAP *pMap = itr->second;
		if (pMap == nullptr || pMap->m_byRoomEvent == 0) 
			continue;
		//if( pMap->IsRoomStatusCheck() == true )	continue;	// 전체방이 클리어 되었다면
		pMap->InitializeRoom();
	}
	TRACE("ServerDlg - ResetBattleZone() : end \n");
}

CServerDlg::~CServerDlg() 
{
	g_bNpcExit = true;

	printf("Esperando a finalizar los hilos de NPC ...");
	foreach (itr, m_arNpcThread)
	{
		CNpcThread * pThread = itr->second;
		pThread->m_thread.waitForExit();
		delete pThread;
	}
	m_arNpcThread.clear();

	FastGuard lock(m_eventThreadLock);
	foreach (itr, m_arEventNpcThread)
	{
		CNpcThread * pThread = itr->second;
		pThread->m_thread.waitForExit();
		delete pThread;
	}
	m_arEventNpcThread.clear();

	printf(" finalizados.\n");

	printf("Esperando a finalizar los hilos de eventos de zona ...");
	m_zoneEventThread.waitForExit();
	printf(" finalizados.\n");

	printf("Esperando a finalizar los hilos del temporizador ...");
	foreach (itr, g_timerThreads)
	{
		(*itr)->waitForExit();
		delete (*itr);
	}
	printf(" finalizados.\n");

	printf("Liberando las sesiones de usuarios ...");
	for (int i = 0; i < MAX_USER; i++)
	{
		if (m_pUser[i] != nullptr)
		{
			delete m_pUser[i];
			m_pUser[i] = nullptr;
		}
	}
	printf(" hecho.\n");

	m_ZoneNpcList.clear();

	printf("Finalizando sistema de conexiones ...");
	m_socketMgr.Shutdown();
	printf(" hecho.\n");
}
