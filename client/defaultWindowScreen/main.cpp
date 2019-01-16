/**
@file main.cpp

@mainpage 64bit Asynnc- Model 서버 접속 다중 유저 공유 마우스 그림판. 왼쪽 버튼 + 드래그는 펜 그리기.

@date 2018/07/26

@author 김규태(gyutae100@gmail.com)

@details  64bit Asynnc- Model 서버 접속 다중 유저 공유 마우스 그림판. 마우스 왼쪽 버튼 + 드래그는 펜 그리기.

@version 0.0.1
*/

#define _WINSOCK_DEPRECATED_NO_WARNINGS //<WSAAsyncSelect 비권장 에러 방지 매크로.

#pragma comment(lib, "ws2_32")

//네트워크 헤더파일 목록.
#include <WinSock2.h>
#include <Ws2tcpip.h>

//윈도우 헤더파일 목록.
#include <Windows.h>

//c++ 헤더 파일 목록.
#include <iostream>
using namespace std;

//c 헤더파일 목록.
#include <string.h>
#include <stdlib.h>
#include <time.h>

//StreamSQ 헤더파일.
#include "CStreamSQ.h"

//서버 네트워크 정보.
#define SERVER_PORT 25000
#define SERVER_IP L"127.0.0.1"

//네트워크 메시지 정의.
#define UM_NETWORK (WM_USER + 1)	//사용자 정의 윈도우 메시지


/**
brief DRAW 패킷 헤더 구조.
*/
#pragma pack(push, 1)
struct HEADER_DRAW {

	unsigned short _len_byte_payload;	//<페이로드 길이.
};
#pragma pack(pop)

/**
brief DRAW 패킷 페이로드 구조.
*/
#pragma pack(push, 1)
struct PAYLOAD_DRAW {

	int		_start_x;
	int		_start_y;

	int		_end_x;
	int		_end_y;
};
#pragma pack(pop)

/**
brief DRAW. pack 압축 적용.
*/
#pragma pack(push, 1)
struct PACKET_DRAW {

	HEADER_DRAW _header;		///<DRAW 패킷 header.
	int		_start_x;
	int		_start_y;
	int		_end_x;
	int		_end_y;
};
#pragma pack(pop)


//전역 변수 목록.
SOCKET g_server_socket;				///<서버 소켓.
HWND g_hwnd;						///<전역 윈도우 핸들.

BOOL g_is_connected_with_server;    ///<서버 커넥트 여부 체크.

CStreamSQ g_recv_stream;			///<수신 스트림 링버퍼
CStreamSQ g_send_stream;			///<송신 스트림 링버퍼


//네트워크 함수 목록
BOOL InitNetwork();
void ProcSend();
void ProcRead();
void Disconnect();
BOOL SendDrawPacket(int start_x, int start_y, int end_x, int end_y);


//콜백 함수 목록.
LRESULT CALLBACK WndProc(HWND hWnd, UINT iMsg,
	WPARAM wParam, LPARAM lParam);


//메인 로직.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpszCmdLine, int nCmdShow){

	//콘솔창 동시 열림 설정.
	AllocConsole();
	freopen("CONOUT$", "wt", stdout);

	HWND hWnd;
	MSG msg;

	//유니코드 한국어 설정.
	_wsetlocale(LC_ALL, L"korean");

	//윈도우 클래스 생성.
	WNDCLASS WndClass;                                                   
	WndClass.style = CS_HREDRAW | CS_VREDRAW;
	WndClass.lpfnWndProc = WndProc;                                 
	WndClass.cbClsExtra = 0;
	WndClass.cbWndExtra = 0;
	WndClass.hInstance = hInstance;                                 
	WndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);       
	WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);     
	WndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	WndClass.lpszMenuName = NULL;                                  
	WndClass.lpszClassName = L"Mouse Drawing Picture";          
	
	//생성한 윈도우 클래스를 레지스터에 등록한다.
	RegisterClass(&WndClass);                                           
																		
	//윈도우창 생성 및 해당 핸들 반환한다.											
	hWnd = CreateWindow(                                               
		L"Mouse Drawing Picture",                                             
		L"Window Title Name",                                               
		WS_OVERLAPPEDWINDOW,                                     
																 
																
		CW_USEDEFAULT,                                                 
		CW_USEDEFAULT,                                                  
		CW_USEDEFAULT,                                                  
		CW_USEDEFAULT,                                                   
		NULL,
		NULL,
		hInstance,
		NULL
	);

	//전역 윈도우 핸들 복사.
	g_hwnd = hWnd;
	
	//네트워크 설정 초기화 및 서버에 연결한다.
	InitNetwork();

	//윈도우를 화면에 출력한다.
	ShowWindow(hWnd, nCmdShow);                               
	UpdateWindow(hWnd);                                             

	//메시지 루프이다.
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);                                       
		DispatchMessage(&msg);                                         
	}

	//네트워크 서버 연결 해제.
	closesocket(g_server_socket);
	WSACleanup();

	return msg.wParam;
}



/**
@brief 윈도우 프로시저 함수
@brief 윈도우의 마우스 관련 이벤트 처리한다.
*/
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam){

	static INT64 start_x; ///<이전 start_x좌표
	static INT64 start_y; ///<이전 start_y좌표

	static BOOL has_pushed_left_mouse_button = FALSE; ///<마우스 왼쪽 버튼이 눌려졌을 때 True, 아닌 경우 False.

	switch (message)
	{

	//네트워크 로직 파트.
	case UM_NETWORK: 

		switch (WSAGETSELECTEVENT(lParam)) {

		case FD_READ: 

			ProcRead();
			break;
		
		case FD_WRITE: 

			ProcSend();
			break;
		

		case FD_CONNECT: 

			g_is_connected_with_server = true;
			break;

		//종료
		case FD_CLOSE: {

			if (TRUE == g_is_connected_with_server) {
			
				PostQuitMessage(0);
			}
			break;
		}
	}
	break;

	//마우스 왼쪽 버튼을 누르고 있을 때
	 //초기 시작 점 한번 설정 및 플래그를 설정한다.
	case WM_LBUTTONDOWN: 

		start_x = LOWORD(lParam);
		start_y = HIWORD(lParam);

		has_pushed_left_mouse_button = TRUE;

		break;
	

	//마우스가 이동할 때
	case WM_MOUSEMOVE: 

		if (has_pushed_left_mouse_button == TRUE) {

			int end_x = LOWORD(lParam);
			int end_y = HIWORD(lParam);

			if (start_x == end_x && start_y == end_y) {
			
				break;
			}

			SendDrawPacket(start_x, start_y, end_x, end_y);

			start_x = end_x;
			start_y = end_y;

		}
		break;

	//마우스 왼쪽 버튼을 땔 때
	//드로잉시라면 드로잉을 멈춘다.
	case WM_LBUTTONUP: 

		has_pushed_left_mouse_button = FALSE;
		break;
	
	case WM_DESTROY: 
		PostQuitMessage(0);
		break;
	
	}
	return(DefWindowProc(hWnd, message, wParam, lParam));
}


//네트워크 초기화 및 서버에 연결.
BOOL InitNetwork() {

	WSADATA wsa;
	SOCKADDR_IN serveraddr;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 0;

	g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (g_server_socket == INVALID_SOCKET)
	{
		return 0;
	}

	WSAAsyncSelect(g_server_socket, g_hwnd, UM_NETWORK, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE);

	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	InetPton(AF_INET, SERVER_IP, &serveraddr.sin_addr);
	serveraddr.sin_port = htons(SERVER_PORT);

	

	//네이글 알고리즘 적용.
	//bool flag_true = false;
	//setsockopt(g_server_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag_true, sizeof(bool));

	//서버에 연결요청.
	int code_err;
	code_err = connect(g_server_socket, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
	if (code_err == SOCKET_ERROR){

		//진짜 에러!!!!
		if (WSAGetLastError() != WSAEWOULDBLOCK){

			return 0;
		}
	}

	return 1;
}


/**
	brief 최대 recv_q가 할당 가능한 크기까지 recv받은 후 recv_q가 추출 할 수 있는 만큼의 패킷의 선을 그려준다. 
*/
void ProcRead() {

	int retval = -1;

	//현재 리시브 큐의 free사이즈를 구한 후 해당 사이즈만큼 동적할당한다.
	int sz_byte_free_recv_q = g_recv_stream.GetSizeOfFree();
	char *tmp_buf = new char[sz_byte_free_recv_q];

	//최대 리시브 큐의 free사이즈까지 한번에 몽땅 recv받자.
	retval = recv(g_server_socket, tmp_buf, sz_byte_free_recv_q, NULL);

	//상대가 fin 패킷을 보낸 경우.
	if (retval == 0) {

		Disconnect();
	}

	//리셋 패킷을 전달 받은 경우.
	if (SOCKET_ERROR/**10054*/ == retval) {

		int code_err =GetLastError();

		//사실 클라쪽에서 리셋 패킷 받은 것을 별도 분기할 필요 없이 
		//그냥 SOCKET_ERROR 시 Disconnect하는게 깔끔.
		if (WSAECONNRESET == code_err) {

			Disconnect();
		}

		Disconnect();
	}

	//리시브 큐에 recv한 수신 패킷들을 삽입하자.
	int eng = g_recv_stream.Put(tmp_buf, retval);
	//센드 스트림 내부 정보 출력한다
	printf("recv sz from server = %d / front=%d \n", retval);
	g_recv_stream.DisplayInfo1();
	//동적할당 해제.
	free(tmp_buf);


//선을 그려준다.
	HDC hdc = GetDC(g_hwnd);

	//리시브 큐에 존재하는 선 그리기가 가능한 모든 수신 패킷을 꺼내 선을 그려준다. 
	for (; ; ) {

		//헤더 크기(unsigned short) 만큼 리시브 큐에서 꺼낼 수 있는지 체크한다.
		if (g_recv_stream.GetSizeOfUse() < sizeof(HEADER_DRAW)) {

			return;
		}

		//헤더를 추출한 후 헤더 정보 중 페이로드 크기가 패킷의 페이로드 크기와 같은지 비교한다.
		unsigned short header_value;
		int len_byte_header = g_recv_stream.Peek( (char *)&header_value, 2 );

		//리시브 할 헤더 길이와 같지 않게 읽은 경우.
		if (len_byte_header != sizeof(HEADER_DRAW)) {

			return;
		}

		//리시브 큐에서 꺼낼 페이로드 크기가 패킷의 페이로드 크기가 안되는 경우.
		if (header_value + sizeof(HEADER_DRAW) > g_recv_stream.GetSizeOfUse()) {

			return;
		}

		//리시브 큐에서 꺼낼 페이로드 크기가 패킷의 페이로드 크기가 되는 경우에는 추출한다.
		PACKET_DRAW packet;
		int len_byte_packet = g_recv_stream.Get( (char *)&packet, header_value+2);

		//리시브 스트림 내부 정보 출력한다.
		//_putws(L"[RECV STREAM GET]");
		//g_recv_stream.DisplayInfo1();


		//겟 패킷 길이와 같지 않게 읽은 경우.
		if (len_byte_packet > sizeof(packet)){

			return;
		}

		//겟 패킷 길이와 같지 않게 읽은 경우.
		if (len_byte_packet < sizeof(packet)) {

			return;
		}



		int start_x = packet._start_x;
		int start_y = packet._start_y;
		int end_x = packet._end_x;
		int end_y = packet._end_y; ///<왜 끊기지 

		//디버깅용
		//printf("FD_READ     = %d %d %d %d \n", start_x, start_y, end_x, end_y);

		MoveToEx(hdc, start_x, start_y, NULL);
		LineTo(hdc, end_x, end_y);



	}

	ReleaseDC(g_hwnd, hdc);

	return;
}

/**
brief 센드 큐에 존재하는 패킷들을 모두 전송한다.
*/
void ProcSend() {
	
	//서버와 연결 여부 체크.
	if (g_is_connected_with_server == false) {

		return;
	}

	//다 보내버리자!!
	int retval = -1;

	//센드 큐에서 센드 가능 용량 체크.
	int sz_byte_use_send = g_send_stream.GetSizeOfUse();

	if (sz_byte_use_send < 0) {

		return;
	}

	//센드 가능 용량 만큼 메모리 동적할당.
	char *tmp_buf = new char[sz_byte_use_send];

	//센드 큐에 존재하는 패킷들을 겟한다.
	retval = g_send_stream.Peek(tmp_buf, sz_byte_use_send);
	g_send_stream.MoveFrontIdx(retval);

	//센드 스트림 내부 정보 출력한다
	//_putws(L"[SEND STREAM GET]"); 
	//g_send_stream.DisplayInfo1();

	//한번에 모든 패킷들을 send한다.
	retval = send(g_server_socket, tmp_buf, sz_byte_use_send, NULL);

	free(tmp_buf);

	return;
}



/**
brief 패킷을 생성 후 센드 큐에 삽입 후 send 이벤트를 호출한다.
*/
BOOL SendDrawPacket(int start_x, int start_y, int end_x, int end_y) {

	//패킷을 생성한다.
	PACKET_DRAW cur_packet_draw;

	//패킷 설정한다.
	cur_packet_draw._header._len_byte_payload = 16;
	int t_start_x = cur_packet_draw._start_x = start_x;
	int t_start_y = cur_packet_draw._start_y = start_y;
	int t_end_x = cur_packet_draw._end_x = end_x;
	int t_end_y = cur_packet_draw._end_y = end_y;

	//현재 센드 큐에 남은 사이즈 구한다.
	int sz_byte_free_send = g_send_stream.GetSizeOfFree();
	
	//현재 센드 큐에 남은 공간이 없는 경우 종료한다. ....?
	if (sz_byte_free_send < sizeof(cur_packet_draw)) {

		return -1;
	}

	//printf("send_packet = %d %d %d %d \n", t_start_x, t_start_y, t_end_x, t_end_y);

	//센드 큐에 패킷을 저장한다.
	int retval = g_send_stream.Put((char *)&cur_packet_draw, sizeof(cur_packet_draw));
	printf("send to server %d\n", retval);

	//센드 스트림 내부 정보 출력한다
	//_putws(L"[SEND STREAM PUT]");
	//g_send_stream.DisplayInfo1();


	ProcSend();
}


void Disconnect() {

	closesocket(g_server_socket);
	WSACleanup();
	exit(1);
}