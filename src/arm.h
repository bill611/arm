/*
 * =============================================================================
 *
 *       Filename:  arm.h
 *
 *    Description:  安防模块(touch)
 *
 *        Version:  1.0
 *        Created:  2016-12-06 21:36:03 
 *       Revision:  1.0
 *
 *         Author:  xubin
 *        Company:  Taichuan
 *
 * =============================================================================
 */
#ifndef _ARM_H
#define _ARM_H

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define  DBG_ARM 1
#define NUMBERS_SAFE			8 // 定义安防路数

	typedef enum _OnOffType {
		OFF = 0,
		ON,
	}OnOffType;

	typedef enum _ArmType{ // 布防方式,用于发送布防撤防结果
		ARMTYPE_ARM,   // 布防
		ARMTYPE_FORCE_ARM, // 强制布防
		ARMTYPE_DISARM, // 撤防
	}ArmType;

	typedef enum _ArmDispStatus{ // 用于显示的状态
		ARMDISP_ARM,
		ARMDISP_DISARM,
		ARMDISP_ACTIVE,
		ARMDISP_ALARM,
	}ArmDispStatus;

	enum {
		ARM_OUT,
		ARM_LEAVE,
		ARM_SELF,
	};

	enum {
		ARM_SOUND_ARM_DELAY,
		ARM_SOUND_ALARM_DELAY,
		ARM_SOUND_ALARM_RING,
	};

	struct _ArmPriv;
	typedef struct _Arm {
		struct _ArmPriv *priv;

		void (*init)(void); // 初始化安防
		void (*destroy)(void); // 结束安防模块

		int (*armming)(int type,int *self_area,char * alarm_error);// 布防
		void (*armForce)(void); // 强制布防
		void (*disarm)(void); // 撤防

		int (*getArmStatus)(void); // 读取布防状态
		int (*getAlarmStatus)(void); // 读取报警状态
		void (*getLeaves)(int *area_leaves); // 读取留守布防防区
		void (*setLeaves)(int *area_leaves); // 设置留守布防防区
		void (*getArmDelayTime)(int *area_time); // 读取布防延时时间
		void (*setArmDelayTime)(int area_num,int area_time); // 设置布防延时时间
		void (*getAlarmDelayTime)(int *area_time); // 读取报警延时时间
		void (*setAlarmDelayTime)(int area_num,int area_time); // 设置报警延时时间
		void (*getDispStatus)(int *status); // 查询当前显示状态

		int (*getArmRingStatus)(void); // 查询播放布防延时声音状态
		int (*getAlarmDelayRingStatus)(void); // 查询播放报警延时声音状态
		int (*getAlarmRingStatus)(void); // 查询播放报警声音状态
		void (*replayArmRing)(void); // 重新播放布防等声音
		void (*thread)(void *arg,int port);

		// 被重载
		void (*sendMessagePopDisarmWindow)(void);// 发送弹出布防窗口消息
		void (*sendMessagePlaySound)(int type,int status);// 发送播放声音消息
		void (*sendArmToCenter)(ArmType type);// 发送布防结果到管理中心
		void (*sendAlarmToCenter)(int id);// 发送报警到管理中心
		void (*saveConfig)(void); // 保存配置
		void (*saveAlarmRecord)(int id); // 保存防区报警记录
		void (*deletOverTimeAlarmRecord)(void); // 删除过期报警记录
		void (*ledArmStatusOnOff)(OnOffType status); // 布防状态指示灯
		void (*alarmBellOnOff)(OnOffType status); // 报警振铃
		void (*lightAlarmOnOff)(OnOffType status); //  外置报警灯
		void (*ledAreaStatusOnOff)(int id,OnOffType status); // 各个防区布防状态指示灯
		void (*ledAreaTrigger)(int id); // 各个防区触发指示(慢闪烁)
		void (*ledAreaAlarm)(int id); // 各个防区报警指示(快闪烁)
		int (*getInputValue)(int port); // 检测各个防区输入状态


		// test
		// void (*testArmGpio)(int port,int value);// 测试用
	}Arm;

	void armCreate(int *arm_status,
			int *arm_delay_time,
			int *alarm_delay_time,
			int *arm_leave_areas);
	extern Arm *arm;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif
