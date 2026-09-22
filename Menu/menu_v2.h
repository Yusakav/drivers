#ifndef MENU_V2_H
#define MENU_V2_H

#include <stdint.h>

#define MENU_V2_MAX_DEPTH 6u

/**
 * @brief 菜单按键类型定义
 */
typedef uint8_t MenuV2_Key;

#define MENU_V2_KEY_NONE    0   /**< 无按键 */
#define MENU_V2_KEY_UP      1   /**< 上键 */
#define MENU_V2_KEY_DOWN    2   /**< 下键 */
#define MENU_V2_KEY_ENTER   3   /**< 确认键 */
#define MENU_V2_KEY_BACK    4   /**< 返回键 */
#define MENU_V2_KEY_HOME    5   /**< 主页键 */

/**
 * @brief 菜单项类型定义
 */
typedef uint8_t MenuV2_ItemKind;

#define MENU_V2_ITEM_FOLDER 0   /**< 子菜单项 */
#define MENU_V2_ITEM_RANGE  1   /**< 数值项 */
#define MENU_V2_ITEM_ACTION 2   /**< 动作项 */

/**
 * @brief 数值类型定义
 */
typedef uint8_t MenuV2_ValueType;

#define MENU_V2_VALUE_INT8   0   /**< 有符号 8 位整数 (char) */
#define MENU_V2_VALUE_UINT8  1   /**< 无符号 8 位整数 (unsigned char) */
#define MENU_V2_VALUE_INT16  2   /**< 有符号 16 位整数 (short) */
#define MENU_V2_VALUE_UINT16 3   /**< 无符号 16 位整数 (unsigned short) */
#define MENU_V2_VALUE_INT32  4   /**< 有符号 32 位整数 (int/long) */
#define MENU_V2_VALUE_UINT32 5   /**< 无符号 32 位整数 (unsigned int/long) */
#define MENU_V2_VALUE_FLOAT  6   /**< 单精度浮点数 (float) */
#define MENU_V2_VALUE_BOOL   7   /**< 布尔值 (0/1) */

/**
 * @brief 菜单事件类型定义
 */
typedef uint8_t MenuV2_Event;

#define MENU_V2_EVENT_SELECTION 0   /**< 选中项变化 */
#define MENU_V2_EVENT_ENTER     1   /**< 进入子菜单 */
#define MENU_V2_EVENT_BACK      2   /**< 返回上一级菜单 */
#define MENU_V2_EVENT_HOME      3   /**< 返回根菜单 */
#define MENU_V2_EVENT_EDIT      4   /**< 进入/退出编辑态 */
#define MENU_V2_EVENT_VALUE     5   /**< 数值变化 */
#define MENU_V2_EVENT_ACTION    6   /**< 动作项触发 */

/**
 * @brief 数值联合体，按 value_type 解释对应成员
 */
typedef union {
    int8_t i8;      /**< 有符号 8 位整数 */
    uint8_t u8;     /**< 无符号 8 位整数 */
    int16_t i16;    /**< 有符号 16 位整数 */
    uint16_t u16;   /**< 无符号 16 位整数 */
    int32_t i32;    /**< 有符号 32 位整数 */
    uint32_t u32;   /**< 无符号 32 位整数 */
    float f;        /**< 单精度浮点数 */
} MenuV2_Number;

/**
 * @brief 数值范围配置，描述数值项的下限、上限与步长
 */
typedef struct {
    MenuV2_Number min;      /**< 下限 */
    MenuV2_Number max;      /**< 上限 */
    MenuV2_Number step;     /**< 步长 */
} MenuV2_Range;

struct MenuV2_Item;
/**
 * @brief 菜单页面，由菜单项数组构成
 */
typedef struct {
    const struct MenuV2_Item *items;    /**< 菜单项数组指针 */
    uint8_t count;                      /**< 菜单项数量 */
} MenuV2_Page;

/**
 * @brief 菜单项，描述单个菜单条目的类型、目标与配置
 */
typedef struct MenuV2_Item {
    const char *title;                  /**< 菜单项标题 */
    union {                             /**< 按 kind 解释的目标 */
        void *value;                    /**< RANGE: 指向外部数值变量 */
        const MenuV2_Page *page;        /**< FOLDER: 指向子页面 */
        void (*action)(void);           /**< ACTION: 动作回调函数 */
    } target;
    union {                             /**< 按 kind 解释的配置 */
        MenuV2_Range range;             /**< RANGE: 数值范围配置 */
        uint8_t unused;                 /**< 其他类型占位 */
    } config;
    struct {                            /**< 位域标志 */
        uint8_t kind : 2;               /**< 菜单项类型 */
        uint8_t value_type : 3;         /**< 数值类型 */
        uint8_t read_only : 1;          /**< 只读标志 */
        uint8_t live : 1;               /**< 实时更新标志 */
        uint8_t reserved : 1;           /**< 保留 */
    } flags;
} MenuV2_Item;

struct MenuV2_Context;
/**
 * @brief 菜单状态变化回调函数类型
 */
typedef void (*MenuV2_ChangeCallback)(const struct MenuV2_Context *context,
                                      MenuV2_Event event,
                                      const MenuV2_Item *current_item,
                                      void *user_data);

/**
 * @brief 菜单运行上下文，保存导航层级栈与状态
 */
typedef struct MenuV2_Context {
    const MenuV2_Page *pages[MENU_V2_MAX_DEPTH];    /**< 每层当前页面指针 */
    uint8_t selected[MENU_V2_MAX_DEPTH];            /**< 每层选中索引 */
    struct {                                        /**< 状态位域 */
        uint8_t depth : 3;                          /**< 当前深度（0=根） */
        uint8_t editing : 1;                        /**< 是否处于编辑态 */
        uint8_t reserved : 4;                       /**< 保留 */
    } state;
    MenuV2_ChangeCallback on_change;                /**< 状态变化回调 */
    void *user_data;                                /**< 透传给回调的用户数据 */
} MenuV2_Context;

#define MENU_V2_PAGE(items_) { (items_), (uint8_t)(sizeof(items_) / sizeof((items_)[0])) }

#define MENU_V2_FOLDER(title_, page_) \
    { (title_), {.page = (page_)}, {.unused = 0}, {.kind = MENU_V2_ITEM_FOLDER} }

#define MENU_V2_ACTION(title_, callback_) \
    { (title_), {.action = (callback_)}, {.unused = 0}, {.kind = MENU_V2_ITEM_ACTION} }

#define MENU_V2_RANGE_INIT(title_, value_, type_, member_, min_, max_, step_, ro_, live_) \
    { (title_), {.value = (value_)}, {.range = {{.member_ = (min_)}, {.member_ = (max_)}, {.member_ = (step_)}}}, \
      {.kind = MENU_V2_ITEM_RANGE, .value_type = (type_), .read_only = (ro_), .live = (live_)} }
      
#define MENU_V2_RANGE_I8(t, v, min, max, step) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_INT8, i8, min, max, step, 0, 0)
#define MENU_V2_RANGE_U8(t, v, min, max, step) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_UINT8, u8, min, max, step, 0, 0)
#define MENU_V2_RANGE_I16(t, v, min, max, step) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_INT16, i16, min, max, step, 0, 0)
#define MENU_V2_RANGE_U16(t, v, min, max, step) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_UINT16, u16, min, max, step, 0, 0)
#define MENU_V2_RANGE_I32(t, v, min, max, step) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_INT32, i32, min, max, step, 0, 0)
#define MENU_V2_RANGE_U32(t, v, min, max, step) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_UINT32, u32, min, max, step, 0, 0)
#define MENU_V2_RANGE_FLOAT(t, v, min, max, step) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_FLOAT, f, min, max, step, 0, 0)
#define MENU_V2_BOOL(t, v) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_BOOL, u8, 0, 1, 1, 0, 0)
#define MENU_V2_LIVE_RANGE_U8(t, v, min, max, step) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_UINT8, u8, min, max, step, 1, 1)
#define MENU_V2_LIVE_RANGE_U16(t, v, min, max, step) MENU_V2_RANGE_INIT(t, v, MENU_V2_VALUE_UINT16, u16, min, max, step, 1, 1)

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(MenuV2_Item) == 24, "MenuV2_Item must remain compact on 32-bit targets");
_Static_assert(sizeof(MenuV2_Page) == 8, "MenuV2_Page must remain compact on 32-bit targets");
_Static_assert(sizeof(MenuV2_Context) <= 48, "MenuV2_Context exceeds the RAM budget");
#endif

/**
 * @brief 初始化菜单上下文
 *
 * @param context   菜单上下文指针
 * @param root_page 根页面指针
 * @param callback  状态变化回调函数
 * @param user_data 用户自定义数据
 *
 * @return uint8_t 初始化成功返回 1，失败返回 0
 */
uint8_t menu_v2_init(MenuV2_Context *context, const MenuV2_Page *root_page,
                     MenuV2_ChangeCallback callback, void *user_data);
/**
 * @brief 处理单个按键，驱动菜单状态机
 *
 * @param context 菜单上下文指针
 * @param key     按键值 (MenuV2_Key)
 *
 * @return uint8_t 按键被处理返回 1，忽略返回 0
 */
uint8_t menu_v2_handle(MenuV2_Context *context, MenuV2_Key key);
/**
 * @brief 获取当前焦点菜单项
 *
 * @param context 菜单上下文指针
 *
 * @return const MenuV2_Item* 当前焦点菜单项指针，无效时返回 NULL
 */
const MenuV2_Item *menu_v2_current(const MenuV2_Context *context);
/**
 * @brief 获取当前页面指定索引处的菜单项
 *
 * @param context 菜单上下文指针
 * @param index   菜单项索引
 *
 * @return const MenuV2_Item* 对应菜单项指针，越界或无效时返回 NULL
 */
const MenuV2_Item *menu_v2_item_at(const MenuV2_Context *context, uint8_t index);
/**
 * @brief 获取当前页面的菜单项数量
 *
 * @param context 菜单上下文指针
 *
 * @return uint8_t 当前页面菜单项数量，无效时返回 0
 */
uint8_t menu_v2_page_count(const MenuV2_Context *context);
/**
 * @brief 获取当前深度的选中索引
 *
 * @param context 菜单上下文指针
 *
 * @return uint8_t 当前选中索引，无效时返回 0
 */
uint8_t menu_v2_selected_index(const MenuV2_Context *context);
/**
 * @brief 获取当前菜单深度
 *
 * @param context 菜单上下文指针
 *
 * @return uint8_t 当前深度（0=根），无效时返回 0
 */
uint8_t menu_v2_depth(const MenuV2_Context *context);
/**
 * @brief 查询是否处于编辑态
 *
 * @param context 菜单上下文指针
 *
 * @return uint8_t 编辑态返回 1，否则返回 0
 */
uint8_t menu_v2_is_editing(const MenuV2_Context *context);

#endif
