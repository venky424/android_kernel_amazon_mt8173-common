#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/irq_regs.h>
#include <asm/stacktrace.h>

#include "interface.h"
#include "met_drv.h"

#define LINE_SIZE	256

struct cookie_info {
	int depth;
	int strlen;
	char strbuf[LINE_SIZE];
};

static DEFINE_PER_CPU(struct cookie_info, info);

noinline void cookie(char *strbuf)
{
	MET_PRINTK("%s\n", strbuf);
}

static void get_kernel_cookie(unsigned long pc, struct cookie_info *pinfo)
{
/*	off_t off; */
	int ret;
#ifdef CONFIG_MODULES
  off_t off;
	struct module *mod = __module_address(pc);

	if (mod) {
		off = pc - (unsigned long)mod->module_core;
		ret = snprintf(pinfo->strbuf + pinfo->strlen, LINE_SIZE - pinfo->strlen,
			       ",%s,%lx", mod->name, off);
		pinfo->strlen += ret;
		/* cookie(current->comm, pc, mod->name, off, 1); */
	} else
#endif
	{
		ret = snprintf(pinfo->strbuf + pinfo->strlen, LINE_SIZE - pinfo->strlen,
			       ",vmlinux,%lx", pc);
		pinfo->strlen += ret;
		/* cookie(current->comm, pc, "vmlinux", pc, 0); */
	}
}

#if defined(__arm__)
static int report_trace(struct stackframe *frame, void *d)
{
	struct cookie_info *pinfo = d;
	unsigned long pc = frame->pc;

	if (pinfo->depth > 0) {
		get_kernel_cookie(pc, pinfo);
		pinfo->depth--;
		return 0;
	}
	return 1;
}
#endif

void met_cookie_polling(unsigned long long stamp, int cpu)
{
	struct pt_regs *regs;
	struct cookie_info *pinfo;
	unsigned long pc;
	int ret, outflag = 0;
	off_t off;

	regs = get_irq_regs();

	if (regs == 0)
		return;

	pc = profile_pc(regs);

	pinfo = &(per_cpu(info, cpu));
	pinfo->strlen = snprintf(pinfo->strbuf, LINE_SIZE, "%s,%lx", current->comm, pc);

	if (user_mode(regs)) {
		struct mm_struct *mm;
		struct vm_area_struct *vma;
		struct path *ppath;

		mm = current->mm;
		for (vma = find_vma(mm, pc); vma; vma = vma->vm_next) {
			if (pc < vma->vm_start || pc >= vma->vm_end)
				continue;

			if (vma->vm_file) {
				ppath = &(vma->vm_file->f_path);

				if (vma->vm_flags & VM_DENYWRITE)
					off = pc;
				else
					off = (vma->vm_pgoff << PAGE_SHIFT) + pc - vma->vm_start;

				ret =
				    snprintf(pinfo->strbuf + pinfo->strlen,
					     LINE_SIZE - pinfo->strlen, ",%s,%lx",
					     (char *)(ppath->dentry->d_name.name), off);
				pinfo->strlen += ret;
				outflag = 1;
				/* cookie(current->comm, pc, (char *)(ppath->dentry->d_name.name), off, 0); */
			} else {
				/* must be an anonymous map */
				ret =
				    snprintf(pinfo->strbuf + pinfo->strlen,
					     LINE_SIZE - pinfo->strlen, ",nofile,%lx", pc);
				pinfo->strlen += ret;
				outflag = 1;
				/* cookie(current->comm, pc, "nofile", pc, 0); */
			}
			break;
		}
	} else {
		/* kernel mode code */
		get_kernel_cookie(pc, pinfo);
		outflag = 1;
	}

	/* check task is resolvable */
	if (0 == outflag)
		return;

	cookie(pinfo->strbuf);
}

static void met_cookie_start(void)
{
	return;
}

static void met_cookie_stop(void)
{
	return;
}

static int met_cookie_process_argument(const char *arg, int len)
{
	unsigned int value;

	if (met_parse_num(arg, &value, len) < 0) {
		met_cookie.mode = 0;
		return -EINVAL;
	}

	met_cookie.mode = 1;

	return 0;
}

static const char help[] =
"  --cookie                              enable sampling task and PC\n";

static int met_cookie_print_help(char *buf, int len)
{
	len = snprintf(buf, PAGE_SIZE, help);
	return len;
}

static const char header[] =
"# cookie: task_name,PC,cookie_name,offset\n"
"met-info [000] 0.0: cookie_header: task_name,PC,cookie_name,offset\n";

static int met_cookie_print_header(char *buf, int len)
{
	int ret;

	ret = snprintf(buf, PAGE_SIZE, header);
	met_cookie.mode = 0;

	return ret;
}

struct metdevice met_cookie = {
	.name = "cookie",
	.type = MET_TYPE_PMU,
	.cpu_related = 1,
	.start = met_cookie_start,
	.stop = met_cookie_stop,
	.polling_interval = 1,
	.timed_polling = met_cookie_polling,
	.tagged_polling = met_cookie_polling,
	.process_argument = met_cookie_process_argument,
	.print_help = met_cookie_print_help,
	.print_header = met_cookie_print_header,
};
