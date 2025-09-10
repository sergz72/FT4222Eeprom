#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftd2xx.h>
#include <libft4222.h>

enum operations {SCAN, READ, WRITE};

#define I2C_READ_BUFFER_LENGTH 65536
#define I2C_WRITE_BUFFER_LENGTH 512

static unsigned char i2c_read_buffer[I2C_READ_BUFFER_LENGTH];
static unsigned char i2c_write_buffer[I2C_WRITE_BUFFER_LENGTH];

static void print_hex_buffer(unsigned char *buffer, int length)
{
  printf("   ");
  for (int i = 0; i < 16; i++)
    printf(" %02x", i);

  for (int i = 0; i < length; i++)
  {
    int row_no = i / 16;
    if (i % 16 == 0)
      printf("\n%02x:", row_no);
    printf(" %02x", *buffer++);
  }
  puts("");
}

/* Sets given slave's current-address counter to specified value */
static FT4222_STATUS setWordAddress(FT_HANDLE      ftHandle,
                                    unsigned short slaveAddr,
                                    unsigned short wordAddress,
                                    unsigned short addressLength)
{
  FT4222_STATUS        ft4222Status;
  unsigned char data[addressLength];

  switch (addressLength)
  {
    case 1: data[0] = (unsigned char)wordAddress; break;
    case 2: data[0] = (unsigned char)(wordAddress >> 8); data[1] = (unsigned char)wordAddress; break;
    default: return FT4222_INVALID_PARAMETER;
  }

  uint16 bytesWritten = 0;
  ft4222Status = FT4222_I2CMaster_Write(ftHandle,
                                        slaveAddr,
                                       data,
                                        addressLength,
                                        &bytesWritten);
  if (FT4222_OK != ft4222Status)
  {
    printf("FT4222_I2CMaster_Write 1 failed (error %d)\n",
           (int)ft4222Status);
    return ft4222Status;
  }

  if (bytesWritten != addressLength)
  {
    printf("FT4222_I2CMaster_Write wrote %u of %u bytes.\n",
           bytesWritten,
           addressLength);
  }

  uint8 controllerStatus = 0;
  ft4222Status = FT4222_I2CMaster_GetStatus(ftHandle,
                                            &controllerStatus);
  if (ft4222Status != FT4222_OK)
  {
    printf("FT4222_I2CMaster_GetStatus failed (%d).\n",
           ft4222Status);
    return ft4222Status;
  }

  if (I2CM_ADDRESS_NACK(controllerStatus) || I2CM_DATA_NACK(controllerStatus))
    return FT4222_FAILED_TO_WRITE_DEVICE;

  return ft4222Status;
}

static bool i2c_check(FT_HANDLE ftHandle, unsigned char address)
{
  FT4222_STATUS status = setWordAddress(ftHandle, address, 0, 1);
  return status == FT4222_OK;
}

static void i2c_scan(FT_HANDLE ftHandle)
{
  puts("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
  printf("00:   ");

  for (unsigned char i = 1; i <= 0x7F; i++)
  {
    if (i % 16 == 0)
      printf("\n%.2x:", i);
    bool rc = i2c_check(ftHandle, i);
    if (rc)
      printf(" %.2x", i);
    else
      printf(" --");
  }
  puts("");
}

static void usage(void)
{
  puts("Usage: FT4222Eeprom <speed>\n  scan\n  read i2c_address address_length address length");
  puts("  write i2c_address address_length address page_size file_name");
}

static void i2c_read(FT_HANDLE ftHandle, unsigned char i2c_address, int address_length, int address, int length)
{
  FT4222_STATUS ft4222Status;
  uint16        bytesRead;

  // Reset slave EEPROM's current word address counter.
  ft4222Status = setWordAddress(ftHandle,
                                i2c_address,
                                address,
                                address_length);
  if (FT4222_OK != ft4222Status)
    return;

  ft4222Status = FT4222_I2CMaster_Read(ftHandle,
                                       i2c_address,
                                       i2c_read_buffer,
                                       (uint16)length,
                                       &bytesRead);
  if (FT4222_OK != ft4222Status)
  {
    printf("FT4222_I2CMaster_Read failed (error %d)\n",
           (int)ft4222Status);
    return;
  }

  if (bytesRead != length)
  {
    printf("FT4222_I2CMaster_Read read %u of %u bytes.\n",
           bytesRead,
           length);
  }
  else
    print_hex_buffer(i2c_read_buffer, length);
}

static void set_address(int address, int address_length)
{
  switch (address_length)
  {
    case 1: i2c_write_buffer[0] = (unsigned char)address; break;
    case 2: i2c_write_buffer[0] = (unsigned char)(address >> 8); i2c_write_buffer[1] = (unsigned char)address; break;
    default: break;
  }
}

static void i2c_write(FT_HANDLE ftHandle, unsigned char i2c_address, int address_length, int address, int page_size, int length)
{
  FT4222_STATUS ft4222Status;
  unsigned char *p = i2c_read_buffer;
  while (length)
  {
    int l = length > page_size ? page_size : length;

    set_address(address, address_length);
    memcpy(i2c_write_buffer + address_length, p, l);

    uint16 bytesWritten = 0;
    uint16 l_with_address = l + address_length;
    ft4222Status = FT4222_I2CMaster_Write(ftHandle,
                                          i2c_address,
                                          i2c_write_buffer,
                                          l_with_address,
                                          &bytesWritten);
    if (FT4222_OK != ft4222Status)
    {
      printf("FT4222_I2CMaster_Write failed (error %d)\n",
             (int)ft4222Status);
      return;
    }

    if (bytesWritten != l_with_address)
    {
      printf("FT4222_I2CMaster_Write wrote %u of %u bytes.\n",
             bytesWritten, l_with_address);
      return;
    }

    length -= l;
    p += l;
    address += l;
    usleep(5000);
  }
  puts("Done.");
}

static int OpenDevice(DWORD *loc_id)
{
  FT_STATUS ftStatus;
  DWORD numDevs = 0;
  FT_DEVICE_LIST_INFO_NODE *devInfo;

  ftStatus = FT_CreateDeviceInfoList(&numDevs);
  if (ftStatus != FT_OK)
  {
    printf("FT_CreateDeviceInfoList failed (error code %d)\n",
           (int)ftStatus);
    return -10;
  }

  if (numDevs == 0)
  {
    printf("No devices connected.\n");
    return -20;
  }

  /* Allocate storage */
  devInfo = calloc((size_t)numDevs,
                   sizeof(FT_DEVICE_LIST_INFO_NODE));
  if (devInfo == NULL)
  {
    printf("Allocation failure.\n");
    return -30;
  }

  /* Populate the list of info nodes */
  ftStatus = FT_GetDeviceInfoList(devInfo, &numDevs);
  if (ftStatus != FT_OK)
  {
    printf("FT_GetDeviceInfoList failed (error code %d)\n",
           (int)ftStatus);
    free(devInfo);
    return -40;
  }

  for (DWORD i = 0; i < numDevs; i++)
  {
    unsigned int devType = devInfo[i].Type;
    size_t       descLen;

    if (devType == FT_DEVICE_4222H_0)
    {
      // In mode 0, the FT4222H presents two interfaces: A and B.
      descLen = strlen(devInfo[i].Description);

      if ('A' == devInfo[i].Description[descLen - 1])
      {
        // Interface A may be configured as an I2C master.
        printf("\nDevice %d is interface A of mode-0 FT4222H:\n", i);
        printf("  0x%08x  %s  %s\n",
               (unsigned int)devInfo[i].ID,
               devInfo[i].SerialNumber,
               devInfo[i].Description);
        *loc_id = devInfo[i].LocId;
        free(devInfo);
        return 0;
      }
      // Interface B of mode 0 is reserved for GPIO.
      printf("Skipping interface B of mode-0 FT4222H.\n");
    }

    if (devType == FT_DEVICE_4222H_1_2)
    {
      // In modes 1 and 2, the FT4222H presents four interfaces but
      // none is suitable for I2C.
      descLen = strlen(devInfo[i].Description);

      printf("Skipping interface %c of mode-1/2 FT4222H.\n",
             devInfo[i].Description[descLen - 1]);
    }

    if (devType == FT_DEVICE_4222H_3)
    {
      // In mode 3, the FT4222H presents a single interface.
      // It may be configured as an I2C Master.
      printf("\nDevice %d is mode-3 FT4222H (single Master/Slave):\n", i);
      printf("  0x%08x  %s  %s\n",
             (unsigned int)devInfo[i].ID,
             devInfo[i].SerialNumber,
             devInfo[i].Description);
      *loc_id = devInfo[i].LocId;
      free(devInfo);
      return 0;
    }
  }

  printf("No FT4222 found.\n");
  free(devInfo);
  return -50;
}

static void CloseHandle(FT_HANDLE ftHandle)
{
  FT4222_UnInitialize(ftHandle);
  FT_Close(ftHandle);
}

static int I2CInit(DWORD locationId, uint32 kbps, FT_HANDLE *ftHandle)
{
  FT_STATUS      ftStatus;
  FT4222_STATUS  ft4222Status;
  FT4222_Version ft4222Version;

  ftStatus = FT_OpenEx((PVOID)(uintptr_t)locationId,
                       FT_OPEN_BY_LOCATION,
                       ftHandle);
  if (ftStatus != FT_OK)
  {
    printf("FT_OpenEx failed (error %d)\n",
           (int)ftStatus);
    return -10;
  }

  ft4222Status = FT4222_GetVersion(*ftHandle,
                                   &ft4222Version);
  if (FT4222_OK != ft4222Status)
  {
    printf("FT4222_GetVersion failed (error %d)\n",
           (int)ft4222Status);
    CloseHandle(*ftHandle);
    return -20;
  }

  printf("Chip version: %08X, LibFT4222 version: %08X\n",
         (unsigned int)ft4222Version.chipVersion,
         (unsigned int)ft4222Version.dllVersion);

  // Configure the FT4222 as an I2C Master
  ft4222Status = FT4222_I2CMaster_Init(*ftHandle, kbps);
  if (FT4222_OK != ft4222Status)
  {
    printf("FT4222_I2CMaster_Init failed (error %d)!\n",
           ft4222Status);
    CloseHandle(*ftHandle);
    return -30;
  }

  // Reset the I2CM registers to a known state.
  ft4222Status = FT4222_I2CMaster_Reset(*ftHandle);
  if (FT4222_OK != ft4222Status)
  {
    printf("FT4222_I2CMaster_Reset failed (error %d)!\n",
           ft4222Status);
    CloseHandle(*ftHandle);
    return -40;
  }

  return 0;
}

int main(int argc, char **argv)
{
  if (argc < 3)
  {
    usage();
    return 1;
  }

  int speed = atoi(argv[1]);
  if (speed < 60 || speed > 3400)
  {
    puts("Invalid speed.");
    return 1;
  }
  enum operations operation;
  if (!strcmp(argv[2], "scan"))
    operation = SCAN;
  else if (!strcmp(argv[2], "read"))
    operation = READ;
  else if (!strcmp(argv[2], "write"))
    operation = WRITE;
  else
  {
    puts("Invalid operation.");
    return 1;
  }

  if ((operation == READ && argc != 7) || (operation == WRITE && argc != 8))
  {
    usage();
    return 1;
  }
  unsigned long int i2c_address = 0;
  int address_length = 0;
  int address = 0;
  int length = 0;
  int page_size = 0;
  if (operation == READ || operation == WRITE)
  {
    i2c_address = strtoul(argv[3], NULL, 16);
    if (i2c_address > 0x7F)
    {
      puts("Invalid I2C address.");
      return 1;
    }
    address_length = atoi(argv[4]);
    if (address_length < 1 || address_length > 2)
    {
      puts("Invalid address length.");
      return 1;
    }
    address = atoi(argv[5]);
    if (address < 0 || address > (1<<8*address_length))
    {
      puts("Invalid address.");
      return 1;
    }
    length = atoi(argv[6]);
    if (length < 1 || length > (operation == READ ? I2C_READ_BUFFER_LENGTH : I2C_WRITE_BUFFER_LENGTH - 1 - address_length))
    {
      puts("Invalid length.");
      return 1;
    }
    if (operation == WRITE)
    {
      struct stat st;
      if (stat(argv[7], &st))
      {
        puts("Failed to stat file.");
        return 1;
      }
      if (st.st_size >= I2C_READ_BUFFER_LENGTH)
      {
        puts("File is too large.");
        return 1;
      }
      FILE *fd = fopen(argv[7], "rb");
      if (!fd)
      {
        puts("Failed to open file.");
        return 1;
      }
      fread(i2c_read_buffer, 1, st.st_size, fd);
      fclose(fd);
      page_size = length;
      length = (int)st.st_size;
    }
  }

  DWORD loc_id;
  int rc = OpenDevice(&loc_id);
  if (rc)
    return rc;

  FT_HANDLE ftHandle;
  rc = I2CInit(loc_id, (uint32)speed, &ftHandle);
  if (rc)
    return rc;

  switch (operation)
  {
    case SCAN:
      i2c_scan(ftHandle);
      break;
    case READ:
      i2c_read(ftHandle, (unsigned char)i2c_address, address_length, address, length);
      break;
    case WRITE:
      i2c_write(ftHandle, (unsigned char)i2c_address, address_length, address, page_size, length);
      break;
  }

  CloseHandle(ftHandle);

  return 0;
}