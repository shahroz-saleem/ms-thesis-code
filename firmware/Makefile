APPLICATION = prasle
BOARD ?= iotlab-m3
RIOTBASE ?= $(CURDIR)/../RIOT

ifeq (,$(wildcard $(RIOTBASE)/Makefile.include))
  $(error RIOT is missing in $(RIOTBASE), see "Getting RIOT" in README.md)
endif

USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_ipv6_default
USEMODULE += sock_udp
USEMODULE += sock_async_event
USEMODULE += sock_aux_rssi
USEMODULE += event_timeout_ztimer
USEMODULE += ztimer_msec

USEMODULE += shell
USEMODULE += shell_cmds_default
USEMODULE += ps
USEMODULE += schedstatistics

CFLAGS += -DCONFIG_GNRC_IPV6_NIB_NO_RTR_SOL=1
CFLAGS += -DCONFIG_SHELL_NO_ECHO=1
CFLAGS += -DCONFIG_SHELL_NO_PROMPT=1

DEVELHELP ?= 1
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
