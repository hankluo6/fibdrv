CONFIG_MODULE_SIG = n
TARGET_MODULE := fibdrv

obj-m := $(TARGET_MODULE).o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

CPUID := 7

GIT_HOOKS := .git/hooks/applied

all: $(GIT_HOOKS) client
	$(MAKE) -C $(KDIR) M=$(PWD) modules

$(GIT_HOOKS):
	@scripts/install-git-hooks
	@echo

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) client out
load:
	sudo insmod $(TARGET_MODULE).ko
unload:
	sudo rmmod $(TARGET_MODULE) || true >/dev/null

client: client.c
	$(CC) -o $@ $^

plot: all
	@sudo bash -c "echo 0 > /proc/sys/kernel/randomize_va_space"
	@sudo bash -c "echo performance > /sys/devices/system/cpu/cpu$(CPUID)/cpufreq/scaling_governor"
	@sudo bash -c "echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo"
	$(MAKE) unload
	$(MAKE) load
	$(shell sudo sh performance.sh)
	python3 scripts/driver.py
	gnuplot plot.gp
	$(MAKE) unload

PRINTF = env printf
PASS_COLOR = \e[32;01m
NO_COLOR = \e[0m
pass = $(PRINTF) "$(PASS_COLOR)$1 Passed [-]$(NO_COLOR)\n"

check: all
	$(MAKE) unload
	$(MAKE) load
	sudo ./client > out
	$(MAKE) unload
	@diff -u out scripts/expected.txt && $(call pass)
	@scripts/verify.py
