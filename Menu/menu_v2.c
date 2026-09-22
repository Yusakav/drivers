#include "menu_v2.h"

#include <string.h>

/**
 * @brief 校验数值项的范围配置是否合法
 *
 * @param item 菜单项指针
 *
 * @return uint8_t 合法返回 1，非法返回 0
 */
static uint8_t range_is_valid(const MenuV2_Item *item)
{
    const MenuV2_Range *range = &item->config.range;
    if (item->target.value == 0) return 0;      /* 数值指针为空则非法 */
    switch (item->flags.value_type) {
    case MENU_V2_VALUE_INT8: return range->step.i8 > 0 && range->min.i8 <= range->max.i8;
    case MENU_V2_VALUE_UINT8: return range->step.u8 > 0 && range->min.u8 <= range->max.u8;
    case MENU_V2_VALUE_INT16: return range->step.i16 > 0 && range->min.i16 <= range->max.i16;
    case MENU_V2_VALUE_UINT16: return range->step.u16 > 0 && range->min.u16 <= range->max.u16;
    case MENU_V2_VALUE_INT32: return range->step.i32 > 0 && range->min.i32 <= range->max.i32;
    case MENU_V2_VALUE_UINT32: return range->step.u32 > 0 && range->min.u32 <= range->max.u32;
    case MENU_V2_VALUE_FLOAT: return range->step.f > 0.0f && range->min.f <= range->max.f;
    case MENU_V2_VALUE_BOOL: return 1;          /* 布尔项恒合法 */
    default: return 0;                          /* 未知类型非法 */
    }
}

/**
 * @brief 校验菜单项是否有效
 *
 * @param item 菜单项指针
 *
 * @return uint8_t 有效返回 1，无效返回 0
 */
static uint8_t item_is_valid(const MenuV2_Item *item)
{
    if (item == 0) return 0;                    /* 空指针无效 */
    if (item->flags.kind == MENU_V2_ITEM_FOLDER)
        return item->target.page != 0 && item->target.page->items != 0 && item->target.page->count != 0;   /* 子页面、数组与数量均须有效 */
    if (item->flags.kind == MENU_V2_ITEM_RANGE) return range_is_valid(item);   /* 数值项委托范围校验 */
    return item->flags.kind == MENU_V2_ITEM_ACTION && item->target.action != 0;    /* 动作项须有回调 */
}

/**
 * @brief 获取当前菜单深度
 *
 * @param context 菜单上下文指针
 *
 * @return uint8_t 当前深度（0=根），无效时返回 0
 */
uint8_t menu_v2_depth(const MenuV2_Context *context)
{
    return context != 0 ? context->state.depth : 0;
}

/**
 * @brief 查询是否处于编辑态
 *
 * @param context 菜单上下文指针
 *
 * @return uint8_t 编辑态返回 1，否则返回 0
 */
uint8_t menu_v2_is_editing(const MenuV2_Context *context)
{
    return context != 0 ? context->state.editing : 0;
}

/**
 * @brief 获取当前页面的菜单项数量
 *
 * @param context 菜单上下文指针
 *
 * @return uint8_t 当前页面菜单项数量，无效时返回 0
 */
uint8_t menu_v2_page_count(const MenuV2_Context *context)
{
    const MenuV2_Page *page;
    if (context == 0 || context->state.depth >= MENU_V2_MAX_DEPTH) return 0;    /* 空指针或深度越界返回 0 */
    page = context->pages[context->state.depth];
    return page != 0 ? page->count : 0;
}

/**
 * @brief 获取当前深度的选中索引
 *
 * @param context 菜单上下文指针
 *
 * @return uint8_t 当前选中索引，无效时返回 0
 */
uint8_t menu_v2_selected_index(const MenuV2_Context *context)
{
    return (context != 0 && context->state.depth < MENU_V2_MAX_DEPTH) ? context->selected[context->state.depth] : 0;
}

/**
 * @brief 获取当前页面指定索引处的菜单项
 *
 * @param context 菜单上下文指针
 * @param index   菜单项索引
 *
 * @return const MenuV2_Item* 对应菜单项指针，越界或无效时返回 NULL
 */
const MenuV2_Item *menu_v2_item_at(const MenuV2_Context *context, uint8_t index)
{
    const MenuV2_Page *page;
    if (context == 0 || context->state.depth >= MENU_V2_MAX_DEPTH) return 0;    /* 空指针或深度越界返回 NULL */
    page = context->pages[context->state.depth];
    if (page == 0 || page->items == 0 || index >= page->count) return 0;        /* 页面、数组无效或索引越界返回 NULL */
    return &page->items[index];
}

/**
 * @brief 获取当前焦点菜单项
 *
 * @param context 菜单上下文指针
 *
 * @return const MenuV2_Item* 当前焦点菜单项指针，无效时返回 NULL
 */
const MenuV2_Item *menu_v2_current(const MenuV2_Context *context)
{
    return menu_v2_item_at(context, menu_v2_selected_index(context));
}

/**
 * @brief 发送状态变化事件通知
 *
 * @param context 菜单上下文指针
 * @param event   事件类型 (MenuV2_Event)
 */
static void notify(MenuV2_Context *context, MenuV2_Event event)
{
    if (context->on_change != 0)
        context->on_change(context, event, menu_v2_current(context), context->user_data);   /* 回调已注册时才触发 */
}

/* 有符号整型数值步进宏：先夹取到 [min,max]，再按 up/down 步进，距边界不足一步时直接吸附到边界；返回 1 表示值已变化 */
#define ADJUST_SIGNED(type_, value_, min_, max_, step_, up_) do { \
    type_ *p_ = (type_ *)(value_); type_ before_ = *p_; int64_t current_ = *p_, min_value_ = (min_), max_value_ = (max_), step_value_ = (step_); \
    if (current_ < min_value_) current_ = min_value_; else if (current_ > max_value_) current_ = max_value_; \
    if (up_) { if (current_ < max_value_) current_ = (max_value_ - current_ <= step_value_) ? max_value_ : current_ + step_value_; } \
    else { if (current_ > min_value_) current_ = (current_ - min_value_ <= step_value_) ? min_value_ : current_ - step_value_; } \
    *p_ = (type_)current_; return *p_ != before_; \
} while (0)
/* 无符号整型数值步进宏：同 ADJUST_SIGNED，使用 uint64_t 中间量防止无符号运算回绕 */
#define ADJUST_UNSIGNED(type_, value_, min_, max_, step_, up_) do { \
    type_ *p_ = (type_ *)(value_); type_ before_ = *p_; uint64_t current_ = *p_, min_value_ = (min_), max_value_ = (max_), step_value_ = (step_); \
    if (current_ < min_value_) current_ = min_value_; else if (current_ > max_value_) current_ = max_value_; \
    if (up_) { if (current_ < max_value_) current_ = (max_value_ - current_ <= step_value_) ? max_value_ : current_ + step_value_; } \
    else { if (current_ > min_value_) current_ = (current_ - min_value_ <= step_value_) ? min_value_ : current_ - step_value_; } \
    *p_ = (type_)current_; return *p_ != before_; \
} while (0)

/**
 * @brief 按方向调整数值项的值
 *
 * @param item 数值菜单项指针
 * @param up   调整方向，非 0 为增加，0 为减少
 *
 * @return uint8_t 值发生变化返回 1，未变化或非法返回 0
 */
static uint8_t adjust_range(const MenuV2_Item *item, uint8_t up)
{
    const MenuV2_Range *range = &item->config.range;
    if (!range_is_valid(item)) return 0;    /* 范围非法直接返回 */
    switch (item->flags.value_type) {
    case MENU_V2_VALUE_INT8: ADJUST_SIGNED(int8_t, item->target.value, range->min.i8, range->max.i8, range->step.i8, up);
    case MENU_V2_VALUE_UINT8: ADJUST_UNSIGNED(uint8_t, item->target.value, range->min.u8, range->max.u8, range->step.u8, up);
    case MENU_V2_VALUE_INT16: ADJUST_SIGNED(int16_t, item->target.value, range->min.i16, range->max.i16, range->step.i16, up);
    case MENU_V2_VALUE_UINT16: ADJUST_UNSIGNED(uint16_t, item->target.value, range->min.u16, range->max.u16, range->step.u16, up);
    case MENU_V2_VALUE_INT32: ADJUST_SIGNED(int32_t, item->target.value, range->min.i32, range->max.i32, range->step.i32, up);
    case MENU_V2_VALUE_UINT32: ADJUST_UNSIGNED(uint32_t, item->target.value, range->min.u32, range->max.u32, range->step.u32, up);
    case MENU_V2_VALUE_FLOAT: {
        float *p = (float *)item->target.value;
        float before = *p;
        if (*p != *p || *p < range->min.f) *p = range->min.f;               /* NaN 或低于下限时重置为下限 */
        else if (*p > range->max.f) *p = range->max.f;                      /* 高于上限时夹取到上限 */
        if (up && *p < range->max.f) *p = ((range->max.f - *p) <= range->step.f) ? range->max.f : *p + range->step.f;
        if (!up && *p > range->min.f) *p = ((*p - range->min.f) <= range->step.f) ? range->min.f : *p - range->step.f;
        return *p != before;                                                /* 返回值是否变化 */
    }
    case MENU_V2_VALUE_BOOL: *(uint8_t *)item->target.value = !*(uint8_t *)item->target.value; return 1;   /* 布尔值取反 */
    default: return 0;                                                      /* 未知类型非法 */
    }
}

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
                     MenuV2_ChangeCallback callback, void *user_data)
{
    if (context == 0 || root_page == 0 || root_page->items == 0 || root_page->count == 0) return 0;   /* 参数校验 */
    memset(context, 0, sizeof(*context));       /* 清零上下文，重置深度/选中/编辑态 */
    context->pages[0] = root_page;              /* 挂载根页面 */
    context->on_change = callback;              /* 注册回调 */
    context->user_data = user_data;             /* 保存用户数据 */
    return 1;
}

/**
 * @brief 处理单个按键，驱动菜单状态机
 *
 * @param context 菜单上下文指针
 * @param key     按键值 (MenuV2_Key)
 *
 * @return uint8_t 按键被处理返回 1，忽略返回 0
 */
uint8_t menu_v2_handle(MenuV2_Context *context, MenuV2_Key key)
{
    const MenuV2_Item *item;
    uint8_t count;
    uint8_t *selected;
    if (context == 0 || context->state.depth >= MENU_V2_MAX_DEPTH) return 0;    /* 空指针或深度越界忽略 */
    item = menu_v2_current(context);
    count = menu_v2_page_count(context);
    selected = &context->selected[context->state.depth];
    if (item == 0 || count == 0) return 0;      /* 当前项或页面无效忽略 */

    if (key == MENU_V2_KEY_UP || key == MENU_V2_KEY_DOWN) {
        if (context->state.editing) {           /* 编辑态：UP/DOWN 步进数值 */
            if (item->flags.kind != MENU_V2_ITEM_RANGE || item->flags.read_only || !adjust_range(item, key == MENU_V2_KEY_UP)) return 0;
            notify(context, MENU_V2_EVENT_VALUE); return 1;
        }
        if (count <= 1) return 0;               /* 单项页面无需移动选中 */
        *selected = (key == MENU_V2_KEY_UP) ? (*selected == 0 ? count - 1 : *selected - 1) : (*selected + 1 == count ? 0 : *selected + 1);   /* 选中索引环形移动 */
        notify(context, MENU_V2_EVENT_SELECTION); return 1;
    }
    if (key == MENU_V2_KEY_ENTER) {
        if (item->flags.kind == MENU_V2_ITEM_RANGE) {       /* RANGE：切换编辑态 */
            if (!range_is_valid(item) || item->flags.read_only) return 0;
            context->state.editing = !context->state.editing; notify(context, MENU_V2_EVENT_EDIT); return 1;
        }
        if (item->flags.kind == MENU_V2_ITEM_FOLDER) {      /* FOLDER：进入子菜单 */
            if (!item_is_valid(item) || context->state.depth + 1 >= MENU_V2_MAX_DEPTH) return 0;   /* 校验并防止深度越界 */
            context->state.depth++;
            context->pages[context->state.depth] = item->target.page;   /* 压栈子页面 */
            context->selected[context->state.depth] = 0;                /* 新层选中首项 */
            notify(context, MENU_V2_EVENT_ENTER); return 1;
        }
        if (item->flags.kind == MENU_V2_ITEM_ACTION) {      /* ACTION：执行动作 */
            if (!item_is_valid(item)) return 0;
            item->target.action(); notify(context, MENU_V2_EVENT_ACTION); return 1;
        }
        return 0;
    }
    if (key == MENU_V2_KEY_BACK) {
        if (context->state.editing) { context->state.editing = 0; notify(context, MENU_V2_EVENT_EDIT); return 1; }  /* 优先退出编辑态 */
        if (context->state.depth == 0) return 0;            /* 根层不可返回 */
        context->state.depth--; notify(context, MENU_V2_EVENT_BACK); return 1;   /* 弹栈返回上一级 */
    }
    if (key == MENU_V2_KEY_HOME) {
        if (context->state.depth == 0 && !context->state.editing && context->selected[0] == 0) return 0;   /* 已在根首项非编辑态则无变化 */
        context->state.depth = 0; context->state.editing = 0; context->selected[0] = 0;    /* 重置到根首项 */
        notify(context, MENU_V2_EVENT_HOME); return 1;
    }
    return 0;
}
