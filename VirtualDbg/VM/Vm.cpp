#include "stdafx.h"

VOID VmStart(PVOID StartContext)
{
	UNREFERENCED_PARAMETER(StartContext);

	//
	// Does the hardware allow it in the first place?
	//
	NTSTATUS status = VTxHardwareStatus();

	if (!NT_SUCCESS(status))
	{
		DbgLog("Intel VT-x is not supported (0x%X)\n", status);
		return;
	}

	//
	// Enable VMX on each processor/core
	//
	status = VTxEnableProcessors(KeNumberProcessors);

	if (!NT_SUCCESS(status))
	{
		DbgLog("Unable to prepare processors for virtualization (0x%X)\n", status);
		return;
	}

	//
	// Synchronize
	//
	KMUTEX mutex;

	KeInitializeMutex(&mutex, 0);
	KeWaitForSingleObject(&mutex, Executive, KernelMode, FALSE, nullptr);

	//
	// Control area for saving states and VM information
	//
	status = ControlAreaInitialize(KeNumberProcessors);

	if (!NT_SUCCESS(status))
	{
		DbgLog("Unable to initialize control area (0x%X)\n", status);
		return;
	}

	//
	// Start virtualization
	//
	DbgLog("Virtualizing %d processors...\n", KeNumberProcessors);

	for (ULONG i = 0; i < (ULONG)KeNumberProcessors; i++)
	{
		KAFFINITY OldAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)(1 << i));

		KIRQL OldIrql = KeRaiseIrqlToDpcLevel();

		_StartVirtualization();

		KeLowerIrql(OldIrql);

		KeRevertToUserAffinityThreadEx(OldAffinity);
	}

	DbgLog("Done\n");

	KeReleaseMutex(&mutex, FALSE);
}

CHAR VmIsActive()
{
	__try
	{
		return _QueryVirtualization();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}

	return FALSE;
}

ULONG_PTR IpiStartVirtualization(ULONG_PTR Argument)
{
	UNREFERENCED_PARAMETER(Argument);

	// IRQL must be DPC_LEVEL or higher
	KIRQL irql = KeGetCurrentIrql();
	
	if (irql < DISPATCH_LEVEL)
		KeRaiseIrqlToDpcLevel();

	// Call assembler stub
	_StartVirtualization();

	// Restore original IRQL
	if (irql < DISPATCH_LEVEL)
		KeLowerIrql(irql);

	// Return value ignored
	return 0;
}

NTSTATUS StartVirtualization(PVOID GuestRsp)
{
	ULONG processorId	= KeGetCurrentProcessorNumber();
	NTSTATUS status		= ControlAreaInitializeProcessor(processorId);
	PVIRT_CPU cpu		= CpuControlArea[processorId];

	if (!NT_SUCCESS(status))
	{
		DbgLog("Failed ControlAreaInitializeProcessor 0x%x\n", status);
		return status;
	}

	CpuSetupVMCS(cpu, GuestRsp);

	status = Virtualize(cpu);

	if (!NT_SUCCESS(status))
	{
		DbgLog("Failed Virtualize\n");
		return status;
	}

	return STATUS_SUCCESS;
}
