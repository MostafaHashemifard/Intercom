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
#include <termios.h>
#include <syslog.h>
#include <math.h>
#include <signal.h>

char uidContainerFile[] = "/uid_container";
char inMessage[256] = {0};
char outMessage[256] = {0};
int socket_desc, sock, c, read_size;
struct sockaddr_in server, client;
pthread_mutex_t inLock, outLock;
unsigned short int inFlag = 0;
unsigned short int outFlag = 0;
unsigned short int isRinging = 0;
unsigned short int equalizerSettingsUpdated = 0;
unsigned short int isLaterCallAvailable = 0;
unsigned short int ringingMsgShowed = 0;
char act[25] = {0};
char CMD[1028] = {0};
char RES[1028] = {0};
char equalizer[][10] = { "val31", "val62", "val125", "val250", "val500", "val1K", "val2K", "val4K", "val8K", "val16K" };
double timeSpent = 0.0;
clock_t begin, now;
typedef enum {FREE, RINGING, INCOMING, OUTGOING}callStatus;

struct phoneStatus
{
	char *lcdPrompt;
	unsigned short int lcdPromptLen;
	callStatus cs;
}phoneStat[4], currentPhoneStat;

int fileExistsAndHasContents(char *address)
{
	struct stat stat_record;
	
	if(stat(address, &stat_record))
		return -1;
	else if(stat_record.st_size <= 1)
		return 0;
	return 1;
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

int isNumerical()
{
	char chunk[128];
	FILE *fp = fopen("/etc/config/keypad.cfg", "r");
	//
	if (fp == NULL)
		return -1;
	//
	if (fgets(chunk, sizeof(chunk), fp) != NULL)
	{
		removeSpaces(chunk);
		if(strlen(chunk))
		{
			fclose(fp);
			return strcmp(chunk, "numerical") ? 0 : 1;
		}
	}
	fclose(fp);
	return -1;
}

int removeDots(char *input)
{
	char *tmp = input;
	do
	{
		while (*tmp == '.')
		{
			++tmp;
        }
    } while (*input++ = *tmp++);
    return strlen(input);
}

int removeYs(char *input)
{
	char *tmp = input;
	do
	{
		while (*tmp == 'Y')
		{
			++tmp;
        }
    } while (*input++ = *tmp++);
    return strlen(input);
}

int removePlusMinus(char *input)
{
	char *tmp = input;
	do
	{
		while (*tmp == '+' || *tmp == '-')
		{
			++tmp;
        }
    } while (*input++ = *tmp++);
    return strlen(input);
}

int setInterfaceAttribs (int fd, int speed, int parity)
{
	struct termios tty;
	memset (&tty, 0, sizeof tty);
	if (tcgetattr (fd, &tty) != 0)
	{
		printf ("error %d from tcgetattr", errno);
		return -1;
	}
	cfsetospeed (&tty, speed);
	cfsetispeed (&tty, speed);
	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;	// 8-bit chars
	tty.c_iflag &= ~IGNBRK;			// disable IGNBRK for mismatched speed tests; otherwise receive break as \000 chars
	tty.c_lflag = 0;				// no signaling chars, no echo, no canonical processing
	tty.c_oflag = 0;				// no remapping, no delays
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);	// shut off xon/xoff ctrl
	tty.c_cflag |= (CLOCAL | CREAD);		// ignore modem controls, enable reading
	tty.c_cflag &= ~(PARENB | PARODD);		// shut off parity
	tty.c_cflag |= parity;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;
	if (tcsetattr (fd, TCSANOW, &tty) != 0)
	{
		printf ("error %d from tcsetattr", errno);
		return -1;
	}
	return 0;
}

int setBlocking (int fd, int should_block)
{
	struct termios tty;
	memset (&tty, 0, sizeof tty);
	if (tcgetattr (fd, &tty) != 0)
	{
		printf ("error %d from tggetattr", errno);
		return 1;
	}
	tty.c_cc[VMIN]  = should_block ? 1 : 0;
	tty.c_cc[VTIME] = 5;
	if (tcsetattr (fd, TCSANOW, &tty) != 0)
		printf ("error %d setting term attributes", errno);
	return 0;
}

void writeAll(int fd, char *data, int len, int flag)
{
	int i;
	char chunk[2];
	
	for(i=0;i<len;i++)
	{
		sprintf(chunk, "%c\n", data[i]);
		write(fd, chunk, 1);
		usleep(10000);
	}
	
	if(flag)
		write(fd, "^\n", 1);
		
	usleep(10000);
}

void *socketThread()
{
	char server_reply[128];
	while(1)
	{
		memset(server_reply, 0, 128);
		if(outFlag == 1)
		{
			if (outMessage != NULL && strlen(outMessage))
			{
				write(sock, outMessage, strlen(outMessage));
				syslog(LOG_SYSLOG, "DEBUG: serial_io_service outMessage: %s\n", outMessage);
				printf("outMessage '%s' sent\n", outMessage);
			}
			pthread_mutex_lock(&outLock);
			outFlag = 0;
			pthread_mutex_unlock(&outLock);
		}
		if(recv(sock , server_reply , 128 , MSG_DONTWAIT) >= 0)
		{
			memset(inMessage, 0, 256);
			strcpy(inMessage, server_reply);
			/*LOG*/
			memset(CMD, 0, 1028);
			sprintf(CMD, "DEBUG: socket thread, inMessage: %s\n", inMessage);
			syslog(LOG_SYSLOG, CMD);
			syslog(LOG_USER, CMD);
			/*LOG-END*/
			pthread_mutex_lock(&inLock);
			inFlag = 1;
			pthread_mutex_unlock(&inLock);
		}
		usleep(100000);
	}
}

int createActionCommand(char *uid)
{
	char res[1028] = {0};
	char cmd[256] = {0};
	char action[16] = {0};
	char duration[16] = {0};
	char pin[8] = {0};
	int number;
	FILE *fp;
	//
	if(!fileExistsAndHasContents("/etc/config/cards.ini"))
		return 0;
	//
	sprintf(cmd, "grep -w %s /etc/config/cards.ini | cut -d '=' -f 1", uid);
	customExec(cmd, res);
	//
	if(res && strlen(res))
	{
		removeSpaces(res);
		//
		number = (res[strlen(res) - 1]) - '0';
		//
		memset(cmd, 0, 256);
		memset(action, 0, 16);
		sprintf(cmd, "grep -w action%d /etc/config/cards.ini  | cut -d '=' -f 2", number);
		if(customExec(cmd, action))
			removeSpaces(action);
		syslog(LOG_USER, "action: %s\n", action);
		//
		memset(cmd, 0, 256);
		memset(duration, 0, 16);
		sprintf(cmd, "grep -w duration%d /etc/config/cards.ini  | cut -d '=' -f 2", number);
		if(customExec(cmd, duration))
			removeSpaces(duration);
		syslog(LOG_USER, "duration: %s\n", duration);
		//
		memset(cmd, 0, 256);
		memset(pin, 0, 8);
		sprintf(cmd, "grep -w pin%d /etc/config/cards.ini  | cut -d '=' -f 2", number);
		if(customExec(cmd, pin))
			removeSpaces(pin);
		syslog(LOG_USER, "pin: %s\n", pin);
		if(strstr(pin, "pd5"))
			number = 3;
		else if(strstr(pin, "pd6"))
			number = 4;
		else if(strstr(pin, "pd7"))
			number = 5;
		else if(strstr(pin, "gp4"))
			number = 1;
		else if(strstr(pin, "gp5"))
			number = 2;
		if(number == 1 || number == 2)
		{
			memset(cmd, 0, 256);
			sprintf(cmd, "screen -S ALARMSGPIOMNGR -d -m alarms_gpio_manager %d %c %s", number, action[0], duration);
			customExec(cmd, res);
		}
		//sprintf(act, "%d:%c:%s", number, action[0], duration);
		sprintf(act, "c%d%c%s\n", number, action[0], duration);
	}
	else
		sprintf(act, "0:O:0");
	//
	return 1;
}

void firstMenuPage(char *resip, char *sipstatus)
{
	char result[512] = {0};
	char cmd[512] = {0};
	char tmp[512] = {0};
	//
	if(customExec("ifconfig eth0.2 | grep 'inet addr' | cut -d ':' -f 2 | cut -d ' ' -f 1", result))
	{
		removeSpaces(result);
		if(result && strlen(result))
		{
			strcpy(resip, result);
			//strcat(tmp, ":");
			memset(result, 0, 512);
			customExec("echo r | nc 127.0.0.1 5555 | grep sip | grep OK", result);
			if (result && strlen(result))
				//strcat(tmp, "Registered");
				strcpy(sipstatus, "Registered");
			else
				//strcat(tmp, "Failed");
				strcpy(sipstatus, "Failed");
		}
		else
		{
			memset(result, 0, 512);
			memset(tmp, 0, 512);
			if(customExec("ifconfig br-lan | grep 'inet addr' | cut -d ':' -f 2 | cut -d ' ' -f 1", result))
			{
				strcpy(resip, result);
				//strcat(tmp, ":");
				memset(result, 0, 512);
				customExec("echo r | nc 127.0.0.1 5555 | grep sip | grep OK", result);
				if (result && strlen(result))
					strcpy(sipstatus, "Registered");
				else
					strcpy(sipstatus, "Failed");
			}
			else
			{
				strcpy(sipstatus, "Failed");
				strcpy(resip, "Unavailable");
			}
		}
	}
	else
	{
		strcpy(sipstatus, "Failed");
		strcpy(resip, "Unavailable");
	}
}

void *serialThread()
{
	int n, fd = open ("/dev/ttyS1", O_RDWR | O_NOCTTY | O_SYNC);
	int startFlag = 0;
	FILE *fp;
	int nUSB0, fdUSB0 = -1;
	double tmpDouble;
	char buf[1000] = {0};
	char number[15] = {0};
	char result[512] = {0};
	char _result[2048] = {0};
	char resIp[32] = {0};
	char sipStatus[16] = {0};
	char cmd[512] = {0};
	char tmp[512] = {0};
	char bufNfc[2048] = {0};
	char uid[64] = {0};
	char *_act;
	char *duration;
	char *pin;
	int msgIndc = 0;
	int menuPageCount = 1;//increase for other menu pages
	unsigned long int oldTimer = 0;
	unsigned long int timer = 0;
	//
	setInterfaceAttribs (fd, B9600, 0);//B115200, 0);//B4800, 0);//B9600, 0);
	setBlocking (fd, 1);
	if(isNumerical())
		writeAll(fd, "N^\n", 2, 0);
	else
		writeAll(fd, "S^\n", 2, 0);
	//writeAll(fd, "^\n", 1, 0);
	//
	phoneStat[0].lcdPrompt = "Ready to make calls";
	phoneStat[0].cs = FREE;
	phoneStat[0].lcdPromptLen = strlen(phoneStat[0].lcdPrompt);
	//
	phoneStat[1].lcdPrompt = "Ringing";
	phoneStat[1].cs = RINGING;
	phoneStat[1].lcdPromptLen = strlen(phoneStat[1].lcdPrompt);
	//
	phoneStat[2].lcdPrompt = "Incoming call";
	phoneStat[2].cs = INCOMING;
	phoneStat[2].lcdPromptLen = strlen(phoneStat[2].lcdPrompt);
	//
	phoneStat[3].lcdPrompt = "Outgoing call";
	phoneStat[3].cs = OUTGOING;
	phoneStat[3].lcdPromptLen = strlen(phoneStat[3].lcdPrompt);
	//
	currentPhoneStat = phoneStat[0];
	//
	setInterfaceAttribs (fdUSB0, B115200, 0);
	setBlocking (fdUSB0, 1);
	
	while(1)
	{
		if(fdUSB0 == -1)
		{
			syslog(LOG_USER, "DEBUG: can not open port\n");
			fdUSB0 = open ("/dev/arduinoGW", O_RDWR | O_NONBLOCK | O_NOCTTY);
		}
		else
		{
			if(startFlag == 0)
			{
				syslog(LOG_USER, "send start flag\n");
				writeAll(fdUSB0, "S", 1, 0);
				startFlag = 1;
			}
			nUSB0 = read(fdUSB0, bufNfc, sizeof(bufNfc));
			if(strlen(bufNfc))
			{
				removeSpaces(bufNfc);
				if(startFlag == 1 && strstr(bufNfc, "Q"))
					startFlag = 0;
				if(strstr(bufNfc, "-"))
					syslog(LOG_USER, "bufNFC: %s NFC module not ready!\n", bufNfc);
				removePlusMinus(bufNfc);
				
				if(strstr(bufNfc, "%"))
					msgIndc++;
					
				if(msgIndc >= 1)
					strcat(uid, bufNfc);
					
				if(uid[0] == '%' && uid[strlen(uid) - 1] == '%')
				{
					writeAll(fd, "%\n", 1, 0);
					uid[strlen(uid) - 1] = '\0';
					sprintf(uid, "%s", strstr(uid, "%") + 1);
					//
					fp = fopen(uidContainerFile, "w");
					fputs(uid, fp);
					fclose(fp);
					//
					createActionCommand(uid);
					//
					usleep(10000);
					if(strstr(act, "0:O:0"))
					{
						writeAll(fdUSB0, "0", 1, 0);
						writeAll(fd, "5Access denied\n", 14, 1);
						usleep(500000);
						writeAll(fd, "d5\n", 2, 1);
					}
					else
					{
						writeAll(fdUSB0, "1", 1, 0);
						syslog(LOG_USER, "uid(%d): %s act: %s\n", strlen(uid), uid, act);
						writeAll(fd, act, strlen(act), 1);
						usleep(500000);
						writeAll(fd, "d4\n", 2, 1);
					}
					//
					msgIndc = 0;
					memset(&uid, 0, 64);
				}
				else if(msgIndc == 2)
				{
					msgIndc = 0;
					memset(&uid, 0, 64);
				}
			}
			memset(&bufNfc, 0, 2048);
		}
		
		
		if(equalizerSettingsUpdated)
		{
			syslog(LOG_SYSLOG, "Equalizer settings updated\n");
			//		
			for(int i = 0; i < 10; i++)
			{
				memset(cmd, 0, 512);
				memset(result, 0, 512);
				memset(tmp, 0, 512);
				sprintf(cmd, "grep %s /etc/config/equalizer.ini", equalizer[i]);
				if(customExec(cmd, result))
					removeSpaces(result);
				sprintf(tmp, "5%s", result);
				writeAll(fd, tmp, strlen(tmp), 1);
				syslog(LOG_SYSLOG, "value: %s\n", result);
				usleep(1000000);
				writeAll(fd, "d5\n", 2, 1);			
				usleep(1000000);
			}
			equalizerSettingsUpdated = 0;
		}
		
		if(currentPhoneStat.cs == RINGING && ringingMsgShowed == 0)
		{
			syslog(LOG_USER, "Ringing\n");
			writeAll(fd, "3\n", 1, 0);
			writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
			ringingMsgShowed = 1;
		}

		if(inFlag == 1)
		{
			syslog(LOG_SYSLOG, "DEBUG: fifo command: %s\n", inMessage);
			if (strstr(inMessage, "C:I")) //incoming call
			{
				if (currentPhoneStat.cs == FREE)
				{
					currentPhoneStat = phoneStat[2];
					writeAll(fd, "3\n", 1, 0);
					writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
				}
			}
			else if (strstr(inMessage, "C:H")) //call hangup
			{
				if (currentPhoneStat.cs != FREE)
				{
					if(isLaterCallAvailable == 1)//(strlen(number)) //later call
					{
						memset(outMessage, 0, 256);
						sprintf(outMessage, "%s@", number);
						pthread_mutex_lock(&outLock);
						outFlag = 1;
						pthread_mutex_unlock(&outLock);
						memset(number, 0, 15);
						memset(buf, 0, 1000);
						writeAll(fd, "d0\n", 2, 1);
						usleep(500000);
						currentPhoneStat = phoneStat[3];
						writeAll(fd, "3\n", 1, 0);
						writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
						isLaterCallAvailable = 0;
					}
					else
					{
						currentPhoneStat = phoneStat[0];
						writeAll(fd, "3\n", 1, 0);
						writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
					}
				}
			}
			else
			{
				usleep(10000);
				if(strstr(inMessage, "0:q:0"))
				{
					syslog(LOG_SYSLOG, "inMessage contains invalid alarm code\n");
					writeAll(fdUSB0, "0", 1, 0);
					//
					writeAll(fd, "5Invalid DTMF\n", 13, 1);
					usleep(500000);
					writeAll(fd, "d5\n", 2, 1);
				}
				else
				{
					writeAll(fdUSB0, "1", 1, 0); //NEW
					printf("inMessage: %s\n", inMessage);
					writeAll(fd, inMessage, strlen(inMessage), 1);
					usleep(500000);
					writeAll(fd, "d4\n", 2, 1);
					usleep(100000);
					writeAll(fd, "d5\n", 2, 1);
				}
			}
			pthread_mutex_lock(&inLock);
			inFlag = 0;
			pthread_mutex_unlock(&inLock);
		}
		else
		{
			n = read(fd, buf, sizeof buf);
			removeSpaces(buf);
			removeDots(buf);
			if (!isNumerical())
			{
				if(strlen(buf))
				{
					if(!strcmp(buf, "a"))
					{
						if(currentPhoneStat.cs == RINGING)
						{
							customExec("run_action answer > /dev/null 2>&1", RES);
							currentPhoneStat = phoneStat[2];
							writeAll(fd, "3\n", 1, 0);
							writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
						}
						else if(strlen(number))
						{
							if(currentPhoneStat.cs != FREE)
							{
								memset(outMessage, 0, 256);
								sprintf(outMessage, "C:H", number);
								pthread_mutex_lock(&outLock);
								outFlag = 1;
								pthread_mutex_unlock(&outLock);
								isLaterCallAvailable = 1;
								usleep(100000);
							}
							else
							{
								/*LOG*/
								syslog(LOG_SYSLOG, "DEBUG: number: %s\n", number);
								/*LOG-END*/
								memset(outMessage, 0, 256);
								sprintf(outMessage, "%s@", number);
								pthread_mutex_lock(&outLock);
								outFlag = 1;
								pthread_mutex_unlock(&outLock);
								memset(number, 0, 15);
								memset(buf, 0, 1000);
								writeAll(fd, "d0\n", 2, 1);
								usleep(500000);
								currentPhoneStat = phoneStat[3];
								writeAll(fd, "3\n", 1, 0);
								writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
							}
						}
						else
						{
							syslog(LOG_SYSLOG, "DEBUG: Detected call hangup from MCU\n");
							//
							memset(outMessage, 0, 256);
							sprintf(outMessage, "C:H", number);
							pthread_mutex_lock(&outLock);
							outFlag = 1;
							pthread_mutex_unlock(&outLock);
							//
							currentPhoneStat = phoneStat[0];
							//
							writeAll(fd, "c\n", 1, 0);
							writeAll(fd, "reset", 5, 0);
							writeAll(fd, "^\n", 1, 0);
						}
					}
					else if(!strcmp(buf, "b"))// && callStatus == 1)
					{			
						syslog(LOG_SYSLOG, "DEBUG: Detected call hangup from MCU\n");
						//
						memset(outMessage, 0, 256);
						sprintf(outMessage, "C:H", number);
						pthread_mutex_lock(&outLock);
						outFlag = 1;
						pthread_mutex_unlock(&outLock);
						//
						/*currentPhoneStat = phoneStat[0];
						//
						writeAll(fd, "c\n", 1, 0);
						writeAll(fd, "reset", 5, 0);
						writeAll(fd, "^\n", 1, 0);*/
						
						currentPhoneStat = phoneStat[0];
						writeAll(fd, "3\n", 1, 0);
						writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
					}
					else if(!strcmp(buf, "c"))
					{
						syslog(LOG_SYSLOG, "DEBUG: Detected volume increase\n");
						memset(outMessage, 0, 256);
						sprintf(outMessage, ">!", number);
						pthread_mutex_lock(&outLock);
						outFlag = 1;
						pthread_mutex_unlock(&outLock);
						memset(number, 0, 15);
						memset(buf, 0, 1000);
						writeAll(fd, "d0\n", 2, 1);
						usleep(500000);
						//
						writeAll(fd, "3Vol+", 6, 1);
						usleep(500000);
						//
						writeAll(fd, "3\n", 1, 0);
						writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
					}
					else if(!strcmp(buf, "d"))
					{
						syslog(LOG_SYSLOG, "DEBUG: Detected volume decrease\n");
						memset(outMessage, 0, 256);
						sprintf(outMessage, "<!", number);
						pthread_mutex_lock(&outLock);
						outFlag = 1;
						pthread_mutex_unlock(&outLock);
						memset(number, 0, 15);
						memset(buf, 0, 1000);
						writeAll(fd, "d0\n", 2, 1);
						usleep(500000);
						//
						writeAll(fd, "3Vol-", 6, 1);
						usleep(500000);
						//
						writeAll(fd, "3\n", 1, 0);
						writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
					}
					else if(!strcmp(buf, "#"))
					{
						if(strlen(number) > 0)
						{
							if(!strcmp(number, "000"))
							{
								writeAll(fd, "c\n", 1, 0);
								writeAll(fd, "menu\n", 4, 1);
								//
								menuPageCount = 1;//increase for other menu pages
								memset(number, 0, 15);
								memset(buf, 0, 1000);
								//
								memset(resIp, 0, 32);
								memset(sipStatus, 0, 16);
								firstMenuPage(resIp, sipStatus);
								syslog(LOG_USER, "DEBUG: res ip: %s sip stat: %s\n", resIp, sipStatus);
								//
								usleep(400000);
								//
								writeAll(fd, "6Local IP address\n", 17, 1);
								usleep(400000);
								writeAll(fd, "7\n", 1, 0);
								writeAll(fd, resIp, strlen(resIp), 1);
								usleep(400000);
								//
								writeAll(fd, "8SIP registration\n", 17, 1);
								usleep(400000);
								writeAll(fd, "9\n", 1, 0);
								writeAll(fd, sipStatus, strlen(sipStatus), 1);
							}
							else
							{
								memset(outMessage, 0, 256);
								sprintf(outMessage, "%s*", number);
								pthread_mutex_lock(&outLock);
								outFlag = 1;
								pthread_mutex_unlock(&outLock);
								memset(number, 0, 15);
								memset(buf, 0, 1000);
								writeAll(fd, "d0\n", 2, 1);
							}
						}
						else if(menuPageCount == 1) //increase for other menu pages
						{
							menuPageCount--;
							if(menuPageCount > 0) 
							{
							}
							else
							{
								writeAll(fd, "c\n", 1, 0);
								writeAll(fd, "reset", 5, 0);
								writeAll(fd, "^\n", 1, 0);
								menuPageCount = 0;
							}
						}
					}
					else
					{
						if(!strcmp(buf, "*"))
						{
							if(strlen(number) > 0)
								number[strlen(number) - 1] = '\0';
						}
						else
						{
							printf("Debug: buf: %s, number: %s\n", buf, number);
							strcat(number, buf);
							memset(buf, 0, 1000);
						}
						syslog(LOG_SYSLOG, "not sending number: %s\n", number);
						/*writeAll(fd, "0\n", 1, 0);
						writeAll(fd, number, strlen(number), 0);
						writeAll(fd, "^\n", 1, 0);*/
					}
				}
			}
			else
			{
				if(strlen(buf))
				{
					if(currentPhoneStat.cs != FREE)
					{
						memset(outMessage, 0, 256);
						sprintf(outMessage, "C:H", number);
						pthread_mutex_lock(&outLock);
						outFlag = 1;
						pthread_mutex_unlock(&outLock);
						usleep(100000);
					}
					strcpy(number, buf);
					/*LOG*/
					memset(CMD, 0, 1028);
					sprintf(CMD, "DEBUG: number: %s\n", number);
					syslog(LOG_USER, CMD);
					/*LOG-END*/
					memset(outMessage, 0, 256);
					sprintf(outMessage, "%s@", number);
					pthread_mutex_lock(&outLock);
					outFlag = 1;
					pthread_mutex_unlock(&outLock);
					memset(number, 0, 15);
					memset(buf, 0, 1000);
					//writeAll(fd, "d0\n", 2, 1);
					currentPhoneStat = phoneStat[3];
					writeAll(fd, "3\n", 1, 0);
					writeAll(fd, currentPhoneStat.lcdPrompt, currentPhoneStat.lcdPromptLen, 1);
				}
			}
		}
		usleep(100000);		
	}
}

int socketInitiation()
{
	//Create socket
	sock = socket(AF_INET , SOCK_STREAM , 0);
	if (sock == -1)
	{
		/*LOG*/
		memset(CMD, 0, 1028);
		sprintf(CMD, "DEBUG: Could not create socket\n");
		syslog(LOG_SYSLOG, CMD);
		syslog(LOG_USER, CMD);
		/*LOG-END*/
		printf("Could not create socket");
		return 0;
	}
	/*LOG*/
	memset(CMD, 0, 1028);
	sprintf(CMD, "DEBUG: Socket created\n");
	syslog(LOG_SYSLOG, CMD);
	syslog(LOG_USER, CMD);
	/*LOG-END*/
	puts("Socket created");
	
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_family = AF_INET;
	server.sin_port = htons(8888);

	//Connect to remote server
	if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0)
	{
		/*LOG*/
		memset(CMD, 0, 1028);
		sprintf(CMD, "DEBUG: connect failed. Error\n");
		syslog(LOG_SYSLOG, CMD);
		syslog(LOG_USER, CMD);
		/*LOG-END*/
		perror("connect failed. Error");
		return 0;
	}
	/*LOG*/
	memset(CMD, 0, 1028);
	sprintf(CMD, "DEBUG: New serial_io_service version Connected\n");
	syslog(LOG_SYSLOG, CMD);
	syslog(LOG_USER, CMD);
	/*LOG-END*/
	puts("Connected\n");
}

void incomingCallSignalHandler(int signum)
{
	syslog(LOG_USER, "Incoming call signal cought\n");
	if (currentPhoneStat.cs == FREE)
	{
		currentPhoneStat = phoneStat[1];
		ringingMsgShowed = 0;
	}
}

void equalizerSignalHandler(int signum)
{
	syslog(LOG_USER, "Equalizer signal cought\n");
	if (!equalizerSettingsUpdated)
		equalizerSettingsUpdated = 1;
}

int main(int argc, char** argv)
{
	begin = clock();
	pthread_t _readSocketThread, _writeSocketThread, _serialThread, _socketThread;
	int iret1, iret2, iret3;
	
	if (pthread_mutex_init(&inLock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }  
	
	if (pthread_mutex_init(&outLock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }  
	
	signal(SIGUSR1, incomingCallSignalHandler);
	signal(SIGUSR2, equalizerSignalHandler);
	
	socketInitiation();
	
    iret1 = pthread_create( &_socketThread, NULL, socketThread, NULL);
	iret2 = pthread_create( &_serialThread, NULL, serialThread, NULL);
	
	pthread_join(_serialThread, NULL);
	pthread_join(_socketThread, NULL);

	pthread_mutex_destroy(&inLock);
	pthread_mutex_destroy(&outLock);
	
	return 0;	
}
