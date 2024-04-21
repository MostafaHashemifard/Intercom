#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/file.h>
#include <syslog.h>

char inMessage[256];
char actionCommand[25] = {0};
int socket_desc, client_sock, c, read_size;
int outFlag = 0;
int inCH = 0;
int inCI = 0;
struct sockaddr_in server, client;
pthread_mutex_t inLock, outLock;
char CMD[1028] = {0};
char RES[1028] = {0};

int fileExistsAndHasContents(char *address)
{
	struct stat stat_record;
	
	if(stat(address, &stat_record))
		return -1;
	else if(stat_record.st_size <= 1)
		return 0;
	return 1;
}

void removeUndigits(char *input)
{
	int i, j = 0;
	for(i = 0;input[i]; i++)
	{
		if (input[i] >= '0' && input[i] <= '9')
		{
			input[j] = input[i];
			j++;
		}
	}
	input[j] = '\0';
}

void removeSpaces(char *input)
{
	char *tmp = input;
	do
	{
		while (*tmp == '\n' || *tmp == '\r' || *tmp == ' ')
		{
			++tmp;
        }
    } while (*input++ = *tmp++);
}

int isNumericChar(char x) {
   return (x >= '0' && x <= '9') ? 1 : 0;
}

int customExec(char *cmd, char *result)
{
	FILE *fp;
	char path[1028];
	char tmp[1028];
	char c;
	int i = 0;
	
	fp = popen(cmd, "r");
	if (fp == NULL)
	{

		/*LOG*/
		memset(CMD, 0, 1028);
		sprintf(CMD, "DEBUG: in function: %s error: %s\n", __FUNCTION__, strerror(errno));
		syslog(LOG_USER, CMD);
		/*LOG-END*/
		printf("Failed to run command\n" );
		return 0;
	}
	
	memset(path, 0, 1028);
	while (fgets(path, sizeof(path), fp) != NULL)
	{
		strcat(result, path);
	}
	//
	pclose(fp);
	return 1;
}


int playerManager(int dtmf, int hangup)
{
	char address[256] = {0};
	char repeat[8] = {0};
	char volume[8] = {0};
	char action[16] = {0};
	char cmd[256] = {0};
	char res[128] = {0};
	int number;
	//
	if(!fileExistsAndHasContents("/etc/config/player.ini"))
		return 0;
	//
	memset(cmd, 0, 256);
	memset(res, 0, 128);
	sprintf(cmd, "grep -w %d /etc/config/player.ini | cut -d '=' -f 1", dtmf);
	customExec(cmd, res);
	//
	if(res)
	{
		removeSpaces(res);
		//
		if(strstr(res, "stopCode"))
		{
			sprintf(action, "stop");
		}
		else
		{
			sprintf(action, "play");
			number = (res[strlen(res) - 1]) - '0';
			//
			memset(cmd, 0, 256);
			memset(address, 0, 256);
			sprintf(cmd, "grep -w fileName%d /etc/config/player.ini  | cut -d '=' -f 2", number);
			if(customExec(cmd, address))
				removeSpaces(address);
			//
			memset(cmd, 0, 256);
			memset(repeat, 0, 8);
			sprintf(cmd, "grep -w repeat%d /etc/config/player.ini  | cut -d '=' -f 2", number);
			if(customExec(cmd, repeat))
				removeSpaces(repeat);
			//
			memset(cmd, 0, 256);
			memset(volume, 0, 8);
			sprintf(cmd, "grep -w volume%d /etc/config/player.ini  | cut -d '=' -f 2", number);
			if(customExec(cmd, volume))
				removeSpaces(volume);
		}
	}
	else
		return 0;
	//
	if(hangup)
	{
		memset(res, 0, 128);
		memset(cmd, 0, 256);
		sprintf(cmd, "run_action hangup > /dev/null 2>&1", res);
		customExec(cmd, res);
		//
		pthread_mutex_lock(&outLock);
		printf("DEBUG: Detected call hangup\n");
		memset(actionCommand, 0, 25);
		sprintf(actionCommand, "C:H");
		outFlag = 1;
		pthread_mutex_unlock(&outLock);
	}
	//
	/*LOG*/
	memset(CMD, 0, 1028);
	sprintf(CMD, "DEBUG: %s file will play with %s volume and repeat: %s with action: %s\n", address, volume, repeat, action);
	syslog(LOG_USER, CMD);
	/*LOG-END*/
	printf("DEBUG: %s file will play with %s volume and repeat: %s\n", address, volume, repeat);
	memset(cmd, 0, 256);
	memset(res, 0, 128);
	sprintf(cmd, "player %s %s %s %s > /dev/null 2>&1", address, repeat, volume, action);
	customExec(cmd, res);
	//
	return 1;
}

int createActionCommand(int dtmf, char *act)
{
	char res[1028] = {0};
	char cmd[256] = {0};
	char action[16] = {0};
	char duration[16] = {0};
	int number;
	FILE *fp;
	//
	if(!fileExistsAndHasContents("/etc/config/alarms.ini"))
		return 0;
	//
	sprintf(cmd, "grep dtmfCode /etc/config/alarms.ini | grep -w %d | cut -d '=' -f 1", dtmf);
	customExec(cmd, res);
	//
	if(res && strlen(res))
	{
			syslog(LOG_SYSLOG, "DEBUG: res: %s\n", res);
			syslog(LOG_SYSLOG, "DEBUG: in if\n");
		removeSpaces(res);
		//
		number = (res[strlen(res) - 1]) - '0';
		//
		memset(cmd, 0, 256);
		memset(action, 0, 16);
		sprintf(cmd, "grep -w action%d /etc/config/alarms.ini  | cut -d '=' -f 2", number);
		if(customExec(cmd, action))
			removeSpaces(action);
		//
		memset(cmd, 0, 256);
		memset(duration, 0, 16);
		sprintf(cmd, "grep -w duration%d /etc/config/alarms.ini  | cut -d '=' -f 2", number);
		if(customExec(cmd, duration))
			removeSpaces(duration);
	}
	else
	{
			syslog(LOG_SYSLOG, "DEBUG: in else\n");
		memset(cmd, 0, 256);
		memset(res, 0, 1028);
		sprintf(cmd, "grep dtmfCodes /etc/config/zonessettings.ini | grep -w %d | cut -d '=' -f 1", dtmf);
		customExec(cmd, res);
			syslog(LOG_SYSLOG, "DEBUG: res: %s\n", res);
		//
		if(res && strlen(res))
		{
			removeSpaces(res);
			//
			number = (res[strlen(res) - 1]) - '0';
			//
			memset(cmd, 0, 256);
			memset(action, 0, 16);
			sprintf(cmd, "grep -w actions%d /etc/config/zonessettings.ini  | cut -d '=' -f 2", number);
			if(customExec(cmd, action))
				removeSpaces(action);
			syslog(LOG_SYSLOG, "DEBUG: action: %s\n", action);
			if(strstr(action, "on") || strstr(action, "1"))
				action[0] = 'h';
			if(strstr(action, "off") || strstr(action, "0"))
				action[0] = 'l';
			//
			memset(cmd, 0, 256);
			memset(duration, 0, 16);
			sprintf(cmd, "grep -w durations%d /etc/config/zonessettings.ini  | cut -d '=' -f 2", number);
			if(customExec(cmd, duration))
				removeSpaces(duration);
			syslog(LOG_SYSLOG, "DEBUG: duration: %s\n", duration);
			number = 0;
		}
		else
			return 0;
	}
	if(number == 1 || number == 2)
	{
		memset(cmd, 0, 256);
		sprintf(cmd, "screen -S ALARMSGPIOMNGR -d -m alarms_gpio_manager %d %c %s", number, action[0], duration);
		customExec(cmd, res);
	}
	//sprintf(act, "%d:%c:%s", number, action[0], duration);
	sprintf(act, "c%d%c%s\n", number, action[0], duration);
			syslog(LOG_SYSLOG, "DEBUG: act: %s\n", act);
	//
	return 1;
}

int playerOrAlarm(int dtmf)
{
	char cmd[256] = {0};
	char res[128] = {0};
	//
	if(fileExistsAndHasContents("/etc/config/alarms.ini"))
	{
		memset(cmd, 0, 256);
		memset(res, 0, 128);
		sprintf(cmd, "grep dtmfCode /etc/config/alarms.ini | grep -w %d | cut -d '=' -f 1", dtmf);
		customExec(cmd, res);
		if(res && strlen(res))
			return 1;
	}
	if(fileExistsAndHasContents("/etc/config/player.ini"))
	{
		memset(cmd, 0, 256);
		memset(res, 0, 128);
		sprintf(cmd, "grep dtmfCode /etc/config/player.ini | grep -w %d | cut -d '=' -f 1", dtmf);
		customExec(cmd, res);
		if(res && strlen(res))
			return 2;
	}
	if(fileExistsAndHasContents("/etc/config/zonessettings.ini"))
	{
		memset(cmd, 0, 256);
		memset(res, 0, 128);
		sprintf(cmd, "grep dtmfCodes /etc/config/zonessettings.ini | grep -w %d | cut -d '=' -f 1", dtmf);
		customExec(cmd, res);
		if(res && strlen(res))
			return 3;
	}
	//
	return 0;
}

/*int isAutoAnswerEnabled()
{
	char cmd[256] = {0};
	char res[128] = {0};
	//
	if(!fileExistsAndHasContents("/etc/config/sipsettings.ini"))
		return -1;
	//
	memset(cmd, 0, 256);
	memset(res, 0, 128);
	sprintf(cmd, "grep autoAnswer /etc/config/sipsettings.ini | cut -d '=' -f 2 | grep -w auto");
	customExec(cmd, res);
	if(res && strlen(res)) //auto answer enabled
		return 1;
	return 0;
}*/

void *socketThread()
{
	char client_message[256] = {0};
	char res[256] = {0};
	char cmd[256] = {0};
	int _playerOrAlarm = 0, dtmf;
	while(1)
	{
		memset(client_message, 0, 256);
		if (outFlag == 1)
		{
			pthread_mutex_lock(&outLock);
			write(client_sock , actionCommand , strlen(actionCommand));
			syslog(LOG_USER, "outMessage '%s' sent\n", actionCommand);
			outFlag = 0;
			pthread_mutex_unlock(&outLock);
		}
		if(recv(client_sock, client_message, 256, MSG_DONTWAIT) >= 0)
		{
			printf("inMessage '%s' recieved\n", client_message);
			if (strstr(client_message, "C:H")) //call hangup
			{
				memset(CMD, 0, 1028);
				sprintf(CMD, "DEBUG: Call hangup from MCU\n");
				syslog(LOG_USER, CMD);
				printf("DEBUG: Call hangup from MCU\n");
				memset(res, 0, 256);
				customExec("run_action hangup", res);
			}
			else if (strstr(client_message, "@")) //dial number
			{
				client_message[strlen(client_message) - 1] = '\0';
				memset(CMD, 0, 1028);
				sprintf(CMD, "DEBUG: Dail number %s from MCU\n", client_message);
				syslog(LOG_USER, CMD);
				printf("DEBUG: Dail number %s from MCU\n", client_message);
				//
				memset(res, 0, 256);
				memset(cmd, 0, 256);
				sprintf(cmd, "run_action dial %s", client_message);
				customExec(cmd, res);
			}
			else if (strstr(client_message, "*")) //stand alone action
			{
				syslog(LOG_USER, "DEBUG: FIFO service. stand alone action, cleint message: %s\n", client_message);
				client_message[strlen(client_message) - 1] = '\0';
				dtmf = atoi(client_message);
				_playerOrAlarm = playerOrAlarm(dtmf);
				if(_playerOrAlarm == 1)
				{
					syslog(LOG_USER, "DEBUG: fifo_service incoming DTMF code: %d is alarm", dtmf);
					memset(actionCommand, 0, 25);
					createActionCommand(dtmf, actionCommand);
					pthread_mutex_lock(&outLock);
					outFlag = 1;
					pthread_mutex_unlock(&outLock);
				}
				else if(_playerOrAlarm == 2)
				{
					playerManager(dtmf, 0);
					syslog(LOG_USER, "DEBUG: fifo_service incoming DTMF code: %d is player", dtmf);
					memset(actionCommand, 0, 25);
					sprintf(actionCommand, "0:p:0");
					pthread_mutex_lock(&outLock);
					outFlag = 1;
					pthread_mutex_unlock(&outLock);
				}
				else if(_playerOrAlarm == 3)
				{
					syslog(LOG_USER, "DEBUG: fifo_service incoming DTMF code: %d is zone dtmf", dtmf);
					memset(actionCommand, 0, 25);
					createActionCommand(dtmf, actionCommand);
					pthread_mutex_lock(&outLock);
					outFlag = 1;
					pthread_mutex_unlock(&outLock);
				}
				else
				{
					syslog(LOG_USER, "DEBUG: fifo_service incoming DTMF code: %d is nothing", dtmf);
					memset(actionCommand, 0, 25);
					sprintf(actionCommand, "0:q:0");
					pthread_mutex_lock(&outLock);
					outFlag = 1;
					pthread_mutex_unlock(&outLock);
				}
			}
			if (strstr(client_message, "!")) //volume command
			{
				client_message[strlen(client_message) - 1] = '\0';
				memset(res, 0, 256);
				memset(cmd, 0, 256);
				sprintf(cmd, "run_action mic %s > /dev/null 2>&1", strchr(client_message, '>') ? "+" : "-");
				customExec(cmd, res);
			}
			memset(client_message, 0, 256);
		}
		usleep(100000);
	}
}

int isAllowdChar(char *input)
{
	int i;
	if (!isdigit(input[0]) && input[0] != 'A' && input[0] != 'B' && input[0] != 'C' && input[0] != 'D' && input[0] != '*' && input[0] != '#' && input[0] != 'E' && input[0] != 'F')
		return 0;
	return 1;
}

void *fifoReadThread()
{
	int fifoD, n, _playerOrAlarm;
	char dtmf[64] = {0};
	char cmd[256] = {0};
	char fifo[256] = {0};
	char res[256] = {0};
	char *dmtfFifo = "/tmp/dtmf.out";
	char *ptr;
	fifoD = open(dmtfFifo, O_RDONLY);
	while(1)
	{
		if(fifoD >= 0)
		{
			n = read(fifoD, fifo, 128);
			removeSpaces(fifo);
			if(strlen(fifo) && fifo != NULL && isAllowdChar(fifo))
			{		
				if(strstr(fifo, "#"))
				{
					removeUndigits(dtmf);
					if(strlen(dtmf) > 0)
					{
						syslog(LOG_USER, "DEBUG: DTMF: %s\n", dtmf);
						_playerOrAlarm = playerOrAlarm(atoi(dtmf));
						if(_playerOrAlarm == 1)
						{
							/*LOG*/
							memset(CMD, 0, 1028);
							sprintf(CMD, "DEBUG: Detected  DTMF : %s is alarm\n", dtmf);
							syslog(LOG_USER, CMD);
							/*LOG-END*/
							printf("DEBUG: Detected  DTMF : %s is alarm\n", dtmf);
							memset(actionCommand, 0, 25);
							createActionCommand(atoi(dtmf), actionCommand);
							pthread_mutex_lock(&outLock);
							outFlag = 1;
							pthread_mutex_unlock(&outLock);
							memset(fifo, 0, 256);
							memset(dtmf, 0, 64);
							memset(cmd, 0, 256);
						}
						else if(_playerOrAlarm == 2)
						{
							/*LOG*/
							memset(CMD, 0, 1028);
							sprintf(CMD, "echo DEBUG: Detected  DTMF : %s is player\n", dtmf);
							syslog(LOG_USER, CMD);
							/*LOG-END*/
							printf("DEBUG: Detected DTMF : %s is player\n", dtmf);
							playerManager(atoi(dtmf), 1);
							memset(fifo, 0, 256);
							memset(dtmf, 0, 64);
							memset(cmd, 0, 256);
						}
						else if(_playerOrAlarm == 3)
						{
							/*LOG*/
							memset(CMD, 0, 1028);
							sprintf(CMD, "DEBUG: Detected  DTMF : %s is zone dtmf\n", dtmf);
							syslog(LOG_USER, CMD);
							/*LOG-END*/
							memset(actionCommand, 0, 25);
							createActionCommand(atoi(dtmf), actionCommand);
							pthread_mutex_lock(&outLock);
							outFlag = 1;
							pthread_mutex_unlock(&outLock);
							memset(fifo, 0, 256);
							memset(dtmf, 0, 64);
							memset(cmd, 0, 256);
						}
						else
						{
							/*LOG*/
							memset(CMD, 0, 1028);
							sprintf(CMD, "echo DEBUG: Detected  DTMF : %s is nothing\n", dtmf);
							syslog(LOG_USER, CMD);
							/*LOG-END*/
							memset(actionCommand, 0, 25);
							sprintf(actionCommand, "0:q:0");
							pthread_mutex_lock(&outLock);
							outFlag = 1;
							pthread_mutex_unlock(&outLock);
							memset(fifo, 0, 256);
							memset(dtmf, 0, 64);
							memset(cmd, 0, 256);
						}
					}
				}
				else if(strstr(fifo, "E"))
				{
					pthread_mutex_lock(&inLock);
					inCH = 0;
					inCI = 0;
					pthread_mutex_unlock(&inLock);
					pthread_mutex_lock(&outLock);
					/*LOG*/
					memset(CMD, 0, 1028);
					sprintf(CMD, "DEBUG: Detected incoming call\n");
					syslog(LOG_USER, CMD);
					/*LOG-END*/
					printf("DEBUG: Detected incoming call\n");
					memset(actionCommand, 0, 25);
					sprintf(actionCommand, "C:I");
					outFlag = 1;
					memset(fifo, 0, 256);
					memset(dtmf, 0, 256);
					memset(cmd, 0, 256);
					pthread_mutex_unlock(&outLock);
				}
				else if(strstr(fifo, "F"))
				{
					pthread_mutex_lock(&outLock);
					/*LOG*/
					memset(CMD, 0, 1028);
					sprintf(CMD, "DEBUG: Detected call hangup");
					syslog(LOG_USER, CMD);
					/*LOG-END*/	
					//* Killing madplay screen *//
					memset(res, 0, 256);
					memset(cmd, 0, 256);
					sprintf(cmd, "player - - - stop > /dev/null 2>&1");
					customExec(cmd, res);
					//* Killing madplay screen *//
					printf("DEBUG: Detected call hangup\n");
					memset(actionCommand, 0, 25);
					sprintf(actionCommand, "C:H");
					outFlag = 1;
					memset(fifo, 0, 256);
					memset(dtmf, 0, 256);
					memset(cmd, 0, 256);
					pthread_mutex_unlock(&outLock);
				}
				else if(strstr(fifo, "A"))
				{
				}
				else if(strstr(fifo, "B"))
				{
				}
				else if(strstr(fifo, "C"))
				{
				}
				else if(strstr(fifo, "D"))
				{
				}
				else
				{
					strcat(dtmf, fifo);
				}
			}
		}
		else
		{
			fifoD = 0;
			fifoD = open(dmtfFifo, O_RDONLY);
		}
		usleep(100000);
	}
}

int socketInitiation()
{
	//Create socket
	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		/*LOG*/
		memset(CMD, 0, 1028);
		sprintf(CMD, "DEBUG: Could not create socket\n");
		syslog(LOG_USER, CMD);
		/*LOG-END*/
		printf("Could not create socket");
		return 0;
	}
	/*LOG*/
	memset(CMD, 0, 1028);
	sprintf(CMD, "DEBUG: Socket Initiated\n");
	syslog(LOG_USER, CMD);
	/*LOG-END*/
	puts("Socket Initiated");
	
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(8888);
	
	//Bind
	if(bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
	{
		/*LOG*/
		memset(CMD, 0, 1028);
		sprintf(CMD, "DEBUG: bind failed. Error\n");
		syslog(LOG_USER, CMD);
		/*LOG-END*/
		perror("bind failed. Error");
		return 0;
	}
	puts("bind done");
	
	//Listen
	listen(socket_desc , 3);
	
	/*LOG*/
	memset(CMD, 0, 1028);
	sprintf(CMD, "DEBUG: Waiting for incoming connections...\n");
	syslog(LOG_USER, CMD);
	/*LOG-END*/
	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_in);
	
	//accept connection from an incoming client
	client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c);
	if (client_sock < 0)
	{
		/*LOG*/
		memset(CMD, 0, 1028);
		sprintf(CMD, "DEBUG: accept failed\n");
		syslog(LOG_USER, CMD);
		/*LOG-END*/
		perror("accept failed");
		return 0;
	}
	/*LOG*/
	memset(CMD, 0, 1028);
	sprintf(CMD, "DEBUG: Connection accepted\n");
	syslog(LOG_USER, CMD);
	/*LOG-END*/
	puts("Connection accepted");
	return 1;
}

int main(int argc, char** argv)
{
	pthread_t _readSocketThread, _writeSocketThread, _fifoReadThread, _socketThread;
	int  iret1, iret2, iret3, iret4;
	
	if (pthread_mutex_init(&outLock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }    
	
	if (pthread_mutex_init(&inLock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }    
	
    socketInitiation();
    
    iret1 = pthread_create( &_socketThread, NULL, socketThread, NULL);
	iret3 = pthread_create( &_fifoReadThread, NULL, fifoReadThread, NULL);

	pthread_join(_socketThread, NULL);
	pthread_join(_fifoReadThread, NULL);

	pthread_mutex_destroy(&inLock);
	pthread_mutex_destroy(&outLock);
		
	return 0;	
}
