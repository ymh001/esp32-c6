#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_FONT_FMT_TXT_LARGE 1
#define LV_MEM_SIZE (96 * 1024U)
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 1
#define LV_LOG_PRINTF 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_ASSERT_HANDLER abort();
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#endif
