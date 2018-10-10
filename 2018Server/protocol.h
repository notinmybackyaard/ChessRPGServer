
#define MAX_ID_LEN 10
#define MAX_STR_SIZE  50

#define MAX_BUFF_SIZE   4000
#define MAX_PACKET_SIZE  255

#define BOARD_WIDTH   300
#define BOARD_HEIGHT  300

#define VIEW_RADIUS   10
#define ATTACK_RANGE  4

#define MAX_USER 2000 

#define NPC_START  3000 
#define NUM_OF_NPC  5000 

#define MY_SERVER_PORT  4000

#define CS_LOGIN	1
#define CS_LOGOUT	2
#define CS_MOVE		3
#define CS_ATTACK	4
#define CS_CHAT		5

#define SC_LOGIN_OK			1
#define SC_LOGIN_FAIL		2 
#define SC_POSITION_INFO	3
#define SC_CHAT				4
#define SC_STAT_CHANGE		5
#define SC_REMOVE_OBJECT	6
#define SC_ADD_OBJECT		7

enum OBJECT_TYPE {PLAYER, MONSTER1, MONSTER2, MONSTER3, MONSTER4, OBSTACLE};
enum DIR {UP, DOWN, LEFT, RIGHT};

#pragma pack (push, 1)

///////////////////////////// Client to server

struct cs_packet_login {
	BYTE SIZE;
	BYTE TYPE;
	WCHAR ID_STR[MAX_ID_LEN];
};

struct cs_packet_logout {
	BYTE SIZE;
	BYTE TYPE;
};

struct cs_packet_move {
	BYTE SIZE;
	BYTE TYPE;
	BYTE DIR;
};

struct cs_packet_attack {
	BYTE SIZE;
	BYTE TYPE;
};

struct cs_packet_chat {
	BYTE SIZE;
	BYTE TYPE;
	WCHAR CHAT_STR[MAX_STR_SIZE];
};

///////////////////////////// Server to client

struct sc_packet_login_ok {
	BYTE SIZE;
	BYTE TYPE;
	WORD ID;
	WORD X_POS;
	WORD Y_POS;
	WORD HP;
};

struct sc_packet_login_fail {
	BYTE SIZE;
	BYTE TYPE;
};

struct sc_packet_position_info {
	BYTE SIZE;
	BYTE TYPE;
	WORD ID;
	WORD X_POS;
	WORD Y_POS;
};

struct sc_packet_chat {
	BYTE SIZE;
	BYTE TYPE;
	WORD ID;
	WCHAR CHAT_STR[MAX_STR_SIZE];
};

struct sc_packet_stat_change {
	BYTE SIZE;
	BYTE TYPE;
	WORD HP;
	BYTE LEVEL;
	DWORD EXP;
};

struct sc_packet_remove_object {
	BYTE SIZE;
	BYTE TYPE;
	WORD ID;
};

struct sc_packet_add_object {
	BYTE SIZE;
	BYTE TYPE;
	WORD ID;
	BYTE OBJECT_TYPE;
};

#pragma pack (pop)