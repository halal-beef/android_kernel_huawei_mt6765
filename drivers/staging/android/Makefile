ccflags-y += -I$(src)			# needed for trace events
subdir-ccflags-y += -I$(srctree)/mm/
MTK_PLATFORM:= $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/$(MTK_PLATFORM)/
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/$(MTK_PLATFORM)/videox/
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat

obj-y					+= ion/
obj-$(CONFIG_FIQ_DEBUGGER)		+= fiq_debugger/

obj-$(CONFIG_ASHMEM)			+= ashmem.o
obj-$(CONFIG_HW_LOGGER)             += hwlogger/
obj-$(CONFIG_HUAWEI_SMART_RECLAIM)    += smart_reclaim.o
obj-$(CONFIG_ANDROID_LOW_MEMORY_KILLER)	+= lowmemorykiller.o
obj-$(CONFIG_HUAWEI_LMK_DBG)		+= lowmem_dbg.o
obj-$(CONFIG_HUAWEI_RCC)        += rcc.o
obj-$(CONFIG_ANDROID_VSOC)		+= vsoc.o
