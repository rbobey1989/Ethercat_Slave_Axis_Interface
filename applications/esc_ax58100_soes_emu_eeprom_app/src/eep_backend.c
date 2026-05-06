#include "eep_backend.h"

#include "esc_eep.h"

#include <stdint.h>
#include <string.h>

#include "stm32f407xx.h"

#define EEP_SHADOW_MAX_SIZE           4096U
#define EEP_FLASH_MAGIC               0x31504545UL
#define EEP_FLASH_SLOT_SIZE           0x20000UL
#define EEP_FLASH_SLOT_A_BASE         0x080C0000UL
#define EEP_FLASH_SLOT_B_BASE         0x080E0000UL
#define EEP_FLASH_SECTOR_A            10U
#define EEP_FLASH_SECTOR_B            11U
#define EEP_FLASH_ERROR_MASK          (FLASH_SR_PGSERR | FLASH_SR_PGPERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR)
#define EEP_FLASH_KEY1                0x45670123UL
#define EEP_FLASH_KEY2                0xCDEF89ABUL

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t image_size;
    uint32_t crc32;
} eep_flash_header_t;

extern const uint8_t g_eep_default_image[];
extern const uint8_t g_eep_default_image_end[];

static uint8_t g_eep_shadow[EEP_SHADOW_MAX_SIZE];
static uint32_t g_eep_image_size;
static uint32_t g_eep_sequence;
static uint32_t g_eep_active_slot;
static uint8_t g_eep_initialized;
static uint8_t g_eep_dirty;

static uint32_t eep_default_image_size(void)
{
    return (uint32_t)(g_eep_default_image_end - g_eep_default_image);
}

static uint32_t eep_crc32(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (uint32_t bit = 0; bit < 8U; ++bit)
        {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

static int eep_flash_wait_ready(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0U)
    {
    }

    if ((FLASH->SR & EEP_FLASH_ERROR_MASK) != 0U)
    {
        FLASH->SR = EEP_FLASH_ERROR_MASK;
        return -1;
    }

    return 0;
}

static void eep_flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        FLASH->KEYR = EEP_FLASH_KEY1;
        FLASH->KEYR = EEP_FLASH_KEY2;
    }
}

static void eep_flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static int eep_flash_erase_sector(uint32_t sector)
{
    if (eep_flash_wait_ready() != 0)
    {
        return -1;
    }

    FLASH->CR &= ~(FLASH_CR_SNB | FLASH_CR_PSIZE);
    FLASH->CR |= FLASH_CR_SER | FLASH_CR_PSIZE_1 | (sector << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;

    if (eep_flash_wait_ready() != 0)
    {
        FLASH->CR &= ~FLASH_CR_SER;
        return -1;
    }

    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB | FLASH_CR_PSIZE);
    return 0;
}

static int eep_flash_program_word(uint32_t address, uint32_t value)
{
    if (eep_flash_wait_ready() != 0)
    {
        return -1;
    }

    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB | FLASH_CR_PSIZE);
    FLASH->CR |= FLASH_CR_PG | FLASH_CR_PSIZE_1;
    *(__IO uint32_t *)address = value;

    if (eep_flash_wait_ready() != 0)
    {
        FLASH->CR &= ~(FLASH_CR_PG | FLASH_CR_PSIZE);
        return -1;
    }

    FLASH->CR &= ~(FLASH_CR_PG | FLASH_CR_PSIZE);
    return (*(__IO uint32_t *)address == value) ? 0 : -1;
}

static int eep_flash_commit_slot(uint32_t slot_base, uint32_t sector)
{
    eep_flash_header_t header;
    uint32_t address;
    uint32_t primask;

    header.magic = EEP_FLASH_MAGIC;
    header.sequence = g_eep_sequence + 1U;
    header.image_size = g_eep_image_size;
    header.crc32 = eep_crc32(g_eep_shadow, g_eep_image_size);

    primask = __get_PRIMASK();
    __disable_irq();

    eep_flash_unlock();

    if (eep_flash_erase_sector(sector) != 0)
    {
        eep_flash_lock();
        __set_PRIMASK(primask);
        return -1;
    }

    address = slot_base;
    for (uint32_t index = 0; index < sizeof(header); index += sizeof(uint32_t))
    {
        uint32_t value;

        memcpy(&value, ((const uint8_t *)&header) + index, sizeof(uint32_t));
        if (eep_flash_program_word(address, value) != 0)
        {
            eep_flash_lock();
            __set_PRIMASK(primask);
            return -1;
        }
        address += sizeof(uint32_t);
    }

    for (uint32_t index = 0; index < g_eep_image_size; index += sizeof(uint32_t))
    {
        uint32_t value = 0xFFFFFFFFUL;
        uint32_t remaining = g_eep_image_size - index;

        if (remaining >= sizeof(uint32_t))
        {
            memcpy(&value, &g_eep_shadow[index], sizeof(uint32_t));
        }
        else
        {
            memcpy(&value, &g_eep_shadow[index], remaining);
        }

        if (eep_flash_program_word(address, value) != 0)
        {
            eep_flash_lock();
            __set_PRIMASK(primask);
            return -1;
        }
        address += sizeof(uint32_t);
    }

    eep_flash_lock();
    __set_PRIMASK(primask);

    g_eep_sequence = header.sequence;
    g_eep_active_slot = slot_base;
    g_eep_dirty = 0U;
    return 0;
}

static int eep_flash_validate_slot(uint32_t slot_base, uint32_t *sequence)
{
    const eep_flash_header_t *header = (const eep_flash_header_t *)slot_base;
    const uint8_t *data = (const uint8_t *)(slot_base + sizeof(eep_flash_header_t));

    if (header->magic != EEP_FLASH_MAGIC)
    {
        return -1;
    }

    if ((header->image_size == 0U)
        || (header->image_size > EEP_SHADOW_MAX_SIZE)
        || ((sizeof(eep_flash_header_t) + header->image_size) > EEP_FLASH_SLOT_SIZE))
    {
        return -1;
    }

    if (eep_crc32(data, header->image_size) != header->crc32)
    {
        return -1;
    }

    memcpy(g_eep_shadow, data, header->image_size);
    g_eep_image_size = header->image_size;
    *sequence = header->sequence;
    return 0;
}

static void eep_load_default_image(void)
{
    uint32_t image_size = eep_default_image_size();

    if ((image_size == 0U) || (image_size > EEP_SHADOW_MAX_SIZE))
    {
        g_eep_image_size = 0U;
        return;
    }

    memcpy(g_eep_shadow, g_eep_default_image, image_size);
    g_eep_image_size = image_size;
}

static int8_t eep_ensure_initialized(void)
{
    if (g_eep_initialized != 0U)
    {
        return 0;
    }

    EEP_init();
    return (g_eep_initialized != 0U) ? 0 : -1;
}

void EEP_init(void)
{
    uint32_t seq_a = 0U;
    uint32_t seq_b = 0U;
    int valid_a;
    int valid_b;

    g_eep_initialized = 0U;
    g_eep_dirty = 0U;
    g_eep_sequence = 0U;
    g_eep_active_slot = 0U;

    eep_load_default_image();
    if (g_eep_image_size == 0U)
    {
        return;
    }

    valid_a = eep_flash_validate_slot(EEP_FLASH_SLOT_A_BASE, &seq_a);
    eep_load_default_image();
    valid_b = eep_flash_validate_slot(EEP_FLASH_SLOT_B_BASE, &seq_b);

    if ((valid_a == 0) && ((valid_b != 0) || (seq_a >= seq_b)))
    {
        eep_flash_validate_slot(EEP_FLASH_SLOT_A_BASE, &seq_a);
        g_eep_sequence = seq_a;
        g_eep_active_slot = EEP_FLASH_SLOT_A_BASE;
    }
    else if (valid_b == 0)
    {
        eep_flash_validate_slot(EEP_FLASH_SLOT_B_BASE, &seq_b);
        g_eep_sequence = seq_b;
        g_eep_active_slot = EEP_FLASH_SLOT_B_BASE;
    }
    else
    {
        eep_load_default_image();
    }

    g_eep_initialized = 1U;
}

int8_t EEP_read(uint32_t addr, uint8_t *data, uint16_t size)
{
    if ((data == NULL) || (eep_ensure_initialized() != 0))
    {
        return -1;
    }

    if ((addr + (uint32_t)size) > g_eep_image_size)
    {
        return -1;
    }

    memcpy(data, &g_eep_shadow[addr], size);
    return 0;
}

int8_t EEP_write(uint32_t addr, uint8_t *data, uint16_t size)
{
    if ((data == NULL) || (eep_ensure_initialized() != 0))
    {
        return -1;
    }

    if ((addr + (uint32_t)size) > g_eep_image_size)
    {
        return -1;
    }

    memcpy(&g_eep_shadow[addr], data, size);
    g_eep_dirty = 1U;
    return 0;
}

void eep_backend_init(void)
{
    EEP_init();
}

void eep_backend_event_handler(void)
{
    EEP_process();

    if (g_eep_dirty != 0U)
    {
        uint32_t next_slot = (g_eep_active_slot == EEP_FLASH_SLOT_A_BASE) ? EEP_FLASH_SLOT_B_BASE : EEP_FLASH_SLOT_A_BASE;
        uint32_t next_sector = (g_eep_active_slot == EEP_FLASH_SLOT_A_BASE) ? EEP_FLASH_SECTOR_B : EEP_FLASH_SECTOR_A;

        if (g_eep_active_slot == 0U)
        {
            next_slot = EEP_FLASH_SLOT_A_BASE;
            next_sector = EEP_FLASH_SECTOR_A;
        }

        (void)eep_flash_commit_slot(next_slot, next_sector);
    }
}

const uint8_t *eep_backend_get_image(uint32_t *size)
{
    if (size != NULL)
    {
        *size = 0U;
    }

    if (eep_ensure_initialized() != 0)
    {
        return NULL;
    }

    if (size != NULL)
    {
        *size = g_eep_image_size;
    }

    return g_eep_shadow;
}
