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
#endif
