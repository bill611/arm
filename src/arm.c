/*
 * =============================================================================
 *
 *       Filename:  arm.c
 *
 *    Description:  安防模块(touch)
 *
 *        Version:  1.0
 *        Created:  2016-12-06 21:40:09
 *       Revision:  1.0
 *
 *         Author:  xubin
 *        Company:  Taichuan
 *
 * =============================================================================
 */
/* ---------------------------------------------------------------------------*
 *                      include head files
 *----------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "stateMachine.h"
#include "timer.h"
#include "arm.h"

/* ---------------------------------------------------------------------------*
 *                  extern variables declare
 *----------------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------*
 *                  internal functions declare
 *----------------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------*
 *                        macro define
 *----------------------------------------------------------------------------*/
#if DBG_ARM > 0
	#define DBG_P( ... ) printf( __VA_ARGS__ )
#else
	#define DBG_P( x... )
#endif

#define ALARM_CHECK_TIME 20 //触发持续时间
#define ALARM_SOUND_TIME	360//报警延时6分钟关闭声音

#define NELEMENTS(array)        /* number of elements in an array */ \
	        (sizeof (array) / sizeof ((array) [0]))

typedef struct _ArmTypeDef{
	StMachine *st_machine;	//防区状态机

	int io_check_cnt;			//异常检测计数用
	int real_time_status; 		//8路安防的实时状态
	int error;					//防区异常状态

	int *config_leave;			//配置留守布防设置
	int *config_arm_time;		//配置布防延时时间
	int *config_alarm_time;		//配置报警延时时间

	int status;					//防区布防状态
	int arm_delay_time;			//防区布防延时
	int alarm;					//防区报警状态
	int alarm_delay_time;		//防区报警延时
	int disp_status;			//防区当前显示状态
}ArmTypeDef;

typedef struct _ArmPriv {
	ArmTypeDef area[NUMBERS_SAFE];
	Timer *timer;
	int *arm_status;		//布防状态
	int alarm_status;		// 设备报警状态
	int func_enable;		//判断是否有安防功能
	int arm_ring_delay;		// 布防延时放音时间,始终为时间最长的布防延时时间
	int alarm_ring_delay ;		//报警延时放音时间,为第一次触发时延时,为0时重新赋值
	int alarm_ring_close_time ;	//持续报警6分钟后关闭声音
	int play_arm_ring_delay;		//布防延时音是否已播放 1播放，0未播放
	int play_alarm_ring_delay ;		//报警延时音是否已播放 1播放，0未播放
	int play_alarm_ring ;			//报警音是否已播放 1播放，0未播放
}ArmPriv;


enum {
	EVENT_ARM,				//布防
	EVENT_DISARM,			//撤防
	EVENT_ACTIVE,			//防区触发
	EVENT_INACTIVE,			//防区恢复
	EVENT_ARM_DELAY_ARRIVE,	//布防延时到
	EVENT_ALARM_DELAY_TIME_ARRIVE,		//报警延时到

};

enum {
	ST_ARM,		//布防状态
	ST_DISARM,		//撤防状态
	ST_DELAY_ARM,	//布防延时状态
	ST_DELAY_ALARM,		//报警延时状态

	ST_TRIG_DISARM_STATE,		//撤防时防区触发状态
	ST_TRIG_DELAY_ARM_STATE,	//布防延时时防区触发状态
	ST_TRIG_DELAY_ALARM_STATE,		//报警延时时防区触发状态

	ST_ALARM				//报警状态
};

enum {
	DO_NO = 0,
	DO_ARM = 0x0001,			//设置布防
	DO_DISARM = 0x0002,			//设置撤防
	DO_ARM_DELAY = 0x0004,		//设置布防延时
	DO_ALARM_DELAY_TIME = 0x0008,	//设置报警延时
	DO_UPDATE_SATE = 0x0010,	//防区触发更新状态
	DO_ARMING	= 0x0020		//报警
};

/* ---------------------------------------------------------------------------*
 *                      variables define
 *----------------------------------------------------------------------------*/
Arm *arm = NULL;
//--------------------布防状态机
//紧急报警防区1-2
static StateTable stm_emergency[] = {
//布防
{	EVENT_ARM,		ST_DISARM,	ST_ARM,	DO_ARM | DO_UPDATE_SATE},

//撤防
{	EVENT_DISARM,	ST_ALARM,	ST_ARM,	DO_DISARM | DO_ARM | DO_UPDATE_SATE},

//触发
{	EVENT_ACTIVE,	ST_ARM,		ST_ALARM,				DO_ARMING | DO_UPDATE_SATE},
{	EVENT_ACTIVE,	ST_DISARM,	ST_TRIG_DISARM_STATE,	DO_UPDATE_SATE},

//恢复
{	EVENT_INACTIVE,	ST_TRIG_DISARM_STATE,ST_DISARM,		DO_UPDATE_SATE},
{	EVENT_INACTIVE,	ST_DISARM,	ST_ARM,					DO_ARM | DO_UPDATE_SATE},
{	EVENT_INACTIVE,	ST_ARM,		ST_ARM,					DO_ARM | DO_UPDATE_SATE},

};
//通用报警防区3-6
static StateTable stm_general[] = {
//布防
{	EVENT_ARM,		ST_DISARM,	ST_DELAY_ARM,	DO_ARM_DELAY},

//撤防
{	EVENT_DISARM,	ST_ARM,						ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_DELAY_ARM,				ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_DELAY_ALARM,				ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_TRIG_DELAY_ARM_STATE,	ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_TRIG_DELAY_ALARM_STATE,	ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_ALARM,					ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},

//触发
{	EVENT_ACTIVE,	ST_ARM,				ST_DELAY_ALARM,				DO_ALARM_DELAY_TIME | DO_UPDATE_SATE},
{	EVENT_ACTIVE,	ST_DISARM,			ST_TRIG_DISARM_STATE,		DO_UPDATE_SATE},
{	EVENT_ACTIVE,	ST_DELAY_ARM,		ST_TRIG_DELAY_ARM_STATE,	DO_UPDATE_SATE},
{	EVENT_ACTIVE,	ST_DELAY_ALARM,		ST_TRIG_DELAY_ALARM_STATE,	DO_UPDATE_SATE},

//恢复
{	EVENT_INACTIVE,	ST_TRIG_DISARM_STATE,		ST_DISARM,		DO_UPDATE_SATE},
{	EVENT_INACTIVE,	ST_TRIG_DELAY_ARM_STATE,	ST_DELAY_ARM,	DO_UPDATE_SATE},
{	EVENT_INACTIVE,	ST_TRIG_DELAY_ALARM_STATE,	ST_DELAY_ALARM,	DO_NO},

//布防延时到
{	EVENT_ARM_DELAY_ARRIVE,	ST_DELAY_ARM,			ST_ARM,						DO_ARM | DO_UPDATE_SATE},
{	EVENT_ARM_DELAY_ARRIVE,	ST_TRIG_DELAY_ARM_STATE,ST_TRIG_DELAY_ALARM_STATE,	DO_ARM | DO_ALARM_DELAY_TIME},

//报警延时到
{	EVENT_ALARM_DELAY_TIME_ARRIVE,	ST_DELAY_ALARM,				ST_ALARM,	DO_ARMING | DO_UPDATE_SATE},
{	EVENT_ALARM_DELAY_TIME_ARRIVE,	ST_TRIG_DELAY_ALARM_STATE,	ST_ALARM,	DO_ARMING | DO_UPDATE_SATE},


};
//立即报警防区7-8
static StateTable stm_immediately[] = {
//布防
{	EVENT_ARM,	ST_DISARM,	ST_DELAY_ARM,	DO_ARM_DELAY},

//撤防
{	EVENT_DISARM,	ST_ARM,						ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_DELAY_ARM,				ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_DELAY_ALARM,				ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_TRIG_DELAY_ARM_STATE,	ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_TRIG_DELAY_ALARM_STATE,	ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},
{	EVENT_DISARM,	ST_ALARM,					ST_DISARM,	DO_DISARM | DO_UPDATE_SATE},

//触发
{	EVENT_ACTIVE,	ST_ARM,			ST_ALARM,					DO_ARMING | DO_UPDATE_SATE},
{	EVENT_ACTIVE,	ST_DISARM,		ST_TRIG_DISARM_STATE,		DO_UPDATE_SATE},
{	EVENT_ACTIVE,	ST_DELAY_ARM,	ST_TRIG_DELAY_ARM_STATE,	DO_UPDATE_SATE},

//恢复
{	EVENT_INACTIVE,	ST_TRIG_DISARM_STATE,		ST_DISARM,		DO_UPDATE_SATE},
{	EVENT_INACTIVE,	ST_TRIG_DELAY_ARM_STATE,	ST_DELAY_ARM,	DO_UPDATE_SATE},

//布防延时到
{	EVENT_ARM_DELAY_ARRIVE,	ST_DELAY_ARM,				ST_ARM,		DO_ARM | DO_UPDATE_SATE},
{	EVENT_ARM_DELAY_ARRIVE,	ST_TRIG_DELAY_ARM_STATE,	ST_ALARM,	DO_ARM | DO_ARMING | DO_UPDATE_SATE,}

};
//---------------------------状态机end


/* ---------------------------------------------------------------------------*/
/**
 * @brief armArmDelayRing 播放布防延时铃声
 *
 * @param status
 */
/* ---------------------------------------------------------------------------*/
static void armArmDelayRing(OnOffType status)
{
	if (status == ON) {
		if (arm->priv->play_arm_ring_delay == 0)
			arm->sendMessagePlaySound(ARM_SOUND_ARM_DELAY,ON);
		arm->priv->play_arm_ring_delay = 1;
	} else {
		if (arm->priv->play_arm_ring_delay == 1)
			arm->sendMessagePlaySound(ARM_SOUND_ARM_DELAY,OFF);
		arm->priv->play_arm_ring_delay = 0;
	}
}
/* ---------------------------------------------------------------------------*/
/**
 * @brief armPlayAlarmDelayRing 播放报警延时铃声
 *
 * @param status
 */
/* ---------------------------------------------------------------------------*/
static void armPlayAlarmDelayRing(OnOffType status)
{
	if (status == ON) {
		if (arm->priv->play_alarm_ring_delay == 0) {
			arm->sendMessagePlaySound(ARM_SOUND_ALARM_DELAY,ON);
			arm->sendMessagePopDisarmWindow();
		}
		arm->priv->play_alarm_ring_delay = 1;
	} else {
		if (arm->priv->play_alarm_ring_delay == 1)
			arm->sendMessagePlaySound(ARM_SOUND_ARM_DELAY,OFF);
		arm->priv->play_alarm_ring_delay = 0;
	}
}
/* ---------------------------------------------------------------------------*/
/**
 * @brief armPlayAlarmRing 播放报警铃声
 *
 * @param status
 */
/* ---------------------------------------------------------------------------*/
static void armPlayAlarmRing(OnOffType status)
{
	if (status == ON) {
		if (arm->priv->play_alarm_ring == 0) {
			arm->sendMessagePlaySound(ARM_SOUND_ALARM_RING,ON);
			arm->sendMessagePopDisarmWindow();
		}
		arm->priv->play_alarm_ring = 1;
	} else {
		if (arm->priv->play_alarm_ring == 1)
			arm->sendMessagePlaySound(ARM_SOUND_ALARM_DELAY,OFF);
		arm->priv->play_alarm_ring = 0;
	}
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief replayArmRing 重新播放安防相关的声音
 */
/* ---------------------------------------------------------------------------*/
static void replayArmRing(void)
{
	arm->priv->play_arm_ring_delay = 0;
	arm->priv->play_alarm_ring_delay = 0;
	arm->priv->play_alarm_ring = 0;
}
/* ---------------------------------------------------------------------------*/
/**
 * @brief armUpdateBallStatus 更新主界面与撤防界面小球状态
 *
 * @param id
 * @param state
 */
/* ---------------------------------------------------------------------------*/
static void armUpdateBallStatus(int id,int state)
{
	char *color[] = {"green","gray","yellow","red"};
	arm->priv->area[id].disp_status = state;
	printf("[%s]id:%d,status:%s\n",__FUNCTION__,id,color[state]);
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief armSetArmStatus 设置设备的状态，同时写入配置文件
 *
 * @param status 0 撤防 1 布防
 */
/* ---------------------------------------------------------------------------*/
static void armSetArmStatus(int status)
{
	*(arm->priv->arm_status) = status;
	arm->saveConfig();
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief armming 布防
 *
 * @param type ARM_OUT外出布防 ARM_LEAVE留守布防 ARM_SELF自选布防
 * @param self_area 自选布防时的布防区域
 * @param alarm_error 布防失败的区域
 *
 * @returns 0 布防失败 1 布防成功
 */
/* ---------------------------------------------------------------------------*/
static 	int armming(int type,int *self_area,char * alarm_error)
{
	int i;
	int error[NUMBERS_SAFE] = {0};
	char buf[5] = {0};
	int error_num = 0;
	int arm_area[NUMBERS_SAFE] = {0};

	if (type == ARM_OUT) {
		for (i=0; i<NUMBERS_SAFE; i++) {
			arm_area[i] = 1;
		}
	} else if (type == ARM_LEAVE) {
		for (i=0; i<NUMBERS_SAFE; i++) {
			arm_area[i] = *(arm->priv->area[i].config_leave);
		}
	} else if (type == ARM_SELF) {
		memcpy(arm_area,self_area,sizeof(arm_area));
	}

	for(i=0; i<NUMBERS_SAFE; i++)	{	//检测防区有无异常
		if(arm_area[i] == 0)
			continue;
		if(arm->priv->area[i].real_time_status)
			error[error_num++] = i;
	}
	if (error_num) {
		sprintf(alarm_error,"%d",error[0] + 1);
		for (i=1; i<error_num; i++) {
			sprintf(buf,",%d",error[i] + 1);
			strcat(alarm_error,buf);
		}
		return 0;
	}

	for(i=0; i<NUMBERS_SAFE; i++) {
		if(arm_area[i] == 0)
			continue;
		arm->priv->area[i].st_machine->msgPost(arm->priv->area[i].st_machine,
				EVENT_ARM,NULL);
	}
	armSetArmStatus(1);
	arm->ledArmStatusOnOff(ON);
	arm->sendArmToCenter(ARMTYPE_ARM);
	return 1;
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief armForce 强制布防
 */
/* ---------------------------------------------------------------------------*/
static 	void armForce(void)
{
	int i;
	for(i=0; i<NUMBERS_SAFE; i++) {
		if(arm->priv->area[i].real_time_status) {
			arm->priv->area[i].status = 0;	//有异常的防区不可布防
		} else {
			arm->priv->area[i].st_machine->msgPost(arm->priv->area[i].st_machine,
					EVENT_ARM,NULL);
			arm->priv->area[i].status = 1;
		}
	}
	armSetArmStatus(1);

	arm->ledArmStatusOnOff(ON);
	arm->sendArmToCenter(ARMTYPE_FORCE_ARM);
}
/* ---------------------------------------------------------------------------*/
/**
 * @brief disarm 撤防
 */
/* ---------------------------------------------------------------------------*/
static 	void disarm(void)
{
	int i;
	arm->priv->alarm_status = 0;
	arm->priv->arm_ring_delay = 0;
	arm->priv->alarm_ring_delay = 0;
	arm->priv->alarm_ring_close_time = 0;

	armSetArmStatus(0);
	arm->alarmBellOnOff(OFF);
	arm->ledArmStatusOnOff(OFF);
	arm->alarmBellOnOff(OFF);
	armArmDelayRing(OFF);
	armPlayAlarmDelayRing(OFF);
	armPlayAlarmRing(OFF);

	for(i=0; i<NUMBERS_SAFE; i++) {
		arm->priv->area[i].st_machine->msgPost(arm->priv->area[i].st_machine,
				EVENT_DISARM,NULL);
	}
	arm->sendArmToCenter(ARMTYPE_DISARM);
}

static int getArmStatus(void)
{
	return *(arm->priv->arm_status);
}
static int getALarmStatus(void)
{
	return arm->priv->alarm_status;
}
static void getDispStatus(int *status)
{
	int i;
	for (i=0; i<NUMBERS_SAFE; i++) {
		status[i] = arm->priv->area[i].disp_status;
	}
}
static void setLeaves(int *area_leaves)
{
	int i;
	for (i=0; i<NUMBERS_SAFE; i++) {
		*(arm->priv->area[i].config_leave) = area_leaves[i];
	}
	arm->saveConfig();
}

static void getLeaves(int *area_leaves)
{
	int i;
	for (i=0; i<NUMBERS_SAFE; i++) {
		area_leaves[i] = *(arm->priv->area[i].config_leave);
	}
}

static void getArmDelayTime(int *area_time)
{
	int i;
	for (i=0; i<NUMBERS_SAFE; i++) {
		area_time[i] = *(arm->priv->area[i].config_arm_time);
	}

}
static	int getArmRingStatus(void)
{
	return arm->priv->arm_ring_delay;
}
static	int getAlarmDelayRingStatus(void)
{
	return arm->priv->alarm_ring_delay;
}
static	int getAlarmRingStatus(void)
{
	return arm->priv->alarm_ring_close_time;
}

static void setArmDelayTime(int area_num,int area_time)
{
	*(arm->priv->area[area_num].config_arm_time) = area_time;
	arm->saveConfig();
}
static void getAlarmDelayTime(int *area_time)
{
	int i;
	for (i=0; i<NUMBERS_SAFE; i++) {
		area_time[i] = *(arm->priv->area[i].config_alarm_time);
	}
	arm->saveConfig();
}
static void setAlarmDelayTime(int area_num,int area_time)
{
	*(arm->priv->area[area_num].config_alarm_time) = area_time;
	arm->saveConfig();
}

static void armTimer1s(int id,int arg)
{
	int i;
	for (i=0; i<NUMBERS_SAFE; i++) {
		if (arm->priv->area[i].arm_delay_time > 0) {
			// printf("[%d]arm_delay_time:%d\n", i,arm->priv->area[i].arm_delay_time);
			if (--arm->priv->area[i].arm_delay_time == 0) {
				arm->priv->area[i].st_machine->msgPost(arm->priv->area[i].st_machine,
						EVENT_ARM_DELAY_ARRIVE,NULL);
			}
		}
		if (arm->priv->area[i].alarm_delay_time > 0) {
			// printf("[%d]alarm_delay_time:%d\n", i,arm->priv->area[i].alarm_delay_time);
			if (--arm->priv->area[i].alarm_delay_time == 0) {
				arm->priv->area[i].st_machine->msgPost(arm->priv->area[i].st_machine,
						EVENT_ALARM_DELAY_TIME_ARRIVE,NULL);
			}
		}
	}
	if (arm->priv->alarm_ring_close_time > 0) {
		arm->priv->alarm_ring_delay = 0;
		arm->priv->arm_ring_delay = 0;
		armPlayAlarmRing(ON);
		DBG_P("alarm_ring_close_time:%d\n", arm->priv->alarm_ring_close_time);
		if (--arm->priv->alarm_ring_close_time == 0)
			armPlayAlarmRing(OFF);
	}
	if (arm->priv->alarm_ring_delay > 0) {
		arm->priv->arm_ring_delay = 0;
		DBG_P("alarm_delay_time:%d\n", arm->priv->alarm_ring_delay);
		armPlayAlarmDelayRing(ON);
		if (--arm->priv->alarm_ring_delay == 0)
			armPlayAlarmDelayRing(OFF);
	}
	if (arm->priv->arm_ring_delay > 0) {
		DBG_P("arm_delay_time:%d\n", arm->priv->arm_ring_delay);
		armArmDelayRing(ON);
		if (--arm->priv->arm_ring_delay == 0)
			armArmDelayRing(OFF);
	}
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief armStateMachineHandle 安防状态机动作执行
 *
 * @param This
 */
/* ---------------------------------------------------------------------------*/
static void armStateMachineHandle(StMachine *This,void *data)
{
	DBG_P("[%d]CurRun:0x%x-->",This->id,This->getCurRun(This));
	//撤防---------------------------
	if (This->getCurRun(This) & DO_DISARM)	{
		DBG_P("DO_DISARM\n");
		arm->priv->area[This->id].arm_delay_time = 0;
		arm->priv->area[This->id].alarm_delay_time = 0;
		arm->priv->area[This->id].alarm = 0;
		arm->priv->area[This->id].status = 0;
	}
	//布防---------------------------
	if (This->getCurRun(This) & DO_ARM)	{
		DBG_P("DO_ARM\n");
		arm->priv->area[This->id].status = 1;
	}
	//布防延时---------------------------
	if (This->getCurRun(This) & DO_ARM_DELAY) {
		DBG_P("DO_ARM_DELAY\n");
		arm->priv->area[This->id].arm_delay_time
			= *(arm->priv->area[This->id].config_arm_time);
		if (arm->priv->arm_ring_delay < *(arm->priv->area[This->id].config_arm_time))
			arm->priv->arm_ring_delay = *(arm->priv->area[This->id].config_arm_time);
	}
	//报警延时---------------------------
	if (This->getCurRun(This) & DO_ALARM_DELAY_TIME)	{
		DBG_P("DO_ALARM_DELAY_TIME\n");
		arm->priv->area[This->id].alarm_delay_time
			= *(arm->priv->area[This->id].config_alarm_time);
		if (arm->priv->alarm_ring_delay == 0)
			arm->priv->alarm_ring_delay = *(arm->priv->area[This->id].config_alarm_time);
		if (arm->priv->alarm_ring_delay > *(arm->priv->area[This->id].config_alarm_time))
			arm->priv->alarm_ring_delay = *(arm->priv->area[This->id].config_alarm_time);
	}
	//报警---------------------------
	if (This->getCurRun(This) & DO_ARMING)	{
		DBG_P("DO_ARMING\n");
		arm->priv->alarm_status = 1;
		arm->priv->area[This->id].alarm = 1;//报警
		//发送报警记录到管理中心
		arm->sendAlarmToCenter(This->id);
		//记录报警记录
		arm->saveAlarmRecord(This->id);
		//查询报警信息是否已经过时，过时需要删除
		arm->deletOverTimeAlarmRecord();
		arm->alarmBellOnOff(ON);
		arm->alarmBellOnOff(ON);

		arm->priv->alarm_ring_close_time = ALARM_SOUND_TIME;
	}
	//防区触发---------------------------
	if (This->getCurRun(This) & DO_UPDATE_SATE) {
		DBG_P("DO_UPDATE_SATE\n");
		int state;
		if (arm->priv->area[This->id].alarm) {
			state = ARMDISP_ALARM;
			arm->ledAreaAlarm(This->id);
		} else if (arm->priv->area[This->id].error) {
			state = ARMDISP_ACTIVE;
			arm->ledAreaTrigger(This->id);
		} else if (arm->priv->area[This->id].status) {
			state = ARMDISP_ARM;
			arm->ledAreaStatusOnOff(This->id,ON);
		} else {
			state = ARMDISP_DISARM;
			arm->ledAreaStatusOnOff(This->id,OFF);
		}
		armUpdateBallStatus(This->id,state);//更新主界面与撤防界面小球状态
	}
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief armInit 安防线程创建初始化
 */
/* ---------------------------------------------------------------------------*/
static void armInit(void)
{
	int i;
	int state;

	if ( getArmStatus() == 1) {
		arm->ledArmStatusOnOff(ON);
		for (i=0; i<NUMBERS_SAFE; i++) {
			if(arm->priv->area[i].error) {
				arm->priv->area[i].status = 0;	//有异常的防区不可布防
				state = ST_DISARM;
				arm->ledAreaStatusOnOff(i,OFF);
				armUpdateBallStatus(i,ARMDISP_ACTIVE);
			} else {
				arm->priv->area[i].status = 1;
				state = ST_ARM;
				arm->ledAreaStatusOnOff(i,ON);
				armUpdateBallStatus(i,ARMDISP_ARM);
			}
			if (i < 2) {
				arm->priv->area[i].st_machine = stateMachineCreate(state,
						stm_emergency, NELEMENTS(stm_emergency),
						i, armStateMachineHandle);
			} else if (i < 6) {
				arm->priv->area[i].st_machine = stateMachineCreate(state,
						stm_general,NELEMENTS(stm_general),
						i,armStateMachineHandle);
			} else {
				arm->priv->area[i].st_machine = stateMachineCreate(state,
						stm_immediately,NELEMENTS(stm_immediately),
						i,armStateMachineHandle);
			}

		}
		return;
	}
	arm->ledArmStatusOnOff(OFF);
	for (i=0; i<NUMBERS_SAFE; i++) {
		if (i < 2) {
			state = ST_ARM;
			arm->priv->area[i].st_machine = stateMachineCreate(ST_ARM,
					stm_emergency,NELEMENTS(stm_emergency),
					i,armStateMachineHandle);
			arm->ledAreaStatusOnOff(i,ON);
		} else if (i < 6) {
			state = ST_DISARM;
			arm->priv->area[i].st_machine = stateMachineCreate(ST_DISARM,
					stm_general,NELEMENTS(stm_general),
					i,armStateMachineHandle);
			arm->ledAreaStatusOnOff(i,OFF);
		} else {
			state = ST_DISARM;
			arm->priv->area[i].st_machine = stateMachineCreate(ST_DISARM,
					stm_immediately,NELEMENTS(stm_immediately),
					i,armStateMachineHandle);
			arm->ledAreaStatusOnOff(i,OFF);
		}
		armUpdateBallStatus(i,state);
	}
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief armThread 安防线程执行
 *
 * @param arg
 * @param port 安防IO编号
 */
/* ---------------------------------------------------------------------------*/
static void armThread(void *arg,int port)
{
	if (arm->getInputValue(port)){
		if (   (arm->priv->area[port].io_check_cnt < ALARM_CHECK_TIME)
				&& (arm->priv->func_enable == 1) )	//有检测到过有安防设备时开始计数
			arm->priv->area[port].io_check_cnt++;	//相应有线安防接口为低时计数器加1，
		//一直出现50次（500ms）时为确认警报
	} else {
		arm->priv->func_enable = 1;
		arm->priv->area[port].io_check_cnt = 0;	//相应有线安防接口为高时清0计数器
		arm->priv->area[port].real_time_status = 0;
	}
	if(arm->priv->area[port].io_check_cnt >= ALARM_CHECK_TIME) {
		arm->priv->area[port].real_time_status = 1;			//本防区异常
	}

	if (arm->priv->area[port].real_time_status == 1) {
		if (arm->priv->area[port].error == 0) {
			arm->priv->area[port].st_machine->msgPost(arm->priv->area[port].st_machine,
					EVENT_ACTIVE, NULL);
		}
		arm->priv->area[port].error = 1;
	} else {
		if (arm->priv->area[port].error == 1) {
			arm->priv->area[port].st_machine->msgPost(arm->priv->area[port].st_machine,
					EVENT_INACTIVE, NULL);
		}
		arm->priv->area[port].error = 0;
	}
	arm->priv->area[port].st_machine->run(arm->priv->area[port].st_machine);
}

static void armDestroy(void)
{
	if (arm->priv)
		free(arm->priv);
	if (arm)
		free(arm);	
}
/* ---------------------------------------------------------------------------*/
/**
 * @brief armLedArmStatusOnOffDefault 布防状态指示灯
 *
 * @param status
 */
/* ---------------------------------------------------------------------------*/
static void armLedArmStatusOnOffDefault(OnOffType status)
{
	if (status == ON)
		printf("[%s]ON\n", __FUNCTION__);
	else
		printf("[%s]OFF\n", __FUNCTION__);
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief armAlarmBellOnOffDefault 报警振铃
 *
 * @param status
 */
/* ---------------------------------------------------------------------------*/
static void armAlarmBellOnOffDefault(OnOffType status)
{
	if (status == ON)
		printf("[%s]ON\n", __FUNCTION__);
	else
		printf("[%s]OFF\n", __FUNCTION__);
}
/* ---------------------------------------------------------------------------*/
/**
 * @brief armLightAlarmOnOffDefault 外置报警灯
 *
 * @param status
 */
/* ---------------------------------------------------------------------------*/
static void armLightAlarmOnOffDefault(OnOffType status)
{
	if (status == ON)
		printf("[%s]ON\n", __FUNCTION__);
	else
		printf("[%s]OFF\n", __FUNCTION__);
}
/* ---------------------------------------------------------------------------*/
/**
 * @brief armLedAreaStatusOnOffDefault 各个防区布防状态指示灯
 *
 * @param id 防区编号
 * @param status
 */
/* ---------------------------------------------------------------------------*/
static void armLedAreaStatusOnOffDefault(int id,OnOffType status)
{
	if (status == ON)
		printf("[%s]led:%d->ON\n", __FUNCTION__,id);
	else
		printf("[%s]led:%d->OFF\n", __FUNCTION__,id);
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief armLedAreaTriggerDefault 各个防区触发指示(慢闪烁)
 *
 * @param id
 */
/* ---------------------------------------------------------------------------*/
static void armLedAreaTriggerDefault(int id)
{
	printf("[%s]led:%d\n", __FUNCTION__,id);
}

/* ---------------------------------------------------------------------------*/
/**
 * @brief armLedAreaAlarmDefault 各个防区报警指示(快闪烁)
 *
 * @param id
 */
/* ---------------------------------------------------------------------------*/
static void armLedAreaAlarmDefault(int id)
{
	printf("[%s]led:%d\n", __FUNCTION__,id);
}


static void armSendMessagePopDisarmWindowDefault(void)
{
	printf("[%s]\n", __FUNCTION__);
}
static void armSendMessagePlaySoundDefault(int type,int status)
{
	printf("[%s]type:%d,status:%d\n", __FUNCTION__,type,status);
}

static void armSaveConfigDefault(void)
{
	printf("[%s]\n", __FUNCTION__);
}
static void armSendArmToCenterDefault(ArmType type)
{
	printf("[%s]type:%d\n", __FUNCTION__,type);
}
static void armSendAlarmToCenterDefault(int id)
{
	printf("[%s]id:%d\n", __FUNCTION__,id);
}
static void armSaveAlarmRecordDefault(int id)
{
	printf("[%s]id:%d\n", __FUNCTION__,id);
}
static void armDeletOverTimeAlarmRecordDefault(void)
{
	printf("[%s]\n", __FUNCTION__);
}
static int armGetInputValueDefault(int port)
{
	return 0;
}
void armCreate(int *arm_status,
		int *arm_delay_time,
		int *alarm_delay_time,
		int *arm_leave_areas)
{
	int i;

	arm = (Arm *) calloc (1,sizeof(Arm));
	arm->priv = (ArmPriv *) calloc (1,sizeof(ArmPriv));
	arm->priv->arm_status = arm_status;
	for (i=0; i<NUMBERS_SAFE; i++) {
		arm->priv->area[i].config_arm_time = &arm_delay_time[i];
		arm->priv->area[i].config_alarm_time = &alarm_delay_time[i];
		arm->priv->area[i].config_leave = &arm_leave_areas[i];
	}
	arm->priv->timer = timerCreate(0,NULL);
	arm->priv->timer->realTimerCreate(arm->priv->timer,1,armTimer1s);
	arm->init = armInit;
	arm->armming = armming;
	arm->armForce = armForce;
	arm->disarm = disarm;
	arm->getArmStatus = getArmStatus;
	arm->getAlarmStatus = getALarmStatus;
	arm->getLeaves = getLeaves;
	arm->setLeaves = setLeaves;
	arm->getArmDelayTime = getArmDelayTime;
	arm->setArmDelayTime = setArmDelayTime;
	arm->getAlarmDelayTime = getAlarmDelayTime;
	arm->setAlarmDelayTime = setAlarmDelayTime;
	arm->getDispStatus = getDispStatus;

	arm->getArmRingStatus = getArmRingStatus;
	arm->getAlarmDelayRingStatus = getAlarmDelayRingStatus;
	arm->getAlarmRingStatus = getAlarmRingStatus;
	arm->replayArmRing = replayArmRing;
	arm->thread = armThread;

	arm->ledArmStatusOnOff = armLedArmStatusOnOffDefault;
	arm->alarmBellOnOff = armAlarmBellOnOffDefault;
	arm->lightAlarmOnOff = armLightAlarmOnOffDefault;
	arm->ledAreaStatusOnOff = armLedAreaStatusOnOffDefault;
	arm->ledAreaTrigger = armLedAreaTriggerDefault;
	arm->ledAreaAlarm = armLedAreaAlarmDefault;
	arm->sendMessagePopDisarmWindow = armSendMessagePopDisarmWindowDefault;
	arm->sendMessagePlaySound = armSendMessagePlaySoundDefault;
	arm->sendArmToCenter = armSendArmToCenterDefault;
	arm->sendAlarmToCenter = armSendAlarmToCenterDefault;
	arm->saveConfig = armSaveConfigDefault;
	arm->saveAlarmRecord = armSaveAlarmRecordDefault;
	arm->deletOverTimeAlarmRecord = armDeletOverTimeAlarmRecordDefault;
	arm->getInputValue = armGetInputValueDefault;

	// for (i=0; i<NUMBERS_SAFE; i++) {
// #ifdef WIN32
		// arm->priv->gpio->SetValue(arm->priv->gpio,i,IO_ACTIVE);
// #endif
		// arm->priv->gpio->addInputThread(arm->priv->gpio,arm->priv->gpio,i,armThread);
	// }
}

