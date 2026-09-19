#include "w25q64_qspi.h"

#define W25Q64_CMD_WRITE_ENABLE          0x06U
#define W25Q64_CMD_READ_STATUS_1         0x05U
#define W25Q64_CMD_READ_STATUS_2         0x35U
#define W25Q64_CMD_WRITE_STATUS_2        0x31U
#define W25Q64_CMD_QUAD_PAGE_PROGRAM     0x32U
#define W25Q64_CMD_SECTOR_ERASE_4KB      0x20U
#define W25Q64_CMD_JEDEC_ID             	 0x9FU
#define W25Q64_CMD_FAST_READ_QUAD_OUTPUT 0x6BU
#define W25Q64_CMD_ENABLE_RESET          0x66U
#define W25Q64_CMD_RESET_DEVICE          0x99U

#define W25Q64_STATUS_BUSY_MASK          0x01U
#define W25Q64_STATUS_WEL_MASK           0x02U
#define W25Q64_STATUS_QE_MASK            0x02U

#define W25Q64_MANUFACTURER_ID           0xEFU
#define W25Q64_DEVICE_ID                 0x4017U

#define W25Q64_COMMAND_TIMEOUT_MS        100U
#define W25Q64_READ_TIMEOUT_MS           10000U
#define W25Q64_PAGE_PROGRAM_TIMEOUT_MS   3000U
#define W25Q64_SECTOR_ERASE_TIMEOUT_MS   5000U
#define W25Q64_RESET_DELAY_MS            1U
#define W25Q64_QUAD_READ_DUMMY_CYCLES    8U
#define W25Q64_POLLING_INTERVAL          0x10U

static QSPI_HandleTypeDef *w25q64_qspi_handle;

/**
  * @brief 填充每条W25Q64命令共同使用的QSPI参数。
  * @param command 指向待初始化的QSPI命令结构体。
  * @retval 无。
  */
static void W25Q64_PrepareCommand(QSPI_CommandTypeDef *command)
{
  command->Instruction = 0U;
  command->Address = 0U;
  command->AlternateBytes = 0U;
  command->AddressSize = QSPI_ADDRESS_24_BITS;
  command->AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
  command->DummyCycles = 0U;
  command->InstructionMode = QSPI_INSTRUCTION_1_LINE;
  command->AddressMode = QSPI_ADDRESS_NONE;
  command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  command->DataMode = QSPI_DATA_NONE;
  command->NbData = 0U;
  command->DdrMode = QSPI_DDR_MODE_DISABLE;
  command->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
  command->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
}

/**
  * @brief 发送一条不包含地址和数据阶段的W25Q64指令。
  * @param instruction 待发送的8位指令码。
  * @retval HAL状态，HAL_OK表示指令发送成功。
  */
static HAL_StatusTypeDef W25Q64_SendSimpleCommand(uint8_t instruction)
{
  QSPI_CommandTypeDef command;

  W25Q64_PrepareCommand(&command);
  command.Instruction = instruction;

  return HAL_QSPI_Command(w25q64_qspi_handle,
                          &command,
                          W25Q64_COMMAND_TIMEOUT_MS);
}

/**
  * @brief 自动轮询状态寄存器1中的指定位直到匹配目标值。
  * @param match 状态位需要匹配的目标值。
  * @param mask 参与比较的状态位掩码。
  * @param timeout 等待超时时间，单位为毫秒。
  * @retval HAL状态，HAL_OK表示状态位匹配成功。
  */
static HAL_StatusTypeDef W25Q64_PollStatus1(uint8_t match,
                                           uint8_t mask,
                                           uint32_t timeout)
{
  QSPI_CommandTypeDef command;
  QSPI_AutoPollingTypeDef polling;

  W25Q64_PrepareCommand(&command);
  command.Instruction = W25Q64_CMD_READ_STATUS_1;
  command.DataMode = QSPI_DATA_1_LINE;
  command.NbData = 1U;

  polling.Match = match;
  polling.Mask = mask;
  polling.Interval = W25Q64_POLLING_INTERVAL;
  polling.StatusBytesSize = 1U;
  polling.MatchMode = QSPI_MATCH_MODE_AND;
  polling.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

  return HAL_QSPI_AutoPolling(w25q64_qspi_handle,
                              &command,
                              &polling,
                              timeout);
}

/**
  * @brief 发送写使能指令并确认状态寄存器的WEL位置位。
  * @param 无。
  * @retval HAL状态，HAL_OK表示写使能成功。
  */
static HAL_StatusTypeDef W25Q64_WriteEnable(void)
{
  HAL_StatusTypeDef status;

  status = W25Q64_SendSimpleCommand(W25Q64_CMD_WRITE_ENABLE);
  if (status != HAL_OK)
  {
    return status;
  }

  return W25Q64_PollStatus1(W25Q64_STATUS_WEL_MASK,
                            W25Q64_STATUS_WEL_MASK,
                            W25Q64_COMMAND_TIMEOUT_MS);
}

/**
  * @brief 读取W25Q64状态寄存器2的当前值。
  * @param status2 用于返回状态寄存器2数值的指针。
  * @retval HAL状态，HAL_OK表示读取成功。
  */
static HAL_StatusTypeDef W25Q64_ReadStatus2(uint8_t *status2)
{
  QSPI_CommandTypeDef command;
  HAL_StatusTypeDef status;

  if (status2 == NULL)
  {
    return HAL_ERROR;
  }

  W25Q64_PrepareCommand(&command);
  command.Instruction = W25Q64_CMD_READ_STATUS_2;
  command.DataMode = QSPI_DATA_1_LINE;
  command.NbData = 1U;

  status = HAL_QSPI_Command(w25q64_qspi_handle,
                            &command,
                            W25Q64_COMMAND_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  return HAL_QSPI_Receive(w25q64_qspi_handle,
                          status2,
                          W25Q64_COMMAND_TIMEOUT_MS);
}

/**
  * @brief 设置状态寄存器2的QE位以启用IO0至IO3四线数据传输。
  * @param 无。
  * @retval HAL状态，HAL_OK表示QE位已经置位。
  */
static HAL_StatusTypeDef W25Q64_EnableQuadMode(void)
{
  QSPI_CommandTypeDef command;
  HAL_StatusTypeDef status;
  uint8_t status2;

  status = W25Q64_ReadStatus2(&status2);
  if (status != HAL_OK)
  {
    return status;
  }

  if ((status2 & W25Q64_STATUS_QE_MASK) != 0U)
  {
    return HAL_OK;
  }

  status = W25Q64_WriteEnable();
  if (status != HAL_OK)
  {
    return status;
  }

  status2 |= W25Q64_STATUS_QE_MASK;
  W25Q64_PrepareCommand(&command);
  command.Instruction = W25Q64_CMD_WRITE_STATUS_2;
  command.DataMode = QSPI_DATA_1_LINE;
  command.NbData = 1U;

  status = HAL_QSPI_Command(w25q64_qspi_handle,
                            &command,
                            W25Q64_COMMAND_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  status = HAL_QSPI_Transmit(w25q64_qspi_handle,
                             &status2,
                             W25Q64_COMMAND_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  status = W25Q64_WaitBusy(W25Q64_PAGE_PROGRAM_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  status = W25Q64_ReadStatus2(&status2);
  if ((status != HAL_OK) || ((status2 & W25Q64_STATUS_QE_MASK) == 0U))
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

/**
  * @brief 初始化W25Q64并校验JEDEC ID及开启四线模式。
  * @param hqspi 指向连接W25Q64的QSPI句柄。
  * @retval HAL状态，HAL_OK表示芯片识别和四线模式配置成功。
  */
HAL_StatusTypeDef W25Q64_Init(QSPI_HandleTypeDef *hqspi)
{
  HAL_StatusTypeDef status;
  uint8_t manufacturer_id;
  uint16_t device_id;

  if ((hqspi == NULL) || (hqspi->Instance != QUADSPI))
  {
    return HAL_ERROR;
  }

  w25q64_qspi_handle = hqspi;

  status = W25Q64_SendSimpleCommand(W25Q64_CMD_ENABLE_RESET);
  if (status != HAL_OK)
  {
    return status;
  }

  status = W25Q64_SendSimpleCommand(W25Q64_CMD_RESET_DEVICE);
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(W25Q64_RESET_DELAY_MS);

  status = W25Q64_ReadID(&manufacturer_id, &device_id);
  if (status != HAL_OK)
  {
    return status;
  }

  if ((manufacturer_id != W25Q64_MANUFACTURER_ID) ||
      (device_id != W25Q64_DEVICE_ID))
  {
    return HAL_ERROR;
  }

  return W25Q64_EnableQuadMode();
}

/**
  * @brief 读取W25Q64的制造商和器件ID。
  * @param manufacturer_id 用于返回8位制造商ID的指针。
  * @param device_id 用于返回16位存储类型和容量ID的指针。
  * @retval HAL状态，HAL_OK表示读取成功。
  */
HAL_StatusTypeDef W25Q64_ReadID(uint8_t *manufacturer_id, uint16_t *device_id)
{
  HAL_StatusTypeDef status;
  uint8_t manufacturer_id_array[1];
  uint8_t memory_type_id_array[1];
  uint8_t capacity_id_array[1];

  if ((w25q64_qspi_handle == NULL) ||
      (manufacturer_id == NULL) ||
      (device_id == NULL))
  {
    return HAL_ERROR;
  }

  status = W25Q64_ReadJedecID(manufacturer_id_array,
                              memory_type_id_array,
                              capacity_id_array);
  if (status != HAL_OK)
  {
    return status;
  }

  *manufacturer_id = manufacturer_id_array[0];
  *device_id = ((uint16_t)memory_type_id_array[0] << 8U) |
               capacity_id_array[0];

  return HAL_OK;
}

/**
  * @brief 按JEDEC顺序读取制造商、存储器类型和容量ID。
  * @param manufacturer_id 用于保存制造商ID的1字节数组。
  * @param memory_type_id 用于保存存储器类型ID的1字节数组。
  * @param capacity_id 用于保存容量ID的1字节数组。
  * @retval HAL状态，HAL_OK表示三个ID均读取成功。
  */
HAL_StatusTypeDef W25Q64_ReadJedecID(uint8_t manufacturer_id[1],
                                    uint8_t memory_type_id[1],
                                    uint8_t capacity_id[1])
{
  QSPI_CommandTypeDef command;
  HAL_StatusTypeDef status;
  uint8_t id[3];

  if ((w25q64_qspi_handle == NULL) ||
      (manufacturer_id == NULL) ||
      (memory_type_id == NULL) ||
      (capacity_id == NULL))
  {
    return HAL_ERROR;
  }

  W25Q64_PrepareCommand(&command);
  command.Instruction = W25Q64_CMD_JEDEC_ID;
  command.DataMode = QSPI_DATA_1_LINE;
  command.NbData = sizeof(id);

  status = HAL_QSPI_Command(w25q64_qspi_handle,
                            &command,
                            W25Q64_COMMAND_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  status = HAL_QSPI_Receive(w25q64_qspi_handle,
                            id,
                            W25Q64_COMMAND_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  manufacturer_id[0] = id[0];
  memory_type_id[0] = id[1];
  capacity_id[0] = id[2];

  return HAL_OK;
}

/**
  * @brief 自动轮询状态寄存器并等待W25Q64结束内部操作。
  * @param timeout 等待超时时间，单位为毫秒。
  * @retval HAL状态，HAL_OK表示BUSY位已经清零。
  */
HAL_StatusTypeDef W25Q64_WaitBusy(uint32_t timeout)
{
  if (w25q64_qspi_handle == NULL)
  {
    return HAL_ERROR;
  }

  return W25Q64_PollStatus1(0U, W25Q64_STATUS_BUSY_MASK, timeout);
}

/**
  * @brief 使用四线数据模式在单个页内编程最多256字节。
  * @param address 页内写入起始地址，范围为0x000000至0x7FFFFF。
  * @param data 指向待写入数据的缓冲区。
  * @param count 待写入字节数，范围为0至256且不能跨页。
  * @retval HAL状态，HAL_OK表示编程完成。
  */
HAL_StatusTypeDef W25Q64_PageProgram(uint32_t address,
                                    const uint8_t *data,
                                    uint16_t count)
{
  QSPI_CommandTypeDef command;
  HAL_StatusTypeDef status;

  if (count == 0U)
  {
    return HAL_OK;
  }

  if ((w25q64_qspi_handle == NULL) ||
      (data == NULL) ||
      (address >= W25Q64_FLASH_SIZE_BYTES) ||
      (count > W25Q64_PAGE_SIZE_BYTES) ||
      ((uint32_t)count > (W25Q64_FLASH_SIZE_BYTES - address)) ||
      (((address & (W25Q64_PAGE_SIZE_BYTES - 1U)) + count) > W25Q64_PAGE_SIZE_BYTES))
  {
    return HAL_ERROR;
  }

  status = W25Q64_WriteEnable();
  if (status != HAL_OK)
  {
    return status;
  }

  W25Q64_PrepareCommand(&command);
  command.Instruction = W25Q64_CMD_QUAD_PAGE_PROGRAM;
  command.Address = address;
  command.AddressMode = QSPI_ADDRESS_1_LINE;
  command.DataMode = QSPI_DATA_4_LINES;
  command.NbData = count;

  status = HAL_QSPI_Command(w25q64_qspi_handle,
                            &command,
                            W25Q64_COMMAND_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  status = HAL_QSPI_Transmit(w25q64_qspi_handle,
                             (uint8_t *)data,
                             W25Q64_COMMAND_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  return W25Q64_WaitBusy(W25Q64_PAGE_PROGRAM_TIMEOUT_MS);
}

/**
  * @brief 擦除指定地址所在的4KB扇区。
  * @param address 扇区内任意地址，范围为0x000000至0x7FFFFF。
  * @retval HAL状态，HAL_OK表示擦除完成。
  */
HAL_StatusTypeDef W25Q64_SectorErase(uint32_t address)
{
  QSPI_CommandTypeDef command;
  HAL_StatusTypeDef status;

  if ((w25q64_qspi_handle == NULL) ||
      (address >= W25Q64_FLASH_SIZE_BYTES))
  {
    return HAL_ERROR;
  }

  status = W25Q64_WriteEnable();
  if (status != HAL_OK)
  {
    return status;
  }

  W25Q64_PrepareCommand(&command);
  command.Instruction = W25Q64_CMD_SECTOR_ERASE_4KB;
  command.Address = address & ~(W25Q64_SECTOR_SIZE_BYTES - 1U);
  command.AddressMode = QSPI_ADDRESS_1_LINE;

  status = HAL_QSPI_Command(w25q64_qspi_handle,
                            &command,
                            W25Q64_COMMAND_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  return W25Q64_WaitBusy(W25Q64_SECTOR_ERASE_TIMEOUT_MS);
}

/**
  * @brief 使用四线输出模式从W25Q64连续读取数据。
  * @param address 读取起始地址，范围为0x000000至0x7FFFFF。
  * @param data 指向接收数据的缓冲区。
  * @param count 待读取字节数且地址范围不能超过芯片容量。
  * @retval HAL状态，HAL_OK表示读取成功。
  */
HAL_StatusTypeDef W25Q64_ReadData(uint32_t address,
                                 uint8_t *data,
                                 uint32_t count)
{
  QSPI_CommandTypeDef command;
  HAL_StatusTypeDef status;

  if (count == 0U)
  {
    return HAL_OK;
  }

  if ((w25q64_qspi_handle == NULL) ||
      (data == NULL) ||
      (address >= W25Q64_FLASH_SIZE_BYTES) ||
      (count > (W25Q64_FLASH_SIZE_BYTES - address)))
  {
    return HAL_ERROR;
  }

  W25Q64_PrepareCommand(&command);
  command.Instruction = W25Q64_CMD_FAST_READ_QUAD_OUTPUT;
  command.Address = address;
  command.AddressMode = QSPI_ADDRESS_1_LINE;
  command.DataMode = QSPI_DATA_4_LINES;
  command.DummyCycles = W25Q64_QUAD_READ_DUMMY_CYCLES;
  command.NbData = count;

  status = HAL_QSPI_Command(w25q64_qspi_handle,
                            &command,
                            W25Q64_COMMAND_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  return HAL_QSPI_Receive(w25q64_qspi_handle,
                          data,
                          W25Q64_READ_TIMEOUT_MS);
}
