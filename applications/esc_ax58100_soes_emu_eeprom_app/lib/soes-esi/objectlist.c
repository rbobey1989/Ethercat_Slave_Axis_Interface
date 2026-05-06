#include "esc_coe.h"
#include "utypes.h"
#include <stddef.h>


static const char acName1000[] = "Device Type";
static const char acName1008[] = "Device Name";
static const char acName1009[] = "Hardware Version";
static const char acName100A[] = "Software Version";
static const char acName1018[] = "Identity Object";
static const char acName1018_00[] = "Max SubIndex";
static const char acName1018_01[] = "Vendor ID";
static const char acName1018_02[] = "Product Code";
static const char acName1018_03[] = "Revision Number";
static const char acName1018_04[] = "Serial Number";
static const char acName1600[] = "Pwm_Cmd";
static const char acName1600_00[] = "Max SubIndex";
static const char acName1600_01[] = "Pwm_0_Cmd";
static const char acName1600_02[] = "Pwm_1_Cmd";
static const char acName1600_03[] = "Pwm_2_Cmd";
static const char acName1600_04[] = "Pwm_3_Cmd";
static const char acName1601[] = "Pwm_En";
static const char acName1601_00[] = "Max SubIndex";
static const char acName1601_01[] = "Pwm_En";
static const char acName1601_02[] = "Pwm_1_En";
static const char acName1601_03[] = "Pwm_2_En";
static const char acName1601_04[] = "Pwm_3_En";
static const char acName1602[] = "Enc_En";
static const char acName1602_00[] = "Max SubIndex";
static const char acName1602_01[] = "Enc_0_En";
static const char acName1602_02[] = "Enc_1_En";
static const char acName1602_03[] = "Enc_2_En";
static const char acName1602_04[] = "Enc_3_En";
static const char acName1603[] = "Outputs";
static const char acName1603_00[] = "Max SubIndex";
static const char acName1603_01[] = "Outputs";
static const char acName1A00[] = "Enc_Pos";
static const char acName1A00_00[] = "Max SubIndex";
static const char acName1A00_01[] = "Enc_0_Pos";
static const char acName1A00_02[] = "Enc_1_Pos";
static const char acName1A00_03[] = "Enc_2_Pos";
static const char acName1A00_04[] = "Enc_3_Pos";
static const char acName1A01[] = "Enc_Vel";
static const char acName1A01_00[] = "Max SubIndex";
static const char acName1A01_01[] = "Enc_0_Vel";
static const char acName1A01_02[] = "Enc_1_Vel";
static const char acName1A01_03[] = "Enc_2_Vel";
static const char acName1A01_04[] = "Enc_3_Vel";
static const char acName1A02[] = "Inputs";
static const char acName1A02_00[] = "Max SubIndex";
static const char acName1A02_01[] = "Inputs";
static const char acName1A03[] = "Enc_Status";
static const char acName1A03_00[] = "Max SubIndex";
static const char acName1A03_01[] = "Enc_0_Status";
static const char acName1A03_02[] = "Enc_1_Status";
static const char acName1A03_03[] = "Enc_2_Status";
static const char acName1A03_04[] = "Enc_3_Status";
static const char acName1A04[] = "Pwm_Status";
static const char acName1A04_00[] = "Max SubIndex";
static const char acName1A04_01[] = "Pwm_0_Status";
static const char acName1A04_02[] = "Pwm_1_Status";
static const char acName1A04_03[] = "Pwm_2_Status";
static const char acName1A04_04[] = "Pwm_3_Status";
static const char acName1C00[] = "Sync Manager Communication Type";
static const char acName1C00_00[] = "Max SubIndex";
static const char acName1C00_01[] = "Communications Type SM0";
static const char acName1C00_02[] = "Communications Type SM1";
static const char acName1C00_03[] = "Communications Type SM2";
static const char acName1C00_04[] = "Communications Type SM3";
static const char acName1C12[] = "Sync Manager 2 PDO Assignment";
static const char acName1C12_00[] = "Max SubIndex";
static const char acName1C12_01[] = "PDO Mapping";
static const char acName1C12_02[] = "PDO Mapping";
static const char acName1C12_03[] = "PDO Mapping";
static const char acName1C12_04[] = "PDO Mapping";
static const char acName1C13[] = "Sync Manager 3 PDO Assignment";
static const char acName1C13_00[] = "Max SubIndex";
static const char acName1C13_01[] = "PDO Mapping";
static const char acName1C13_02[] = "PDO Mapping";
static const char acName1C13_03[] = "PDO Mapping";
static const char acName1C13_04[] = "PDO Mapping";
static const char acName1C13_05[] = "PDO Mapping";
static const char acName6000[] = "Enc_Pos";
static const char acName6000_00[] = "Max SubIndex";
static const char acName6000_01[] = "Enc_0_Pos";
static const char acName6000_02[] = "Enc_1_Pos";
static const char acName6000_03[] = "Enc_2_Pos";
static const char acName6000_04[] = "Enc_3_Pos";
static const char acName6001[] = "Enc_Vel";
static const char acName6001_00[] = "Max SubIndex";
static const char acName6001_01[] = "Enc_0_Vel";
static const char acName6001_02[] = "Enc_1_Vel";
static const char acName6001_03[] = "Enc_2_Vel";
static const char acName6001_04[] = "Enc_3_Vel";
static const char acName6002[] = "Inputs";
static const char acName6003[] = "Enc_Status";
static const char acName6003_00[] = "Max SubIndex";
static const char acName6003_01[] = "Enc_0_Status";
static const char acName6003_02[] = "Enc_1_Status";
static const char acName6003_03[] = "Enc_2_Status";
static const char acName6003_04[] = "Enc_3_Status";
static const char acName6004[] = "Pwm_Status";
static const char acName6004_00[] = "Max SubIndex";
static const char acName6004_01[] = "Pwm_0_Status";
static const char acName6004_02[] = "Pwm_1_Status";
static const char acName6004_03[] = "Pwm_2_Status";
static const char acName6004_04[] = "Pwm_3_Status";
static const char acName7000[] = "Pwm_Cmd";
static const char acName7000_00[] = "Max SubIndex";
static const char acName7000_01[] = "Pwm_0_Cmd";
static const char acName7000_02[] = "Pwm_1_Cmd";
static const char acName7000_03[] = "Pwm_2_Cmd";
static const char acName7000_04[] = "Pwm_3_Cmd";
static const char acName7001[] = "Pwm_En";
static const char acName7001_00[] = "Max SubIndex";
static const char acName7001_01[] = "Pwm_En";
static const char acName7001_02[] = "Pwm_1_En";
static const char acName7001_03[] = "Pwm_2_En";
static const char acName7001_04[] = "Pwm_3_En";
static const char acName7002[] = "Enc_En";
static const char acName7002_00[] = "Max SubIndex";
static const char acName7002_01[] = "Enc_0_En";
static const char acName7002_02[] = "Enc_1_En";
static const char acName7002_03[] = "Enc_2_En";
static const char acName7002_04[] = "Enc_3_En";
static const char acName7003[] = "Outputs";

const _objd SDO1000[] =
{
  {0x0, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1000, 5001, NULL},
};
const _objd SDO1008[] =
{
  {0x0, DTYPE_VISIBLE_STRING, 232, ATYPE_RO, acName1008, 0, "4 Axis Pwm Enc Channels GPIOs"},
};
const _objd SDO1009[] =
{
  {0x0, DTYPE_VISIBLE_STRING, 40, ATYPE_RO, acName1009, 0, "0.0.1"},
};
const _objd SDO100A[] =
{
  {0x0, DTYPE_VISIBLE_STRING, 40, ATYPE_RO, acName100A, 0, "0.0.1"},
};
const _objd SDO1018[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1018_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1018_01, 1829, NULL},
  {0x02, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1018_02, 890418, NULL},
  {0x03, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1018_03, 1, NULL},
  {0x04, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1018_04, 1, &Obj.serial},
};
const _objd SDO1600[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1600_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1600_01, 0x70000120, NULL},
  {0x02, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1600_02, 0x70000220, NULL},
  {0x03, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1600_03, 0x70000320, NULL},
  {0x04, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1600_04, 0x70000420, NULL},
};
const _objd SDO1601[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1601_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1601_01, 0x70010108, NULL},
  {0x02, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1601_02, 0x70010208, NULL},
  {0x03, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1601_03, 0x70010308, NULL},
  {0x04, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1601_04, 0x70010408, NULL},
};
const _objd SDO1602[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1602_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1602_01, 0x70020108, NULL},
  {0x02, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1602_02, 0x70020208, NULL},
  {0x03, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1602_03, 0x70020308, NULL},
  {0x04, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1602_04, 0x70020408, NULL},
};
const _objd SDO1603[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1603_00, 1, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1603_01, 0x70030010, NULL},
};
const _objd SDO1A00[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1A00_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A00_01, 0x60000120, NULL},
  {0x02, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A00_02, 0x60000220, NULL},
  {0x03, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A00_03, 0x60000320, NULL},
  {0x04, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A00_04, 0x60000420, NULL},
};
const _objd SDO1A01[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1A01_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A01_01, 0x60010120, NULL},
  {0x02, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A01_02, 0x60010220, NULL},
  {0x03, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A01_03, 0x60010320, NULL},
  {0x04, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A01_04, 0x60010420, NULL},
};
const _objd SDO1A02[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1A02_00, 1, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A02_01, 0x60020010, NULL},
};
const _objd SDO1A03[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1A03_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A03_01, 0x60030108, NULL},
  {0x02, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A03_02, 0x60030208, NULL},
  {0x03, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A03_03, 0x60030308, NULL},
  {0x04, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A03_04, 0x60030408, NULL},
};
const _objd SDO1A04[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1A04_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A04_01, 0x60040108, NULL},
  {0x02, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A04_02, 0x60040208, NULL},
  {0x03, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A04_03, 0x60040308, NULL},
  {0x04, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A04_04, 0x60040408, NULL},
};
const _objd SDO1C00[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_01, 1, NULL},
  {0x02, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_02, 2, NULL},
  {0x03, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_03, 3, NULL},
  {0x04, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_04, 4, NULL},
};
const _objd SDO1C12[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C12_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C12_01, 0x1600, NULL},
  {0x02, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C12_02, 0x1601, NULL},
  {0x03, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C12_03, 0x1602, NULL},
  {0x04, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C12_04, 0x1603, NULL},
};
const _objd SDO1C13[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C13_00, 5, NULL},
  {0x01, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C13_01, 0x1A00, NULL},
  {0x02, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C13_02, 0x1A01, NULL},
  {0x03, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C13_03, 0x1A02, NULL},
  {0x04, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C13_04, 0x1A03, NULL},
  {0x05, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C13_05, 0x1A04, NULL},
};
const _objd SDO6000[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName6000_00, 4, NULL},
  {0x01, DTYPE_INTEGER32, 32, ATYPE_RO | ATYPE_TXPDO, acName6000_01, 0, &Obj.Enc_Pos[0]},
  {0x02, DTYPE_INTEGER32, 32, ATYPE_RO | ATYPE_TXPDO, acName6000_02, 0, &Obj.Enc_Pos[1]},
  {0x03, DTYPE_INTEGER32, 32, ATYPE_RO | ATYPE_TXPDO, acName6000_03, 0, &Obj.Enc_Pos[2]},
  {0x04, DTYPE_INTEGER32, 32, ATYPE_RO | ATYPE_TXPDO, acName6000_04, 0, &Obj.Enc_Pos[3]},
};
const _objd SDO6001[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName6001_00, 4, NULL},
  {0x01, DTYPE_INTEGER32, 32, ATYPE_RO | ATYPE_TXPDO, acName6001_01, 0, &Obj.Enc_Vel[0]},
  {0x02, DTYPE_INTEGER32, 32, ATYPE_RO | ATYPE_TXPDO, acName6001_02, 0, &Obj.Enc_Vel[1]},
  {0x03, DTYPE_INTEGER32, 32, ATYPE_RO | ATYPE_TXPDO, acName6001_03, 0, &Obj.Enc_Vel[2]},
  {0x04, DTYPE_INTEGER32, 32, ATYPE_RO | ATYPE_TXPDO, acName6001_04, 0, &Obj.Enc_Vel[3]},
};
const _objd SDO6002[] =
{
  {0x0, DTYPE_UNSIGNED16, 16, ATYPE_RO | ATYPE_TXPDO, acName6002, 0, &Obj.Inputs},
};
const _objd SDO6003[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName6003_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED8, 8, ATYPE_RO | ATYPE_TXPDO, acName6003_01, 0, &Obj.Enc_Status[0]},
  {0x02, DTYPE_UNSIGNED8, 8, ATYPE_RO | ATYPE_TXPDO, acName6003_02, 0, &Obj.Enc_Status[1]},
  {0x03, DTYPE_UNSIGNED8, 8, ATYPE_RO | ATYPE_TXPDO, acName6003_03, 0, &Obj.Enc_Status[2]},
  {0x04, DTYPE_UNSIGNED8, 8, ATYPE_RO | ATYPE_TXPDO, acName6003_04, 0, &Obj.Enc_Status[3]},
};
const _objd SDO6004[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName6004_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED8, 8, ATYPE_RO | ATYPE_TXPDO, acName6004_01, 0, &Obj.Pwm_Status[0]},
  {0x02, DTYPE_UNSIGNED8, 8, ATYPE_RO | ATYPE_TXPDO, acName6004_02, 0, &Obj.Pwm_Status[1]},
  {0x03, DTYPE_UNSIGNED8, 8, ATYPE_RO | ATYPE_TXPDO, acName6004_03, 0, &Obj.Pwm_Status[2]},
  {0x04, DTYPE_UNSIGNED8, 8, ATYPE_RO | ATYPE_TXPDO, acName6004_04, 0, &Obj.Pwm_Status[3]},
};
const _objd SDO7000[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName7000_00, 4, NULL},
  {0x01, DTYPE_INTEGER32, 32, ATYPE_WO | ATYPE_RXPDO, acName7000_01, 0, &Obj.Pwm_Cmd[0]},
  {0x02, DTYPE_INTEGER32, 32, ATYPE_WO | ATYPE_RXPDO, acName7000_02, 0, &Obj.Pwm_Cmd[1]},
  {0x03, DTYPE_INTEGER32, 32, ATYPE_WO | ATYPE_RXPDO, acName7000_03, 0, &Obj.Pwm_Cmd[2]},
  {0x04, DTYPE_INTEGER32, 32, ATYPE_WO | ATYPE_RXPDO, acName7000_04, 0, &Obj.Pwm_Cmd[3]},
};
const _objd SDO7001[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName7001_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED8, 8, ATYPE_WO | ATYPE_RXPDO, acName7001_01, 0, &Obj.Pwm_En[0]},
  {0x02, DTYPE_UNSIGNED8, 8, ATYPE_WO | ATYPE_RXPDO, acName7001_02, 0, &Obj.Pwm_En[1]},
  {0x03, DTYPE_UNSIGNED8, 8, ATYPE_WO | ATYPE_RXPDO, acName7001_03, 0, &Obj.Pwm_En[2]},
  {0x04, DTYPE_UNSIGNED8, 8, ATYPE_WO | ATYPE_RXPDO, acName7001_04, 0, &Obj.Pwm_En[3]},
};
const _objd SDO7002[] =
{
  {0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName7002_00, 4, NULL},
  {0x01, DTYPE_UNSIGNED8, 8, ATYPE_WO | ATYPE_RXPDO, acName7002_01, 0, &Obj.Enc_En[0]},
  {0x02, DTYPE_UNSIGNED8, 8, ATYPE_WO | ATYPE_RXPDO, acName7002_02, 0, &Obj.Enc_En[1]},
  {0x03, DTYPE_UNSIGNED8, 8, ATYPE_WO | ATYPE_RXPDO, acName7002_03, 0, &Obj.Enc_En[2]},
  {0x04, DTYPE_UNSIGNED8, 8, ATYPE_WO | ATYPE_RXPDO, acName7002_04, 0, &Obj.Enc_En[3]},
};
const _objd SDO7003[] =
{
  {0x0, DTYPE_UNSIGNED16, 16, ATYPE_WO | ATYPE_RXPDO, acName7003, 0, &Obj.Outputs},
};

const _objectlist SDOobjects[] =
{
  {0x1000, OTYPE_VAR, 0, 0, acName1000, SDO1000},
  {0x1008, OTYPE_VAR, 0, 0, acName1008, SDO1008},
  {0x1009, OTYPE_VAR, 0, 0, acName1009, SDO1009},
  {0x100A, OTYPE_VAR, 0, 0, acName100A, SDO100A},
  {0x1018, OTYPE_RECORD, 4, 0, acName1018, SDO1018},
  {0x1600, OTYPE_RECORD, 4, 0, acName1600, SDO1600},
  {0x1601, OTYPE_RECORD, 4, 0, acName1601, SDO1601},
  {0x1602, OTYPE_RECORD, 4, 0, acName1602, SDO1602},
  {0x1603, OTYPE_RECORD, 1, 0, acName1603, SDO1603},
  {0x1A00, OTYPE_RECORD, 4, 0, acName1A00, SDO1A00},
  {0x1A01, OTYPE_RECORD, 4, 0, acName1A01, SDO1A01},
  {0x1A02, OTYPE_RECORD, 1, 0, acName1A02, SDO1A02},
  {0x1A03, OTYPE_RECORD, 4, 0, acName1A03, SDO1A03},
  {0x1A04, OTYPE_RECORD, 4, 0, acName1A04, SDO1A04},
  {0x1C00, OTYPE_ARRAY, 4, 0, acName1C00, SDO1C00},
  {0x1C12, OTYPE_ARRAY, 4, 0, acName1C12, SDO1C12},
  {0x1C13, OTYPE_ARRAY, 5, 0, acName1C13, SDO1C13},
  {0x6000, OTYPE_ARRAY, 4, 0, acName6000, SDO6000},
  {0x6001, OTYPE_ARRAY, 4, 0, acName6001, SDO6001},
  {0x6002, OTYPE_VAR, 0, 0, acName6002, SDO6002},
  {0x6003, OTYPE_ARRAY, 4, 0, acName6003, SDO6003},
  {0x6004, OTYPE_ARRAY, 4, 0, acName6004, SDO6004},
  {0x7000, OTYPE_ARRAY, 4, 0, acName7000, SDO7000},
  {0x7001, OTYPE_ARRAY, 4, 0, acName7001, SDO7001},
  {0x7002, OTYPE_ARRAY, 4, 0, acName7002, SDO7002},
  {0x7003, OTYPE_VAR, 0, 0, acName7003, SDO7003},
  {0xffff, 0xff, 0xff, 0xff, NULL, NULL}
};
