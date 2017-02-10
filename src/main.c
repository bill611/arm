#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include "arm.h"

static int port_value[NUMBERS_SAFE];
static void ledArmStatusOnOff(OnOffType status)
{
	if (status == ON)
		printf("[%s]ON\n", __FUNCTION__);
	else
		printf("[%s]OFF\n", __FUNCTION__);
}

static void alarmBellOnOff(OnOffType status)
{
	if (status == ON)
		printf("[%s]ON\n", __FUNCTION__);
	else
		printf("[%s]OFF\n", __FUNCTION__);
}

static void lightAlarmOnOff(OnOffType status)
{
	if (status == ON)
		printf("[%s]ON\n", __FUNCTION__);
	else
		printf("[%s]OFF\n", __FUNCTION__);
}

static void ledAreaStatusOnOff(int id,OnOffType status)
{
	if (status == ON)
		printf("[%s]led:%d->ON\n", __FUNCTION__,id);
	else
		printf("[%s]led:%d->OFF\n", __FUNCTION__,id);
}

static void ledAreaTrigger(int id)
{
	printf("[%s]led:%d\n", __FUNCTION__,id);
}

static void ledAreaAlarm(int id)
{
	printf("[%s]led:%d\n", __FUNCTION__,id);
}

static void sendMessagePopDisarmWindow(void)
{
	printf("[%s]\n", __FUNCTION__);
}
static void sendMessagePlaySound(int type,int status)
{
	printf("[%s]type:%d,status:%d\n", __FUNCTION__,type,status);
}

static void saveConfig(void)
{
	printf("[%s]\n", __FUNCTION__);
}
static void sendArmToCenter(ArmType type)
{
	printf("[%s]type:%d\n", __FUNCTION__,type);
}
static void sendAlarmToCenter(int id)
{
	printf("[%s]id:%d\n", __FUNCTION__,id);
}
static void saveAlarmRecord(int id)
{
	printf("[%s]id:%d\n", __FUNCTION__,id);
}
static void deletOverTimeAlarmRecord(void)
{
	printf("[%s]\n", __FUNCTION__);
}
static int getInputValue(int port)
{
	return port_value[port];
}

static void *thread(void *arg)
{
	int i;
	while(1) {
		for (i=0; i<NUMBERS_SAFE; i++) {
			arm->thread(NULL,i);	
		}
		usleep(10000);
	}
	pthread_exit(NULL);
	return NULL;
}
int main(int argc, char *argv[])
{
	int arm_status = 0;
	int arm_delay_time[NUMBERS_SAFE] = {0,0,12,13,14,15,16,18};
	int alarm_delay_time[NUMBERS_SAFE] = {0,0,12,13,14,15,0,0};
	int arm_leave_areas[NUMBERS_SAFE] = {1,1,1,1,0,0,1,1};
	armCreate(&arm_status,arm_delay_time,alarm_delay_time,arm_leave_areas);
	arm->ledArmStatusOnOff = ledArmStatusOnOff;
	arm->alarmBellOnOff = alarmBellOnOff;
	arm->lightAlarmOnOff = lightAlarmOnOff;
	arm->ledAreaStatusOnOff = ledAreaStatusOnOff;
	arm->ledAreaTrigger = ledAreaTrigger;
	arm->ledAreaAlarm = ledAreaAlarm;
	arm->sendMessagePopDisarmWindow = sendMessagePopDisarmWindow;
	arm->sendMessagePlaySound = sendMessagePlaySound;
	arm->sendArmToCenter = sendArmToCenter;
	arm->sendAlarmToCenter = sendAlarmToCenter;
	arm->saveConfig = saveConfig;
	arm->saveAlarmRecord = saveAlarmRecord;
	arm->deletOverTimeAlarmRecord = deletOverTimeAlarmRecord;
	arm->getInputValue = getInputValue;
	arm->init();

	int result;
	pthread_t m_pthread;                    //线程号
	pthread_attr_t threadAttr1;             //线程属性
	pthread_attr_init(&threadAttr1);        //附加参数
	//设置线程为自动销毁
	pthread_attr_setdetachstate(&threadAttr1,PTHREAD_CREATE_DETACHED);
	result = pthread_create(&m_pthread,&threadAttr1,thread,NULL);
	if(result) {
	   printf("[%s] pthread failt,Error code:%d\n",__FUNCTION__,result);
	}
	pthread_attr_destroy(&threadAttr1);     //释放附加参数
	char cmd[10];
	while (1) {
		puts("Input cmd: io disarm arm f_arm");
		scanf("%s",cmd);
		if (strcmp(cmd,"io") == 0 ) {
			puts("Input port,value");
			int port,value;
			scanf("%d,%d",&port,&value);
			port_value[port] = value;	
		} else if (strcmp(cmd,"disarm") == 0 ) {
			arm->disarm();
		} else if (strcmp(cmd,"arm") == 0 ) {
			char arming_error[10];
			arm->armming(ARM_OUT,NULL,arming_error);
		} else if (strcmp(cmd,"f_arm") == 0 ) {
			arm->armForce();
		}
		sleep(3);
	}
	arm->destroy();
	return 0;
}
