/*
 *	Intel SMP support routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 *      (c) 2002,2003 Andi Kleen, SuSE Labs.
 *
 *	i386 and x86_64 integration by Glauber Costa <gcosta@redhat.com>
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>

#include <linux/nmi.h>
#include <asm/mtrr.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <mach_ipi.h>
#include <mach_apic.h>
/*
 *	Some notes on x86 processor bugs affecting SMP operation:
 *
 *	Pentium, Pentium Pro, II, III (and all CPUs) have bugs.
 *	The Linux implications for SMP are handled as follows:
 *
 *	Pentium III / [Xeon]
 *		None of the E1AP-E3AP errata are visible to the user.
 *
 *	E1AP.	see PII A1AP
 *	E2AP.	see PII A2AP
 *	E3AP.	see PII A3AP
 *
 *	Pentium II / [Xeon]
 *		None of the A1AP-A3AP errata are visible to the user.
 *
 *	A1AP.	see PPro 1AP
 *	A2AP.	see PPro 2AP
 *	A3AP.	see PPro 7AP
 *
 *	Pentium Pro
 *		None of 1AP-9AP errata are visible to the normal user,
 *	except occasional delivery of 'spurious interrupt' as trap #15.
 *	This is very rare and a non-problem.
 *
 *	1AP.	Linux maps APIC as non-cacheable
 *	2AP.	worked around in hardware
 *	3AP.	fixed in C0 and above steppings microcode update.
 *		Linux does not use excessive STARTUP_IPIs.
 *	4AP.	worked around in hardware
 *	5AP.	symmetric IO mode (normal Linux operation) not affected.
 *		'noapic' mode has vector 0xf filled out properly.
 *	6AP.	'noapic' mode might be affected - fixed in later steppings
 *	7AP.	We do not assume writes to the LVT deassering IRQs
 *	8AP.	We do not enable low power mode (deep sleep) during MP bootup
 *	9AP.	We do not use mixed mode
 *
 *	Pentium
 *		There is a marginal case where REP MOVS on 100MHz SMP
 *	machines with B stepping processors can fail. XXX should provide
 *	an L1cache=Writethrough or L1cache=off option.
 *
 *		B stepping CPUs may hang. There are hardware work arounds
 *	for this. We warn about it in case your board doesn't have the work
 *	arounds. Basically that's so I can tell anyone with a B stepping
 *	CPU and SMP problems "tough".
 *
 *	Specific items [From Pentium Processor Specification Update]
 *
 *	1AP.	Linux doesn't use remote read
 *	2AP.	Linux doesn't trust APIC errors
 *	3AP.	We work around this
 *	4AP.	Linux never generated 3 interrupts of the same priority
 *		to cause a lost local interrupt.
 *	5AP.	Remote read is never used
 *	6AP.	not affected - worked around in hardware
 *	7AP.	not affected - worked around in hardware
 *	8AP.	worked around in hardware - we get explicit CS errors if not
 *	9AP.	only 'noapic' mode affected. Might generate spurious
 *		interrupts, we log only the first one and count the
 *		rest silently.
 *	10AP.	not affected - worked around in hardware
 *	11AP.	Linux reads the APIC between writes to avoid this, as per
 *		the documentation. Make sure you preserve this as it affects
 *		the C stepping chips too.
 *	12AP.	not affected - worked around in hardware
 *	13AP.	not affected - worked around in hardware
 *	14AP.	we always deassert INIT during bootup
 *	15AP.	not affected - worked around in hardware
 *	16AP.	not affected - worked around in hardware
 *	17AP.	not affected - worked around in hardware
 *	18AP.	not affected - worked around in hardware
 *	19AP.	not affected - worked around in BIOS
 *
 *	If this sounds worrying believe me these bugs are either ___RARE___,
 *	or are signal timing bugs worked around in hardware and there's
 *	about nothing of note with C stepping upwards.
 */

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */
static void native_smp_send_reschedule(int cpu)
{
	if (unlikely(cpu_is_offline(cpu))) {
		WARN_ON(1);
		return;
	}
	send_IPI_mask(cpumask_of_cpu(cpu), RESCHEDULE_VECTOR);
}

void native_send_call_func_single_ipi(int cpu)
{
	send_IPI_mask(cpumask_of_cpu(cpu), CALL_FUNCTION_SINGLE_VECTOR);
}

void native_send_call_func_ipi(cpumask_t mask)
{
	cpumask_t allbutself;

	allbutself = cpu_online_map;
	cpu_clear(smp_processor_id(), allbutself);

	if (cpus_equal(mask, allbutself) &&
	    cpus_equal(cpu_online_map, cpu_callout_map))
		send_IPI_allbutself(CALL_FUNCTION_VECTOR);
	else
		send_IPI_mask(mask, CALL_FUNCTION_VECTOR);
}

static DEFINE_SPINLOCK(nmi_call_lock);
static struct nmi_call_data_struct {
	smp_nmi_function func;
	void *info;
	atomic_t started;
	atomic_t finished;
	cpumask_t cpus_called;
	int wait;
} *nmi_call_data;

static int smp_nmi_callback(struct pt_regs *regs, int cpu)
{
	smp_nmi_function func;
	void *info;
	int wait;

	func = nmi_call_data->func;
	info = nmi_call_data->info;
	wait = nmi_call_data->wait;
	ack_APIC_irq();
	/* prevent from calling func() multiple times */
	if (cpu_test_and_set(cpu, nmi_call_data->cpus_called))
		return 0;
	/*
	 * notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	mb();
	atomic_inc(&nmi_call_data->started);
	/* at this point the nmi_call_data structure is out of scope */
	irq_enter();
	func(regs, info);
	irq_exit();
	if (wait)
		atomic_inc(&nmi_call_data->finished);

	return 1;
}

/*
 * This function tries to call func(regs, info) on each cpu.
 * Func must be fast and non-blocking.
 * May be called with disabled interrupts and from any context.
 */
int smp_nmi_call_function(smp_nmi_function func, void *info, int wait)
{
	struct nmi_call_data_struct data;
	int cpus;

	cpus = num_online_cpus() - 1;
	if (!cpus)
		return 0;

	data.func = func;
	data.info = info;
	data.wait = wait;
	atomic_set(&data.started, 0);
	atomic_set(&data.finished, 0);
	cpus_clear(data.cpus_called);
	/* prevent this cpu from calling func if NMI happens */
	cpu_set(smp_processor_id(), data.cpus_called);

	if (!spin_trylock(&nmi_call_lock))
		return -1;

	nmi_call_data = &data;
	set_nmi_ipi_callback(smp_nmi_callback);
	mb();

	/* Send a message to all other CPUs and wait for them to respond */
	send_IPI_allbutself(APIC_DM_NMI);
	while (atomic_read(&data.started) != cpus)
		barrier();

	unset_nmi_ipi_callback();
	if (wait)
		while (atomic_read(&data.finished) != cpus)
			barrier();
	spin_unlock(&nmi_call_lock);

	return 0;
}

static void stop_this_cpu(void *dummy)
{
	local_irq_disable();
	/*
	 * Remove this CPU:
	 */
	cpu_clear(smp_processor_id(), cpu_online_map);
	disable_local_APIC();
	if (hlt_works(smp_processor_id()))
		for (;;) halt();
	for (;;);
}

/*
 * this function calls the 'stop' function on all other CPUs in the system.
 */

static void native_smp_send_stop(void)
{
	unsigned long flags;

	if (reboot_force)
		return;

	smp_call_function(stop_this_cpu, NULL, 0);
	local_irq_save(flags);
	disable_local_APIC();
	local_irq_restore(flags);
}

/*
 * Reschedule call back. Nothing to do,
 * all the work is done automatically when
 * we return from the interrupt.
 */
void smp_reschedule_interrupt(struct pt_regs *regs)
{
	ack_APIC_irq();
#ifdef CONFIG_X86_32
	__get_cpu_var(irq_stat).irq_resched_count++;
#else
	add_pda(irq_resched_count, 1);
#endif
}

void smp_call_function_interrupt(struct pt_regs *regs)
{
	ack_APIC_irq();
	irq_enter();
	generic_smp_call_function_interrupt();
#ifdef CONFIG_X86_32
	__get_cpu_var(irq_stat).irq_call_count++;
#else
	add_pda(irq_call_count, 1);
#endif
	irq_exit();
}

void smp_call_function_single_interrupt(struct pt_regs *regs)
{
	ack_APIC_irq();
	irq_enter();
	generic_smp_call_function_single_interrupt();
#ifdef CONFIG_X86_32
	__get_cpu_var(irq_stat).irq_call_count++;
#else
	add_pda(irq_call_count, 1);
#endif
	irq_exit();
}

struct smp_ops smp_ops = {
	.smp_prepare_boot_cpu = native_smp_prepare_boot_cpu,
	.smp_prepare_cpus = native_smp_prepare_cpus,
	.cpu_up = native_cpu_up,
	.smp_cpus_done = native_smp_cpus_done,

	.smp_send_stop = native_smp_send_stop,
	.smp_send_reschedule = native_smp_send_reschedule,

	.send_call_func_ipi = native_send_call_func_ipi,
	.send_call_func_single_ipi = native_send_call_func_single_ipi,
};
EXPORT_SYMBOL_GPL(smp_ops);
