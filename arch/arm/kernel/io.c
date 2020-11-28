#include <linux/export.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/spinlock.h>

static DEFINE_RAW_SPINLOCK(__io_lock);

/*
 * Generic atomic MMIO modify.
 *
 * Allows thread-safe access to registers shared by unrelated subsystems.
 * The access is protected by a single MMIO-wide lock.
 */
void atomic_io_modify_relaxed(void __iomem *reg, u32 mask, u32 set)
{
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&__io_lock, flags);
	value = readl_relaxed(reg) & ~mask;
	value |= (set & mask);
	writel_relaxed(value, reg);
	raw_spin_unlock_irqrestore(&__io_lock, flags);
}
EXPORT_SYMBOL(atomic_io_modify_relaxed);

void atomic_io_modify(void __iomem *reg, u32 mask, u32 set)
{
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&__io_lock, flags);
	value = readl_relaxed(reg) & ~mask;
	value |= (set & mask);
	writel(value, reg);
	raw_spin_unlock_irqrestore(&__io_lock, flags);
}
EXPORT_SYMBOL(atomic_io_modify);

/*
 * Copy data from IO memory space to "real" memory space.
 * This needs to be optimized.
 */
void _memcpy_fromio(void *to, const volatile void __iomem *from, size_t count)
{
	unsigned char *t = to;
	while (count) {
		count--;
		*t = readb(from);
		t++;
		from++;
	}
}
EXPORT_SYMBOL(_memcpy_fromio);

void _memcpy_fromiow(void *to, const volatile void __iomem *from, size_t count)
{
	unsigned short *t = to;

	BUG_ON(count % 2);
	while (count) {
		count -= 2;
		*t = readw(from);
		t++;
		from += 2;
	}
}
EXPORT_SYMBOL(_memcpy_fromiow);

void _memcpy_fromiol(void *to, const volatile void __iomem *from, size_t count)
{
	unsigned long *t = to;

	BUG_ON(count % 4);
	while (count) {
		count -= 4;
		*t = readl(from);
		t++;
		from += 4;
	}
}
EXPORT_SYMBOL(_memcpy_fromiol);

/*
 * Copy data from "real" memory space to IO memory space.
 * This needs to be optimized.
 */
void _memcpy_toio(volatile void __iomem *to, const void *from, size_t count)
{
	const unsigned char *f = from;
	while (count) {
		count--;
		writeb(*f, to);
		f++;
		to++;
	}
}
EXPORT_SYMBOL(_memcpy_toio);

void _memcpy_toiow(volatile void __iomem *to, const void *from, size_t count)
{
	const unsigned short *f = from;

	BUG_ON(count % 2);
	while (count) {
		count -= 2;
		writew(*f, to);
		f++;
		to += 2;
	}
}
EXPORT_SYMBOL(_memcpy_toiow);

void _memcpy_toiol(volatile void __iomem *to, const void *from, size_t count)
{
	const unsigned long *f = from;

	BUG_ON(count % 4);
	while (count) {
		count -= 4;
		writel(*f, to);
		f++;
		to += 4;
	}
}
EXPORT_SYMBOL(_memcpy_toiol);

/*
 * "memset" on IO memory space.
 * This needs to be optimized.
 */
void _memset_io(volatile void __iomem *dst, int c, size_t count)
{
	while (count) {
		count--;
		writeb(c, dst);
		dst++;
	}
}
EXPORT_SYMBOL(_memset_io);
