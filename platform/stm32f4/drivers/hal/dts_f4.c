/***************************************************************
 * @file    :  dts_f4.c
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2026-08-30
 * @brief   :  板级设备树解析库(零 malloc, 纯指针运算)
 *
 * @note    :
 *   dtb 由 tools/pack/gen_dtb.py 编译 dts 生成, 格式见 platform/hal/dts.h。
 *   加载优先级: SPI flash 主槽 → 备份槽 → 固件内置静态表(兜底)。
 *   crc32 复用 common/crc.c crc32_checksum(zlib 兼容), 与 Python 侧一致。
 ***************************************************************/
#include <string.h>
#include <stddef.h>
#include "dts.h"
#include "hal_flash.h"
#include "crc.h"
#include "os_debug.h"

/* 从 flash 读出的 dtb 暂存区(flash 不可寻址, 需搬进 RAM) */
static uint8_t s_blob[DTS_FLASH_SLOT_MAX];
static dts_ctx_t s_ctx;


/*****************************************************
 * @fn       dts_crc16
 * @brief    CRC16/CCITT-FALSE(init=0xFFFF, poly=0x1021)
 *****************************************************/
static uint16_t dts_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;

    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}


/*****************************************************
 * @fn       dts_validate
 * @brief    校验 dtb 完整性与版本, 返回 0 有效
 *****************************************************/
static int dts_validate(const uint8_t *blob, uint32_t len)
{
    const dts_header_t *h = (const dts_header_t *)blob;
    uint32_t body_crc;

    if (blob == NULL || len < sizeof(dts_header_t)) {
        return -1;
    }
    if (h->magic != DTS_MAGIC || h->version != DTS_VERSION) {
        return -1;
    }
    if (h->body_len < sizeof(dts_header_t) || h->body_len > DTS_FLASH_SLOT_MAX) {
        return -1;
    }
    if (h->body_len > len) {
        return -1;
    }
    /* 头部 CRC(前 28B, 不含 hdr_crc/reserved) */
    if (dts_crc16(blob, 28) != h->hdr_crc) {
        return -1;
    }
    /* 数据区 CRC32(头部之后), 与 gen_dtb.py 的 zlib.crc32 一致 */
    body_crc = crc32_checksum(blob + sizeof(dts_header_t), h->body_len - sizeof(dts_header_t));
    if (body_crc != h->body_crc32) {
        return -1;
    }
    return 0;
}


/*****************************************************
 * @fn       dts_bind
 * @brief    绑定已校验的 blob 到上下文(零拷贝)
 *****************************************************/
static int dts_bind(const uint8_t *blob, uint32_t len, dts_source_t src)
{
    const dts_header_t *h;

    if (dts_validate(blob, len) != 0) {
        return -1;
    }
    h = (const dts_header_t *)blob;

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.blob     = blob;
    s_ctx.blob_len = len;
    s_ctx.hdr      = h;
    s_ctx.nodes    = (const dts_node_t *)(blob + sizeof(dts_header_t));
    s_ctx.props    = (const dts_prop_t *)(blob + sizeof(dts_header_t)
                                          + (size_t)h->node_cnt * sizeof(dts_node_t));
    s_ctx.strtab   = (const char *)(blob + h->strtab_off);
    s_ctx.src      = src;
    return 0;
}


/*****************************************************
 * @fn       dts_load_flash
 * @brief    从 SPI flash 物理地址读一个 dtb 槽并校验
 *****************************************************/
int dts_load_flash(uint32_t addr)
{
    dts_header_t hdr;
    uint32_t body_len;

    hal_spi_flash_read(addr, (uint8_t *)&hdr, sizeof(hdr));
    if (hdr.magic != DTS_MAGIC) {
        return -1;
    }
    body_len = hdr.body_len;
    if (body_len < sizeof(dts_header_t) || body_len > DTS_FLASH_SLOT_MAX) {
        return -1;
    }
    hal_spi_flash_read(addr, s_blob, body_len);
    return dts_bind(s_blob, body_len, DTS_SRC_FLASH);
}


/*****************************************************
 * @fn       dts_load_static
 * @brief    绑定固件内置静态表
 *****************************************************/
int dts_load_static(const uint8_t *blob, uint32_t len)
{
    return dts_bind(blob, len, DTS_SRC_STATIC);
}


/*****************************************************
 * @fn       dts_write_slot
 * @brief    把当前绑定的 blob 写入指定 flash 槽(读回比对一致则跳过)
 * @note     仅在两个槽都失效时的自愈路径调用, 不覆盖有效数据
 *****************************************************/
static void dts_write_slot(uint32_t addr)
{
    uint32_t i;
    uint32_t chunk;
    uint8_t cmp_buf[64];
    int need_write = 0;

    if (s_ctx.blob == NULL || s_ctx.blob_len == 0) {
        return;
    }
    /* 读回比对, 一致则跳过(避免无谓擦写) */
    for (i = 0; i < s_ctx.blob_len; i += sizeof(cmp_buf)) {
        chunk = s_ctx.blob_len - i;
        if (chunk > sizeof(cmp_buf)) {
            chunk = sizeof(cmp_buf);
        }
        hal_spi_flash_read(addr + i, cmp_buf, chunk);
        if (memcmp(cmp_buf, s_ctx.blob + i, chunk) != 0) {
            need_write = 1;
            break;
        }
    }
    if (!need_write) {
        return;
    }

    hal_spi_flash_erase_sector(addr);
    hal_spi_flash_write(addr, (uint8_t *)s_ctx.blob, s_ctx.blob_len);
    os_debug("dts: 自愈写入槽 0x%08lx (%luB)\n",
             (unsigned long)addr, (unsigned long)s_ctx.blob_len);
}


/*****************************************************
 * @fn       dts_load_default
 * @brief    默认加载: flash 主槽 → 备份槽 → 内置静态表
 * @note     主槽失效时用备份槽自愈主槽; 双槽失效时静态兜底并自愈主槽
 *****************************************************/
void dts_load_default(void)
{
    if (dts_load_flash(DTS_FLASH_PRIMARY) == 0) {
        return;
    }
    if (dts_load_flash(DTS_FLASH_BACKUP) == 0) {
        os_debug("dts: 主槽无效, 使用备份槽\n");
        dts_write_slot(DTS_FLASH_PRIMARY);   /* 备份自愈主槽 */
        return;
    }
    if (dts_load_static(dts_fallback_blob, dts_fallback_blob_len) == 0) {
        os_debug("dts: flash dtb 均无效, 回落内置静态表\n");
        dts_write_slot(DTS_FLASH_PRIMARY);   /* 首次上电自愈 */
        return;
    }
    os_debug("dts: 加载失败\n");
}


/*****************************************************
 * 查询接口
 *****************************************************/
const dts_ctx_t *dts_ctx(void)
{
    return &s_ctx;
}

dts_source_t dts_source(void)
{
    return s_ctx.src;
}

const char *dts_source_str(void)
{
    switch (s_ctx.src) {
    case DTS_SRC_FLASH:  return "flash";
    case DTS_SRC_STATIC: return "static";
    default:             return "none";
    }
}

uint32_t dts_version(void)
{
    return s_ctx.hdr ? s_ctx.hdr->version : 0;
}

const char *dts_model(void)
{
    return dts_prop_str(dts_root(), "model", "");
}

const dts_node_t *dts_root(void)
{
    return (s_ctx.hdr && s_ctx.hdr->node_cnt > 0) ? &s_ctx.nodes[0] : NULL;
}

static uint16_t dts_node_index(const dts_node_t *n)
{
    ptrdiff_t d;

    if (n == NULL || s_ctx.hdr == NULL) {
        return 0xFFFF;
    }
    d = n - s_ctx.nodes;
    if (d < 0 || d >= s_ctx.hdr->node_cnt) {
        return 0xFFFF;
    }
    return (uint16_t)d;
}

const dts_node_t *dts_find_by_name(const char *name)
{
    size_t namelen;
    uint16_t i;

    if (s_ctx.hdr == NULL || name == NULL) {
        return NULL;
    }
    namelen = strlen(name);

    for (i = 0; i < s_ctx.hdr->node_cnt; i++) {
        const dts_node_t *n = &s_ctx.nodes[i];
        const char *nm = s_ctx.strtab + n->name_off;

        /* 精确匹配 */
        if (strcmp(nm, name) == 0) {
            return n;
        }
        /* 基础名匹配: 忽略 '@addr' 后缀 */
        {
            const char *at = strchr(nm, '@');
            if (at && (size_t)(at - nm) == namelen && strncmp(nm, name, namelen) == 0) {
                return n;
            }
        }
        /* label 属性匹配 */
        {
            const dts_prop_t *lp = dts_prop_find(n, "label");
            if (lp && lp->type == DTS_PROP_STR &&
                strcmp(s_ctx.strtab + lp->val_off, name) == 0) {
                return n;
            }
        }
    }
    return NULL;
}

const dts_node_t *dts_child_first(const dts_node_t *parent)
{
    uint16_t pidx = dts_node_index(parent);
    uint16_t i;

    if (pidx == 0xFFFF || s_ctx.hdr == NULL) {
        return NULL;
    }
    for (i = 1; i < s_ctx.hdr->node_cnt; i++) {
        if (s_ctx.nodes[i].parent == pidx) {
            return &s_ctx.nodes[i];
        }
    }
    return NULL;
}

const dts_node_t *dts_child_next(const dts_node_t *parent, const dts_node_t *cur)
{
    uint16_t pidx = dts_node_index(parent);
    uint16_t start;
    uint16_t i;

    if (pidx == 0xFFFF || cur == NULL || s_ctx.hdr == NULL) {
        return NULL;
    }
    start = dts_node_index(cur);
    if (start == 0xFFFF) {
        return NULL;
    }
    for (i = start + 1; i < s_ctx.hdr->node_cnt; i++) {
        if (s_ctx.nodes[i].parent == pidx) {
            return &s_ctx.nodes[i];
        }
    }
    return NULL;
}

const dts_prop_t *dts_prop_find(const dts_node_t *n, const char *key)
{
    uint16_t i;

    if (n == NULL || key == NULL || s_ctx.hdr == NULL) {
        return NULL;
    }
    for (i = 0; i < n->prop_cnt; i++) {
        const dts_prop_t *p = &s_ctx.props[n->prop_start + i];
        if (strcmp(s_ctx.strtab + p->key_off, key) == 0) {
            return p;
        }
    }
    return NULL;
}

const char *dts_prop_str(const dts_node_t *n, const char *key, const char *def)
{
    const dts_prop_t *p = dts_prop_find(n, key);

    if (p == NULL || p->type != DTS_PROP_STR) {
        return def;
    }
    return s_ctx.strtab + p->val_off;
}

int dts_prop_u32(const dts_node_t *n, const char *key, uint32_t *out)
{
    const dts_prop_t *p = dts_prop_find(n, key);

    if (p == NULL || p->type != DTS_PROP_U32) {
        return -1;
    }
    *out = p->val_off;
    return 0;
}

/* end */
