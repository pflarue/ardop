// ARDOP TNC Host Interface using TCP
//

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE
#include <windows.h>
#pragma comment(lib, "WS2_32.Lib")

#define ioctl ioctlsocket
#else

#define UINT unsigned int
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <netdb.h>

#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <fcntl.h>
#include <syslog.h>
#include <pthread.h>

#define SOCKET int

#define INVALID_SOCKET  (SOCKET)(~0)
#define SOCKET_ERROR            (-1)
#define WSAGetLastError() errno
#define GetLastError() errno 
#define closesocket close
int _memicmp(unsigned char *a, unsigned char *b, int n);
#endif

#define MAX_PENDING_CONNECTS 4

#include "ARDOPC.h"

#define GetBuff() _GetBuff(__FILE__, __LINE__)
#define ReleaseBuffer(s) _ReleaseBuffer(s, __FILE__, __LINE__)
#define Q_REM(s) _Q_REM(s, __FILE__, __LINE__)
#define C_Q_ADD(s, b) _C_Q_ADD(s, b, __FILE__, __LINE__)

VOID * _Q_REM(VOID *Q, char * File, int Line);
int _C_Q_ADD(VOID *Q, VOID *BUFF, char * File, int Line);
UINT _ReleaseBuffer(VOID *BUFF, char * File, int Line);
VOID * _GetBuff(char * File, int Line);
int C_Q_COUNT(VOID *Q);

void ProcessCommandFromHost(char * strCMD);
BOOL checkcrc16(unsigned char * Data, unsigned short length);
int ReadCOMBlockEx(HANDLE fd, char * Block, int MaxLength, BOOL * Error);
VOID ProcessPacketBytes(UCHAR * RXBuffer, int Read);
int ReadCOMBlock(HANDLE fd, char * Block, int MaxLength );

extern int port;
extern int pktport;

extern BOOL UseKISS;			// Enable Packet (KISS) interface

extern BOOL NeedID;				// SENDID Command Flag
extern BOOL NeedCWID;			// SENDCWID Command Flag
extern BOOL NeedTwoToneTest;

SOCKET TCPControlSock = 0, TCPDataSock = 0, PktSock = 0;
SOCKET ListenSock = 0, DataListenSock = 0, PktListenSock = 0;
SOCKET GUISock = 0;				// UDP socket for GUI interface

int GUIActive = 0;
int LastGUITime = 0;
extern int WaterfallActive;		// Waterfall display turned on
extern int SpectrumActive;		// Spectrum display turned on

struct sockaddr_in GUIHost;

BOOL CONNECTED = FALSE;
BOOL DATACONNECTED = FALSE;
BOOL PKTCONNECTED = FALSE;

// Host to TNC RX Buffer

UCHAR ARDOPBuffer[8192];
int InputLen = 0;

UCHAR ARDOPDataBuffer[8192];
int DataInputLen = 0;

extern char LogDir[256];

/*UINT FREE_Q = 0;

int MAXBUFFS = 0;
int QCOUNT = 0;
int MINBUFFCOUNT = 65535;
int NOBUFFCOUNT = 0;
int BUFFERWAITS = 0;
int NUMBEROFBUFFERS = 0;

unsigned int Host_Q;			// Frames for Host
*/

// Convert IP Address to Text

VOID Format_Addr(struct sockaddr_in * sin, char * dst)
{
	unsigned char work[4];
	memcpy(work, &sin->sin_addr.s_addr, 4);
	sprintf(dst,"%d.%d.%d.%d", work[0], work[1], work[2], work[3]);
	return;
}



UCHAR bytLastCMD_DataSent[256];

//	Function to send a text command to the Host

void TCPSendCommandToHost(char * strText)
{
	//	This sends a command response to the Host.
	//	It is simply a string terminated by a CR.

	UCHAR bytToSend[1024];
	int len;
	int ret;
	
	len = sprintf(bytToSend,"%s\r", strText);

	if (CONNECTED)
	{
		ret = send(TCPControlSock, bytToSend, len, 0);
		ret = WSAGetLastError();

		if (CommandTrace) WriteDebugLog(LOGDEBUG, " Command Trace TO Host %s", strText);
		return;
	}
	return;
}


void TCPSendCommandToHostQuiet(char * strText)
{
	//	This sends a command response to the Host.
	//	It is simply a string terminated by a CR.
	//	Used for PTT commands and INPUTPEAKS notifications to Host.
	//	Not logged to the Debug Log.

	UCHAR bytToSend[256];
	int len;
	int ret;
	
	len = sprintf(bytToSend,"%s\r", strText);

	if (CONNECTED)
	{
		ret = send(TCPControlSock, bytToSend, len, 0);
		ret = WSAGetLastError();

		return;
	}
	return;
}

void TCPQueueCommandToHost(char * strText)
{
	//	This wrapper sends a command response to the Host.
	//	It is simply a string terminated by a CR.
	//	Response queuing seems to be a legacy from the original ARDOP TNC, but is not used in ARDOPC.
	//  Good canidate for removing and replacing with TCPSendCommandToHost.
	SendCommandToHost(strText);
}

void TCPSendReplyToHost(char * strText)
{
	//  This wrapper sends a reply to the Host.
	//  It is simply a string terminated by a CR.

	SendCommandToHost(strText);
}

// Experimental logging of FEC Packets

FILE *FEClogfile = NULL;

BOOL LogFEC = TRUE;

void WriteFECLog(UCHAR * Msg, int Len)
{
#ifdef WIN32

	SYSTEMTIME st;
	char Value[128];

	GetSystemTime(&st);
	
	if (FEClogfile == NULL)
	{
	if (LogDir[0])
		sprintf(Value, "%s/%s_%04d%02d%02d.log",
				LogDir, "ARDOPFECLog", st.wYear, st.wMonth, st.wDay);
	else		
		sprintf(Value, "%s_%04d%02d%02d.log",
				"ARDOPFECLog", st.wYear, st.wMonth, st.wDay);
		
	if ((FEClogfile = fopen(Value, "ab")) == NULL)
			return;

	}
	fwrite (Msg, 1, Len, FEClogfile);

	fclose(FEClogfile);
	FEClogfile = NULL;

#endif
}



void TCPAddTagToDataAndSendToHost(UCHAR * bytData, char * strTag, int Len)
{
	//  Subroutine to add a short 3 byte tag (ARQ, FEC, ERR, or IDF) to data and send to the host
	//  The reason for this is to allow the host to determine the type of data and handle it appropriately.
	//  An example for a FEC response is "<LENGTH><FEC><DATA>"
	//  Sometimes this will end up replying with FECFEC instead of just FEC, but it is TBD if that is a bug or not.
	//  I think the reason this happens, is when sending data, you need to prefix with FEC, and when the data
	//  is received and sent to the host, it just tacks on an extra FEC.

	//  strTag has the type Tag to prepend to data  "ARQ", "FEC" or "ERR"
	//  The host is supposed to use this to determine how to handle the data, usually it is stripped off by the host.

	//  Max data size should be 2000 bytes or less for timing purposes
	//  I think largest apcet is about 1360 bytes

	UCHAR * bytToSend;
	UCHAR buff[1500];

	int ret;

	if (blnInitializing)
		return;

	if (CommandTrace) WriteDebugLog(LOGDEBUG, "[AddTagToDataAndSendToHost] bytes=%d Tag %s", Len, strTag);

	//	Have to save copy for possible retry (and possibly until previous 
	//	command is acked

	bytToSend = buff;

	Len += 3;					// Add 3 bytes for the tag (FEC, ARQ, ERR)
	bytToSend[0] = Len >> 8;	//' MS byte of count  (Includes strDataType but does not include the two trailing CRC bytes)
	bytToSend[1] = Len  & 0xFF;// LS Byte
	memcpy(&bytToSend[2], strTag, 3);
	memcpy(&bytToSend[5], bytData, Len - 3);
	Len +=2;				//  len

	ret = send(TCPDataSock, bytToSend, Len, 0);

	if (strcmp(strTag, "FEC") == 0)
		if (LogFEC)
			WriteFECLog(bytToSend, Len);



	return;
}

VOID ARDOPProcessCommand(UCHAR * Buffer, int MsgLen)
{
	Buffer[MsgLen - 1] = 0;		// Remove CR
	
	if (_memicmp(Buffer, "RDY", 3) == 0)
	{
		//	Command ACK. Remove from buffer and send next if a ??????

		return;
	}
	ProcessCommandFromHost(Buffer);
}

BOOL InReceiveProcess = FALSE;		// Flag to stop reentry


void ProcessReceivedControl()
{
	int Len, MsgLen;
	char * ptr, * ptr2;
	char Buffer[8192];

	// shouldn't get several messages per packet, as each should need an ack
	// May get message split over packets
	if (InReceiveProcess)
		return;

	//	This is the command port, which only listens on port 8515 or otherwise specified
	//  as a command line arguemnt. It is used for all commands to the TNC, such as
	//  setting parameters. 

	//	The command format is expected to be "<COMMAND><CR>"
	//  It is important for it to be an actual carriage return, as a newline will not be recognized
				
	Len = recv(TCPControlSock, &ARDOPBuffer[InputLen], 8192 - InputLen, 0);

	//  A socket connection will periodically send a TCPKeepAlive packet
	//  If we stop getting these, then we should close the connection.
	if (Len == 0 || Len == SOCKET_ERROR)
	{
		closesocket(TCPControlSock);
		TCPControlSock = 0;

		CONNECTED = FALSE;
		LostHost();

		return;					
	}

	InputLen += Len;

loop:

	// Here we will wait for our input length to be at least 5 bytes before continuing processing,
	// as we might have read the buffer while the message was being sent, and we need to wait for the whole message.
	// A timeout may be a good idea here, but it is not implemented.
	if (InputLen < 4)
		return;	

	//  We are looking for a carriage return in our buffer, as that is the end of a command.
	ptr = memchr(ARDOPBuffer, '\r', InputLen);

	// If there is no carriage return, we need to wait for more data
	if (ptr == 0)
		return;

	// since we found the carriage return, we look at the next byte
	ptr2 = &ARDOPBuffer[InputLen];

	// I am not sure why we are comparing the distance to the next byte in the buffer,
	// since it should always evalute to 1, because ptr is the location of the carriage return,
	// which is the end of that message, and the next byte, regardless of wherever it is, will
	// always be 1. Maybe this is a holdover of processing DATA messages, which may be split over packets,
	// but I am not sure. Would be cool to have someone look into this to see if the if is even required in
	// the control port processing. All commands are short anyway.
	if ((ptr2 - ptr) == 1)
	{
		// Usual Case - single meg in buffer
	
		MsgLen = InputLen;

		// We may be reentered as a result of processing,
		//	so reset InputLen Here

		InputLen=0;
		InReceiveProcess = TRUE;
		ARDOPProcessCommand(ARDOPBuffer, MsgLen);
		InReceiveProcess = FALSE;
		return;
	}
	else
	{
		// buffer contains more that 1 message

		//	I dont think this should happen, but...

		MsgLen = InputLen - (ptr2-ptr) + 1;	// Include CR

		memcpy(Buffer, ARDOPBuffer, MsgLen);

		memmove(ARDOPBuffer, ptr + 1,  InputLen-MsgLen);
		InputLen -= MsgLen;

		InReceiveProcess = TRUE;
		ARDOPProcessCommand(Buffer, MsgLen);
		InReceiveProcess = FALSE;

		if (InputLen < 0)
		{
			InputLen = 0;
			InReceiveProcess = FALSE;
			return;
		}
		goto loop;
	}
		
	// Getting bad data ?? Should we just reset ??
	
	WriteDebugLog(LOGDEBUG, "ARDOP BadHost Message ?? %s", ARDOPBuffer);
	InputLen = 0;
	return;
}





void ProcessReceivedData()
{
	int Len, MsgLen;
	char Buffer[8192];
	int DataLen;

	// shouldn't get several messages per packet, as each should need an ack
	// May get message split over packets
	if (InReceiveProcess)
		return;

	//	This is the data port, which only listens on port 8516 (8515+1) or otherwise specified (+1)
	//  as a command line arguemnt. It is used for all data sent to the TNC, such as
	//  for ARQ or FEC.

	//	The command format is expected to be "<LENGTH><MODE><DATA>"
	//  The LENGTH is a two-byte big-endian integer, followed by the text FEC or ARQ, followed by the data.
	//  No carriage return is expected, as the data may contain binary data.
	//  An example valid message is "0008FECHELLO"
	//  This will load FECHELLO into the buffer.
	//	Note that the MODE is included in the length, so the data length is the total length + 3
	//  This behavior is a little strange, to keep the FEC, but right now it is simply what it does.
				
	Len = recv(TCPDataSock, &ARDOPDataBuffer[DataInputLen], 8192 - DataInputLen, 0);

	if (Len == 0 || Len == SOCKET_ERROR)
	{
		// Does this mean closed?
		
		closesocket(TCPDataSock);
		TCPDataSock = 0;

		DATACONNECTED = FALSE;
		LostHost();
		return;					
	}

	DataInputLen += Len;

loop:
	// this will repeat until the entire message is sent, which is controlled by the length of DataInputLen
	
	// Here we will wait for our input length to be at least 4 bytes before continuing processing,
	// as we might have read the buffer while the message was being sent, and we need to wait for the whole message.
	// A timeout may be a good idea here, but it is not implemented.
	if (DataInputLen < 3)
		return;

	// check we have it all

	// This gets the two bytes from the beginning of the buffer, which is the length of the data sent by the host
	DataLen = (ARDOPDataBuffer[0] << 8) + ARDOPDataBuffer[1]; // HI First (Big Endian)
	// I think these are Big Endian because it's readable to humans (a length of 9 is 0x00 0x09), but I am not sure.
	
	// Until we have recieved the amount of bytes specified in the length from the host, wait for more.
	if (DataInputLen < DataLen + 2)
		return;

	// We include our two bytes for the length, since we will be sending this over the air (I think)
	MsgLen = DataLen + 2;

	// Copy the data (except for the two length bytes at the beginning) into the buffer that will be sent by the TNC.
	memcpy(Buffer, &ARDOPDataBuffer[2], DataLen);

	// Subtract the data we are about to send to the Buffer from the ARDOPDataBuffer
	DataInputLen -= MsgLen;

	// Move the remaining data in the ARDOPDataBuffer to the beginning,
	// so we can process the next part of the message if there is one.
	if (DataInputLen > 0)
		memmove(ARDOPDataBuffer, &ARDOPDataBuffer[MsgLen],  DataInputLen);

	InReceiveProcess = TRUE;
	AddDataToDataToSend(Buffer, DataLen);
	InReceiveProcess = FALSE;
	
	// See if anything else in buffer

	if (DataInputLen > 0)
		goto loop;

	// If we end up with a negative length because of MsgLen subtraction, reset it to 0
	// we are done processing data to send.
	if (DataInputLen < 0)
		DataInputLen = 0;

	return;
}

SOCKET OpenSocket4(int port)
{
	// This is very standard socket opening code for TCP, and is used for both the command and data ports.
	struct sockaddr_in  local_sin;  /* Local socket - internet style */
	struct sockaddr_in * psin;
	SOCKET sock = 0;
	u_long param=1;

	psin=&local_sin;
	psin->sin_family = AF_INET;
	psin->sin_addr.s_addr = INADDR_ANY;

	if (port)
	{
		sock = socket(AF_INET, SOCK_STREAM, 0);

	    if (sock == INVALID_SOCKET)
		{
	        WriteDebugLog(LOGDEBUG, "socket() failed error %d", WSAGetLastError());
			return 0;
		}

		setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (char *)&param,4);

		psin->sin_port = htons(port);        // Convert to network ordering 

		if (bind( sock, (struct sockaddr *) &local_sin, sizeof(local_sin)) == SOCKET_ERROR)
		{
			WriteDebugLog(LOGINFO, "bind(sock) failed port %d Error %d", port, WSAGetLastError());

		    closesocket(sock);
			return FALSE;
		}

		if (listen( sock, MAX_PENDING_CONNECTS ) < 0)
		{
			WriteDebugLog(LOGINFO, "listen(sock) failed port %d Error %d", port, WSAGetLastError());
			return FALSE;
		}
		ioctl(sock, FIONBIO, &param);
	}
	return sock;
}


SOCKET OpenUDPSocket(int port)
{
	// This is very standard socket opening code for UDP.
	// This is used mostly for a GUI interface, I believe specifically for the waterfall display.
	struct sockaddr_in  local_sin;  /* Local socket - internet style */
	struct sockaddr_in * psin;
	SOCKET sock = 0;
	u_long param=1;

	psin=&local_sin;
	psin->sin_family = AF_INET;
	psin->sin_addr.s_addr = INADDR_ANY;

	if (port)
	{
		sock = socket(AF_INET, SOCK_DGRAM, 0);

	    if (sock == INVALID_SOCKET)
		{
	        WriteDebugLog(LOGDEBUG, "socket() failed error %d", WSAGetLastError());
			return 0;
		}

		setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (char *)&param,4);

		psin->sin_port = htons(port);        // Convert to network ordering 

		if (bind( sock, (struct sockaddr *) &local_sin, sizeof(local_sin)) == SOCKET_ERROR)
		{
			WriteDebugLog(LOGINFO, "bind(sock) failed port %d Error %d", port, WSAGetLastError());
		    closesocket(sock);
			return FALSE;
		}	
		ioctl(sock, FIONBIO, &param);
	}
	return sock;
}

// As far as I can tell, this is no longer used. Its code is commented out.
VOID InitQueue();

BOOL TCPHostInit()
{
#ifdef WIN32
	WSADATA WsaData;			 // receives data from WSAStartup

	WSAStartup(MAKEWORD(2, 0), &WsaData);
#endif

	WriteDebugLog(LOGALERT, "%s listening on port %d", ProductName, port);
	if (UseKISS && pktport)
		WriteDebugLog(LOGALERT, "%s listening for KISS frames on port %d", ProductName, pktport);
//	InitQueue();

	// Here is where our listening ports for commands and data are opened.
	ListenSock = OpenSocket4(port);
	DataListenSock = OpenSocket4(port + 1);
	if (UseKISS && pktport)
		PktListenSock = OpenSocket4(pktport);

	GUISock = OpenUDPSocket(port);

	return ListenSock;
}

void TCPHostPoll()
{
	// Check for incoming connect or data

	fd_set readfs;
	fd_set errorfs;
	struct timeval timeout;
	int ret;
	int addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in sin;  
	u_long param=1;

	// Check for Rig control data

	if (hCATDevice && CONNECTED)
	{
		UCHAR RigBlock[256];
		int Len;

		Len = ReadCOMBlock(hCATDevice, RigBlock, 256);

		if (Len && EnableHostCATRX)
		{
			UCHAR * ptr = RigBlock;
			char RigCommand[1024] = "RADIOHEX ";
			char * ptr2 = &RigCommand[9] ;
			int i, j;

			while (Len--)
			{
				i = *(ptr++);
				j = i >>4;
				j += '0';		// ascii
				if (j > '9')
					j += 7;
				*(ptr2++) = j;

				j = i & 0xf;
				j += '0';		// ascii
				if (j > '9')
					j += 7;
				*(ptr2++) = j;
			}
			*(ptr2) = 0;
			SendCommandToHost(RigCommand);
		}
	}

	if (ListenSock == 0)
		goto NoARDOPTCP;			// Could just be runing packet over TCP

	FD_ZERO(&readfs);	
	FD_ZERO(&errorfs);

	FD_SET(ListenSock,&readfs);

	timeout.tv_sec = 0;				// No wait
	timeout.tv_usec = 0;	

	ret = select(ListenSock + 1, &readfs, NULL, NULL, &timeout);

	if (ret == -1)
	{
		ret = WSAGetLastError();
		WriteDebugLog(LOGDEBUG, "%d ", ret);
		perror("listen select");
	}
	else
	{
		if (ret)
		{
			if (FD_ISSET(ListenSock, &readfs))
			{
				TCPControlSock = accept(ListenSock, (struct sockaddr * )&sin, &addrlen);
	    
				if (TCPControlSock == INVALID_SOCKET)
				{
					WriteDebugLog(LOGDEBUG, "accept() failed error %d", WSAGetLastError());
					return;
				}
				WriteDebugLog(LOGINFO, "Host Control Session Connected");
					
				ioctl(TCPControlSock, FIONBIO, &param);
				CONNECTED = TRUE;
//				SendCommandToHost("RDY");
			}
		}
	}

	if (DataListenSock == 0)
		return;

	FD_ZERO(&readfs);	
	FD_ZERO(&errorfs);
	FD_SET(DataListenSock,&readfs);

	timeout.tv_sec = 0;				// No wait
	timeout.tv_usec = 0;	

	ret = select(DataListenSock + 1, &readfs, NULL, NULL, &timeout);

	if (ret == -1)
	{
		ret = WSAGetLastError();
		WriteDebugLog(LOGDEBUG, "%d ", ret);
		perror("data listen select");
	}
	else
	{
		if (ret)
		{
			if (FD_ISSET(DataListenSock, &readfs))
			{
				TCPDataSock = accept(DataListenSock, (struct sockaddr * )&sin, &addrlen);
	    
				if (TCPDataSock == INVALID_SOCKET)
				{
					WriteDebugLog(LOGDEBUG, "accept() failed error %d", WSAGetLastError());
					return;
				}
				WriteDebugLog(LOGINFO, "Host Data Session Connected");
					
				ioctl(TCPDataSock, FIONBIO, &param);
				DATACONNECTED = TRUE;
			}
		}
	}

// if we are removing packet modes, we can remove this code
NoARDOPTCP:

	if (PktListenSock == 0)
		goto NoPkt;

	FD_ZERO(&readfs);	
	FD_ZERO(&errorfs);
	FD_SET(PktListenSock,&readfs);

	timeout.tv_sec = 0;				// No wait
	timeout.tv_usec = 0;	

	ret = select(PktListenSock + 1, &readfs, NULL, NULL, &timeout);

	if (ret == -1)
	{
		ret = WSAGetLastError();
		WriteDebugLog(LOGDEBUG, "%d ", ret);
		perror("pkt listen select");
	}
	else
	{
		if (ret)
		{
			if (FD_ISSET(PktListenSock, &readfs))
			{
				PktSock = accept(PktListenSock, (struct sockaddr * )&sin, &addrlen);
	    
				if (PktSock == INVALID_SOCKET)
				{
					WriteDebugLog(LOGDEBUG, "accept() pkt failed error %d", WSAGetLastError());
					return;
				}
				WriteDebugLog(LOGINFO, "Packet Session Connected");
					
				ioctl(PktSock, FIONBIO, &param);
				PKTCONNECTED = TRUE;
			}
		}
	}

NoPkt:

	if (CONNECTED)
	{
		FD_ZERO(&readfs);	
		FD_ZERO(&errorfs);

		FD_SET(TCPControlSock,&readfs);
		FD_SET(TCPControlSock,&errorfs);

		timeout.tv_sec = 0;				// No wait
		timeout.tv_usec = 0;	
		
		ret = select(TCPControlSock + 1, &readfs, NULL, &errorfs, &timeout);

		if (ret == SOCKET_ERROR)
		{
			WriteDebugLog(LOGDEBUG, "Data Select failed %d ", WSAGetLastError());
			goto Lost;
		}
		if (ret > 0)
		{
			//	See what happened

			if (FD_ISSET(TCPControlSock, &readfs))
			{
				GetSemaphore();
				ProcessReceivedControl();
				FreeSemaphore();
			}
								
			if (FD_ISSET(TCPControlSock, &errorfs))
			{
Lost:	
				WriteDebugLog(LOGDEBUG, "TCP Control Connection lost");
			
				CONNECTED = FALSE;

				closesocket(TCPControlSock);
				TCPControlSock= 0;
				return;
			}
		}
	}
	if (DATACONNECTED)
	{
		FD_ZERO(&readfs);	
		FD_ZERO(&errorfs);

		FD_SET(TCPDataSock,&readfs);
		FD_SET(TCPDataSock,&errorfs);

		timeout.tv_sec = 0;				// No wait
		timeout.tv_usec = 0;	
		
		ret = select(TCPDataSock + 1, &readfs, NULL, &errorfs, &timeout);

		if (ret == SOCKET_ERROR)
		{
			WriteDebugLog(LOGDEBUG, "Data Select failed %d ", WSAGetLastError());
			goto DCLost;
		}
		if (ret > 0)
		{
			//	See what happened

			if (FD_ISSET(TCPDataSock, &readfs))
			{
				GetSemaphore();
				ProcessReceivedData();
				FreeSemaphore();
			}
								
			if (FD_ISSET(TCPDataSock, &errorfs))
			{
	DCLost:	
				WriteDebugLog(LOGDEBUG, "TCP Data Connection lost");
			
				DATACONNECTED = FALSE;

				closesocket(TCPControlSock);
				TCPDataSock= 0;
				return;
			}
		}
	}

	if (PKTCONNECTED)
	{
		FD_ZERO(&readfs);	
		FD_ZERO(&errorfs);

		FD_SET(PktSock,&readfs);
		FD_SET(PktSock,&errorfs);

		timeout.tv_sec = 0;				// No wait
		timeout.tv_usec = 0;	
		
		ret = select(PktSock + 1, &readfs, NULL, &errorfs, &timeout);

		if (ret == SOCKET_ERROR)
		{
			WriteDebugLog(LOGDEBUG, "Pkt Select failed %d ", WSAGetLastError());
			goto PktLost;
		}
		if (ret > 0)
		{
			//	See what happened

			if (FD_ISSET(PktSock, &readfs))
			{
				unsigned long Read;
				unsigned char RXBuffer[4096];
				
				Read = recv(PktSock, RXBuffer, 4096, 0);

				if (Read == 0 || Read == SOCKET_ERROR)
				{
					// Does this mean closed?
		
					closesocket(PktSock);
					PktSock = 0;

					PKTCONNECTED = FALSE;
					LostHost();					
				}
				else
					ProcessPacketBytes(RXBuffer, Read);		// Process all in buffer
			}
										
			if (FD_ISSET(PktSock, &errorfs))
			{
PktLost:	
				WriteDebugLog(LOGDEBUG, "Pkt Data Connection lost");
			
				PKTCONNECTED = FALSE;

				closesocket(PktSock);
				PktSock = 0;
				return;
			}
		}
		// Look for anything to send on packet sessions

		CheckForPktMon();
		CheckForPktData(0);
	}

	// the GUI code seems to mostly be for the waterfall display, because
	// actual terminal control is done via the command port, and data is sent via the data port.
	if (GUISock)
	{
		// Look for datagram from GUI Client
		// This almost definitely interfaces with ARDOP_GUI
		// - avaliable at https://www.cantab.net/users/john.wiseman/Downloads/Beta/
		// Source code also looks avaliable there. Would be cool to get it better documented.

		int Len, addrLen = addrlen = sizeof(struct sockaddr_in);
		char GUIMsg[256];

		Len = recvfrom(GUISock, GUIMsg, 256, 0, (struct sockaddr *)&GUIHost, &addrLen);

		if (Len > 0)
		{
			GUIMsg[Len] = 0;
			LastGUITime = Now;

			if (GUIActive == FALSE)
			{
				char Addr[32];
				Format_Addr(&GUIHost, Addr);
				WriteDebugLog(LOGDEBUG, "GUI Connected from Address %s", Addr);
				GUIActive = TRUE;
			}

			if (strcmp(GUIMsg, "Waterfall") == 0)
			{
				WaterfallActive = 1;
				SpectrumActive = 0;
			}
			else if (strcmp(GUIMsg, "Spectrum") == 0)
			{
				WaterfallActive = 0;
				SpectrumActive = 1;
			}
			else if (strcmp(GUIMsg, "Disable") == 0)
			{
				WaterfallActive = 0;
				SpectrumActive = 0;
			}
			else if (strcmp(GUIMsg, "SENDID") == 0)
			{
				if (ProtocolState == DISC)
					NeedID = TRUE;			// Send from background
			}
			else if (strcmp(GUIMsg, "TWOTONETEST") == 0)
			{
				if (ProtocolState == DISC)
					NeedTwoToneTest = TRUE;			// Send from background
			}
			else if (strcmp(GUIMsg, "SENDCWID") == 0)
			{
				if (ProtocolState == DISC)
					NeedCWID = TRUE;			// Send from background
			}
		}
		else
		{
			if ((Now - LastGUITime) > 15000)
			{
				if (GUIActive)
					WriteDebugLog(LOGDEBUG, "GUI Connection lost");

				GUIActive = FALSE;
			}
		}
	}
}

/*

// Buffer handling routines
	
#define BUFFLEN 1500
#define NUMBUFFS 64

UCHAR DATAAREA[BUFFLEN * NUMBUFFS] = "";

UCHAR * NEXTFREEDATA = DATAAREA;

VOID InitQueue()
{
	int i;

	NEXTFREEDATA = DATAAREA;
	NUMBEROFBUFFERS = MAXBUFFS = 0;
	
	for (i = 0; i < NUMBUFFS; i++)
	{
		ReleaseBuffer((UINT *)NEXTFREEDATA);
		NEXTFREEDATA += BUFFLEN;

		NUMBEROFBUFFERS++;
		MAXBUFFS++;
	}
}


VOID * _Q_REM(VOID *PQ, char * File, int Line)
{
	UINT * Q;
	UINT * first;
	UINT next;

	//	PQ may not be word aligned, so copy as bytes (for ARM5)

	Q = (UINT *) PQ;

//	if (Semaphore.Flag == 0)
//		WriteDebugLog(LOGDEBUG, ("Q_REM called without semaphore from %s Line %d", File, Line);

	first = (UINT *)Q[0];

	if (first == 0) return (0);			// Empty

	next= first[0];						// Address of next buffer

	Q[0] = next;

	return (first);
}


UINT _ReleaseBuffer(VOID *pBUFF, char * File, int Line)
{
	UINT * pointer, * BUFF = pBUFF;
	int n = 0;

//	if (Semaphore.Flag == 0)
//		WriteDebugLog(LOGDEBUG, ("ReleaseBuffer called without semaphore from %s Line %d", File, Line);

	pointer = (UINT *)FREE_Q;

	*BUFF=(UINT)pointer;

	FREE_Q=(UINT)BUFF;

	QCOUNT++;

	return 0;
}

int _C_Q_ADD(VOID *PQ, VOID *PBUFF, char * File, int Line)
{
	UINT * Q;
	UINT * BUFF = (UINT *)PBUFF;
	UINT * next;
	int n = 0;

//	PQ may not be word aligned, so copy as bytes (for ARM5)

	Q = (UINT *) PQ;

//	if (Semaphore.Flag == 0)
//		WriteDebugLog(LOGDEBUG, ("C_Q_ADD called without semaphore from %s Line %d", File, Line);


	BUFF[0]=0;							// Clear chain in new buffer

	if (Q[0] == 0)						// Empty
	{
		Q[0]=(UINT)BUFF;				// New one on front
		return(0);
	}

	next = (UINT *)Q[0];

	while (next[0]!=0)
	{
		next=(UINT *)next[0];			// Chain to end of queue
	}
	next[0]=(UINT)BUFF;					// New one on end

	return(0);
}

int C_Q_COUNT(VOID *PQ)
{
	UINT * Q;
	int count = 0;

//	PQ may not be word aligned, so copy as bytes (for ARM5)

	Q = (UINT *) PQ;

	//	SEE HOW MANY BUFFERS ATTACHED TO Q HEADER

	while (*Q)
	{
		count++;
		if ((count + QCOUNT) > MAXBUFFS)
		{
			WriteDebugLog(LOGDEBUG, ("C_Q_COUNT Detected corrupt Q %p len %d", PQ, count);
			return count;
		}
		Q = (UINT *)*Q;
	}

	return count;
}

VOID * _GetBuff(char * File, int Line)
{
	UINT * Temp = Q_REM(&FREE_Q);

//	FindLostBuffers();

//	if (Semaphore.Flag == 0)
//		WriteDebugLog(LOGDEBUG, ("GetBuff called without semaphore from %s Line %d", File, Line);

	if (Temp)
	{
		QCOUNT--;

		if (QCOUNT < MINBUFFCOUNT)
			MINBUFFCOUNT = QCOUNT;

	}
	else
		WriteDebugLog(LOGDEBUG, ("Warning - Getbuff returned NULL");

	return Temp;
}

*/


int SendtoGUI(char Type, unsigned char * Msg, int Len)	
{
	// Again more ARDOP_GUI interfacing code.
	// If we do not plan to support an official GUI, this code can be removed.
	unsigned char GUIMsg[16384];

	if (GUIActive == FALSE)
		return 0;

	if (Len > 16000)
		return 0;

	GUIMsg[0] = Type;
	memcpy(GUIMsg + 1, Msg, Len);
 
	return sendto(GUISock, GUIMsg, Len + 1, 0, (struct sockaddr *)&GUIHost, sizeof(GUIHost));
}
