#include "stdafx.h"
#include <sstream>
#include "../shared/Ini.h"

extern bool g_bRunning;
std::vector<Thread *> g_timerThreads;

LoginServer::LoginServer() : m_sLastVersion(__VERSION), m_fp(nullptr)
{
}

bool LoginServer::Startup()
{
	GetInfoFromIni();

	m_fp = fopen("./Login.log", "a");
	if (m_fp == nullptr)
	{
		printf("ERROR: Unable to open log file.\n");
		return false;
	}

	if (!m_DBProcess.Connect(m_ODBCName, m_ODBCLogin, m_ODBCPwd)) 
	{
		printf("ERROR: No es posible conectarse a la base de datos con la configuracion actual.\n");
		return false;
	}

	printf("Connected to database server.\n");
	if (!m_DBProcess.LoadVersionList())
	{
		printf("ERROR: No ha sido posible cargar el listado de versiones.\n");
		return false;
	}

	printf("Ultima version en la base de datos: %d\n", GetVersion());
	InitPacketHandlers();

	if (!m_socketMgr.Listen(_LISTEN_PORT, MAX_USER))
	{
		printf("ERROR: No se puede escuchar en el puerto especificado.\n");
		return false;
	}

	m_socketMgr.RunServer();
	g_timerThreads.push_back(new Thread(Timer_UpdateUserCount));
	return true;
}

uint32 LoginServer::Timer_UpdateUserCount(void * lpParam)
{
	while (g_bRunning)
	{
		g_pMain->UpdateServerList();
		sleep(60 * SECOND);
	}
	return 0;
}

void LoginServer::GetServerList(Packet & result)
{
	FastGuard lock(m_serverListLock);
	result.append(m_serverListPacket.contents(), m_serverListPacket.size());
}

void LoginServer::UpdateServerList()
{
	// Update the user counts first
	m_DBProcess.LoadUserCountList();

	FastGuard lock(m_serverListLock);
	Packet & result = m_serverListPacket;

	result.clear();
	result << uint8(m_ServerList.size());
	foreach (itr, m_ServerList) 
	{		
		_SERVER_INFO *pServer = *itr;

		result << pServer->strServerIP;
#if __VERSION >= 1888
		result << pServer->strLanIP;
#endif
		result << pServer->strServerName;

		if (pServer->sUserCount <= pServer->sPlayerCap)
			result << pServer->sUserCount;
		else
			result << int16(-1);
#if __VERSION >= 1453
		result << pServer->sServerID << pServer->sGroupID;
		result << pServer->sPlayerCap << pServer->sFreePlayerCap;

#if __VERSION < 1600
		result << uint8(1); // unknown, 1 in 15XX samples, 0 in 18XX+
#else
		result << uint8(0); 
#endif

		// we read all this stuff from ini, TO-DO: make this more versatile.
		result	<< pServer->strKarusKingName << pServer->strKarusNotice 
			<< pServer->strElMoradKingName << pServer->strElMoradNotice;
#endif
	}
}

void LoginServer::GetInfoFromIni()
{
	CIni ini(CONF_LOGIN_SERVER);

	ini.GetString("DOWNLOAD", "URL", "ftp.yoursite.net", m_strFtpUrl, false);
	ini.GetString("DOWNLOAD", "PATH", "/", m_strFilePath, false);

	ini.GetString("ODBC", "DSN", "KN_online", m_ODBCName, false);
	ini.GetString("ODBC", "UID", "knight", m_ODBCLogin, false);
	ini.GetString("ODBC", "PWD", "knight", m_ODBCPwd, false);

	int nServerCount = ini.GetInt("SERVER_LIST", "COUNT", 1);
	if (nServerCount <= 0) 
		nServerCount = 1;

	char key[20]; 
	_SERVER_INFO* pInfo = nullptr;

	m_ServerList.reserve(nServerCount);

	// TO-DO: Replace this nonsense with something a little more versatile
	for (int i = 0; i < nServerCount; i++)
	{
		pInfo = new _SERVER_INFO;

		_snprintf(key, sizeof(key), "SERVER_%02d", i);
		ini.GetString("SERVER_LIST", key, "127.0.0.1", pInfo->strServerIP, false);

		_snprintf(key, sizeof(key), "LANIP_%02d", i);
		ini.GetString("SERVER_LIST", key, "127.0.0.1", pInfo->strLanIP, false);

		_snprintf(key, sizeof(key), "NAME_%02d", i);
		ini.GetString("SERVER_LIST", key, "TEST|Server 1", pInfo->strServerName, false);

		_snprintf(key, sizeof(key), "ID_%02d", i);
		pInfo->sServerID = ini.GetInt("SERVER_LIST", key, 1);

		_snprintf(key, sizeof(key), "GROUPID_%02d", i);
		pInfo->sGroupID = ini.GetInt("SERVER_LIST", key, 1);

		_snprintf(key, sizeof(key), "PREMLIMIT_%02d", i);
		pInfo->sPlayerCap = ini.GetInt("SERVER_LIST", key, MAX_USER);

		_snprintf(key, sizeof(key), "FREELIMIT_%02d", i);
		pInfo->sFreePlayerCap = ini.GetInt("SERVER_LIST", key, MAX_USER);

		_snprintf(key, sizeof(key), "KING1_%02d", i);
		ini.GetString("SERVER_LIST", key, "", pInfo->strKarusKingName);

		_snprintf(key, sizeof(key), "KING2_%02d", i);
		ini.GetString("SERVER_LIST", key, "", pInfo->strElMoradKingName);

		_snprintf(key, sizeof(key), "KINGMSG1_%02d", i);
		ini.GetString("SERVER_LIST", key, "", pInfo->strKarusNotice);

		_snprintf(key, sizeof(key), "KINGMSG2_%02d", i);
		ini.GetString("SERVER_LIST", key, "", pInfo->strElMoradNotice);

		m_ServerList.push_back(pInfo);
	}

	// Read news from INI (max 3 blocks)
#define BOX_START '#' << uint8(0) << '\n'
#define LINE_ENDING uint8(0) << '\n'
#define BOX_END BOX_START << LINE_ENDING

	m_news.Size = 0;
	std::stringstream ss;
	for (int i = 0; i < 3; i++)
	{
		string title, message;

		_snprintf(key, sizeof(key), "TITLE_%02d", i);
		ini.GetString("NEWS", key, "", title);
		if (title.empty())
			continue;

		_snprintf(key, sizeof(key), "MESSAGE_%02d", i);
		ini.GetString("NEWS", key, "", message);
		if (message.empty())
			continue;

		size_t oldPos = 0, pos = 0;
		ss << title << BOX_START;

		// potentially support multiline by making | act as linebreaks (same as the TBL afaik, so at least we're conformant).
		//replace(messages[i].begin(), messages[i].end(), '|', '\n');
		//while ((pos = message.find('\r', pos)) != string::npos)
		//	message.erase(pos, 1);
		//Remove \n for now, perhaps re-implement later
		//while ((pos = message.find('\n', pos)) != string::npos)
		//	message.erase(pos, 1);

		ss << message << LINE_ENDING << BOX_END;
	}

	m_news.Size = ss.str().size();
	if (m_news.Size)
		memcpy(&m_news.Content, ss.str().c_str(), m_news.Size);
}

void LoginServer::WriteLogFile(string & logMessage)
{
	FastGuard lock(m_lock);
	fwrite(logMessage.c_str(), logMessage.length(), 1, m_fp);
	fflush(m_fp);
}

void LoginServer::ReportSQLError(OdbcError *pError)
{
	if (pError == nullptr)
		return;

	// This is *very* temporary.
	string errorMessage = string_format(_T("ODBC error occurred.\r\nSource: %s\r\nError: %s\r\nDescription: %s\n"),
		pError->Source.c_str(), pError->ExtendedErrorMessage.c_str(), pError->ErrorMessage.c_str());

	TRACE("%s", errorMessage.c_str());
	WriteLogFile(errorMessage);
	delete pError;
}

LoginServer::~LoginServer() 
{
	printf("Esperando a cerrar los hilos del temporizador ...");
	foreach (itr, g_timerThreads)
	{
		(*itr)->waitForExit();
		delete (*itr);
	}
	printf(" exited.\n");

	foreach (itr, m_ServerList)
		delete *itr;
	m_ServerList.clear();

	foreach (itr, m_VersionList)
		delete itr->second;
	m_VersionList.clear();

	if (m_fp != nullptr)
		fclose(m_fp);

	printf("Finalizando sistema de conexiones ...");
	m_socketMgr.Shutdown();
	printf(" hecho.\n");
}
