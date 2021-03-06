#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
#ifdef CONFIG_MMU_NOTIFIER
#ifndef HAVE_MMU_NOTIFIER_CALL_SRCU
#include <linux/mmu_notifier.h>
DEFINE_STATIC_SRCU(srcu);
void mmu_notifier_call_srcu(struct rcu_head *rcu,
			    void (*func)(struct rcu_head *rcu))
{
	call_srcu(&srcu, rcu, func);
}
EXPORT_SYMBOL_GPL(mmu_notifier_call_srcu);
#endif

#ifndef HAVE_MMU_NOTIFIER_UNREGISTER_NO_RELEASE
#include <linux/mmu_notifier.h>
#include <linux/rculist.h>
#include <linux/sched.h>
void mmu_notifier_unregister_no_release(struct mmu_notifier *mn,
					struct mm_struct *mm)
{
	spin_lock(&mm->mmu_notifier_mm->lock);
	/*
	 * Can not use list_del_rcu() since __mmu_notifier_release
	 * can delete it before we hold the lock.
	 */
	hlist_del_init_rcu(&mn->hlist);
	spin_unlock(&mm->mmu_notifier_mm->lock);

	BUG_ON(atomic_read(&mm->mm_count) <= 0);
	mmdrop(mm);
}
EXPORT_SYMBOL_GPL(mmu_notifier_unregister_no_release);
#endif
#endif
#endif
