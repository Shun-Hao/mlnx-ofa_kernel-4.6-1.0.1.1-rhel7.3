#ifndef IB_VERBS_EXP_DEF_H
#define IB_VERBS_EXP_DEF_H

enum ib_qpg_type {
	IB_QPG_NONE	= 0,
	IB_QPG_PARENT	= (1<<0),
	IB_QPG_CHILD_RX = (1<<1),
	IB_QPG_CHILD_TX = (1<<2)
};

enum ib_exp_start_values {
	IB_EXP_ACCESS_FLAGS_SHIFT = 0x0F
};

enum ib_exp_access_flags {
	/* Initial values are non-exp defined as part of  ib_access_flags */
	IB_EXP_ACCESS_PHYSICAL_ADDR	    = (1 << (16 + IB_EXP_ACCESS_FLAGS_SHIFT)),
};

enum ib_nvmf_offload_type {
	IB_NVMF_WRITE_OFFLOAD		 = (1ULL << 0),
	IB_NVMF_READ_OFFLOAD		 = (1ULL << 1),
	IB_NVMF_READ_WRITE_OFFLOAD	 = (1ULL << 2),
	IB_NVMF_READ_WRITE_FLUSH_OFFLOAD = (1ULL << 3),
};

struct ib_nvmf_init_data {
	enum ib_nvmf_offload_type	type;
	u8				log_max_namespace;
	u32				offloaded_capsules_count;
	u32				cmd_size;
	u8				data_offset;
	u8				log_max_io_size;
	u8				nvme_memory_log_page_size;
	u8				staging_buffer_log_page_size;
	u16				staging_buffer_number_of_pages;
	u32				staging_buffer_page_offset;
	u16				nvme_queue_size;
	u64				*staging_buffer_pas;
};

struct ib_odp_statistics {
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING

	atomic_t num_page_fault_pages;

	atomic_t num_invalidation_pages;

	atomic_t num_invalidations;

	atomic_t invalidations_faults_contentions;

	atomic_t num_page_faults;

	atomic_t num_prefetches_handled;

	atomic_t num_prefetch_pages;
#endif
};

#endif
