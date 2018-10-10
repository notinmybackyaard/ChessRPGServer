#define WIN32_LEAN_AND_MEAN  
#define INITGUID
#define _HAS_ITERATOR_DEBUGGING 0

// 디비용

#include <WinSock2.h>
#include <windows.h>
#include <windowsx.h>
#include <thread>
#include <vector>
#include <iostream>
#include <set>
#include <unordered_set>
#include <mutex>
#include <queue>
#include <locale.h>
#include "protocol.h"
#include <sqlext.h>   

#pragma comment (lib, "ws2_32.lib")
#pragma comment(lib,  "lua53.lib")

using namespace std;

void ProcessLogin(int key);
void SendRemoveObject(int client, int object_id);
void SendStatChange(int client);
void SendPutObject(int client, int object_id);

// For Script
extern "C"
{
#include "include\lua.h"
#include "include\lauxlib.h"
#include "include\lualib.h"
}

void display_error(lua_State* L, int errnum)
{
	printf("Erorr : %s\n", lua_tostring(L, -1));
	lua_pop(L, 1);
}

//For DB
void show_error() {	printf("error\n");}
void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);
bool GetUserDataFromDB(int new_key);
void UpdateUserDataToDB(int new_key);

bool IsNPC(int id);

static const int EVT_RECV = 0;
static const int EVT_SEND = 1;
static const int EVT_MOVE = 2;
static const int EVT_PLAYER_MOVE = 3; 
static const int EVT_RESPOWN = 4;
static const int EVT_HEAL = 5;
static const int EVT_ATTACKMOVE = 6;
static const int EVT_ATTACK = 7;

struct EXOver {
	WSAOVERLAPPED wsaover;
	char event_type;
	int event_target;
	WSABUF wsabuf;
	char io_buf[MAX_BUFF_SIZE];
};

struct CLIENT {
	SOCKET s;
	bool in_use;
	short x, y;
	unordered_set<int> viewlist;
	mutex vlm;
	int last_tick_time;
	bool is_active;
	wchar_t Name[MAX_ID_LEN];
	lua_State* L;
	unsigned char dir;
	short hp;
	unsigned char level;
	int exp;
	bool is_alive;
	unsigned char type;
	bool is_attacking;


	EXOver exover;
	int packet_size;
	int prev_size;
	char prev_packet[MAX_PACKET_SIZE];
};

HANDLE g_iocp;

CLIENT g_clients[NUM_OF_NPC];


struct EVENT {
	unsigned int s_time;
	int type;
	int object; // 누가 공격하고 힐링해야 하는가
	int target;
};

class mycomparison
{
	bool reverse;
public:
	mycomparison() {}
	bool operator() (const EVENT lhs, const EVENT rhs) const
	{
		return (lhs.s_time > rhs.s_time);
	}
};

priority_queue <EVENT, vector<EVENT>, mycomparison> timer_queue;

void add_timer(int id, int type, unsigned int s_time, int target)
{
	timer_queue.push(EVENT{ s_time, type, id, target });
}

void timer_thread()
{
	while (true) {
		Sleep(10);
		while (false == timer_queue.empty()) {
			if (timer_queue.top().s_time >= GetTickCount()) break;
			EVENT ev = timer_queue.top();
			timer_queue.pop();
			EXOver *ex = new EXOver;
			ex->event_type = ev.type;
			ex->event_target = ev.target;
			PostQueuedCompletionStatus(g_iocp, 1, ev.object, &ex->wsaover);

		}
	}
}

void error_display(const char* msg, int err_no) {
	WCHAR *lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg;
	wcout << L"에러 " << lpMsgBuf << endl;
	LocalFree(lpMsgBuf);
}

bool CanSee(int a, int b)
{
	int dist_sq = (g_clients[a].x - g_clients[b].x) * (g_clients[a].x - g_clients[b].x)
		+ (g_clients[a].y - g_clients[b].y) * (g_clients[a].y - g_clients[b].y);
	return (dist_sq <= VIEW_RADIUS * VIEW_RADIUS);
}

int GetDis(int a, int b)
{
	int dist_sq = (g_clients[a].x - g_clients[b].x) * (g_clients[a].x - g_clients[b].x)
		+ (g_clients[a].y - g_clients[b].y) * (g_clients[a].y - g_clients[b].y);
	return dist_sq;
}

bool CanAttack(int a, int b)
{
	int dist_x = (g_clients[a].x - g_clients[b].x) * (g_clients[a].x - g_clients[b].x);
	int dist_y = (g_clients[a].y - g_clients[b].y) * (g_clients[a].y - g_clients[b].y);
	return ((dist_x == 1 && dist_y == 0) || (dist_x == 0 && dist_y == 1));
}

bool IsNPC(int id)
{
	return (id >= NPC_START) && (id < NUM_OF_NPC);
}

bool IsAttackRange(int a, int b)
{
	int dist_sq = (g_clients[a].x - g_clients[b].x) * (g_clients[a].x - g_clients[b].x)
		+ (g_clients[a].y - g_clients[b].y) * (g_clients[a].y - g_clients[b].y);
	return (dist_sq <= ATTACK_RANGE * ATTACK_RANGE);
}

void WakeUpNPC(int id, int target)
{
	if (false == IsNPC(id)) return;
	if (g_clients[id].is_attacking) return;

	if (g_clients[id].is_active) return;
	g_clients[id].is_active = true;
	if (g_clients[id].type == MONSTER3 || g_clients[id].type == MONSTER4)
		add_timer(id, EVT_MOVE, GetTickCount(), target);
}

void AttackPlayer(int id, int target)
{
	if (g_clients[id].is_attacking) return;
	
	g_clients[id].is_attacking = true;
	add_timer(id, EVT_ATTACKMOVE, GetTickCount(), target);
	
}

void MonsterAttacked(int cl, int id)
{
	if (false == IsNPC(id)) return;
	if (!g_clients[id].is_alive) return;

	g_clients[id].hp -= 50; // 플레이어 공격력

	//SendChatPacket();


	if (g_clients[id].hp <= 0)
	{
		add_timer(id, EVT_RESPOWN, GetTickCount() + 5000, -1);
		
		g_clients[cl].exp += g_clients[id].level * 5;
		if (g_clients[cl].exp >= pow(2, g_clients[cl].level - 1) * 100)
		{
			g_clients[cl].exp = 0;
			g_clients[cl].level++;
		}
		SendStatChange(cl);


		for (int i = 0; i < MAX_USER; ++i)
		{
			if (!g_clients[i].in_use) continue;
			if (CanSee(id, i))
				SendRemoveObject(i, id);
		}

		g_clients[id].hp = 100;
		g_clients[id].is_alive = false;
	}

	if (g_clients[id].type == MONSTER2) return;
	if (g_clients[id].type == MONSTER4) return;
	AttackPlayer(id, cl);
}

void PlayerAttacked(int cl)
{
	g_clients[cl].hp -= 10;

	if (g_clients[cl].hp <= 0)
	{
		add_timer(cl, EVT_RESPOWN, GetTickCount() + 10000, -1);
		SendRemoveObject(cl, cl);
	}
	SendStatChange(cl);

	if (!g_clients[cl].is_attacking)
		add_timer(cl, EVT_HEAL, GetTickCount() + 5000, -1);
	g_clients[cl].is_attacking = true;

}

void Initialize()
{
	wcout.imbue(locale("korean"));

	for (auto &cl : g_clients) {
		cl.in_use = false;
		cl.x = rand() % BOARD_WIDTH;
		cl.y = rand() % BOARD_HEIGHT;
		cl.exover.event_type = EVT_RECV;
		cl.exover.wsabuf.buf = cl.exover.io_buf;
		cl.exover.wsabuf.len = sizeof(cl.exover.io_buf);
		cl.packet_size = 0;
		cl.prev_size = 0;
		cl.last_tick_time = 0;
		cl.is_active = false;
		cl.dir = 0;
		ZeroMemory(cl.Name, MAX_ID_LEN * sizeof(wchar_t));
		cl.hp = 0;
		cl.level = 0;
		cl.exp = 0;
		cl.is_alive = true;
		cl.type = PLAYER;
		cl.is_attacking = false;
	}

	for (int i = NPC_START; i < NUM_OF_NPC; ++i)
	{
		lua_State* L = luaL_newstate();
		luaL_openlibs(L);
		int error = luaL_loadfile(L, "monster.lua");
		lua_pcall(L, 0, 0, 0);
		lua_getglobal(L, "set_myid");
		lua_pushnumber(L, i);
		lua_pcall(L, 1, 0, 0);
		g_clients[i].L = L;

		lua_getglobal(g_clients[i].L, "LoadMonsterData");
		lua_pcall(g_clients[i].L, 0, 6, 0);
		g_clients[i].type = (int)lua_tonumber(L, -6);
		g_clients[i].x = (int)lua_tonumber(L, -5);
		g_clients[i].y = (int)lua_tonumber(L, -4);
		g_clients[i].level = (int)lua_tonumber(L, -3);
		g_clients[i].hp = (int)lua_tonumber(L, -2);
		char* name = (char *)lua_tostring(L, -1);

		size_t len = strlen(name);
		if (len >= MAX_ID_LEN) len = MAX_ID_LEN - 1;

		size_t wlen;
		mbstowcs_s(&wlen, g_clients[i].Name, len, name, _TRUNCATE);

		lua_pop(L, 6);
	}

	cout << "NPC 초기화 완료" << endl;

	WSADATA   wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

}

void SendPacket(int cl, void *packet)
{
	EXOver *o = new EXOver;
	unsigned char *p = reinterpret_cast<unsigned char *>(packet);
	memcpy(o->io_buf, packet, p[0]);
	o->event_type = EVT_SEND;
	o->wsabuf.buf = o->io_buf;
	o->wsabuf.len = p[0];
	ZeroMemory(&o->wsaover, sizeof(WSAOVERLAPPED));

	int ret = WSASend(g_clients[cl].s, &o->wsabuf, 1, NULL, 0, &o->wsaover, NULL);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		/*if (WSA_IO_PENDING != err_no)
		error_display("Error in SendPacket:", err_no);*/
	}
	//printf("SendPacket to Client [ %d ] Type [ %d ] Size [ %d ]\n", cl, (int)p[1], (int)p[0]);
}

void SendLoginOK(int client)
{
	sc_packet_login_ok p;
	p.ID = client;
	p.SIZE = sizeof(sc_packet_login_ok);
	p.TYPE = SC_LOGIN_OK;
	p.HP = g_clients[client].hp;
	p.X_POS = g_clients[client].x;
	p.Y_POS = g_clients[client].y;

	SendPacket(client, &p);
}

void SendLoginFail(int client)
{

	sc_packet_login_fail p;
	p.SIZE = sizeof(sc_packet_login_fail);
	p.TYPE = SC_LOGIN_FAIL;

	SendPacket(client, &p);

	std::cout << "Client [" << client << "] Login Failed.\n";
}

void SendStatChange(int client)
{
	sc_packet_stat_change p;
	p.SIZE = sizeof(sc_packet_stat_change);
	p.TYPE = SC_STAT_CHANGE;
	p.HP = g_clients[client].hp;
	p.LEVEL = g_clients[client].level;
	p.EXP = g_clients[client].exp;

	SendPacket(client, &p);
}

void SendChatPacket(int client, int chatter, wchar_t* mess)
{
	sc_packet_chat  p;
	p.ID = chatter;
	p.SIZE = sizeof(p);
	p.TYPE = SC_CHAT;
	wcscpy_s(p.CHAT_STR, mess);
	SendPacket(client, &p);
}

void SendRemoveObject(int client, int object_id)
{
	sc_packet_remove_object p;
	p.ID = object_id;
	p.SIZE = sizeof(sc_packet_remove_object);
	p.TYPE = SC_REMOVE_OBJECT;

	SendPacket(client, &p);
}

void SendPutObject(int client, int object_id)
{
	sc_packet_position_info pos_p;
	pos_p.ID = object_id;
	pos_p.SIZE = sizeof(sc_packet_position_info);
	pos_p.TYPE = SC_POSITION_INFO;
	pos_p.X_POS = g_clients[object_id].x;
	pos_p.Y_POS = g_clients[object_id].y;

	SendPacket(client, &pos_p);

	sc_packet_add_object add_p;
	add_p.ID = object_id;
	add_p.SIZE = sizeof(sc_packet_add_object);
	add_p.TYPE = SC_ADD_OBJECT;
	add_p.OBJECT_TYPE = g_clients[object_id].type;

	SendPacket(client, &add_p);
}

void SendAddObject(int client, int object_id)
{
	sc_packet_add_object add_p;
	add_p.ID = object_id;
	add_p.SIZE = sizeof(sc_packet_add_object);
	add_p.TYPE = SC_ADD_OBJECT;
	add_p.OBJECT_TYPE = g_clients[object_id].type;

	SendPacket(client, &add_p);
}


void DisconnectPlayer(int cl)
{
	closesocket(g_clients[cl].s);

	std::cout << "Client [" << cl << "] Disconnected.\n";

	sc_packet_remove_object p;
	p.ID = cl;
	p.SIZE = sizeof(p);
	p.TYPE = SC_REMOVE_OBJECT;

	// 이것도 이중락 따라서 복사하고 쓰자
	g_clients[cl].vlm.lock();
	unordered_set <int> vl_copy = g_clients[cl].viewlist;
	g_clients[cl].vlm.unlock();
	g_clients[cl].viewlist.clear();

	for (int id : vl_copy) {
		if (true == IsNPC(id)) continue;
		g_clients[id].vlm.lock();
		if (g_clients[id].in_use == true) {
			if (0 != g_clients[id].viewlist.count(cl)) {
				g_clients[id].viewlist.erase(cl);
				g_clients[id].vlm.unlock();
				SendPacket(id, &p);
			}
		}
		else
			g_clients[id].vlm.unlock();
	}
	g_clients[cl].in_use = false;

}

void ProcessPacket(int cl, char *packet)
{
	if (CS_LOGIN == packet[1])
	{
		cs_packet_login *p = reinterpret_cast<cs_packet_login *>(packet);
		wcscpy_s(g_clients[cl].Name, p->ID_STR);
		if (*(g_clients[cl].Name) != NULL)
		{
			if (false == GetUserDataFromDB(cl))
			{
				ZeroMemory(g_clients[cl].Name, MAX_ID_LEN * sizeof(wchar_t));
				SendLoginFail(cl);
			}
			else
				ProcessLogin(cl);
		}
		else
		{
			DisconnectPlayer(cl);
			SendLoginFail(cl);
		}
	}
	else if (CS_ATTACK == packet[1])
	{
		if (!g_clients[cl].viewlist.empty())
			for (auto i : g_clients[cl].viewlist)
				if (IsNPC(i))
					if (CanAttack(cl, i))
						MonsterAttacked(cl, i);
		return;
	}
	else if (CS_MOVE == packet[1])
	{
		cs_packet_move *p = reinterpret_cast<cs_packet_move *>(packet);

		switch (p->DIR) {
		case UP:
			g_clients[cl].y--;
			if (0 > g_clients[cl].y) g_clients[cl].y = 0;
			break;
		case DOWN:
			g_clients[cl].y++;
			if (BOARD_HEIGHT <= g_clients[cl].y) g_clients[cl].y = BOARD_HEIGHT - 1;
			break;
		case LEFT:
			g_clients[cl].x--;
			if (0 > g_clients[cl].x) g_clients[cl].x = 0;
			break;
		case RIGHT:
			g_clients[cl].x++;
			if (BOARD_WIDTH <= g_clients[cl].x) g_clients[cl].x = BOARD_WIDTH - 1;
			break;
		default: printf("Unknown Protocol from Client[ %d ]\n", cl);
			return;
		}
	}
	else if (CS_CHAT == packet[1])
	{
		cs_packet_chat *p = reinterpret_cast<cs_packet_chat *>(packet);
		
		if (!g_clients[cl].viewlist.empty())
			for (auto i : g_clients[cl].viewlist)
			{
				if(!IsNPC(i))
				SendChatPacket(i, cl, p->CHAT_STR);
			}
		SendChatPacket(cl, cl, p->CHAT_STR);
		return;
	}
	sc_packet_add_object add_p;
	add_p.ID = cl;
	add_p.SIZE = sizeof(sc_packet_add_object);
	add_p.TYPE = SC_ADD_OBJECT;
	add_p.OBJECT_TYPE = PLAYER;

	sc_packet_position_info pos_p;
	pos_p.ID = cl;
	pos_p.SIZE = sizeof(sc_packet_position_info);
	pos_p.TYPE = SC_POSITION_INFO;
	pos_p.X_POS = g_clients[cl].x;
	pos_p.Y_POS = g_clients[cl].y;


	unordered_set <int> new_view_list; // 새로운 뷰리스트 생성
	for (int i = 0; i < MAX_USER; ++i) {
		if (i == cl) continue;
		if (false == g_clients[i].in_use) continue;
		if (false == CanSee(cl, i)) continue;
		// 새로운 뷰리스트 (나를 기준) // 새로 이동하고 보이는 모든 애들
		new_view_list.insert(i);
	}
	for (int i = NPC_START; i < NUM_OF_NPC; ++i) {
		if (false == CanSee(cl, i)) continue;
		new_view_list.insert(i);

		if (g_clients[i].type == MONSTER1) continue;
		if (g_clients[i].type == MONSTER3) continue;
		if (IsAttackRange(i, cl))
			AttackPlayer(i, cl);
	}

	SendPacket(cl, &pos_p);

	for (auto id : new_view_list) {
		g_clients[cl].vlm.lock();
		// 나의 기존 뷰리스트에는 없었다 // 즉 새로 들어왔다
		if (0 == g_clients[cl].viewlist.count(id)) {
			g_clients[cl].viewlist.insert(id);
			g_clients[cl].vlm.unlock();
			WakeUpNPC(id, cl);
			SendPutObject(cl, id);

			if (true == IsNPC(id)) continue;

			g_clients[id].vlm.lock();
			// 상대방한테 내가 없었다? // 추가
			if (0 == g_clients[id].viewlist.count(cl)) {
				g_clients[id].viewlist.insert(cl);
				g_clients[id].vlm.unlock();
				SendPutObject(id, cl);
			}
			// 상대방한테 내가 있었다? // 위치값만
			else
				g_clients[id].vlm.unlock();
			SendPacket(id, &pos_p);
		}
		// 이동해도 있는데 나의 이전 뷰리스트에도 있었다.
		else {
			// 클라이언트 cl 안쓰니까 즉시 언락
			g_clients[cl].vlm.unlock();
			// 상대방에게는 내가 없었다
			if (true == IsNPC(id)) continue;
			g_clients[id].vlm.lock();
			if (0 == g_clients[id].viewlist.count(cl)) {
				g_clients[id].viewlist.insert(cl);
				g_clients[id].vlm.unlock();
				SendPutObject(id, cl);
			}
			// 상대방에게 내가 있었다 // 그냥 무브 패킷만 보내면 됨
			else {
				g_clients[id].vlm.unlock();
				SendPacket(id, &pos_p);
			}

		}

	}

	// 아마 이동해서 지웠는데 포문으로 다시 접근해서 그러는듯 //해결
	// 클라이언트 마다 만들어줘야 하는가? // 그냥 삭제하는 임시 리스트아닌가? // thread_local로 생성 // 해결
	// 나를 안 그린다.... // 해결
	// disconnect에서 문제 생긴다. // 해결
	// Lock을 걸어야 하는가?

	// 나의 이전 뷰리스트에 있는 애들
	g_clients[cl].vlm.lock();
	unordered_set <int> old_v = g_clients[cl].viewlist;
	g_clients[cl].vlm.unlock();
	for (auto id : old_v) {
		if (0 == new_view_list.count(id)) {
			if (cl == id) continue;
			g_clients[cl].vlm.lock();
			g_clients[cl].viewlist.erase(id); // 계속 락 언락하지 말고 따로 리무브리스트만들고 한번에 지우는게 좋음
			g_clients[cl].vlm.unlock();
			SendRemoveObject(cl, id);

			if (true == IsNPC(id)) continue;
			g_clients[id].vlm.lock();
			// 있을 경우에만 지우게 하자.
			if (0 != g_clients[id].viewlist.count(cl)) {
				g_clients[id].viewlist.erase(cl);
				g_clients[id].vlm.unlock();
				SendRemoveObject(id, cl);
			}
			else {
				g_clients[id].vlm.unlock();
			}
		}
	}
	//g_clients[cl].vlm.unlock(); // 왜 루프 끝나고 언락? 포문 돌릴때 다른 스레드에서 뷰리스트 바꾸면 터질 수 있음
	// 전체 루프를 락을 건다? 흠... 게다가 2중락?
	// 차라리 위에 old_v만들자


	SendPacket(cl, &pos_p);

	for (int i = 0; i < MAX_USER; ++i)
		if (true == g_clients[i].in_use) SendPacket(i, &pos_p);
}


void Move_NPC(int i, int target) {
	unordered_set <int> old_vl;
	for (int id = 0; id < MAX_USER; ++id)
		if (true == g_clients[id].in_use)
			if (true == CanSee(id, i)) {
				old_vl.insert(id);
			}

	switch (rand() % 4) {
	case 0: if (g_clients[i].y > 0) g_clients[i].y--; break;
	case 1: if (g_clients[i].y < BOARD_HEIGHT - 1) g_clients[i].y++; break;
	case 2: if (g_clients[i].x > 0) g_clients[i].x--; break;
	case 3: if (g_clients[i].x < BOARD_WIDTH - 1) g_clients[i].x++; break;
	default: break;
	}

	volatile int k = 0;
	for (int j = 0; j < 10000; ++j) {
		k = k + j;
	}

	unordered_set<int> new_vl;
	for (int id = 0; id < MAX_USER; ++id)
		if (true == g_clients[id].in_use)
			if (true == CanSee(id, i)) new_vl.insert(id);

	if (g_clients[i].type == MONSTER4)
	if (IsAttackRange(i, target))
		AttackPlayer(i, target);

	sc_packet_position_info p_packet;
	p_packet.ID = i;
	p_packet.SIZE = sizeof(sc_packet_position_info);
	p_packet.TYPE = SC_POSITION_INFO;
	p_packet.X_POS = g_clients[i].x;
	p_packet.Y_POS = g_clients[i].y;

	// PutObject
	for (auto id : new_vl)
	{
		g_clients[id].vlm.lock();
		if (0 == g_clients[id].viewlist.count(i))
		{
			g_clients[id].viewlist.insert(i);
			g_clients[id].vlm.unlock();
			SendPutObject(id, i);
		}
		else
		{
			g_clients[id].vlm.unlock();
			SendPacket(id, &p_packet);
		}
	}
	// RemoveObject
	for (auto id : old_vl)
	{
		if (0 == new_vl.count(id))
		{
			g_clients[id].vlm.lock();
			if (0 != g_clients[id].viewlist.count(i)) {
				g_clients[id].viewlist.erase(i);
				g_clients[id].vlm.unlock();
				SendRemoveObject(id, i);
			}
			else {
				g_clients[id].vlm.unlock();
			}
		}
	}

	if (true != new_vl.empty()) {
		add_timer(i, EVT_MOVE, GetTickCount() + 1000, target);
	}
	else // 공격중일때 조건 하나 더 추가
	{
		g_clients[i].is_active = false; // 추가 코드
	}
}

void WorkerThread()
{

	while (true) {
		unsigned long data_size;
		unsigned long long key; // 64비트 모드에서는 long long으로 32비트에서는 long으로
		WSAOVERLAPPED *p_over;

		BOOL is_success = GetQueuedCompletionStatus(g_iocp,
			&data_size, &key, &p_over, INFINITE);
		//printf("GQCS from client [ %d ] with size [ %d ]\n", key, data_size);
		// 접속종료 처리
		if (0 == data_size) {
			UpdateUserDataToDB(key);
			DisconnectPlayer(key);
			continue;
		}
		// 에러 처리
		if (0 == is_success) {
			printf("Error in GQCS key[ %d ]\n", key);
			DisconnectPlayer(key);
			continue;
		}
		// Send/Recv 처리
		EXOver *o = reinterpret_cast<EXOver *>(p_over);
		if (EVT_RECV == o->event_type)
		{
			int r_size = data_size;
			char *ptr = o->io_buf;
			while (0 < r_size) {
				if (0 == g_clients[key].packet_size)
					g_clients[key].packet_size = ptr[0];
				int remain = g_clients[key].packet_size - g_clients[key].prev_size;
				if (remain <= r_size) {
					memcpy(g_clients[key].prev_packet + g_clients[key].prev_size,
						ptr, remain);
					ProcessPacket(static_cast<int>(key), g_clients[key].prev_packet);
					r_size -= remain;
					ptr += remain;
					g_clients[key].packet_size = 0;
					g_clients[key].prev_size = 0;
				}
				else {
					memcpy(g_clients[key].prev_packet + g_clients[key].prev_size,
						ptr,
						r_size);
					g_clients[key].prev_size += r_size;
					r_size -= r_size;
					ptr += r_size;
				}
			}
			unsigned long rflag = 0;
			ZeroMemory(&o->wsaover, sizeof(WSAOVERLAPPED));
			WSARecv(g_clients[key].s, &o->wsabuf, 1, NULL,
				&rflag, &o->wsaover, NULL);
		}
		else if (EVT_SEND == o->event_type)
		{
			delete o;
		}
		else if (EVT_MOVE == o->event_type)
		{
			if (g_clients[key].is_attacking)
			{
				delete o;
				continue;
			}
			EXOver *o = reinterpret_cast<EXOver *>(p_over);
			int player = o->event_target;
			Move_NPC(key, player);
			delete o;
		}
		else if (EVT_PLAYER_MOVE == o->event_type)
		{

			delete o;
		}
		else if (EVT_RESPOWN == o->event_type)
		{
			if (!IsNPC(key))
			{
				g_clients[key].x = 0;
				g_clients[key].y = 0;
				g_clients[key].exp *= 0.5;
				g_clients[key].hp = 100;
				for (int i = 0; i < MAX_USER; ++i)
				{
					if (!g_clients[i].in_use) continue;
					if (!CanSee(i, key)) continue;
					SendPutObject(i, key);

				}
				for (int i = NPC_START; i < NUM_OF_NPC; ++i)
				{
					if (!g_clients[i].is_alive) continue;
					if (!CanSee(i, key)) continue;
					SendPutObject(key, i);
				}
				SendStatChange(key);
			}
			for (int i = 0; i < MAX_USER; ++i)
			{
				if (!g_clients[i].in_use) continue;
				if (CanSee(i, key))
				{
					SendPutObject(i, key);
					g_clients[i].viewlist.insert(key);
				}
			}
			g_clients[key].is_alive = true;
			delete o;
		}
		else if (EVT_HEAL == o->event_type)
		{
			if (g_clients[key].hp <= 0)
			{
				g_clients[key].is_attacking = false;
				delete o;
				continue;
			}

			g_clients[key].hp += 10;

			if (g_clients[key].hp < 100)
				add_timer(key, EVT_HEAL, GetTickCount() + 5000, -1);
			else
			{
				g_clients[key].hp = 100;
				g_clients[key].is_attacking = false;
			}
			
			SendStatChange(key);
			delete o;
		}
		else if (EVT_ATTACKMOVE == o->event_type)
		{
			EXOver *o = reinterpret_cast<EXOver *>(p_over);
			int player = o->event_target;

			if (CanAttack(key, player))
			{
				if (g_clients[key].is_alive && g_clients[player].hp > 0)
					add_timer(key, EVT_ATTACK, GetTickCount(), player);
				else
					g_clients[key].is_attacking = false;
				continue;
			}
			else if (g_clients[key].y > g_clients[player].y) g_clients[key].y--;
			else if (g_clients[key].y < g_clients[player].y) g_clients[key].y++;
			else if (g_clients[key].x > g_clients[player].x) g_clients[key].x--;
			else if (g_clients[key].x < g_clients[player].x) g_clients[key].x++;


			sc_packet_position_info pos_p;
			pos_p.ID = key;
			pos_p.SIZE = sizeof(sc_packet_position_info);
			pos_p.TYPE = SC_POSITION_INFO;
			pos_p.X_POS = g_clients[key].x;
			pos_p.Y_POS = g_clients[key].y;

			for (int i = 0; i < MAX_USER; ++i)
			{
				if (!g_clients[i].in_use) continue;
				if (!CanSee(i, key)) continue;
				SendPacket(i, &pos_p);
			}

			if(CanSee(key, player))
				add_timer(key, EVT_ATTACKMOVE, GetTickCount() + 1000, player);
			else
				g_clients[key].is_attacking = false;
			delete o;
		}
		else if (EVT_ATTACK == o->event_type)
		{
			EXOver *o = reinterpret_cast<EXOver *>(p_over);
			int player = o->event_target;


			PlayerAttacked(player);

			add_timer(key, EVT_ATTACKMOVE, GetTickCount() + 1000, player);
			delete o;
		}
		else
		{
			cout << "Unknown Event Error in Worker Thread!" << endl;
		}
	}
}

void AcceptThread()
{
	auto g_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED); // 마지막 인자에 Overlapped 넣어줘야함

	SOCKADDR_IN bind_addr;
	ZeroMemory(&bind_addr, sizeof(SOCKADDR_IN));
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(MY_SERVER_PORT);
	bind_addr.sin_addr.s_addr = INADDR_ANY; // 아무 주소나 다 받겠다.

	::bind(g_socket, reinterpret_cast<sockaddr *>(&bind_addr),
		sizeof(SOCKADDR)); // 땡땡 안쓰면 C++11의 bind라는 전혀 다른 함수가 호출됨
	listen(g_socket, 1000); // 인자 2번째는 백로그 // 최대 몇개까지 기다리게 할 것인가
	while (true) {
		SOCKADDR_IN c_addr;
		ZeroMemory(&c_addr, sizeof(SOCKADDR_IN));
		c_addr.sin_family = AF_INET;
		c_addr.sin_port = htons(MY_SERVER_PORT);
		c_addr.sin_addr.s_addr = INADDR_ANY;
		int c_length = sizeof(SOCKADDR_IN);

		auto new_socket = WSAAccept(g_socket,
			reinterpret_cast<sockaddr *>(&c_addr),
			&c_length, NULL, NULL);
		cout << "New Client Accepted\n";
		int new_key = -1;
		for (int i = 0; i < MAX_USER; ++i)
			if (false == g_clients[i].in_use) {
				new_key = i;
				break;
			}
		if (-1 == new_key) {
			cout << "MAX USER EXCEEDED!!!" << endl;
			continue;
		}
		cout << "New Client's ID = " << new_key << endl;
		g_clients[new_key].s = new_socket;
		g_clients[new_key].x = 0;
		g_clients[new_key].y = 0;
		ZeroMemory(&g_clients[new_key].exover.wsaover, sizeof(WSAOVERLAPPED)); // 리시브할때마다 클리어 해줘야 한다.   // 오버랩드 구조체 클리어
		CreateIoCompletionPort(reinterpret_cast<HANDLE>(new_socket),
			g_iocp, new_key, 0);
		g_clients[new_key].viewlist.clear(); // 뷰 리스트 클리어
		g_clients[new_key].in_use = true; // 이거 위치 여기여야만 한다는데 왜그런지 모르겠음 // 멀티쓰레드 떄문이라는데....
		unsigned long flag = 0;
		int ret = WSARecv(new_socket, &g_clients[new_key].exover.wsabuf, 1,
			NULL, &flag, &g_clients[new_key].exover.wsaover, NULL);
		if (0 != ret) {
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
				error_display("Recv in AcceptThread", err_no);
		}
	}
}

void ProcessLogin(int key)
{
	sc_packet_add_object add_p;
	add_p.SIZE = sizeof(sc_packet_add_object);
	add_p.TYPE = SC_ADD_OBJECT;
	add_p.ID = key;
	add_p.OBJECT_TYPE = PLAYER;

	sc_packet_position_info pos_p;
	pos_p.ID = key;
	pos_p.SIZE = sizeof(sc_packet_position_info);
	pos_p.TYPE = SC_POSITION_INFO;
	pos_p.X_POS = g_clients[key].x;
	pos_p.Y_POS = g_clients[key].y;

	// 나에게 로그인 OK 패킷 보내기
	SendLoginOK(key);

	// 초기 스텟 보내기
	SendStatChange(key);

	// 나의 접속을 모든 플레이어에게 알림
	for (int i = 0; i < MAX_USER; ++i) {
		if (true == g_clients[i].in_use)
		{
			if (false == CanSee(i, key)) continue;
			if (i == key) continue;

			SendPacket(i, &pos_p);
			SendPacket(i, &add_p);

			g_clients[i].vlm.lock();
			g_clients[i].viewlist.insert(key);
			g_clients[i].vlm.unlock();
		}
	}

	// 나에게 접속중인 다른 플레이어 정보를 전송
	// 나에게 주위에 있는 NPC의 정보를 전송
	for (int i = 0; i < MAX_USER; ++i) {
		if (true == g_clients[i].in_use)
			if (i != key) {
				if (false == CanSee(i, key)) continue;
				add_p.ID = i;
				pos_p.ID = i;
				pos_p.X_POS = g_clients[i].x;
				pos_p.Y_POS = g_clients[i].y;

				g_clients[key].vlm.lock();
				g_clients[key].viewlist.insert(i);
				g_clients[key].vlm.unlock();
				SendPacket(key, &pos_p);
				SendPacket(key, &add_p);
			}
	}
	for (int i = NPC_START; i < NUM_OF_NPC; i++)
	{
		if (false == CanSee(key, i)) continue;
		add_p.ID = i;
		add_p.OBJECT_TYPE = g_clients[i].type; // 루아 연동...?
		pos_p.ID = i;
		pos_p.X_POS = g_clients[i].x;
		pos_p.Y_POS = g_clients[i].y;
		g_clients[key].vlm.lock();
		g_clients[key].viewlist.insert(i);
		g_clients[key].vlm.unlock();
		WakeUpNPC(i, key);
		SendPacket(key, &pos_p);
		SendPacket(key, &add_p);
	}
}

/************************************************************************
/* HandleDiagnosticRecord : display error/warning information
/*
/* Parameters:
/*      hHandle     ODBC handle
/*      hType       Type of handle (SQL_HANDLE_STMT, SQL_HANDLE_ENV, SQL_HANDLE_DBC)
/*      RetCode     Return code of failing command
/************************************************************************/
void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode) // 핸들, 핸들타입, 리턴코드를 넣으면 왜 에러가 났는지 알려주는 코드이다.
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER  iError;
	WCHAR       wszMessage[1000];
	WCHAR       wszState[SQL_SQLSTATE_SIZE + 1];

	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT *)NULL) == SQL_SUCCESS) {
		// Hide data truncated..
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

bool GetUserDataFromDB(int new_key)
{
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLWCHAR szName[MAX_ID_LEN];
	SQLSMALLINT positionX;
	SQLSMALLINT positionY;
	SQLSMALLINT hp;
	SQLCHAR level;
	SQLINTEGER exp;

	SQLLEN cbName = 0, cbPositionX = 0, cbPositionY = 0, cbHP = 0, cbLevel = 0, cbExp = 0;

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2013184002_GameDB", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
					wchar_t QueryText[500] = {};
					wcscpy_s(QueryText, L"EXEC select_UserData ");
					wcscat_s(QueryText, g_clients[new_key].Name);

					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)QueryText, SQL_NTS);
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						// Bind columns 
						retcode = SQLBindCol(hstmt, 1, SQL_C_WCHAR, &szName, MAX_ID_LEN, &cbName); 
						retcode = SQLBindCol(hstmt, 2, SQL_C_SHORT, &positionX, 100, &cbPositionX);
						retcode = SQLBindCol(hstmt, 3, SQL_C_SHORT, &positionY, 100, &cbPositionY);
						retcode = SQLBindCol(hstmt, 4, SQL_C_SHORT, &hp, 100, &cbHP);
						retcode = SQLBindCol(hstmt, 5, SQL_C_UTINYINT, &level, 100, &cbLevel); 
						retcode = SQLBindCol(hstmt, 6, SQL_C_SLONG, &exp, 100, &cbExp);

						// Fetch and print each row of data. On an error, display a message and exit.  
						retcode = SQLFetch(hstmt);
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
						{
							printf("Loading %S UserData From DB Success!!\n", g_clients[new_key].Name);
							g_clients[new_key].x = positionX;
							g_clients[new_key].y = positionY;
							g_clients[new_key].hp = hp;
							g_clients[new_key].level = level;
							g_clients[new_key].exp = exp;
						}
						else
						{
							printf("ID doesn't exist in DB");
							return false;
						}
					}
					else {
						HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					}
					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}
					SQLDisconnect(hdbc);
				}
				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
	return true;
}

void UpdateUserDataToDB(int new_key)
{

	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;


	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2013184002_GameDB", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
					wchar_t QueryText[500] = {};
					wchar_t PosX[10] = {};
					wchar_t PosY[10] = {};
					wchar_t HP[10] = {};
					wchar_t Level[10] = {};
					wchar_t Exp[50] = {};
					wcscpy_s(QueryText,L"EXEC update_UserData @Param1 = ");
					wcscat_s(QueryText, g_clients[new_key].Name);
					wcscat_s(QueryText, L", @Param2 = ");
					wsprintf(PosX, L"%d", g_clients[new_key].x);
					wcscat_s(QueryText, PosX);
					wcscat_s(QueryText, L", @Param3 = ");
					wsprintf(PosY, L"%d", g_clients[new_key].y);
					wcscat_s(QueryText, PosY);
					wcscat_s(QueryText, L", @Param4 = ");
					wsprintf(HP, L"%d", g_clients[new_key].hp);
					wcscat_s(QueryText, HP);
					wcscat_s(QueryText, L", @Param5 = ");
					wsprintf(Level, L"%d", g_clients[new_key].level);
					wcscat_s(QueryText, Level);
					wcscat_s(QueryText, L", @Param6 = ");
					wsprintf(Exp, L"%d", g_clients[new_key].exp);
					wcscat_s(QueryText, Exp);

					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)QueryText, SQL_NTS); // SQL 명령어 사용하는 것 // 실제 명령어 넣어주면 된다.
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						printf("Update %S UserData To DB Success!!\n", g_clients[new_key].Name);
					}
					else {
						HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					}
					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}
					SQLDisconnect(hdbc);
				}
				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
}


int main()
{
	vector <thread> w_threads;
	Initialize();

	for (int i = 0; i < 4; ++i)
		w_threads.push_back(thread{ WorkerThread });

	thread a_thread{ AcceptThread };
	thread t_thread{ timer_thread };

	for (auto &th : w_threads) th.join();
	a_thread.join();
	//ai_thread.join();
	t_thread.join();
	WSACleanup();
}