/*
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>

#include <linux/regulator/consumer.h>
#include <linux/miscdevice.h>
//#include <mach/gpiomux.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/time.h>
#include <linux/spinlock_types.h>
#include <linux/kthread.h>
#include <linux/reboot.h>
#include <linux/pwm.h>
#include "pn547.h"
#include "mt6355/mtk_pmic_api.h"
//#include <linux/wakelock.h>
#include <mtk_clkbuf_ctl.h>
#include <mt-plat/upmu_common.h>
#ifdef CONFIG_HUAWEI_HW_DEV_DCT
#include <linux/hw_dev_dec.h>
#endif

#ifdef CONFIG_HUAWEI_DEV_SELFCHECK
#include <huawei_platform/dev_detect/hw_dev_detect.h>
#endif

#ifdef CONFIG_HUAWEI_DSM
#include <dsm/dsm_pub.h>
#include <linux/errno.h>
#endif
#define _read_sample_test_size			40
#define NFC_TRY_NUM 3
#define UICC_SUPPORT_CARD_EMULATION (1<<0)
#define eSE_SUPPORT_CARD_EMULATION (1<<1)
#define UICC_2_SUPPORT_CARD_EMULATION (1<<2)
#define CARD_UNKNOWN	0
#define CARD1_SELECT    1
#define CARD2_SELECT    2
#define UICC_CARD2_SELECT    3
#define MAX_ATTRIBUTE_BUFFER_SIZE 128

#define ENABLE_START	0
#define ENABLE_END		1
#define MAX_CONFIG_NAME_SIZE	64
//#define	MAX_BRCM_CONFIG_NAME_SIZE	64
#define MAX_NFC_CHIP_TYPE_SIZE   32
static char g_nfc_nxp_config_name[MAX_CONFIG_NAME_SIZE];
static char g_nfc_brcm_config_name[MAX_CONFIG_NAME_SIZE];
static char nfc_chip_type[MAX_NFC_CHIP_TYPE_SIZE];

#define MAX_BUFFER_SIZE	512
#define SIGNAL_INTERRUPT_WAIT -512
static int firmware_update = 0;
//static int lcd_status = UNKNOWN_LCD;
static int g_nfc_single_channel;
static int wait_event_interruptible_ret = 0;
static int nfc_at_result;
static int nfc_switch_state;
static int g_nfc_activated_se_info; /* record activated se info when nfc enable process */
#define NFC_PINCTRL_STATE_ACTIVE	"nfc_active"
#define NFC_PINCTRL_STATE_SUSPEND	"nfc_suspend"

struct pn547_dev	{
	wait_queue_head_t	read_wq;
	struct mutex		read_mutex;
	struct mutex		irq_wake_mutex;
	struct device		*dev;
	struct i2c_client	*client;
	struct miscdevice	pn547_device;
	struct clk             *nfc_clk;
	struct pinctrl *pn547_pinctrl;
	struct pinctrl_state *pinctrl_state_active;
	struct pinctrl_state *pinctrl_state_suspend;

	unsigned int		sim_status;
	unsigned int		sim_switch;
	unsigned int		enable_status;
	unsigned int 		ven_gpio;
	unsigned int 		firm_gpio;
	unsigned int		irq_gpio;
	unsigned int		clk_req_gpio;
	bool				irq_enabled;
	bool			    irq_wake_enabled;
	spinlock_t		irq_enabled_lock;
	bool		       	do_reading;
    struct wakeup_source   wl;
	bool cancel_read;
};
static struct pn547_dev *nfcdev = NULL;

static int g_nfc_hal_dmd_no = 0;    /* record last hal dmd no */
#ifdef CONFIG_HUAWEI_DSM
static struct dsm_dev dsm_nfc = {
    .name = "dsm_nfc",
    .device_name = NULL,
    .ic_name = NULL,
    .module_name = NULL,
    .fops = NULL,
    .buff_size = 1024,
};

static struct dsm_client *nfc_dclient = NULL;

#endif

/*
 *FUNCTION: pn547_disable_irq_wake
 *DESCRIPTION: disable irq wakeup function
 *Parameters
 * struct  pn547_dev *: device structure
 *RETURN VALUE
 * none
 */
static void pn547_disable_irq_wake(struct pn547_dev *pn547_dev)
{
	int ret = 0;

	mutex_lock(&pn547_dev->irq_wake_mutex);
	if (pn547_dev->irq_wake_enabled) {
		pn547_dev->irq_wake_enabled = false;
		ret = irq_set_irq_wake(pn547_dev->client->irq,0);
		if (ret) {
			pr_err("%s failed: ret=%d\n", __func__, ret);
		}
	}
	mutex_unlock(&pn547_dev->irq_wake_mutex);
}

/*
 *FUNCTION: pn547_enable_irq_wake
 *DESCRIPTION: enable irq wakeup function
 *Parameters
 * struct  pn547_dev *: device structure
 *RETURN VALUE
 * none
 */
static void pn547_enable_irq_wake(struct pn547_dev *pn547_dev)
{
	int ret = 0;

	mutex_lock(&pn547_dev->irq_wake_mutex);
	if (!pn547_dev->irq_wake_enabled) {
		pn547_dev->irq_wake_enabled = true;
		ret = irq_set_irq_wake(pn547_dev->client->irq,1);
		if (ret) {
			pr_err("%s failed: ret=%d\n", __func__, ret);
		}
	}
	mutex_unlock(&pn547_dev->irq_wake_mutex);
}

/*FUNCTION: get_nfc_config_name
  *DESCRIPTION: get nfc configure files' name from device tree system, save result in global variable
  *Parameters
  * none
  *RETURN VALUE
  * none */
static void get_nfc_config_name(struct device *dev)
{
	int ret = -1;
	struct device_node *np = dev->of_node;
	const char *out_value = NULL;
	

	memset(g_nfc_nxp_config_name, 0, MAX_CONFIG_NAME_SIZE);
	memset(g_nfc_brcm_config_name, 0, MAX_CONFIG_NAME_SIZE);

	/*get huawei_nfc_info node*/
	/*np = of_find_node_by_name(NULL, "huawei_nfc_info");
	if(!of_device_is_available(np)){
	     pr_err("%s: not find node <huawei_nfc_info> !\n", __func__);
	     return ;
	}*/

	/*get nfc_nxp_conf_name*/
	ret=of_property_read_string(np, "nfc_nxp_conf_name", &out_value);
	strncpy(g_nfc_nxp_config_name, out_value, MAX_CONFIG_NAME_SIZE-1);
	if (ret != 0){
		memset(g_nfc_nxp_config_name, 0, MAX_CONFIG_NAME_SIZE);
		pr_err("%s: can't get nfc nxp config name\n", __func__);
	}
	pr_info("%s: nfc nxp config name:%s\n", __func__, g_nfc_nxp_config_name);

	/*get nfc_brcm_conf_name*/
	ret=of_property_read_string(np, "nfc_brcm_conf_name", &out_value);
	strncpy(g_nfc_brcm_config_name, out_value, MAX_CONFIG_NAME_SIZE-1);
	if (ret != 0){
		memset(g_nfc_brcm_config_name, 0, MAX_CONFIG_NAME_SIZE);
		pr_err("%s: can't get nfc brcm config name\n", __func__);
	}
	pr_info("%s: nfc brcm config name:%s\n", __func__, g_nfc_brcm_config_name);

	return ;
}

/*FUNCTION: pn547_enable_nfc
  *DESCRIPTION: reset cmd sequence to enable pn547
  *Parameters
  * struct  pn547_dev *pdev: device structure
  *RETURN VALUE
  * none */
static void pn547_enable_nfc(struct  pn547_dev *pdev)
{
	/*hardware reset*/
	/* power on */
	gpio_set_value(pdev->ven_gpio, 1);// 1
	msleep(20);

	/* power off */
	gpio_set_value(pdev->ven_gpio, 0);
	msleep(60);

	/* power on */
	gpio_set_value(pdev->ven_gpio, 1);
	msleep(20);

	return ;
}

/*FUNCTION: pn547_disable_irq
  *DESCRIPTION: disable irq function
  *Parameters
  * struct  pn547_dev *: device structure
  *RETURN VALUE
  * none */
static void pn547_disable_irq(struct pn547_dev *pn547_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pn547_dev->irq_enabled_lock, flags);
	if (pn547_dev->irq_enabled) {
		disable_irq_nosync(pn547_dev->client->irq);
		pn547_dev->irq_enabled = false;
	}
	spin_unlock_irqrestore(&pn547_dev->irq_enabled_lock, flags);
}

/*FUNCTION: pn547_enable_irq
  *DESCRIPTION: enable irq function
  *Parameters
  * struct  pn547_dev *: device structure
  *RETURN VALUE
  * none */
static void pn547_enable_irq(struct pn547_dev *pn547_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pn547_dev->irq_enabled_lock, flags);
	if (!pn547_dev->irq_enabled){
		pn547_dev->irq_enabled = true;
		enable_irq(pn547_dev->client->irq);
	}
	spin_unlock_irqrestore(&pn547_dev->irq_enabled_lock, flags);
}

/*FUNCTION: pn547_dev_irq_handler
  *DESCRIPTION: irq handler, jump here when receive an irq request from NFC chip
  *Parameters
  * int irq: irq number
  * void *dev_id:device structure
  *RETURN VALUE
  * irqreturn_t: irq handle result */
static irqreturn_t pn547_dev_irq_handler(int irq, void *dev_id)
{
	struct pn547_dev *pn547_dev = dev_id;
	if(gpio_get_value(pn547_dev->irq_gpio) != 1){
		return IRQ_HANDLED;
	}

	pn547_disable_irq(pn547_dev);
    if(SIGNAL_INTERRUPT_WAIT==wait_event_interruptible_ret)
    {
        __pm_wakeup_event(&pn547_dev->wl, jiffies_to_msecs(3 * HZ));
    }
    else
    {
        __pm_wakeup_event(&pn547_dev->wl, jiffies_to_msecs(1 * HZ));
    }
	pn547_dev->do_reading = 1;

	/* Wake up waiting readers */
	wake_up(&pn547_dev->read_wq);

	return IRQ_HANDLED;
}

/*FUNCTION: pn547_dev_read
  *DESCRIPTION: read i2c data
  *Parameters
  * struct file *filp:device structure
  * char __user *buf:return to user buffer
  * size_t count:read data count
  * loff_t *offset:offset
  *RETURN VALUE
  * ssize_t: result */
static ssize_t pn547_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	struct pn547_dev *pn547_dev = filp->private_data;
	char tmp[MAX_BUFFER_SIZE] = {0};
	char *tmpStr = NULL;
	int ret = -1;
	int i;
	int retry = 0;

       /*max size is 512*/
	if (count > MAX_BUFFER_SIZE){
		count = MAX_BUFFER_SIZE;
	}

	mutex_lock(&pn547_dev->read_mutex);

	/*read data when interrupt occur*/
	if (!gpio_get_value(pn547_dev->irq_gpio)){
		if (filp->f_flags & O_NONBLOCK){
			ret = -EAGAIN;
            pr_err("%s : EAGAIN,%d\n", __func__, ret);
			goto fail;
		}

		pn547_disable_irq(pn547_dev);
		pn547_dev->do_reading = 0;
		pn547_enable_irq(pn547_dev);
		ret = wait_event_interruptible(pn547_dev->read_wq,
				pn547_dev->do_reading);
        wait_event_interruptible_ret = ret;
		/*user cancel data read op*/
		if (pn547_dev->cancel_read) {
			pn547_dev->cancel_read = false;
			ret = -1;
            pr_err("%s : cancel_read,%d\n", __func__, ret);
			goto fail;
		}

		if (ret){
			goto fail;
		}
	}

	tmpStr = (char *)kzalloc(sizeof(tmp)*2 + 1, GFP_KERNEL);
	if (!tmpStr) {
		pr_err("%s:Cannot allocate memory for read tmpStr.\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}
	/* Read data, at most tree times */
	for(retry = 0; retry < NFC_TRY_NUM; retry++){
		ret = i2c_master_recv(pn547_dev->client, tmp, count);
		for (i = 0; i < count; i++) {
			snprintf(&tmpStr[i * 2], 3, "%02X", tmp[i]);
		}
		pr_info("%s : retry = %d, ret = %d, count = %3d > %s\n", __func__, retry, ret, (int)count, tmpStr);
		if (ret == (int)count){
			break;
		}else{
			if(retry > 0){
			    pr_info("%s : read retry times =%d returned %d\n", __func__,retry,ret);
			}
			msleep(10);
			continue;
		}
	}
	kfree(tmpStr);
	mutex_unlock(&pn547_dev->read_mutex);

    if (ret != (int)count) {
        pr_err("%s : i2c_master_recv returned %d\n", __func__, ret);
        ret = -EIO;
    }

	if (ret < 0) {
		pr_err("%s: PN547 i2c_master_recv returned %d\n", __func__, ret);
		return ret;
	}

	if (ret > count) {
		pr_err("%s: received too many bytes from i2c (%d)\n", __func__, ret);
		return -EIO;
	}

	/*copy data to user*/
	if (copy_to_user(buf, tmp, ret)) {
		pr_warning("%s : failed to copy to user space\n", __func__);
		return -EFAULT;
	}
	udelay(1000);
	return ret;

fail:
	mutex_unlock(&pn547_dev->read_mutex);
	//pr_err("%s : goto fail, and ret : %d \n", __func__, ret);

	return ret;
}

/*FUNCTION: pn547_dev_write
  *DESCRIPTION: write i2c data
  *Parameters
  * struct file *filp:device structure
  * char __user *buf:user buffer to write
  * size_t count:write data count
  * loff_t *offset:offset
  *RETURN VALUE
  * ssize_t: result */
static ssize_t pn547_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offset)
{
	struct pn547_dev  *pn547_dev = filp->private_data;
	char tmp[MAX_BUFFER_SIZE];
	char *tmpStr = NULL;
	int ret = -1;
	int retry = 0;
	int i;

	/*max size is 512*/
	if (count > MAX_BUFFER_SIZE){
		count = MAX_BUFFER_SIZE;
	}

	/*copy data from user*/
	if (copy_from_user(tmp, buf, count)){
		pr_err("%s : failed to copy from user space\n", __func__);
		return -EFAULT;
	}

	/* Write data */
	tmpStr = (char *)kzalloc(sizeof(tmp)*2 + 1, GFP_KERNEL);
	if (!tmpStr) {
		pr_info("%s:Cannot allocate memory for write tmpStr.\n", __func__);
		return -ENOMEM;
	}
	/* Write data, at most tree times */
	for(retry = 0; retry < NFC_TRY_NUM; retry++){
		ret = i2c_master_send(pn547_dev->client, tmp, count);
		for (i = 0; i < count; i++) {
			snprintf(&tmpStr[i * 2], 3, "%02X", tmp[i]);
		}
		pr_info("%s : retry = %d, ret = %d, count = %3d > %s\n", __func__, retry, ret, (int)count, tmpStr);
		if (ret == (int)count){
			break;
		}else{
			if(retry > 0){
			    pr_info("%s : send retry times =%d returned %d\n", __func__,retry,ret);
			}
			msleep(50);
			continue;
		}
	}
	kfree(tmpStr);

    if(ret != (int)count) {
        pr_err("%s : i2c_master_send returned %d\n", __func__, ret);
        ret = -EIO;
    }

	return ret;
}

/*FUNCTION: pn547_i2c_write
  *DESCRIPTION: write i2c data, only use in check_sim_status
  *Parameters
  * struct  pn547_dev *pdev:device structure
  * const char *buf:user buffer to write
  * int count:write data count
  *RETURN VALUE
  * ssize_t: result */
static ssize_t pn547_i2c_write(struct  pn547_dev *pdev,const char *buf,int count)
{
	int ret = -1;
	int retry = 0;
	char *tmpStr = NULL;
	int i;

	tmpStr = (char *)kzalloc(255*2, GFP_KERNEL);
	if (!tmpStr) {
		pr_info("%s:Cannot allocate memory for write tmpStr.\n", __func__);
		return -ENOMEM;
	}
	/* Write data, we have 3 chances */
	for (retry = 0; retry < NFC_TRY_NUM; retry++){
		ret = i2c_master_send(pdev->client, buf, (int)count);
		for (i = 0; i < count; i++) {
			snprintf(&tmpStr[i * 2], 3, "%02X", buf[i]);
		}
		pr_info("%s : retry = %d, ret = %d, count = %3d > %s\n", __func__, retry, ret, (int)count, tmpStr);
		if (ret == (int)count) {
			break;
		} else {
			if (retry > 0) {
				pr_info("%s : send data try =%d returned %d\n", __func__, retry, ret);
			}
			msleep(10);
			continue;
		}
	}
	kfree(tmpStr);
	if (ret != (int)count) {
		pr_err("%s : i2c_master_send returned %d\n", __func__, ret);
		ret = -EIO;
	}

	return (size_t)ret;
}

/*FUNCTION: pn547_i2c_read
  *DESCRIPTION: read i2c data, only use in check_sim_status
  *Parameters
  * struct  pn547_dev *pdev:device structure
  * const char *buf:read to user buffer
  * int count:read data count
  *RETURN VALUE
  * ssize_t: result */
static ssize_t pn547_i2c_read(struct  pn547_dev *pdev,char *buf,int count)
{
	int ret = -1;
	int retry = 0;
	char *tmpStr = NULL;
	int i;

	mutex_lock(&pdev->read_mutex);
	if (!gpio_get_value(pdev->irq_gpio)){

		/* for -ERESTARTSYS, we have 3 chances */
		for (retry = 0; retry < NFC_TRY_NUM; retry++){
			pn547_disable_irq(pdev);
			pdev->do_reading = 0;
			pn547_enable_irq(pdev);

		      /* wait_event_interruptible_timeout Returns:
	               *0: if the @timeout elapsed
	               *-ERESTARTSYS: if it was interrupted by a signal
	               *>0:the remaining jiffies (at least 1) if the @condition = true before the @timeout elapsed.*/
			ret = wait_event_interruptible_timeout(pdev->read_wq,
					pdev->do_reading,msecs_to_jiffies(1000));
			pn547_disable_irq(pdev);
            wait_event_interruptible_ret = ret;
			if (ret <= 0) {
				pr_err("%s : wait retry count %d!,ret = %d\n", __func__,retry,ret);
				continue;
			}
			break;
		}

		if (ret <= 0){
			pr_err("%s : wait_event_interruptible_timeout error!,ret = %d\n", __func__,ret);
			ret = -1;
			goto fail;
		}
	}

	tmpStr = (char *)kzalloc(255*2, GFP_KERNEL);
	if (!tmpStr) {
		pr_info("%s:Cannot allocate memory for write tmpStr.\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}
	/* Read data, we have 3 chances */
	for (retry = 0; retry < NFC_TRY_NUM; retry++){
		ret = i2c_master_recv(pdev->client, buf, (int)count);
		for (i = 0; i < count; i++) {
			snprintf(&tmpStr[i * 2], 3, "%02X", buf[i]);
		}
		pr_info("%s : retry = %d, ret = %d, count = %3d > %s\n", __func__, retry, ret, (int)count, tmpStr);
		if (ret == (int)count){
			break;
		}else{
			pr_err("%s : read data try =%d returned %d\n", __func__,retry,ret);
			msleep(10);
			continue;
		}
	}
	kfree(tmpStr);
	mutex_unlock(&pdev->read_mutex);

    if(ret != (int)count) {
        pr_err("%s : i2c_master_recv returned %d\n", __func__, ret);
        ret = -EIO;
    }
    return (size_t)ret;

fail:
	mutex_unlock(&pdev->read_mutex);
	return (size_t)ret;
}

/*FUNCTION: check_sim_status
  *DESCRIPTION: To test if the SWP interfaces are operational between the NFCC and the connected NFCEEs.
  *  Test sequence:
  *  1) hardware reset chip
  *  2) send CORE_RESET_CMD
  *  3) send CORE_INIT_CMD
  *  4) send NCI_PROPRIETARY_ACT_CMD
  *  5) send SYSTEM_TEST_SWP_INTERFACE_CMD(SWP1/2)
  *Parameters
  * struct i2c_client *client:i2c device structure
  * struct  pn547_dev *pdev:device structure
  *RETURN VALUE
  * int: check result */
static int check_sim_status(struct i2c_client *client, struct  pn547_dev *pdev)
{
	int ret=-1;

	unsigned char recvBuf[40] = {0};
	const  char send_reset[] = {0x20,0x00,0x01,0x01};//CORE_RESET_CMD
	const  char init_cmd[] = {0x20,0x01,0x00}; //CORE_INIT_CMD

	const  char read_config[] = {0x2F,0x02,0x00}; //SYSTEM_PROPRIETARY_ACT_CMD
	const  char read_config_UICC[] = {0x2F,0x3E,0x01,0x00};//SYSTEM_TEST_SWP_INTERFACE_CMD(swp1)
	//const  char read_config_eSE[] = {0x2F,0x3E,0x01,0x01};	//SYSTEM_TEST_SWP_INTERFACE_CMD(eSE )
    //const  char read_config_UICC2[] = {0x2F,0x3E,0x01,0x02};	//SYSTEM_TEST_SWP_INTERFACE_CMD(swp2)
	pr_info("pn547 - %s : enter\n", __func__);

	pdev->sim_status = 0;
	/*hardware reset*/
	gpio_set_value(pdev->firm_gpio, 0);
	pn547_enable_nfc(pdev);

	/*write CORE_RESET_CMD*/
	ret = pn547_i2c_write(pdev, send_reset, sizeof(send_reset));
	if (ret < 0) {
		pr_err("%s: CORE_RESET_CMD pn547_i2c_write failed, ret:%d\n", __func__, ret);
		goto failed;
	}
	/*read response*/
	ret = pn547_i2c_read(pdev, recvBuf, _read_sample_test_size);
	if (ret < 0) {
		pr_err("%s: CORE_RESET_RSP pn547_i2c_read failed, ret:%d\n", __func__, ret);
		goto failed;
	}

	udelay(500);
	/*write CORE_INIT_CMD*/
	ret = pn547_i2c_write(pdev, init_cmd, sizeof(init_cmd));
	if (ret < 0) {
		pr_err("%s: CORE_INIT_CMD pn547_i2c_write failed, ret:%d\n", __func__, ret);
		goto failed;
	}
	/*read response*/
	ret = pn547_i2c_read(pdev, recvBuf, _read_sample_test_size);
	if (ret < 0) {
		pr_err("%s: CORE_INIT_RSP pn547_i2c_read failed, ret:%d\n", __func__, ret);
		goto failed;
	}

	udelay(500);
	/*write NCI_PROPRIETARY_ACT_CMD*/
	ret = pn547_i2c_write(pdev, read_config, sizeof(read_config));
	if (ret < 0) {
		pr_err("%s: PRO_ACT_CMD pn547_i2c_write failed, ret:%d\n", __func__, ret);
		goto failed;
	}
	/*read response*/
	ret = pn547_i2c_read(pdev, recvBuf, _read_sample_test_size);
	if (ret < 0) {
		pr_err("%s: PRO_ACT_RSP pn547_i2c_read failed, ret:%d\n", __func__, ret);
		goto failed;
	}

	udelay(500);
	/*write TEST_SWP_CMD UICC*/
	ret = pn547_i2c_write(pdev, read_config_UICC, sizeof(read_config_UICC));
	if (ret < 0) {
		pr_err("%s: TEST_UICC_CMD pn547_i2c_write failed, ret:%d\n", __func__, ret);
		goto failed;
	}
	/*read response*/
	ret = pn547_i2c_read(pdev, recvBuf, _read_sample_test_size);
	if (ret < 0) {
		pr_err("%s: TEST_UICC_RSP pn547_i2c_read failed, ret:%d\n", __func__, ret);
		goto failed;
	}

	mdelay(10);
	/*read notification*/
	ret = pn547_i2c_read(pdev, recvBuf, _read_sample_test_size);
	if (ret < 0) {
		pr_err("%s: TEST_UICC_NTF pn547_i2c_read failed, ret:%d\n", __func__, ret);
		goto failed;
	}

	/*NTF's format: 6F 3E 02 XX1 XX2 -> "XX1 == 0" means SWP link OK*/
	if (recvBuf[0] == 0x6F && recvBuf[1] == 0x3E && recvBuf[3] == 0x00) {
		pdev->sim_status |= UICC_SUPPORT_CARD_EMULATION;
	}

	
	return pdev->sim_status;
failed:
	pdev->sim_status = ret;
	return ret;
}

/*FUNCTION: nfc_fwupdate_store
  *DESCRIPTION: store nfc firmware update result.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  * size_t count:data count
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_fwupdate_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{

	/*if success, store firmware update result into variate firmware_update*/
	if ('1' == buf[0]) {
		firmware_update = 1;
		pr_err("%s:firmware update success\n", __func__);
	}

	return (ssize_t)count;
}

/*FUNCTION: nfc_fwupdate_show
  *DESCRIPTION: show nfc firmware update result.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_fwupdate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	/*get firmware update result from variate firmware_update*/
	return (ssize_t)(snprintf(buf, sizeof(firmware_update)+1, "%d", firmware_update));
}

/*FUNCTION: nxp_config_name_store
  *DESCRIPTION: store nxp_config_name, infact do nothing now.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  * size_t count:data count
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nxp_config_name_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	/*no need store config name, do nothing now*/
	return (ssize_t)count;
}

/*FUNCTION: nxp_config_name_show
  *DESCRIPTION: get nxp_config_name from variate g_nfc_nxp_config_name.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nxp_config_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return (ssize_t)(snprintf(buf, strlen(g_nfc_nxp_config_name)+1, "%s", g_nfc_nxp_config_name));
}
/*FUNCTION: nfc_brcm_conf_name_store
  *DESCRIPTION: store nfc_brcm_conf_name, infact do nothing now.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  * size_t count:data count
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_brcm_conf_name_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	/*no need store config name, do nothing now*/
	return (ssize_t)count;
}
/*FUNCTION: nfc_brcm_conf_name_show
  *DESCRIPTION: get nfc_brcm_conf_name from variate g_nfc_brcm_config_name.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_brcm_conf_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return (ssize_t)(snprintf(buf, strlen(g_nfc_brcm_config_name)+1, "%s", g_nfc_brcm_config_name));
}
/*FUNCTION: nfc_sim_status_show
  *DESCRIPTION: get nfc-sim card support result.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  *RETURN VALUE
  *		eSE		UICC	value
  *		0		0		0	(not support)
  *		0		1		1	(swp1 support)
  *		1		0		2	(swp2 support)
  *		1		1		3	(all support)
  *		<0	:error */
static ssize_t nfc_sim_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status=-1;
	int retry = 0;

	struct i2c_client *i2c_client_dev = container_of(dev, struct i2c_client, dev);
	struct pn547_dev *pn547_dev;
	pn547_dev = i2c_get_clientdata(i2c_client_dev);

	if(pn547_dev == NULL)	{
		pr_err("%s:  pn547_dev == NULL!\n", __func__);
		return status;
	}

	pr_info("%s: enter!\n", __func__);
	/* if failed, we have 3 chances */
	for (retry = 0; retry < NFC_TRY_NUM; retry++){
		status = check_sim_status(i2c_client_dev,pn547_dev);
		if(status < 0){
			pr_err("%s: check_sim_status error!retry count=%d\n", __func__, retry);
			msleep(10);
			continue;
		}
		break;
	}


	pr_info("%s: status=%d\n", __func__, status);
	return (ssize_t)(snprintf(buf, MAX_ATTRIBUTE_BUFFER_SIZE-1, "%d\n", status));
}

/*FUNCTION: nfc_sim_switch_store
  *DESCRIPTION: save user sim card select result.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  * size_t count:data count
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_sim_switch_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct i2c_client *i2c_client_dev = container_of(dev, struct i2c_client, dev);
	struct pn547_dev *pn547_dev;
	int val = 0;

	pn547_dev = i2c_get_clientdata(i2c_client_dev);
	if(pn547_dev == NULL)	{
		pr_err("%s:  pn547_dev == NULL!\n", __func__);
		return 0;
	}

	/*save card select result*/
	if (sscanf(buf, "%1d", &val) == 1) {
		pr_err("%s: input val = %d!\n", __func__,val);

		if (val == CARD1_SELECT){
			pn547_dev->sim_switch = CARD1_SELECT;
		}else if (val == CARD2_SELECT){
			pn547_dev->sim_switch = CARD2_SELECT;
		}else if (val == UICC_CARD2_SELECT){
			pn547_dev->sim_switch = UICC_CARD2_SELECT;
		}else{
			return -EINVAL;
		}
	}else{
		return -EINVAL;
	}

	return (ssize_t)count;
}

/*FUNCTION: nfc_sim_switch_show
  *DESCRIPTION: get user sim card select result.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_sim_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *i2c_client_dev = container_of(dev, struct i2c_client, dev);
	struct pn547_dev *pn547_dev;
	pn547_dev = i2c_get_clientdata(i2c_client_dev);

	if(pn547_dev == NULL)	{
		pr_err("%s:  pn547_dev == NULL!\n", __func__);
		return 0;
	}
	return (ssize_t)(snprintf(buf,  MAX_ATTRIBUTE_BUFFER_SIZE-1, "%d\n", pn547_dev->sim_switch));
}

/*FUNCTION: rd_nfc_sim_status_show
  *DESCRIPTION: get nfc-sim card support result from variate, no need to send check commands again.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  *RETURN VALUE
  *		eSE		UICC	value
  *		0		0		0	(not support)
  *		0		1		1	(swp1 support)
  *		1		0		2	(swp2 support)
  *		1		1		3	(all support)
  *		<0	:error */
static ssize_t rd_nfc_sim_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status=-1;
	struct i2c_client *i2c_client_dev = container_of(dev, struct i2c_client, dev);
	struct pn547_dev *pn547_dev;
	pn547_dev = i2c_get_clientdata(i2c_client_dev);
	if(pn547_dev == NULL)	{
		pr_err("%s:  pn547_dev == NULL!\n", __func__);
		return status;
	}
	return (ssize_t)(snprintf(buf, MAX_ATTRIBUTE_BUFFER_SIZE-1,"%d\n", pn547_dev->sim_status));
}

/*FUNCTION: nfc_enable_status_store
  *DESCRIPTION: store nfc_enable_status for RD test.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  * size_t count:data count
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_enable_status_store(struct device *dev, struct device_attribute *attr,
             const char *buf, size_t count)
{
	struct i2c_client *i2c_client_dev = container_of(dev, struct i2c_client, dev);
	struct pn547_dev *pn547_dev;
	int val = 0;

	pn547_dev = i2c_get_clientdata(i2c_client_dev);
	if(pn547_dev == NULL)	{
		pr_err("%s:  pn547_dev == NULL!\n", __func__);
		return 0;
	}

	/*save nfc enable status*/
	if (sscanf(buf, "%1d", &val) == 1) {
		if(val == ENABLE_START){
			pn547_dev->enable_status = ENABLE_START;
		}else if(val == ENABLE_END){
			pn547_dev->enable_status = ENABLE_END;
		}else{
			return -EINVAL;
		}
	}else{
		return -EINVAL;
	}

	return (ssize_t)count;
}

/*FUNCTION: nfc_enable_status_show
  *DESCRIPTION: show nfc_enable_status for RD test.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_enable_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *i2c_client_dev = container_of(dev, struct i2c_client, dev);
	struct pn547_dev *pn547_dev;
	pn547_dev = i2c_get_clientdata(i2c_client_dev);
	if(pn547_dev == NULL){
		pr_err("%s:  pn547_dev == NULL!\n", __func__);
		return 0;
	}
	return (ssize_t)(snprintf(buf,  MAX_ATTRIBUTE_BUFFER_SIZE-1, "%d\n", pn547_dev->enable_status));
}

/*FUNCTION: nfc_card_num_show
  *DESCRIPTION: show supported nfc_card_num, which config in device tree system.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_card_num_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int temp = 0;
	int ret = -1;
	struct device_node *np = dev->of_node;

	/*np = of_find_node_by_name(NULL, "huawei_nfc_info");
	if(!of_device_is_available(np)){
	     pr_err("%s: not find node <huawei_nfc_info> !\n", __func__);
	     temp = UICC_SUPPORT_CARD_EMULATION;
	     return (ssize_t)(snprintf(buf,  MAX_ATTRIBUTE_BUFFER_SIZE-1, "%d\n", (unsigned char)temp));
	}*/

	/*read supported nfc_card_num from device tree system.*/
	ret=of_property_read_u32(np, "nfc_card_num", &temp);
	if(ret){
		temp = UICC_SUPPORT_CARD_EMULATION;
		pr_err("%s: can't get nfc card num config!\n", __func__);
	}
	return (ssize_t)(snprintf(buf,  MAX_ATTRIBUTE_BUFFER_SIZE-1, "%d\n", (unsigned char)temp));
}

/*FUNCTION: nfc_chip_type_show
  *DESCRIPTION: show nfc_chip_type, which config in device tree system.
  *Parameters
  * struct device *dev:device structure
  * struct device_attribute *attr:device attribute
  * const char *buf:user buffer
  *RETURN VALUE
  * ssize_t:  result */
static ssize_t nfc_chip_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret = -1;
    struct device_node *np = dev->of_node;
    const char *out_value = NULL;

    memset(nfc_chip_type, 0, MAX_NFC_CHIP_TYPE_SIZE);

    /*np = of_find_node_by_name(NULL, "huawei_nfc_info");
    if(!of_device_is_available(np)){
         pr_err("%s: not find node <huawei_nfc_info> !\n", __func__);
         strncpy(nfc_chip_type, "pn547", MAX_NFC_CHIP_TYPE_SIZE-1);
         return (ssize_t)(snprintf(buf,  MAX_ATTRIBUTE_BUFFER_SIZE-1, "%s", nfc_chip_type));
    }*/

    /*read nfc_chip_type from device tree system.*/
    ret=of_property_read_string(np, "nfc_chip_type", &out_value);
    strncpy(nfc_chip_type, out_value, MAX_NFC_CHIP_TYPE_SIZE-1);
    if(ret != 0){
        pr_err("%s: can't get nfc nfc_chip_type, default pn547\n", __func__);
        strncpy(nfc_chip_type, "pn547", MAX_NFC_CHIP_TYPE_SIZE-1);
    }

    return (ssize_t)(snprintf(buf,  MAX_ATTRIBUTE_BUFFER_SIZE-1, "%s", nfc_chip_type));
}


void set_nfc_single_channel(struct device *dev)
{
	int ret = -1;
    struct device_node *np = dev->of_node;
    const char *out_value = NULL;
	//char single_channel_dts_str[MAX_CONFIG_NAME_SIZE] = {0};
    pr_err("%s: nfc single channel begin:%d\n", __func__, g_nfc_single_channel);
    /*np = of_find_node_by_name(NULL, "huawei_nfc_info");
    if(!of_device_is_available(np)){
        pr_err("%s: not find node <huawei_nfc_info> !\n", __func__);
        g_nfc_single_channel = 0;
        return;
    }*/
    ret=of_property_read_string(np, "nfc_single_channel", &out_value);
	if (ret != 0) {
		pr_err("%s: can't get nfc single channel dts config\n", __func__);
		g_nfc_single_channel = 0;
		return;
	}
	pr_err("%s: get nfc single channel dts config %s\n", __func__,out_value);
	if (!strcasecmp(out_value, "true")) {
		g_nfc_single_channel = 1;
	}
	pr_err("%s: nfc single channel:%d\n", __func__, g_nfc_single_channel);
	return;

}
static ssize_t nfc_single_channel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return (ssize_t)(snprintf(buf, MAX_ATTRIBUTE_BUFFER_SIZE-1, "%d", g_nfc_single_channel));
}

/*lint -e516 -e515 -e717 -e960 -e712 -e747*/
int nfc_record_dmd_info(long dmd_no, const char *dmd_info)
{
/*lint -e529 -esym(529,*)*/
#ifdef CONFIG_HUAWEI_DSM
    if (dmd_no < NFC_DMD_NUMBER_MIN || dmd_no > NFC_DMD_NUMBER_MAX
        || dmd_info == NULL || NULL == nfc_dclient) {
        pr_info("%s: para error: %ld\n", __func__, dmd_no); /*lint !e960*/
        return -1;
    }

    pr_info("%s: dmd no: %ld - %s", __func__, dmd_no, dmd_info); /*lint !e960*/
    if (!dsm_client_ocuppy(nfc_dclient)) {
        dsm_client_record(nfc_dclient, "DMD info:%s", dmd_info);
        dsm_client_notify(nfc_dclient, dmd_no);
    }
#endif
    return 0;
}
/*lint +e516 +e515 +e717 +e960 +e712 +e747*/
/*lint -e529 +esym(529,*)*/
static ssize_t nfc_hal_dmd_info_store(struct device *dev, struct device_attribute *attr,
             const char *buf, size_t count)
{
    int val = 0;
    char dmd_info_from_hal[64] = {'\0'};
    /* The length of DMD error number is 9. */
    if (sscanf(buf, "%9d", &val) == 1) {
        if (val < NFC_DMD_NUMBER_MIN || val > NFC_DMD_NUMBER_MAX) {
            return (ssize_t)count;
        }
        g_nfc_hal_dmd_no = val;
        /* The max length of content for current dmd description set as 63.
           Example for DMD Buf: '923002014 CoreReset:600006A000D1A72000'.
           A space as a separator is between dmd error no and description.*/
        if (sscanf(buf, "%*s%63s", dmd_info_from_hal) == 1) {
            nfc_record_dmd_info(val, dmd_info_from_hal);
        }

    } else {
        pr_err("%s: get dmd number error\n", __func__);
    }
    return (ssize_t)count;
}

static ssize_t nfc_hal_dmd_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return (ssize_t)(snprintf(buf, MAX_ATTRIBUTE_BUFFER_SIZE-1, "%d", g_nfc_hal_dmd_no));
}
static ssize_t nfc_at_result_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	if(buf!=NULL)
	{
		nfc_at_result=buf[0]-CHAR_0; /*file storage str*/
	}
	return (ssize_t)count;
}

static ssize_t nfc_at_result_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return (ssize_t)(snprintf(buf, sizeof(nfc_at_result)+1, "%d", nfc_at_result));
}

static ssize_t nfc_switch_state_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	if(buf!=NULL)
	{
		nfc_switch_state=buf[0]-CHAR_0; /*file storage str*/
	}
	return (ssize_t)count;
}

static ssize_t nfc_switch_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return (ssize_t)(snprintf(buf, sizeof(nfc_switch_state)+1, "%d", nfc_switch_state));
}

static ssize_t nfc_activated_se_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return (ssize_t)(snprintf(buf, MAX_ATTRIBUTE_BUFFER_SIZE-1, "%d", g_nfc_activated_se_info));
}

static ssize_t nfc_activated_se_info_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int val = 0;
	if (sscanf(buf, "%1d", &val) == 1) {
		g_nfc_activated_se_info = val;
	} else {
		//hwlog_err("%s: set g_nfc_activated_se_info error\n", __func__);
		g_nfc_activated_se_info = 0;
	}
	//hwlog_info("%s: g_nfc_activated_se_info:%d\n", __func__, g_nfc_activated_se_info);
	return (ssize_t)count;
}


static int recovery_close_nfc(struct i2c_client *client, struct  pn547_dev *pdev)
{
	int ret;
	int nfc_rece_length = 40;

	unsigned char recvBuf[40] = {0};

	const  char send_reset[] = {0x20, 0x00, 0x01, 0x00};
	const  char init_cmd[] = {0x20, 0x01, 0x00};

	unsigned char set_ven_config[] = {0x20,0x02,0x05,0x01,0xA0,0x07,0x01,0x02};
	//unsigned char get_ven_config[] = {0x40,0x02,0x02,0x00,0x00};

	/*hardware reset*/
	/* power on */
	gpio_set_value(pdev->firm_gpio, 0);
	gpio_set_value(pdev->ven_gpio, 1);
	msleep(20);

	/* power off */
	gpio_set_value(pdev->ven_gpio, 0);
	msleep(60);

	/* power on */
	gpio_set_value(pdev->ven_gpio, 1);
	msleep(20);

	/*write CORE_RESET_CMD*/
	ret = pn547_i2c_write(pdev, send_reset, sizeof(send_reset));
	if (ret < 0) {
		pr_err("%s: pn547_i2c_write failed, ret:%d\n", __func__, ret);
		goto failed;
	}
	/*read response*/
	ret = pn547_i2c_read(pdev, recvBuf, nfc_rece_length);
	if (ret < 0) {
		pr_err("%s: pn547_i2c_read failed, ret:%d\n", __func__, ret);
		goto failed;
	}

	udelay(500);
	/*write CORE_INIT_CMD*/
	ret = pn547_i2c_write(pdev, init_cmd, sizeof(init_cmd));
	if (ret < 0) {
		pr_err("%s: pn547_i2c_write failed, ret:%d\n", __func__, ret);
		goto failed;
	}
	/*read response*/
	ret = pn547_i2c_read(pdev, recvBuf, nfc_rece_length);
	if (ret < 0) {
		pr_err("%s: pn547_i2c_read failed, ret:%d\n", __func__, ret);
		goto failed;
	}

	msleep(10);

	/*write set_ven_config*/
	ret = pn547_i2c_write(pdev, set_ven_config, sizeof(set_ven_config));
	if (ret < 0) {
		pr_err("%s: pn547_i2c_write failed, ret:%d\n", __func__, ret);
		goto failed;
	}
	/*read response*/
	ret = pn547_i2c_read(pdev, recvBuf, nfc_rece_length);
	if (ret < 0) {
		pr_err("%s: pn547_i2c_read failed, ret:%d\n", __func__, ret);
		goto failed;
	}

	return 0;
failed:
	return -1;
}


static ssize_t nfc_recovery_close_nfc_store(struct device *dev, struct device_attribute *attr,
            const char *buf, size_t count)
{
    return (ssize_t)count;
}

static ssize_t nfc_recovery_close_nfc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int status = -1;
	struct i2c_client *i2c_client_dev = container_of(dev, struct i2c_client, dev);
	struct pn547_dev *pn547_dev;
	pn547_dev = i2c_get_clientdata(i2c_client_dev);
	if (pn547_dev == NULL) {
		pr_err("%s: pn547_dev == NULL!\n", __func__);
		return status;
	}
	pr_info("%s: enter!\n", __func__);
	status = recovery_close_nfc(i2c_client_dev, pn547_dev);
	if (status < 0) {
		pr_err("%s: check_sim_status error!\n", __func__);
	}
	pr_info("%s: status=%d\n", __func__, status);
	return (ssize_t)(snprintf(buf, MAX_ATTRIBUTE_BUFFER_SIZE-1, "%d\n", status));
}

/*register device node to communication with user space*/
static struct device_attribute pn547_attr[] ={
    __ATTR(nfc_fwupdate, 0664, nfc_fwupdate_show, nfc_fwupdate_store),
    __ATTR(nxp_config_name, 0664, nxp_config_name_show, nxp_config_name_store),
    __ATTR(nfc_brcm_conf_name, 0664, nfc_brcm_conf_name_show, nfc_brcm_conf_name_store),
    __ATTR(nfc_sim_switch, 0664, nfc_sim_switch_show, nfc_sim_switch_store),
    __ATTR(nfc_sim_status, 0444, nfc_sim_status_show, NULL),
    __ATTR(rd_nfc_sim_status, 0444, rd_nfc_sim_status_show, NULL),
    __ATTR(nfc_enable_status, 0664, nfc_enable_status_show, nfc_enable_status_store),
    __ATTR(nfc_card_num, 0444, nfc_card_num_show, NULL),
    __ATTR(nfc_chip_type, 0444, nfc_chip_type_show, NULL),
    __ATTR(nfc_single_channel, 0444, nfc_single_channel_show, NULL),
    __ATTR(nfc_hal_dmd, 0664, nfc_hal_dmd_info_show, nfc_hal_dmd_info_store),
    __ATTR(nfc_at_result, 0664, nfc_at_result_show, nfc_at_result_store),
    __ATTR(nfc_switch_state, 0664, nfc_switch_state_show, nfc_switch_state_store),
    __ATTR(nfc_activated_se_info, 0664, nfc_activated_se_info_show, nfc_activated_se_info_store),
    __ATTR(nfc_recovery_close_nfc, 0664, nfc_recovery_close_nfc_show, nfc_recovery_close_nfc_store),
};

/*FUNCTION: create_sysfs_interfaces
  *DESCRIPTION: create_sysfs_interfaces.
  *Parameters
  * struct device *dev:device structure
  *RETURN VALUE
  * int:  result */
static int create_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(pn547_attr); i++) {
		if (device_create_file(dev, pn547_attr + i)) {
			goto error;
		}
	}

	return 0;
error:
	for ( ; i >= 0; i--) {
		device_remove_file(dev, pn547_attr + i);
	}

	pr_err("%s:pn547 unable to create sysfs interface.\n", __func__ );
	return -1;
}
/*FUNCTION: remove_sysfs_interfaces
  *DESCRIPTION: remove_sysfs_interfaces.
  *Parameters
  * struct device *dev:device structure
  *RETURN VALUE
  * int:  result */
static int remove_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(pn547_attr); i++) {
		device_remove_file(dev, pn547_attr + i);
	}

	return 0;
}

static int nfc_ven_low_beforepwd(struct notifier_block *this, unsigned long code,
                void *unused)
{
    pr_info("[%s]: enter!\n", __func__);
    if(nfcdev == NULL)
    {
        pr_err("[%s]: nfcdev NULL.don't need close nfc!\n", __func__);
        return 0;
    }
    gpio_set_value(nfcdev->ven_gpio,0 );
    msleep(10);
    return 0;
}

static struct notifier_block nfc_ven_low_notifier = {
    .notifier_call = nfc_ven_low_beforepwd,
};

/*FUNCTION: check_pn547
  *DESCRIPTION: To test if nfc chip is ok
  *Parameters
  * struct i2c_client *client:i2c device structure
  * struct  pn547_dev *pdev:device structure
  *RETURN VALUE
  * int: check result */
static int check_pn547(struct i2c_client *client, struct  pn547_dev *pdev)
{
	int ret = -1;
	int count = 0;
	const char host_to_pn547[1] = {0x20};
	const char firm_dload_cmd[8]={0x00, 0x04, 0xD0, 0x09, 0x00, 0x00, 0xB1, 0x84};
	//pr_err("%s:check pn547 jump \n", __func__);
	//return 0;
	/* power on */
	gpio_set_value(pdev->firm_gpio, 0);
	pn547_enable_nfc(pdev);

	do {
		ret = i2c_master_send(client, host_to_pn547, sizeof(host_to_pn547));
		if (ret < 0) {
			pr_err("%s:pn547_i2c_write failed and ret = %d,at %d times\n", __func__, ret, count);
		} else {
			pr_info("%s:pn547_i2c_write success and ret = %d,at %d times\n", __func__, ret, count);
			msleep(10);
			pn547_enable_nfc(pdev);

			break;
		}
		count++;
		msleep(10);
	} while (count < NFC_TRY_NUM);

	/*In case firmware dload failed, will cause host_to_pn547 cmd send failed*/
	for (count = 0; count < NFC_TRY_NUM; count++) {
		gpio_set_value(pdev->firm_gpio, 1);
		pn547_enable_nfc(pdev);

		ret = i2c_master_send(client, firm_dload_cmd, sizeof(firm_dload_cmd));
		if (ret < 0) {
			pr_err("%s:pn547_i2c_write download cmd failed:%d, ret = %d\n", __func__, count, ret);
			continue;
		}
		gpio_set_value(pdev->firm_gpio, 0);
		pn547_enable_nfc(pdev);

		break;
	}

	gpio_set_value(pdev->firm_gpio, 0);
	gpio_set_value(pdev->ven_gpio, 0);

	return ret;
}

/*FUNCTION: pn547_dev_open
  *DESCRIPTION: pn547_dev_open, used by user space to enable pn547
  *Parameters
  * struct inode *inode:device inode
  * struct file *filp:device file
  *RETURN VALUE
  * int: result */
static int pn547_dev_open(struct inode *inode, struct file *filp)
{
	struct pn547_dev *pn547_dev = container_of(filp->private_data,
						struct pn547_dev,
						pn547_device);
	filp->private_data = pn547_dev;
	pn547_enable_irq(pn547_dev);
	pr_err("%s : %d,%d\n", __func__, imajor(inode), iminor(inode));

	return 0;
}

/*FUNCTION: pn547_dev_ioctl
  *DESCRIPTION: pn547_dev_ioctl, used by user space
  *Parameters
  * struct file *filp:device file
  * unsigned int cmd:command
  * unsigned long arg:parameters
  *RETURN VALUE
  * long: result */
static long pn547_dev_ioctl(struct file *filp,
			    unsigned int cmd, unsigned long arg)
{
	struct pn547_dev *pn547_dev = filp->private_data;

	pr_info("%s ++    cmd = 0x%x \n", __func__,cmd);

	switch (cmd) {
	case PN544_SET_PWR:
		if (arg == 2) {
			/* power on with firmware download (requires hw reset)
			 */
			gpio_set_value(pn547_dev->ven_gpio,0);
			gpio_set_value(pn547_dev->firm_gpio, 1);
			msleep(60);
			gpio_set_value(pn547_dev->ven_gpio, 0);
			msleep(60);
			gpio_set_value(pn547_dev->ven_gpio, 1);
			msleep(60);
		} else if (arg == 1) {
			/* power on */
			pr_err("%s power on\n", __func__);
			gpio_set_value(pn547_dev->firm_gpio, 0);
			gpio_set_value(pn547_dev->ven_gpio, 1);// 1
			pn547_enable_irq_wake(pn547_dev);
			msleep(20);
		} else  if (arg == 0) {
			/* power off */
			pr_err("%s power off\n", __func__);
			gpio_set_value(pn547_dev->firm_gpio, 0);
			gpio_set_value(pn547_dev->ven_gpio, 0); //0
			pn547_disable_irq_wake(pn547_dev);
			msleep(60);
		} else if (arg == 3) {
			pr_info("%s Read Cancel\n", __func__);
			pn547_dev->cancel_read = true;
			pn547_dev->do_reading = 1;
			wake_up(&pn547_dev->read_wq);
		} else {
			pr_err("%s bad arg %lu\n", __func__, arg);
			return -EINVAL;
		}
		break;
	default:
		pr_err("%s bad ioctl 0x%x\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations pn547_dev_fops = {
	.owner	= THIS_MODULE,
	.llseek	= no_llseek,
	.read	= pn547_dev_read,
	.write	= pn547_dev_write,
	.open	= pn547_dev_open,
	.unlocked_ioctl	= pn547_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = pn547_dev_ioctl,
#endif
};

/*FUNCTION: pn547_parse_dt
  *DESCRIPTION: pn547_parse_dt, get gpio configuration from device tree system
  *Parameters
  * struct device *dev:device data
  * struct pn547_i2c_platform_data *pdata:i2c data
  *RETURN VALUE
  * int: result */
static int pn547_parse_dt(struct device *dev,
			 struct pn547_i2c_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
 	int ret = 0;

	/*int gpio*/
	pdata->irq_gpio =  of_get_named_gpio_flags(np, "nxp,nfc_int", 0,NULL);
	pr_err( "pdata->irq_gpio = %d \n", pdata->irq_gpio);
	if (pdata->irq_gpio < 0) {
		pr_err( "failed to get \"huawei,nfc_int\"\n");
		goto err;
	}

	/*nfc_fm_dload gpio*/
	pdata->fwdl_en_gpio = of_get_named_gpio_flags(np, "nxp,nfc_firm", 0,NULL);
	pr_err( "pdata->fwdl_en_gpio = %d \n", pdata->fwdl_en_gpio);
	if (pdata->fwdl_en_gpio< 0) {
		pr_err( "failed to get \"huawei,nfc_firm\"\n");
		goto err;
	}

	/*nfc_ven gpio*/
	pdata->ven_gpio = of_get_named_gpio_flags(np, "nxp,nfc_ven", 0,NULL);
	pr_err( "pdata->ven_gpio = %d \n", pdata->ven_gpio);
	if (pdata->ven_gpio < 0) {
		pr_err( "failed to get \"huawei,nfc_ven\"\n");
		goto err;
	}

	/*nfc_clk_req gpio*/
	/*pdata->clk_req_gpio = of_get_named_gpio(np, "nxp,nfc_clk", 0);
	pr_err( "pdata->clk_req_gpio = %d \n", pdata->clk_req_gpio);
	if (pdata->clk_req_gpio < 0) {
		pr_err( "failed to get \"huawei,nfc_clk\"\n");
		goto err;
	}*/

#if 0
	of_property_read_u32_array(np, "nxp,nfc_ven", &(pdata->ven_gpio), 1);	
	of_property_read_u32_array(np, "nxp,nfc_firm", &(pdata->fwdl_en_gpio), 1);
	of_property_read_u32_array(np, "nxp,nfc_int", &(pdata->irq_gpio), 1);
	of_property_read_u32_array(np, "nxp,nfc_clk", &(pdata->clk_req_gpio), 1);
	pdata->ven_gpio = 50;
	pdata->fwdl_en_gpio = 8;
	pdata->irq_gpio = 14;
	pdata->clk_req_gpio = 136;
#endif
	pr_info("%s :clk-req-gpio=%d, irq_gpio=%d, fwdl_en_gpio=%d, ven_gpio=%d \n",__func__,pdata->clk_req_gpio, 
		pdata->irq_gpio, pdata->fwdl_en_gpio,pdata->ven_gpio);

err:
	return ret;
}
/*
static struct gpiomux_setting nfc_irq_act = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv  = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_DOWN,
};
static struct gpiomux_setting nfc_irq_sus = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv  = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_DOWN,
};
static struct gpiomux_setting nfc_fwdl_act = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv  = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_DOWN,
};
static struct gpiomux_setting nfc_fwdl_sus = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv  = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_DOWN,
};
*/

/*FUNCTION: pn547_gpio_request
  *DESCRIPTION: pn547_gpio_request, nfc gpio configuration
  *Parameters
  * struct device *dev:device data
  * struct pn547_i2c_platform_data *pdata:i2c data
  *RETURN VALUE
  * int: result */
static int pn547_gpio_request(struct device *dev,
				struct pn547_i2c_platform_data *pdata)
{
	int ret = -1;
//	int gpio_config=0;

	pr_info("%s : pn547_gpio_request enter\n", __func__);

	//NFC_INT
	ret = gpio_request(pdata->irq_gpio, "nfc_int");
	if(ret){
		goto err_irq;
	}
//	msm_gpiomux_write(pdata->irq_gpio,GPIOMUX_ACTIVE, &nfc_irq_act, NULL);
//	msm_gpiomux_write(pdata->irq_gpio,GPIOMUX_SUSPENDED, &nfc_irq_sus, NULL);
//	gpio_config = GPIO_CFG(pdata->irq_gpio, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA);
//	gpio_tlmm_config(gpio_config, GPIO_CFG_ENABLE);
	ret = gpio_direction_input(pdata->irq_gpio);
	if(ret){
		goto err_fwdl_en;
	}

	//NFC_FWDL
	ret = gpio_request(pdata->fwdl_en_gpio, "nfc_wake");
	if(ret){
		goto err_fwdl_en;
	}
//	msm_gpiomux_write(pdata->fwdl_en_gpio,GPIOMUX_ACTIVE, &nfc_fwdl_act, NULL);
//	msm_gpiomux_write(pdata->fwdl_en_gpio,GPIOMUX_SUSPENDED, &nfc_fwdl_sus, NULL);
//	gpio_config = GPIO_CFG(pdata->fwdl_en_gpio, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA);
//	gpio_tlmm_config(gpio_config, GPIO_CFG_ENABLE);
	ret = gpio_direction_output(pdata->fwdl_en_gpio,0);
	if(ret){
		goto err_ven;
	}

	//NFC_VEN
	ret=gpio_request(pdata->ven_gpio,"nfc_ven");
	if(ret){
		goto err_ven;
	}
	ret = gpio_direction_output(pdata->ven_gpio, 0);
	if(ret){
		goto err_clk_req;
	}

	//NFC_CLKReq
	/*ret=gpio_request(pdata->clk_req_gpio,"nfc_clk_req");
	if(ret){
		goto err_clk_req;
	}
	ret = gpio_direction_input(pdata->clk_req_gpio);
	if(ret){
		goto err_clk_input;
	}*/

	return 0;

//err_clk_input:
//	gpio_free(pdata->clk_req_gpio);
err_clk_req:
	gpio_free(pdata->ven_gpio);
err_ven:
	gpio_free(pdata->fwdl_en_gpio);
err_fwdl_en:
	gpio_free(pdata->irq_gpio);
err_irq:

	pr_err( "%s: gpio request err %d\n", __func__, ret);
	return ret;
}
/*FUNCTION: pn547_gpio_release
  *DESCRIPTION: pn547_gpio_release, release nfc gpio
  *Parameters
  * struct pn547_i2c_platform_data *pdata:i2c data
  *RETURN VALUE
  * none */
static void pn547_gpio_release(struct pn547_i2c_platform_data *pdata)
{
	gpio_free(pdata->ven_gpio);
	gpio_free(pdata->irq_gpio);
	gpio_free(pdata->fwdl_en_gpio);
	//gpio_free(pdata->clk_req_gpio);
}

/*static int pn547_pinctrl_init(struct pn547_dev *pn547_dev)
{
	int ret = 0;

	pn547_dev->pn547_pinctrl = devm_pinctrl_get(pn547_dev->dev);
	if (IS_ERR_OR_NULL(pn547_dev->pn547_pinctrl)) {
		ret = PTR_ERR(pn547_dev->pn547_pinctrl);
		pr_err("%s: get pn547_pinctrl error: %d\n", __func__, ret);
		goto err_pinctrl_get;
	}

	pn547_dev->pinctrl_state_active = pinctrl_lookup_state(
			pn547_dev->pn547_pinctrl, NFC_PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(pn547_dev->pinctrl_state_active)) {
		ret = PTR_ERR(pn547_dev->pinctrl_state_active);
		pr_err("%s: get pinctrl_state_active error: %d\n", __func__, ret);
		goto err_pinctrl_lookup;
	}
	pn547_dev->pinctrl_state_suspend = pinctrl_lookup_state(
			pn547_dev->pn547_pinctrl, NFC_PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(pn547_dev->pinctrl_state_suspend)) {
		pr_err("%s: get pinctrl_state_suspend error: %d\n", __func__, ret);
		goto err_pinctrl_lookup;
	}

	ret = pinctrl_select_state(pn547_dev->pn547_pinctrl, pn547_dev->pinctrl_state_active);
	if (!ret) {
		pr_info("%s: pn547 select pinctrl_state_active success\n", __func__);
		return 0;
	} else {
		pr_err("%s: select pinctrl_state_active error: %d\n", __func__, ret);
	}

err_pinctrl_lookup:
	devm_pinctrl_put(pn547_dev->pn547_pinctrl);
err_pinctrl_get:
	pn547_dev->pn547_pinctrl = NULL;
	return ret;
}*/

/*FUNCTION: pn547_probe
  *DESCRIPTION: pn547_probe
  *Parameters
  * struct i2c_client *client:i2c client data
  * const struct i2c_device_id *id:i2c device id
  *RETURN VALUE
  * int: result */
static int pn547_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = 0;
	struct clk *nfc_clk = NULL;
	struct pn547_i2c_platform_data *platform_data;
	struct pn547_dev *pn547_dev;
	//int aa=100;

	dev_dbg(&client->dev, "%s begin:2\n", __func__);
#ifdef CONFIG_HUAWEI_DSM
	if(!nfc_dclient){
	   nfc_dclient = dsm_register_client (&dsm_nfc);
	}
#endif

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s : need I2C_FUNC_I2C\n", __func__);
		return  -ENODEV;
	}

	/*create interface node*/
	ret = create_sysfs_interfaces(&client->dev);
	if (ret < 0) {
		pr_err("Failed to create_sysfs_interfaces\n");
		return -ENODEV;
	}
	ret = sysfs_create_link(NULL,&client->dev.kobj,"nfc");
	if(ret < 0) {
		pr_err("Failed to sysfs_create_link\n");
		return -ENODEV;
	}
	platform_data = kzalloc(sizeof(struct pn547_i2c_platform_data),
				GFP_KERNEL);
	if (platform_data == NULL) {
		dev_err(&client->dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_platform_data;
	}

	/*get gpio config*/
	ret = pn547_parse_dt(&client->dev, platform_data);
	if (ret < 0) {
		dev_err(&client->dev, "failed to parse device tree: %d\n", ret);
		goto err_parse_dt;
	}

	/*config nfc clock*/
	//int ret = 0;
	/*ret = pmic_config_interface((unsigned int)(PMIC_XO_EXTBUF3_EN_M_ADDR), (unsigned int)(1),
		(unsigned int)(PMIC_XO_EXTBUF3_EN_M_MASK),
		(unsigned int)(PMIC_XO_EXTBUF3_EN_M_SHIFT));

	ret = pmic_config_interface((unsigned int)(PMIC_XO_EXTBUF3_MODE_ADDR), (unsigned int)(1),
		(unsigned int)(PMIC_XO_EXTBUF3_MODE_MASK),
		(unsigned int)(PMIC_XO_EXTBUF3_MODE_SHIFT));
	*/
	
	
	/*clock set complete**/
	
	
	/*nfc_clk  = clk_get(&client->dev, "pn547_clk");
	if (nfc_clk == NULL) {
		dev_err(&client->dev, "failed to get clk: %d\n", ret);
		goto err_parse_dt;
	}
	(void)clk_set_rate(nfc_clk, 26000000);
	ret = clk_prepare_enable(nfc_clk);
	if (ret) {
		dev_err(&client->dev, "failed to enable clk: %d\n", ret);

#ifdef CONFIG_HUAWEI_DSM
		nfc_dsm_report(DSM_NFC_CLK_ENABLE_ERROR_NO, ret);
#endif

		goto err_gpio_request;
	}*/

	/*config nfc gpio*/
	ret = pn547_gpio_request(&client->dev, platform_data);
	if (ret) {
		dev_err(&client->dev, "failed to request gpio\n");
		goto err_gpio_request;
	}

	pn547_dev = kzalloc(sizeof(*pn547_dev), GFP_KERNEL);
	if (pn547_dev == NULL) {
		dev_err(&client->dev,
				"failed to allocate memory for module data\n");
		ret = -ENOMEM;
		goto err_exit;
	}
	client->irq = gpio_to_irq(platform_data->irq_gpio);

	pn547_dev->irq_gpio = platform_data->irq_gpio;
	pn547_dev->ven_gpio  = platform_data->ven_gpio;
	pn547_dev->firm_gpio  = platform_data->fwdl_en_gpio;
	//pn547_dev->clk_req_gpio = platform_data->clk_req_gpio;
	pn547_dev->client   = client;
	pn547_dev->dev = &client->dev;
	pn547_dev->do_reading = 0;
	pn547_dev->sim_switch = CARD1_SELECT;/*sim_select = 1,UICC select*/
	pn547_dev->sim_status = CARD_UNKNOWN;
	pn547_dev->enable_status = ENABLE_START;
	pn547_dev->nfc_clk = nfc_clk;
	pn547_dev->irq_wake_enabled = false;
	/*ret = pn547_pinctrl_init(pn547_dev);
	if (ret != 0) {
		pr_err("%s: pn547_pinctrl_init failed\n", __func__);
		goto err_pinctrl;
	}*/
	nfcdev = pn547_dev;
	/*notifier for supply shutdown*/
	pr_err("%s:register_reboot_notifier\n", __func__);
	register_reboot_notifier(&nfc_ven_low_notifier);
	pr_err("%s:check pn547\n", __func__);
	/*check if nfc chip is ok*/
	ret = check_pn547(client, pn547_dev);

	if(ret < 0){
		pr_err("%s: pn547 check failed \n", __func__);
		//while(--aa)
		//{
		//	gpio_set_value(pn547_dev->ven_gpio, 1);
		//	pr_err("%s: pn547 check wangxiaoli 1 \n", __func__);
		//	msleep(1000);
		//	gpio_set_value(pn547_dev->ven_gpio, 0);
		//}
		goto err_i2c;
	}
	

	gpio_set_value(pn547_dev->firm_gpio, 0);
	gpio_set_value(pn547_dev->ven_gpio, 0); //0

	/* Initialise mutex and work queue */
	init_waitqueue_head(&pn547_dev->read_wq);
	mutex_init(&pn547_dev->read_mutex);
	mutex_init(&pn547_dev->irq_wake_mutex);
	spin_lock_init(&pn547_dev->irq_enabled_lock);
    wakeup_source_init(&pn547_dev->wl,"nfc_locker");
	pn547_dev->pn547_device.minor = MISC_DYNAMIC_MINOR;
	pn547_dev->pn547_device.name = "pn544";
	pn547_dev->pn547_device.fops = &pn547_dev_fops;
	ret = misc_register(&pn547_dev->pn547_device);
	if (ret) {
		dev_err(&client->dev, "%s: misc_register err %d\n",
			__func__, ret);
		goto err_misc_register;
	}
	 	/* set clk*/
	//clk_buf_ctrl_internal(CLK_BUF_NFC, true);
	//clk_buf_ctrl(CLK_BUF_NFC, true);
    
	/* request irq.  the irq is set whenever the chip has data available
	 * for reading.  it is cleared when all data has been read.
	 */
	dev_info(&client->dev, "%s : requesting IRQ %d\n",
		__func__, client->irq);
	pn547_dev->irq_enabled = true;
	ret = request_irq(client->irq, pn547_dev_irq_handler,
			IRQF_TRIGGER_HIGH | IRQF_NO_SUSPEND | IRQF_ONESHOT,
			client->name, pn547_dev);
	if (ret) {
		dev_err(&client->dev, "request_irq failed\n");
		goto err_request_irq_failed;
	}
	
	pn547_disable_irq(pn547_dev);
	i2c_set_clientdata(client, pn547_dev);

#ifdef CONFIG_HUAWEI_HW_DEV_DCT
	/* detect current device successful, set the flag as present */
	set_hw_dev_flag(DEV_I2C_NFC);
#endif

#ifdef CONFIG_HUAWEI_DEV_SELFCHECK
	/* detect current device successful, set the flag as present */
	set_hw_dev_detect_result(DEV_DETECT_NFC);
	pr_info("%s set_hw_dev_detect_result success.\n", __func__);
#endif

	/* get and save configure name*/
	get_nfc_config_name(&client->dev);
	set_nfc_single_channel(&client->dev);
	//lcd_status = read_lcd_type();
	pr_info("%s success.\n", __func__);
	return 0;

err_request_irq_failed:
	misc_deregister(&pn547_dev->pn547_device);

err_misc_register:
	mutex_destroy(&pn547_dev->read_mutex);
	mutex_destroy(&pn547_dev->irq_wake_mutex);
	kfree(pn547_dev);
err_exit:
err_i2c:
	pn547_gpio_release(platform_data);
//err_pinctrl:
err_gpio_request:
	/*if (nfc_clk) {
		clk_put(nfc_clk);
		nfc_clk = NULL;
	}*/
err_parse_dt:
	kfree(platform_data);
err_platform_data:
	remove_sysfs_interfaces(&client->dev);

	dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	return ret;
}

/*FUNCTION: pn547_remove
  *DESCRIPTION: pn547_remove
  *Parameters
  * struct i2c_client *client:i2c client data
  *RETURN VALUE
  * int: result */
static int pn547_remove(struct i2c_client *client)
{
	struct pn547_dev *pn547_dev;
	dev_info(&client->dev, "%s ++\n", __func__);
    unregister_reboot_notifier(&nfc_ven_low_notifier);
	pn547_dev = i2c_get_clientdata(client);
    gpio_set_value(pn547_dev->ven_gpio, 0); //0
	free_irq(client->irq, pn547_dev);
	misc_deregister(&pn547_dev->pn547_device);
	mutex_destroy(&pn547_dev->read_mutex);
	mutex_destroy(&pn547_dev->irq_wake_mutex);
    wakeup_source_trash(&pn547_dev->wl);
	remove_sysfs_interfaces(&client->dev);
	if (pn547_dev->nfc_clk) {
		clk_put(pn547_dev->nfc_clk);
		pn547_dev->nfc_clk = NULL;
	}
	gpio_free(pn547_dev->irq_gpio);
	gpio_free(pn547_dev->ven_gpio);
	gpio_free(pn547_dev->firm_gpio);
	//gpio_free(pn547_dev->clk_req_gpio);
	kfree(pn547_dev);

	return 0;
}

static const struct i2c_device_id pn547_id[] = {
	{ "pn547", 0 },
	{ }
};

static struct of_device_id pn547_match_table[] = {
	{ .compatible = "mediatek,nq-pn544", },
	{ },
};

static struct i2c_driver pn547_driver = {
	.id_table	= pn547_id,
	.probe		= pn547_probe,
	.remove		= pn547_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "pn547",
		.of_match_table	= pn547_match_table,
	},
};

/*
 * module load/unload record keeping
 */

static int __init pn547_dev_init(void)
{
	pr_info("### %s begin! \n",__func__);
	return i2c_add_driver(&pn547_driver);
}
module_init(pn547_dev_init);

static void __exit pn547_dev_exit(void)
{
	i2c_del_driver(&pn547_driver);
}
module_exit(pn547_dev_exit);

MODULE_AUTHOR("SERI");
MODULE_DESCRIPTION("NFC pn547 driver");
MODULE_LICENSE("GPL");
