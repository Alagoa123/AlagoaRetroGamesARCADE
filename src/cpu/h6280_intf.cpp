#include "burnint.h"
#include "h6280/h6280.h"
#include "h6280_intf.h"
#include <stddef.h>

#define MAX_H6280	2	//

#define MEMORY_SPACE	0x200000
#define PAGE_SIZE	0x800
#define PAGE_MASK	0x7ff
#define PAGE_SHIFT	11
#define PAGE_COUNT	MEMORY_SPACE / PAGE_SIZE

#define READ		0
#define WRITE		1
#define FETCH		2

struct h6280_handler
{
	UINT8 (*h6280Read)(UINT32 address);
	void (*h6280Write)(UINT32 address, UINT8 data);
	void (*h6280WriteIO)(UINT8 port, UINT8 data);
	UINT8 *mem[3][PAGE_COUNT];

	h6280_Regs *h6280;
};

static struct h6280_handler sHandler[MAX_H6280];
static struct h6280_handler *sPointer;

INT32 nh6280CpuCount = 0;
INT32 nh6280CpuActive = -1;

void h6280_set_irq_line(INT32 irqline, INT32 state);

static void core_set_irq(INT32 cpu, INT32 line, INT32 state)
{
	INT32 active = nh6280CpuActive;

	if (cpu != active)
	{
		h6280Close();
		h6280Open(cpu);
	}

	h6280SetIRQLine(line, state);

	if (cpu != active)
	{
		h6280Close();
		h6280Open(active);
	}
}

cpu_core_config H6280Config =
{
	"h6280",
	h6280Open,
	h6280Close,
	h6280Read,
	h6280_write_rom,
	h6280GetActive,
	h6280TotalCycles,
	h6280NewFrame,
	h6280Idle,
	core_set_irq,
	h6280Run,
	h6280RunEnd,
	h6280Reset,
	h6280Scan,
	h6280Exit,
	0x200000,
	0
};

void h6280MapMemory(UINT8 *src, UINT32 start, UINT32 finish, INT32 type)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280MapMemory called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280MapMemory called with no CPU open\n"));
#endif

	UINT32 len = (finish-start) >> PAGE_SHIFT;

	for (UINT32 i = 0; i < len+1; i++)
	{
		UINT32 offset = i + (start >> PAGE_SHIFT);
		if (type & (1 <<  READ)) sPointer->mem[ READ][offset] = src + (i << PAGE_SHIFT);
		if (type & (1 << WRITE)) sPointer->mem[WRITE][offset] = src + (i << PAGE_SHIFT);
		if (type & (1 << FETCH)) sPointer->mem[FETCH][offset] = src + (i << PAGE_SHIFT);
	}
}

INT32 h6280DummyIrqCallback(INT32)
{
	return 0;
}

void h6280SetIrqCallbackHandler(INT32 (*callback)(INT32))
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280SetIrqCallbackHandler called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280SetIrqCallbackHandler called with no CPU open\n"));
#endif

	h6280_irqcallback(callback);
}

void h6280SetWriteHandler(void (*write)(UINT32, UINT8))
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280SetWriteHandler called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280SetWriteHandler called with no CPU open\n"));
#endif

	sPointer->h6280Write = write;
}

void h6280SetWritePortHandler(void (*write)(UINT8, UINT8))
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280SetWritePortHandler called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280SetWritePortHandler called with no CPU open\n"));
#endif

	sPointer->h6280WriteIO = write;
}

void h6280SetReadHandler(UINT8 (*read)(UINT32))
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280SetReadHandler called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280SetReadPortHandler called with no CPU open\n"));
#endif

	sPointer->h6280Read = read;
}

void h6280_write_rom(UINT32 address, UINT8 data)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280_write_rom called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280_write_rom called with no CPU open\n"));
#endif

	address &= 0x1fffff;

	if (sPointer->mem[READ][address >> PAGE_SHIFT] != NULL) {
		sPointer->mem[READ][address >> PAGE_SHIFT][address & PAGE_MASK] = data;
	}

	if (sPointer->mem[FETCH][address >> PAGE_SHIFT] != NULL) {
		sPointer->mem[FETCH][address >> PAGE_SHIFT][address & PAGE_MASK] = data;
	}

	if (sPointer->mem[WRITE][address >> PAGE_SHIFT] != NULL) {
		sPointer->mem[WRITE][address >> PAGE_SHIFT][address & PAGE_MASK] = data;
	}

	if (sPointer->h6280Write != NULL) {
		sPointer->h6280Write(address, data);
	}
}

void h6280WritePort(UINT8 port, UINT8 data)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280_write_port called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280_write_port called with no CPU open\n"));
#endif

//	bprintf (0, _T("%5.5x write port\n"), port);

	if (sPointer->h6280WriteIO != NULL) {
		sPointer->h6280WriteIO(port, data);
		return;
	}

	return;
}

void h6280Write(UINT32 address, UINT8 data)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280_write called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280_write called with no CPU open\n"));
#endif

	address &= 0x1fffff;

//	bprintf (0, _T("%5.5x write\n"), address);

	if (sPointer->mem[WRITE][address >> PAGE_SHIFT] != NULL) {
		sPointer->mem[WRITE][address >> PAGE_SHIFT][address & PAGE_MASK] = data;
		return;
	}

	if (sPointer->h6280Write != NULL) {
		sPointer->h6280Write(address, data);
		return;
	}

	return;
}

UINT8 h6280Read(UINT32 address)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280_read called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280_read called with no CPU open\n"));
#endif

	address &= 0x1fffff;

//	bprintf (0, _T("%5.5x read\n"), address);

	if (sPointer->mem[ READ][address >> PAGE_SHIFT] != NULL) {
		return sPointer->mem[ READ][address >> PAGE_SHIFT][address & PAGE_MASK];
	}

	if (sPointer->h6280Read != NULL) {
		return sPointer->h6280Read(address);
	}

	return 0;
}

UINT8 h6280Fetch(UINT32 address)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280_fetch1 called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280_fetch1 called with no CPU open\n"));
#endif

	address &= 0x1fffff;

	if (sPointer->mem[FETCH][address >> PAGE_SHIFT] != NULL) {
		return sPointer->mem[FETCH][address >> PAGE_SHIFT][address & PAGE_MASK];
	}

	if (sPointer->h6280Read != NULL) {
		return sPointer->h6280Read(address);
	}

	return 0;
}

void h6280SetIRQLine(INT32 line, INT32 state)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280SetIRQLine called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280SetIRQLine called with no CPU open\n"));
#endif

	if (state == CPU_IRQSTATUS_AUTO) {
		h6280_set_irq_line(line, 1);
		h6280Run(10);
		h6280_set_irq_line(line, 0);
	} else {
		h6280_set_irq_line(line, state);
	}
}

void h6280Init(INT32 nCpu)
{
	DebugCPU_H6280Initted = 1;

#if defined FBNEO_DEBUG
	if (nCpu >= MAX_H6280) bprintf(PRINT_ERROR, _T("h6280Init nCpu is more than MAX_CPU %d (MAX is %d)\n"), nCpu, MAX_H6280);
#endif

	sPointer = &sHandler[nCpu];

	sPointer->h6280 = (h6280_Regs*)BurnMalloc(sizeof(h6280_Regs));

	if (nCpu >= nh6280CpuCount) nh6280CpuCount = nCpu+1;

	for (INT32 i = 0; i < 3; i++) {
		for (INT32 j = 0; j < (MEMORY_SPACE / PAGE_SIZE); j++) {
			sPointer->mem[i][j] = NULL;
		}
	}

	sPointer->h6280Write = NULL;
	sPointer->h6280Read = NULL;
	sPointer->h6280WriteIO = NULL;

	h6280SetVDCPenalty(1); // default on

	CpuCheatRegister(nCpu, &H6280Config);
}

void h6280Exit()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280Exit called without init\n"));
#endif

	if (!DebugCPU_H6280Initted) return;

	for (INT32 i = 0; i < MAX_H6280; i++) {
		sPointer = &sHandler[i];

		sPointer->h6280Write = NULL;
		sPointer->h6280Read = NULL;
		sPointer->h6280WriteIO = NULL;

		if (sPointer->h6280) {
			BurnFree(sPointer->h6280);
		}
	}

	nh6280CpuCount = 0;
	DebugCPU_H6280Initted = 0;
}

void h6280Open(INT32 num)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280Open called without init\n"));
	if (num >= nh6280CpuCount) bprintf(PRINT_ERROR, _T("h6280Open called with invalid index %x\n"), num);
	if (nh6280CpuActive != -1) bprintf(PRINT_ERROR, _T("h6280Open called with CPU already open with index %x\n"), num);
#endif

	sPointer = &sHandler[num % MAX_H6280];

	h6280_set_context(sPointer->h6280);

	nh6280CpuActive = num;
}

void h6280Close()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280Close called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280Close called with no CPU open\n"));
#endif

	h6280_get_context(sPointer->h6280);

//	sPointer = NULL; // not safe...

	nh6280CpuActive = -1;
}

INT32 h6280GetActive()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280GetActive called without init\n"));
	if (nh6280CpuActive == -1) bprintf(PRINT_ERROR, _T("h6280GetActive called with no CPU open\n"));
#endif

	return nh6280CpuActive;
}

void h6280NewFrame()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_H6280Initted) bprintf(PRINT_ERROR, _T("h6280NewFrame called without init\n"));
#endif

	h6280_handler *ptr;

	for (INT32 i = 0; i < MAX_H6280; i++)
	{
		ptr = &sHandler[i % MAX_H6280];
		sHandler->h6280->h6280_totalcycles = 0;
	}
}

INT32 h6280Scan(INT32 nAction)
{
	struct BurnArea ba;

	char szName[64];

	if (nAction & ACB_DRIVER_DATA) {
		for (INT32 i = 0; i < MAX_H6280; i++)
		{
			h6280_handler *ptr = &sHandler[i];
			h6280_Regs *p = ptr->h6280;

			if (p == NULL) continue;

			memset(&ba, 0, sizeof(ba));
			ba.Data	  = p;
			ba.nLen	  = STRUCT_SIZE_HELPER(h6280_Regs, io_buffer);
			sprintf (szName, "h6280 Registers for Chip #%d", i);
			ba.szName = szName;
			BurnAcb(&ba);
		}
	}

	return 0;
}
