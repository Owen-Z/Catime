/**
 * @file timer.c
 * @brief 计时器核心功能实现
 * 
 * 本文件包含计时器的核心逻辑实现，包括时间格式转换、输入解析、配置保存等功能，
 * 同时维护计时器的各种状态和配置参数。
 */

#include "../include/timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <shellapi.h>

/** @name 计时器状态标志
 *  @{ */
BOOL CLOCK_IS_PAUSED = FALSE;          ///< 计时器暂停状态标志
BOOL CLOCK_SHOW_CURRENT_TIME = FALSE;  ///< 显示当前时间模式标志
BOOL CLOCK_USE_24HOUR = TRUE;          ///< 使用24小时制标志
BOOL CLOCK_SHOW_SECONDS = TRUE;        ///< 显示秒数标志
BOOL CLOCK_COUNT_UP = FALSE;           ///< 倒计时/正计时模式标志
char CLOCK_STARTUP_MODE[20] = "COUNTDOWN"; ///< 启动模式（倒计时/正计时）
/** @} */

/** @name 计时器时间参数 
 *  @{ */
int CLOCK_TOTAL_TIME = 0;              ///< 总计时时间（秒）
int countdown_elapsed_time = 0;        ///< 倒计时已用时间（秒）
int countup_elapsed_time = 0;          ///< 正计时累计时间（秒）
time_t CLOCK_LAST_TIME_UPDATE = 0;     ///< 最后更新时间戳
/** @} */

/** @name 消息状态标志
 *  @{ */
BOOL countdown_message_shown = FALSE;  ///< 倒计时完成消息显示状态
BOOL countup_message_shown = FALSE;    ///< 正计时完成消息显示状态
int pomodoro_work_cycles = 0;          ///< 番茄钟工作周期计数
/** @} */

/** @name 超时动作配置
 *  @{ */
TimeoutActionType CLOCK_TIMEOUT_ACTION = TIMEOUT_ACTION_MESSAGE; ///< 超时动作类型
char CLOCK_TIMEOUT_TEXT[50] = "";      ///< 超时显示文本
char CLOCK_TIMEOUT_FILE_PATH[MAX_PATH] = ""; ///< 超时执行文件路径
/** @} */

/** @name 番茄钟时间设置
 *  @{ */
int POMODORO_WORK_TIME = 25 * 60;    ///< 番茄钟工作时间（25分钟）
int POMODORO_SHORT_BREAK = 5 * 60;   ///< 番茄钟短休息时间（5分钟）
int POMODORO_LONG_BREAK = 15 * 60;   ///< 番茄钟长休息时间（15分钟）
int POMODORO_LOOP_COUNT = 1;         ///< 番茄钟循环次数（默认1次）
/** @} */

/** @name 预设时间选项
 *  @{ */
int time_options[MAX_TIME_OPTIONS];    ///< 预设时间选项数组
int time_options_count = 0;            ///< 有效预设时间数量
/** @} */

/**
 * @brief 格式化显示时间
 * @param remaining_time 剩余时间（秒）
 * @param[out] time_text 格式化后的时间字符串输出缓冲区
 * 
 * 根据当前配置（12/24小时制、是否显示秒数、倒计时/正计时模式）将时间格式化为字符串。
 * 支持三种显示模式：当前系统时间、倒计时剩余时间、正计时累计时间。
 */
void FormatTime(int remaining_time, char* time_text) {
    if (CLOCK_SHOW_CURRENT_TIME) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        int hour = tm_info->tm_hour;
        
        if (!CLOCK_USE_24HOUR) {
            if (hour == 0) {
                hour = 12;
            } else if (hour > 12) {
                hour -= 12;
            }
        }

        if (CLOCK_SHOW_SECONDS) {
            sprintf(time_text, "%d:%02d:%02d", 
                    hour, tm_info->tm_min, tm_info->tm_sec);
        } else {
            sprintf(time_text, "%d:%02d", 
                    hour, tm_info->tm_min);
        }
        return;
    }

    if (CLOCK_COUNT_UP) {
        int hours = countup_elapsed_time / 3600;
        int minutes = (countup_elapsed_time % 3600) / 60;
        int seconds = countup_elapsed_time % 60;

        if (hours > 0) {
            sprintf(time_text, "%d:%02d:%02d", hours, minutes, seconds);
        } else if (minutes > 0) {
            sprintf(time_text, "    %d:%02d", minutes, seconds);
        } else {
            sprintf(time_text, "        %d", seconds);
        }
        return;
    }

    int remaining = CLOCK_TOTAL_TIME - countdown_elapsed_time;
    if (remaining <= 0) {
        time_text[0] = '\0';
        return;
    }

    int hours = remaining / 3600;
    int minutes = (remaining % 3600) / 60;
    int seconds = remaining % 60;

    if (hours > 0) {
        sprintf(time_text, "%d:%02d:%02d", hours, minutes, seconds);
    } else if (minutes > 0) {
        if (minutes >= 10) {
            sprintf(time_text, "    %d:%02d", minutes, seconds);
        } else {
            sprintf(time_text, "    %d:%02d", minutes, seconds);
        }
    } else {
        if (seconds < 10) {
            sprintf(time_text, "          %d", seconds);
        } else {
            sprintf(time_text, "        %d", seconds);
        }
    }
}

/**
 * @brief 解析用户输入的时间字符串
 * @param input 用户输入的时间字符串
 * @param[out] total_seconds 解析得到的总秒数
 * @return int 解析成功返回1，失败返回0
 * 
 * 支持多种输入格式：
 * - 单一数字（默认分钟）："25" → 25分钟
 * - 带单位："1h30m" → 1小时30分钟
 * - 两段格式："25 3" → 25分钟3秒
 * - 三段格式："1 30 15" → 1小时30分钟15秒
 * - 混合格式："25 30m" → 25小时30分钟
 */
int ParseInput(const char* input, int* total_seconds) {
    if (!isValidInput(input)) return 0;

    int total = 0;
    char input_copy[256];
    strncpy(input_copy, input, sizeof(input_copy)-1);
    input_copy[sizeof(input_copy)-1] = '\0';

    // 检查是否是目标时间格式（以't'结尾）
    int len = strlen(input_copy);
    if (len > 0 && input_copy[len-1] == 't') {
        // 移除't'后缀
        input_copy[len-1] = '\0';
        
        // 获取当前时间
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        
        // 目标时间，初始化为当前日期
        struct tm tm_target = *tm_now;
        tm_target.tm_sec = 0; // 默认秒数为0
        
        // 解析目标时间
        int hour = -1, minute = -1, second = -1;
        int count = 0;
        char *token = strtok(input_copy, " ");
        
        while (token && count < 3) {
            int value = atoi(token);
            if (count == 0) hour = value;
            else if (count == 1) minute = value;
            else if (count == 2) second = value;
            count++;
            token = strtok(NULL, " ");
        }
        
        // 设置目标时间
        if (hour >= 0) tm_target.tm_hour = hour;
        if (minute >= 0) tm_target.tm_min = minute;
        if (second >= 0) tm_target.tm_sec = second;
        
        // 计算时间差（秒）
        time_t target_time = mktime(&tm_target);
        
        // 如果目标时间已过，则设置为明天的同一时间
        if (target_time <= now) {
            tm_target.tm_mday += 1;
            target_time = mktime(&tm_target);
        }
        
        total = (int)difftime(target_time, now);
    } else {
        // 检查是否含有单位标识符
        BOOL hasUnits = FALSE;
        for (int i = 0; input_copy[i]; i++) {
            char c = tolower((unsigned char)input_copy[i]);
            if (c == 'h' || c == 'm' || c == 's') {
                hasUnits = TRUE;
                break;
            }
        }
        
        if (hasUnits) {
            // 对于带单位的输入，合并所有标记单位的部分
            char* parts[10] = {0}; // 最多存储10个部分
            int part_count = 0;
            
            // 分割输入字符串
            char* token = strtok(input_copy, " ");
            while (token && part_count < 10) {
                parts[part_count++] = token;
                token = strtok(NULL, " ");
            }
            
            // 处理每个部分
            for (int i = 0; i < part_count; i++) {
                char* part = parts[i];
                int part_len = strlen(part);
                BOOL has_unit = FALSE;
                
                // 检查这一部分是否有单位
                for (int j = 0; j < part_len; j++) {
                    char c = tolower((unsigned char)part[j]);
                    if (c == 'h' || c == 'm' || c == 's') {
                        has_unit = TRUE;
                        break;
                    }
                }
                
                if (has_unit) {
                    // 如果带单位，按单位处理
                    char unit = tolower((unsigned char)part[part_len-1]);
                    part[part_len-1] = '\0'; // 移除单位
                    int value = atoi(part);
                    
                    switch (unit) {
                        case 'h': total += value * 3600; break;
                        case 'm': total += value * 60; break;
                        case 's': total += value; break;
                    }
                } else if (i < part_count - 1 && 
                          strlen(parts[i+1]) > 0 && 
                          tolower((unsigned char)parts[i+1][strlen(parts[i+1])-1]) == 'h') {
                    // 如果后一项带h单位，当前项是小时
                    total += atoi(part) * 3600;
                } else if (i < part_count - 1 && 
                          strlen(parts[i+1]) > 0 && 
                          tolower((unsigned char)parts[i+1][strlen(parts[i+1])-1]) == 'm') {
                    // 如果后一项带m单位，当前项是小时
                    total += atoi(part) * 3600;
                } else {
                    // 默认按两段或三段格式处理
                    if (part_count == 2) {
                        // 两段格式：第一段是分钟，第二段是秒
                        if (i == 0) total += atoi(part) * 60;
                        else total += atoi(part);
                    } else if (part_count == 3) {
                        // 三段格式：时:分:秒
                        if (i == 0) total += atoi(part) * 3600;
                        else if (i == 1) total += atoi(part) * 60;
                        else total += atoi(part);
                    } else {
                        // 其他情况按分钟处理
                        total += atoi(part) * 60;
                    }
                }
            }
        } else {
            // 不含单位的处理
            char* parts[3] = {0}; // 最多存储3个部分(时、分、秒)
            int part_count = 0;
            
            // 分割输入字符串
            char* token = strtok(input_copy, " ");
            while (token && part_count < 3) {
                parts[part_count++] = token;
                token = strtok(NULL, " ");
            }
            
            if (part_count == 1) {
                // 单个数字，按分钟计算
                total = atoi(parts[0]) * 60;
            } else if (part_count == 2) {
                // 两个数字：分:秒
                total = atoi(parts[0]) * 60 + atoi(parts[1]);
            } else if (part_count == 3) {
                // 三个数字：时:分:秒
                total = atoi(parts[0]) * 3600 + atoi(parts[1]) * 60 + atoi(parts[2]);
            }
        }
    }

    *total_seconds = total;
    if (*total_seconds <= 0) return 0;

    if (*total_seconds > INT_MAX) {
        return 0;
    }

    return 1;
}

/**
 * @brief 验证输入字符串是否合法
 * @param input 待验证的输入字符串
 * @return int 合法返回1，非法返回0
 * 
 * 有效字符检查规则：
 * - 仅允许数字、空格和结尾的h/m/s单位标识
 * - 至少包含一个数字
 * - 最多两个空格分隔符
 */
int isValidInput(const char* input) {
    if (input == NULL || *input == '\0') {
        return 0;
    }

    int len = strlen(input);
    int digitCount = 0;

    for (int i = 0; i < len; i++) {
        if (isdigit(input[i])) {
            digitCount++;
        } else if (input[i] == ' ') {
            // 允许任意数量的空格
        } else if (i == len - 1 && (input[i] == 'h' || input[i] == 'm' || input[i] == 's' || input[i] == 't')) {
            // 允许最后一个字符是h、m、s或t（t表示目标时间）
        } else {
            return 0;
        }
    }

    if (digitCount == 0) {
        return 0;
    }

    return 1;
}

/**
 * @brief 重置计时器
 * 
 * 重置计时器状态，包括暂停标志、已计时间等
 */
void ResetTimer(void) {
    // 重置计时状态
    if (CLOCK_COUNT_UP) {
        countup_elapsed_time = 0;
    } else {
        countdown_elapsed_time = 0;
    }
    
    // 取消暂停状态
    CLOCK_IS_PAUSED = FALSE;
    
    // 重置消息显示标志
    countdown_message_shown = FALSE;
    countup_message_shown = FALSE;
}

/**
 * @brief 切换计时器暂停状态
 * 
 * 在暂停和继续状态之间切换计时器
 */
void TogglePauseTimer(void) {
    CLOCK_IS_PAUSED = !CLOCK_IS_PAUSED;
    
    // 如果暂停，记录当前时间点
    if (CLOCK_IS_PAUSED) {
        CLOCK_LAST_TIME_UPDATE = time(NULL);
    }
}

/**
 * @brief 将默认启动时间写入配置文件
 * @param seconds 默认启动时间（秒）
 * 
 * 配置文件路径：
 * - 优先使用 %LOCALAPPDATA%\Catime\config.txt
 * - 备用路径 .\\asset\\config.txt
 */
void WriteConfigDefaultStartTime(int seconds) {
    char config_path[MAX_PATH];
    char temp_path[MAX_PATH];
    
    // 获取配置文件路径
    char* appdata_path = getenv("LOCALAPPDATA");
    if (appdata_path) {
        snprintf(config_path, MAX_PATH, "%s\\Catime\\config.txt", appdata_path);
        snprintf(temp_path, MAX_PATH, "%s\\Catime\\config.txt.tmp", appdata_path);
    } else {
        strcpy(config_path, ".\\asset\\config.txt");
        strcpy(temp_path, ".\\asset\\config.txt.tmp");
    }
    
    FILE* file = fopen(config_path, "r");
    FILE* temp = fopen(temp_path, "w");
    
    if (!file || !temp) {
        if (file) fclose(file);
        if (temp) fclose(temp);
        return;
    }
    
    char line[256];
    int found = 0;
    
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "CLOCK_DEFAULT_START_TIME=", 25) == 0) {
            fprintf(temp, "CLOCK_DEFAULT_START_TIME=%d\n", seconds);
            found = 1;
        } else {
            fputs(line, temp);
        }
    }
    
    if (!found) {
        fprintf(temp, "CLOCK_DEFAULT_START_TIME=%d\n", seconds);
    }
    
    fclose(file);
    fclose(temp);
    
    remove(config_path);
    rename(temp_path, config_path);
}

/**
 * @brief 将启动模式写入配置文件
 * @param mode 启动模式字符串（"COUNTDOWN" 或 "COUNTUP"）
 * 
 * 配置文件路径：
 * - 优先使用 %LOCALAPPDATA%\Catime\config.txt
 * - 备用路径 .\\asset\\config.txt
 */
void WriteConfigStartupMode(const char* mode) {
    char config_path[MAX_PATH];
    char temp_path[MAX_PATH];
    
    // 获取配置文件路径
    char* appdata_path = getenv("LOCALAPPDATA");
    if (appdata_path) {
        snprintf(config_path, MAX_PATH, "%s\\Catime\\config.txt", appdata_path);
        snprintf(temp_path, MAX_PATH, "%s\\Catime\\config.txt.tmp", appdata_path);
    } else {
        strcpy(config_path, ".\\asset\\config.txt");
        strcpy(temp_path, ".\\asset\\config.txt.tmp");
    }
    
    FILE* file = fopen(config_path, "r");
    FILE* temp = fopen(temp_path, "w");
    
    if (!file || !temp) {
        if (file) fclose(file);
        if (temp) fclose(temp);
        return;
    }
    
    char line[256];
    int found = 0;
    
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "STARTUP_MODE=", 13) == 0) {
            fprintf(temp, "STARTUP_MODE=%s\n", mode);
            found = 1;
        } else {
            fputs(line, temp);
        }
    }
    
    if (!found) {
        fprintf(temp, "STARTUP_MODE=%s\n", mode);
    }
    
    fclose(file);
    fclose(temp);
    
    remove(config_path);
    rename(temp_path, config_path);
}