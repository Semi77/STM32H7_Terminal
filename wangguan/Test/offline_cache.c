#include "offline_cache.h"
#include "w25q64_qspi.h"
#include "memory_layout.h"
#include <stddef.h>
#include <string.h>

/* 后4MB中前两个扇区保存序号预留日志，其余扇区保存32字节数据记录。 */
#define CACHE_BASE EXT_CACHE_BASE
#define DATA_BASE (CACHE_BASE + 8192U)
#define DATA_END EXT_FLASH_END
#define SECTOR_SIZE 4096U
#define RECORD_SIZE 32U
#define SECTOR_COUNT ((DATA_END - DATA_BASE) / SECTOR_SIZE)
#define RECORD_MAGIC 0x48474331U
#define JOURNAL_MAGIC 0x48474931U
#define COMMITTED 0xFFFFFFFEU
#define CONSUMED 0xFFFFFFFCU
#define ID_BLOCK 65536U

typedef struct {
  uint32_t magic;
  GatewaySample sample;
  uint32_t crc;
  uint32_t state;
} Record;
typedef struct {
  uint32_t magic, limit, reserved[4], crc, state;
} Journal;
_Static_assert(sizeof(Record) == RECORD_SIZE, "record size");
_Static_assert(sizeof(Journal) == RECORD_SIZE, "journal size");
static uint16_t sector_pending[SECTOR_COUNT];
static uint32_t head, tail, pending, overwritten, next_id, id_limit, journal_addr;
static bool ready;
static uint8_t scan_buffer[SECTOR_SIZE];
volatile uint32_t g_offline_cache_scan_sectors = UINT32_MAX;

/**
  * @brief 计算data前length字节的CRC32，检查掉电半写和数据损坏。
  * @retval CRC32值。
  */
static uint32_t crc32(const void *data, uint32_t length)
{
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;
  for (uint32_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (uint32_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
  }
  return ~crc;
}

/**
  * @brief 判断record是否为完整提交且CRC正确的数据记录。
  * @retval true表示有效。
  */
static bool valid(const Record *record)
{
  return record->magic == RECORD_MAGIC &&
    (record->state == COMMITTED || record->state == CONSUMED) &&
    record->crc == crc32(record, offsetof(Record, crc));
}

/**
  * @brief 将address推进一个记录位置，到分区末尾时回绕。
  * @retval 下一记录地址。
  */
static uint32_t advance(uint32_t address)
{
  address += RECORD_SIZE;
  return address >= DATA_END ? DATA_BASE : address;
}

/**
  * @brief 寻找当前head之后的有效队首，跳过空扇区和损坏记录。
  * @retval true表示找到，false表示无记录或读取失败。
  */
static bool seek_head(void)
{
  for (uint32_t visited = 0; pending && visited < (DATA_END-DATA_BASE)/RECORD_SIZE; ++visited) {
    uint32_t sector = (head-DATA_BASE)/SECTOR_SIZE;
    if (!sector_pending[sector]) {
      head = DATA_BASE + ((sector+1U)%SECTOR_COUNT)*SECTOR_SIZE;
      continue;
    }
    Record record;
    if (W25Q64_ReadData(head, (uint8_t *)&record, sizeof(record)) != HAL_OK) return false;
    if (valid(&record) && record.state == COMMITTED) return true;
    head = advance(head);
  }
  return false;
}

/**
  * @brief 将data的前28字节写入address后单独提交状态，避免半写记录被使用。
  * @retval true表示两阶段写入均成功。
  */
static bool commit(uint32_t address, const void *data)
{
  uint32_t state = COMMITTED;
  return W25Q64_PageProgram(address, data, 28) == HAL_OK &&
    W25Q64_PageProgram(address+28U, (const uint8_t *)&state, 4) == HAL_OK;
}

/**
  * @brief 在双扇区日志预留下一段序号，旧日志保留到新记录提交完成。
  * @retval true表示成功，false表示写失败或序号耗尽。
  */
static bool reserve_ids(void)
{
  if (id_limit > UINT32_MAX-ID_BLOCK) return false;
  uint32_t address = journal_addr == UINT32_MAX ? CACHE_BASE : journal_addr+RECORD_SIZE;
  if (address >= DATA_BASE) address = CACHE_BASE;
  if (address % SECTOR_SIZE == 0 && W25Q64_SectorErase(address) != HAL_OK) return false;
  Journal entry;
  memset(&entry, 0xFF, sizeof(entry));
  entry.magic = JOURNAL_MAGIC;
  entry.limit = id_limit+ID_BLOCK;
  entry.crc = crc32(&entry, offsetof(Journal, crc));
  /* 写失败后停用本次实例，重启扫描会跳过半写日志。 */
  if (!commit(address, &entry)) { ready = false; return false; }
  next_id = id_limit+1U;
  id_limit = entry.limit;
  journal_addr = address;
  return true;
}

bool OfflineCache_Init(void)
{
  g_offline_cache_scan_sectors = 0U;
  ready = false;
  pending = overwritten = id_limit = 0;
  head = tail = DATA_BASE;
  journal_addr = UINT32_MAX;
  memset(sector_pending, 0, sizeof(sector_pending));
  /* 扫描序号日志；记录之后的半写槽位也必须跳过，不能直接覆盖编程。 */
  for (uint32_t sector = CACHE_BASE; sector < DATA_BASE; sector += SECTOR_SIZE) {
    if (W25Q64_ReadData(sector, scan_buffer, sizeof(scan_buffer)) != HAL_OK) {
      g_offline_cache_scan_sectors = UINT32_MAX;
      return false;
    }
    for (uint32_t offset = 0; offset < SECTOR_SIZE; offset += RECORD_SIZE) {
      Journal entry;
      memcpy(&entry, scan_buffer+offset, sizeof(entry));
      if (entry.magic == JOURNAL_MAGIC && entry.state == COMMITTED &&
          entry.crc == crc32(&entry, offsetof(Journal, crc)) && entry.limit > id_limit) {
        id_limit = entry.limit;
        journal_addr = sector+offset;
      }
    }
  }
  if (journal_addr != UINT32_MAX) {
    uint32_t address = journal_addr+RECORD_SIZE;
    while (address % SECTOR_SIZE != 0) {
      Journal entry;
      if (W25Q64_ReadData(address, (uint8_t *)&entry, sizeof(entry)) != HAL_OK) {
        g_offline_cache_scan_sectors = UINT32_MAX;
        return false;
      }
      bool erased = true;
      for (uint32_t i=0; i<sizeof(entry); ++i) if (((uint8_t *)&entry)[i] != 0xFF) erased=false;
      if (erased) break;
      journal_addr = address;
      address += RECORD_SIZE;
    }
  }
  uint32_t newest = 0, oldest = UINT32_MAX;
  for (uint32_t sector = 0; sector < SECTOR_COUNT; ++sector) {
    uint32_t base = DATA_BASE+sector*SECTOR_SIZE;
    if (W25Q64_ReadData(base, scan_buffer, sizeof(scan_buffer)) != HAL_OK) {
      g_offline_cache_scan_sectors = UINT32_MAX;
      return false;
    }
    for (uint32_t offset = 0; offset < SECTOR_SIZE; offset += RECORD_SIZE) {
      Record record;
      memcpy(&record, scan_buffer+offset, sizeof(record));
      if (!valid(&record)) continue;
      if (record.sample.sequence > newest) {
        newest = record.sample.sequence;
        tail = advance(base+offset);
      }
      if (record.state == COMMITTED) {
        ++pending; ++sector_pending[sector];
        if (record.sample.sequence < oldest) { oldest=record.sample.sequence; head=base+offset; }
      }
    }
    /* 每完成一个扇区就发布进度，供看门狗任务判断扫描是否仍在推进。 */
    g_offline_cache_scan_sectors = sector + 1U;
  }
  /* 序号日志损坏时不冒险复用已有记录的序号。 */
  if (newest > id_limit || !reserve_ids()) {
    g_offline_cache_scan_sectors = UINT32_MAX;
    return false;
  }
  ready = true;
  g_offline_cache_scan_sectors = UINT32_MAX;
  return true;
}

bool OfflineCache_NextSequence(uint32_t *sequence)
{
  if (!ready || !sequence) return false;
  if (next_id > id_limit && !reserve_ids()) return false;
  *sequence = next_id++;
  return true;
}

bool OfflineCache_Append(const GatewaySample *sample)
{
  if (!ready || !sample) return false;
  /* 跳过掉电留下的半写槽位，只有到达扇区边界才擦除。 */
  while (tail % SECTOR_SIZE != 0) {
    Record old;
    if (W25Q64_ReadData(tail, (uint8_t *)&old, sizeof(old)) != HAL_OK) return false;
    bool erased = true;
    for (uint32_t i=0; i<sizeof(old); ++i) if (((uint8_t *)&old)[i] != 0xFF) erased=false;
    if (erased) break;
    tail = advance(tail);
  }
  uint32_t sector = (tail-DATA_BASE)/SECTOR_SIZE;
  if (tail % SECTOR_SIZE == 0) {
    if (W25Q64_SectorErase(tail) != HAL_OK) { ready=false; return false; }
    overwritten += sector_pending[sector];
    pending -= sector_pending[sector];
    sector_pending[sector] = 0;
    if (pending && (head-DATA_BASE)/SECTOR_SIZE == sector) {
      head = DATA_BASE + ((sector+1U)%SECTOR_COUNT)*SECTOR_SIZE;
      if (!seek_head()) { ready=false; return false; }
    }
  }
  Record record = {RECORD_MAGIC, *sample, 0, UINT32_MAX};
  record.crc = crc32(&record, offsetof(Record, crc));
  uint32_t address = tail;
  if (!commit(address, &record)) { ready=false; return false; }
  tail = advance(tail);
  if (!pending) head = address;
  ++pending; ++sector_pending[sector];
  return true;
}

bool OfflineCache_Peek(GatewaySample *sample)
{
  if (!ready || !sample || !pending || !seek_head()) return false;
  Record record;
  if (W25Q64_ReadData(head, (uint8_t *)&record, sizeof(record)) != HAL_OK) return false;
  *sample = record.sample;
  return true;
}

bool OfflineCache_Ack(uint32_t sequence)
{
  GatewaySample sample;
  if (!OfflineCache_Peek(&sample) || sample.sequence != sequence) return false;
  uint32_t state = CONSUMED;
  if (W25Q64_PageProgram(head+28U, (uint8_t *)&state, sizeof(state)) != HAL_OK) return false;
  --pending;
  --sector_pending[(head-DATA_BASE)/SECTOR_SIZE];
  head = advance(head);
  return true;
}

uint32_t OfflineCache_Count(void) { return pending; }
uint32_t OfflineCache_Overwritten(void) { return overwritten; }
