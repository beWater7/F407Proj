#ifndef _HAL_DTS_H
#define _HAL_DTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 板级设备树(dts)解析库
 *
 * dtb 由 tools/pack/gen_dtb.py 从 platform/boards/<board>/<board>.dts 编译生成:
 *   [dts_header_t 32B][nodes[] 16B/个][props[] 8B/个][strtab]
 *
 * 加载策略(dts_load_default):
 *   SPI flash 主槽(0xF00000) → 备份槽(0xF01000) → 固件内置 C 数组(兜底)
 * 三个来源都带 CRC 校验, 保证分区表永远可用。
 */

/* 格式魔数 / 格式版本 */
#define DTS_MAGIC          0xD45B0001u
#define DTS_VERSION        1u

/* dtb 保留区(SPI flash 物理地址, 与 f407zg.dts 头部注释约定一致) */
#define DTS_FLASH_PRIMARY  0x00F00000u
#define DTS_FLASH_BACKUP   0x00F01000u
#define DTS_FLASH_SLOT_MAX 4096u      /* 单槽最大 dtb 长度 */

/* 属性类型 */
typedef enum {
    DTS_PROP_EMPTY = 0,
    DTS_PROP_U32   = 1,   /* val_off 内联存放值 */
    DTS_PROP_STR   = 2,   /* val_off 指向 strtab */
} dts_prop_type_t;

/* 头部(32B) */
typedef struct __attribute__((packed)) {
    uint32_t magic;       /* DTS_MAGIC */
    uint32_t version;     /* 格式版本, 与固件兼容性检查 */
    uint32_t body_len;    /* 整个 blob 长度 */
    uint32_t body_crc32;  /* CRC32(blob[32:]) */
    uint32_t strtab_off;  /* strtab 绝对偏移 */
    uint32_t strtab_len;
    uint16_t node_cnt;
    uint16_t prop_cnt;
    uint16_t hdr_crc;     /* CRC16/CCITT-FALSE(blob[0:28]) */
    uint16_t reserved;
} dts_header_t;

/* 节点(16B) */
typedef struct __attribute__((packed)) {
    uint16_t name_off;    /* 节点名(strtab 偏移, 根节点为空串) */
    uint16_t parent;      /* 父节点下标, 0xFFFF=根 */
    uint16_t prop_start;  /* props[] 起始下标 */
    uint16_t prop_cnt;
    uint32_t reg_addr;    /* reg=<addr size> 第一个 cell */
    uint32_t reg_size;    /* reg 第二个 cell */
} dts_node_t;

/* 属性(8B) */
typedef struct __attribute__((packed)) {
    uint16_t key_off;     /* 属性名(strtab 偏移) */
    uint8_t  type;
    uint8_t  len;         /* STR: 字符串长度(不含 \0); U32: 4 */
    uint32_t val_off;     /* STR: strtab 偏移; U32: 值本身 */
} dts_prop_t;

/* 数据来源 */
typedef enum {
    DTS_SRC_NONE = 0,
    DTS_SRC_FLASH,        /* 从 SPI flash dtb 保留区读出 */
    DTS_SRC_STATIC,       /* 固件内置 C 数组兜底 */
} dts_source_t;

/* 解析上下文: 零拷贝, 直接指向已加载的 dtb 内存 */
typedef struct {
    const uint8_t      *blob;
    uint32_t            blob_len;
    const dts_header_t *hdr;
    const dts_node_t   *nodes;
    const dts_prop_t   *props;
    const char         *strtab;
    dts_source_t        src;
} dts_ctx_t;

/* 固件内置兜底表(由 gen_dtb.py 生成到 app/User/app/dts/dts_fallback.c) */
extern const uint8_t  dts_fallback_blob[];
extern const uint32_t dts_fallback_blob_len;

/* ---- 加载 ---- */
/* 默认加载: flash 主槽 → 备份槽 → 内置静态表 */
void dts_load_default(void);
/* 从 SPI flash 指定物理地址加载一个 dtb 槽 */
int  dts_load_flash(uint32_t addr);
/* 绑定给定 blob(通常为内置静态表) */
int  dts_load_static(const uint8_t *blob, uint32_t len);

/* ---- 查询 ---- */
const dts_ctx_t     *dts_ctx(void);
dts_source_t         dts_source(void);
const char          *dts_model(void);              /* 根节点 model 属性 */
uint32_t             dts_version(void);            /* 格式版本 */
const char          *dts_source_str(void);         /* "flash"/"static"/"none" */
const dts_node_t    *dts_root(void);
/* 按名称查找: 精确匹配, 或 '@' 之前的基础名匹配, 或 label 属性匹配 */
const dts_node_t    *dts_find_by_name(const char *name);
const dts_node_t    *dts_child_first(const dts_node_t *parent);
const dts_node_t    *dts_child_next(const dts_node_t *parent, const dts_node_t *cur);
const dts_prop_t    *dts_prop_find(const dts_node_t *n, const char *key);
const char          *dts_prop_str(const dts_node_t *n, const char *key, const char *def);
int                  dts_prop_u32(const dts_node_t *n, const char *key, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _HAL_DTS_H */
