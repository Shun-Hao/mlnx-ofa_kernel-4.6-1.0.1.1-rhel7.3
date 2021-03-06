#ifndef COMPAT_LINUX_UAPI_DEVLINK_H
#define COMPAT_LINUX_UAPI_DEVLINK_H

#include "../../../compat/config.h"

#ifndef HAVE_DEVLINK_HAS_ESWITCH_MODE_GET_SET
enum devlink_eswitch_mode {
	DEVLINK_ESWITCH_MODE_LEGACY,
	DEVLINK_ESWITCH_MODE_SWITCHDEV,
};
#endif

#ifndef HAVE_DEVLINK_HAS_ESWITCH_INLINE_MODE_GET_SET
enum devlink_eswitch_inline_mode {
	DEVLINK_ESWITCH_INLINE_MODE_NONE,
	DEVLINK_ESWITCH_INLINE_MODE_LINK,
	DEVLINK_ESWITCH_INLINE_MODE_NETWORK,
	DEVLINK_ESWITCH_INLINE_MODE_TRANSPORT,
};
#endif

#ifndef HAVE_DEVLINK_HAS_ESWITCH_ENCAP_MODE_SET
enum devlink_eswitch_encap_mode {
	DEVLINK_ESWITCH_ENCAP_MODE_NONE,
	DEVLINK_ESWITCH_ENCAP_MODE_BASIC,
};
#endif

#ifdef HAVE_DEVLINK_H
#include_next <uapi/linux/devlink.h>
#else /* HAVE_DEVLINK_H */

#endif /* HAVE_DEVLINK_H */

#endif /* COMPAT_LINUX_UAPI_DEVLINK_H */
