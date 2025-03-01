/*
 * Copyright © 2009 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include <drm/drm_dp_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <drm/drm_dp_mst_helper.h>

#include "drm_crtc_helper_internal.h"

/**
 * DOC: dp helpers
 *
 * These functions contain some common logic and helpers at various abstraction
 * levels to deal with Display Port sink devices and related things like DP aux
 * channel transfers, EDID reading over DP aux channels, decoding certain DPCD
 * blocks, ...
 */

/* Helpers for DP link training */
static u8 dp_link_status(const u8 link_status[DP_LINK_STATUS_SIZE], int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

static u8 dp_get_lane_status(const u8 link_status[DP_LINK_STATUS_SIZE],
			     int lane)
{
	int i = DP_LANE0_1_STATUS + (lane >> 1);
	int s = (lane & 1) * 4;
	u8 l = dp_link_status(link_status, i);
	return (l >> s) & 0xf;
}

bool drm_dp_channel_eq_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			  int lane_count)
{
	u8 lane_align;
	u8 lane_status;
	int lane;

	lane_align = dp_link_status(link_status,
				    DP_LANE_ALIGN_STATUS_UPDATED);
	if ((lane_align & DP_INTERLANE_ALIGN_DONE) == 0)
		return false;
	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_CHANNEL_EQ_BITS) != DP_CHANNEL_EQ_BITS)
			return false;
	}
	return true;
}
EXPORT_SYMBOL(drm_dp_channel_eq_ok);

bool drm_dp_clock_recovery_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			      int lane_count)
{
	int lane;
	u8 lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_LANE_CR_DONE) == 0)
			return false;
	}
	return true;
}
EXPORT_SYMBOL(drm_dp_clock_recovery_ok);

u8 drm_dp_get_adjust_request_voltage(const u8 link_status[DP_LINK_STATUS_SIZE],
				     int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
		 DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_VOLTAGE_SWING_SHIFT;
}
EXPORT_SYMBOL(drm_dp_get_adjust_request_voltage);

u8 drm_dp_get_adjust_request_pre_emphasis(const u8 link_status[DP_LINK_STATUS_SIZE],
					  int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
		 DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_PRE_EMPHASIS_SHIFT;
}
EXPORT_SYMBOL(drm_dp_get_adjust_request_pre_emphasis);

u8 drm_dp_get_adjust_request_post_cursor(const u8 link_status[DP_LINK_STATUS_SIZE],
					 unsigned int lane)
{
	unsigned int offset = DP_ADJUST_REQUEST_POST_CURSOR2;
	u8 value = dp_link_status(link_status, offset);

	return (value >> (lane << 1)) & 0x3;
}
EXPORT_SYMBOL(drm_dp_get_adjust_request_post_cursor);

void drm_dp_link_train_clock_recovery_delay(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	unsigned long rd_interval = dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
					 DP_TRAINING_AUX_RD_MASK;

	if (rd_interval > 4)
		DRM_DEBUG_KMS("AUX interval %lu, out of range (max 4)\n",
			      rd_interval);

	if (rd_interval == 0 || dpcd[DP_DPCD_REV] >= DP_DPCD_REV_14)
		rd_interval = 100;
	else
		rd_interval *= 4 * USEC_PER_MSEC;

	usleep_range(rd_interval, rd_interval * 2);
}
EXPORT_SYMBOL(drm_dp_link_train_clock_recovery_delay);

void drm_dp_link_train_channel_eq_delay(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	unsigned long rd_interval = dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
					 DP_TRAINING_AUX_RD_MASK;

	if (rd_interval > 4)
		DRM_DEBUG_KMS("AUX interval %lu, out of range (max 4)\n",
			      rd_interval);

	if (rd_interval == 0)
		rd_interval = 400;
	else
		rd_interval *= 4 * USEC_PER_MSEC;

	usleep_range(rd_interval, rd_interval * 2);
}
EXPORT_SYMBOL(drm_dp_link_train_channel_eq_delay);

u8 drm_dp_link_rate_to_bw_code(int link_rate)
{
	/* Spec says link_bw = link_rate / 0.27Gbps */
	return link_rate / 27000;
}
EXPORT_SYMBOL(drm_dp_link_rate_to_bw_code);

int drm_dp_bw_code_to_link_rate(u8 link_bw)
{
	/* Spec says link_rate = link_bw * 0.27Gbps */
	return link_bw * 27000;
}
EXPORT_SYMBOL(drm_dp_bw_code_to_link_rate);

#define AUX_RETRY_INTERVAL 500 /* us */

static inline void
drm_dp_dump_access(const struct drm_dp_aux *aux,
		   u8 request, uint offset, void *buffer, int ret)
{
	const char *arrow = request == DP_AUX_NATIVE_READ ? "->" : "<-";

	if (ret > 0)
		DRM_DEBUG_DP("%s: 0x%05x AUX %s (ret=%3d) %*ph\n",
			     aux->name, offset, arrow, ret, min(ret, 20), buffer);
	else
		DRM_DEBUG_DP("%s: 0x%05x AUX %s (ret=%3d)\n",
			     aux->name, offset, arrow, ret);
}

/**
 * DOC: dp helpers
 *
 * The DisplayPort AUX channel is an abstraction to allow generic, driver-
 * independent access to AUX functionality. Drivers can take advantage of
 * this by filling in the fields of the drm_dp_aux structure.
 *
 * Transactions are described using a hardware-independent drm_dp_aux_msg
 * structure, which is passed into a driver's .transfer() implementation.
 * Both native and I2C-over-AUX transactions are supported.
 */

static int drm_dp_dpcd_access(struct drm_dp_aux *aux, u8 request,
			      unsigned int offset, void *buffer, size_t size)
{
	struct drm_dp_aux_msg msg;
	unsigned int retry, native_reply;
	int err = 0, ret = 0;

	memset(&msg, 0, sizeof(msg));
	msg.address = offset;
	msg.request = request;
	msg.buffer = buffer;
	msg.size = size;

	mutex_lock(&aux->hw_mutex);

	/*
	 * The specification doesn't give any recommendation on how often to
	 * retry native transactions. We used to retry 7 times like for
	 * aux i2c transactions but real world devices this wasn't
	 * sufficient, bump to 32 which makes Dell 4k monitors happier.
	 */
	for (retry = 0; retry < 32; retry++) {
		if (ret != 0 && ret != -ETIMEDOUT) {
			usleep_range(AUX_RETRY_INTERVAL,
				     AUX_RETRY_INTERVAL + 100);
		}

		ret = aux->transfer(aux, &msg);
		if (ret >= 0) {
			native_reply = msg.reply & DP_AUX_NATIVE_REPLY_MASK;
			if (native_reply == DP_AUX_NATIVE_REPLY_ACK) {
				if (ret == size)
					goto unlock;

				ret = -EPROTO;
			} else
				ret = -EIO;
		}

		/*
		 * We want the error we return to be the error we received on
		 * the first transaction, since we may get a different error the
		 * next time we retry
		 */
		if (!err)
			err = ret;
	}

	DRM_DEBUG_KMS("%s: Too many retries, giving up. First error: %d\n",
		      aux->name, err);
	ret = err;

unlock:
	mutex_unlock(&aux->hw_mutex);
	return ret;
}

/**
 * drm_dp_dpcd_read() - read a series of bytes from the DPCD
 * @aux: DisplayPort AUX channel (SST or MST)
 * @offset: address of the (first) register to read
 * @buffer: buffer to store the register values
 * @size: number of bytes in @buffer
 *
 * Returns the number of bytes transferred on success, or a negative error
 * code on failure. -EIO is returned if the request was NAKed by the sink or
 * if the retry count was exceeded. If not all bytes were transferred, this
 * function returns -EPROTO. Errors from the underlying AUX channel transfer
 * function, with the exception of -EBUSY (which causes the transaction to
 * be retried), are propagated to the caller.
 */
ssize_t drm_dp_dpcd_read(struct drm_dp_aux *aux, unsigned int offset,
			 void *buffer, size_t size)
{
	int ret;

	/*
	 * HP ZR24w corrupts the first DPCD access after entering power save
	 * mode. Eg. on a read, the entire buffer will be filled with the same
	 * byte. Do a throw away read to avoid corrupting anything we care
	 * about. Afterwards things will work correctly until the monitor
	 * gets woken up and subsequently re-enters power save mode.
	 *
	 * The user pressing any button on the monitor is enough to wake it
	 * up, so there is no particularly good place to do the workaround.
	 * We just have to do it before any DPCD access and hope that the
	 * monitor doesn't power down exactly after the throw away read.
	 */
	if (!aux->is_remote) {
		ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, DP_DPCD_REV,
					 buffer, 1);
		if (ret != 1)
			goto out;
	}

	if (aux->is_remote)
		ret = drm_dp_mst_dpcd_read(aux, offset, buffer, size);
	else
		ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, offset,
					 buffer, size);

out:
	drm_dp_dump_access(aux, DP_AUX_NATIVE_READ, offset, buffer, ret);
	return ret;
}
EXPORT_SYMBOL(drm_dp_dpcd_read);

/**
 * drm_dp_dpcd_write() - write a series of bytes to the DPCD
 * @aux: DisplayPort AUX channel (SST or MST)
 * @offset: address of the (first) register to write
 * @buffer: buffer containing the values to write
 * @size: number of bytes in @buffer
 *
 * Returns the number of bytes transferred on success, or a negative error
 * code on failure. -EIO is returned if the request was NAKed by the sink or
 * if the retry count was exceeded. If not all bytes were transferred, this
 * function returns -EPROTO. Errors from the underlying AUX channel transfer
 * function, with the exception of -EBUSY (which causes the transaction to
 * be retried), are propagated to the caller.
 */
ssize_t drm_dp_dpcd_write(struct drm_dp_aux *aux, unsigned int offset,
			  void *buffer, size_t size)
{
	int ret;

	if (aux->is_remote)
		ret = drm_dp_mst_dpcd_write(aux, offset, buffer, size);
	else
		ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_WRITE, offset,
					 buffer, size);

	drm_dp_dump_access(aux, DP_AUX_NATIVE_WRITE, offset, buffer, ret);
	return ret;
}
EXPORT_SYMBOL(drm_dp_dpcd_write);

/**
 * drm_dp_dpcd_read_link_status() - read DPCD link status (bytes 0x202-0x207)
 * @aux: DisplayPort AUX channel
 * @status: buffer to store the link status in (must be at least 6 bytes)
 *
 * Returns the number of bytes transferred on success or a negative error
 * code on failure.
 */
int drm_dp_dpcd_read_link_status(struct drm_dp_aux *aux,
				 u8 status[DP_LINK_STATUS_SIZE])
{
	return drm_dp_dpcd_read(aux, DP_LANE0_1_STATUS, status,
				DP_LINK_STATUS_SIZE);
}
EXPORT_SYMBOL(drm_dp_dpcd_read_link_status);

/**
 * drm_dp_send_real_edid_checksum() - send back real edid checksum value
 * @aux: DisplayPort AUX channel
 * @real_edid_checksum: real edid checksum for the last block
 *
 * Returns:
 * True on success
 */
bool drm_dp_send_real_edid_checksum(struct drm_dp_aux *aux,
				    u8 real_edid_checksum)
{
	u8 link_edid_read = 0, auto_test_req = 0, test_resp = 0;

	if (drm_dp_dpcd_read(aux, DP_DEVICE_SERVICE_IRQ_VECTOR,
			     &auto_test_req, 1) < 1) {
		DRM_ERROR("%s: DPCD failed read at register 0x%x\n",
			  aux->name, DP_DEVICE_SERVICE_IRQ_VECTOR);
		return false;
	}
	auto_test_req &= DP_AUTOMATED_TEST_REQUEST;

	if (drm_dp_dpcd_read(aux, DP_TEST_REQUEST, &link_edid_read, 1) < 1) {
		DRM_ERROR("%s: DPCD failed read at register 0x%x\n",
			  aux->name, DP_TEST_REQUEST);
		return false;
	}
	link_edid_read &= DP_TEST_LINK_EDID_READ;

	if (!auto_test_req || !link_edid_read) {
		DRM_DEBUG_KMS("%s: Source DUT does not support TEST_EDID_READ\n",
			      aux->name);
		return false;
	}

	if (drm_dp_dpcd_write(aux, DP_DEVICE_SERVICE_IRQ_VECTOR,
			      &auto_test_req, 1) < 1) {
		DRM_ERROR("%s: DPCD failed write at register 0x%x\n",
			  aux->name, DP_DEVICE_SERVICE_IRQ_VECTOR);
		return false;
	}

	/* send back checksum for the last edid extension block data */
	if (drm_dp_dpcd_write(aux, DP_TEST_EDID_CHECKSUM,
			      &real_edid_checksum, 1) < 1) {
		DRM_ERROR("%s: DPCD failed write at register 0x%x\n",
			  aux->name, DP_TEST_EDID_CHECKSUM);
		return false;
	}

	test_resp |= DP_TEST_EDID_CHECKSUM_WRITE;
	if (drm_dp_dpcd_write(aux, DP_TEST_RESPONSE, &test_resp, 1) < 1) {
		DRM_ERROR("%s: DPCD failed write at register 0x%x\n",
			  aux->name, DP_TEST_RESPONSE);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(drm_dp_send_real_edid_checksum);

/**
 * drm_dp_downstream_max_clock() - extract branch device max
 *                                 pixel rate for legacy VGA
 *                                 converter or max TMDS clock
 *                                 rate for others
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 *
 * Returns max clock in kHz on success or 0 if max clock not defined
 */
int drm_dp_downstream_max_clock(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				const u8 port_cap[4])
{
	int type = port_cap[0] & DP_DS_PORT_TYPE_MASK;
	bool detailed_cap_info = dpcd[DP_DOWNSTREAMPORT_PRESENT] &
		DP_DETAILED_CAP_INFO_AVAILABLE;

	if (!detailed_cap_info)
		return 0;

	switch (type) {
	case DP_DS_PORT_TYPE_VGA:
		return port_cap[1] * 8 * 1000;
	case DP_DS_PORT_TYPE_DVI:
	case DP_DS_PORT_TYPE_HDMI:
	case DP_DS_PORT_TYPE_DP_DUALMODE:
		return port_cap[1] * 2500;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_max_clock);

/**
 * drm_dp_downstream_max_bpc() - extract branch device max
 *                               bits per component
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 *
 * Returns max bpc on success or 0 if max bpc not defined
 */
int drm_dp_downstream_max_bpc(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			      const u8 port_cap[4])
{
	int type = port_cap[0] & DP_DS_PORT_TYPE_MASK;
	bool detailed_cap_info = dpcd[DP_DOWNSTREAMPORT_PRESENT] &
		DP_DETAILED_CAP_INFO_AVAILABLE;
	int bpc;

	if (!detailed_cap_info)
		return 0;

	switch (type) {
	case DP_DS_PORT_TYPE_VGA:
	case DP_DS_PORT_TYPE_DVI:
	case DP_DS_PORT_TYPE_HDMI:
	case DP_DS_PORT_TYPE_DP_DUALMODE:
		bpc = port_cap[2] & DP_DS_MAX_BPC_MASK;

		switch (bpc) {
		case DP_DS_8BPC:
			return 8;
		case DP_DS_10BPC:
			return 10;
		case DP_DS_12BPC:
			return 12;
		case DP_DS_16BPC:
			return 16;
		}
		/* fall through */
	default:
		return 0;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_max_bpc);

/**
 * drm_dp_downstream_id() - identify branch device
 * @aux: DisplayPort AUX channel
 * @id: DisplayPort branch device id
 *
 * Returns branch device id on success or NULL on failure
 */
int drm_dp_downstream_id(struct drm_dp_aux *aux, char id[6])
{
	return drm_dp_dpcd_read(aux, DP_BRANCH_ID, id, 6);
}
EXPORT_SYMBOL(drm_dp_downstream_id);

/**
 * drm_dp_downstream_debug() - debug DP branch devices
 * @m: pointer for debugfs file
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 * @aux: DisplayPort AUX channel
 *
 */
void drm_dp_downstream_debug(struct seq_file *m,
			     const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			     const u8 port_cap[4], struct drm_dp_aux *aux)
{
	bool detailed_cap_info = dpcd[DP_DOWNSTREAMPORT_PRESENT] &
				 DP_DETAILED_CAP_INFO_AVAILABLE;
	int clk;
	int bpc;
	char id[7];
	int len;
	uint8_t rev[2];
	int type = port_cap[0] & DP_DS_PORT_TYPE_MASK;
	bool branch_device = dpcd[DP_DOWNSTREAMPORT_PRESENT] &
			     DP_DWN_STRM_PORT_PRESENT;

	seq_printf(m, "\tDP branch device present: %s\n",
		   branch_device ? "yes" : "no");

	if (!branch_device)
		return;

	switch (type) {
	case DP_DS_PORT_TYPE_DP:
		seq_puts(m, "\t\tType: DisplayPort\n");
		break;
	case DP_DS_PORT_TYPE_VGA:
		seq_puts(m, "\t\tType: VGA\n");
		break;
	case DP_DS_PORT_TYPE_DVI:
		seq_puts(m, "\t\tType: DVI\n");
		break;
	case DP_DS_PORT_TYPE_HDMI:
		seq_puts(m, "\t\tType: HDMI\n");
		break;
	case DP_DS_PORT_TYPE_NON_EDID:
		seq_puts(m, "\t\tType: others without EDID support\n");
		break;
	case DP_DS_PORT_TYPE_DP_DUALMODE:
		seq_puts(m, "\t\tType: DP++\n");
		break;
	case DP_DS_PORT_TYPE_WIRELESS:
		seq_puts(m, "\t\tType: Wireless\n");
		break;
	default:
		seq_puts(m, "\t\tType: N/A\n");
	}

	memset(id, 0, sizeof(id));
	drm_dp_downstream_id(aux, id);
	seq_printf(m, "\t\tID: %s\n", id);

	len = drm_dp_dpcd_read(aux, DP_BRANCH_HW_REV, &rev[0], 1);
	if (len > 0)
		seq_printf(m, "\t\tHW: %d.%d\n",
			   (rev[0] & 0xf0) >> 4, rev[0] & 0xf);

	len = drm_dp_dpcd_read(aux, DP_BRANCH_SW_REV, rev, 2);
	if (len > 0)
		seq_printf(m, "\t\tSW: %d.%d\n", rev[0], rev[1]);

	if (detailed_cap_info) {
		clk = drm_dp_downstream_max_clock(dpcd, port_cap);

		if (clk > 0) {
			if (type == DP_DS_PORT_TYPE_VGA)
				seq_printf(m, "\t\tMax dot clock: %d kHz\n", clk);
			else
				seq_printf(m, "\t\tMax TMDS clock: %d kHz\n", clk);
		}

		bpc = drm_dp_downstream_max_bpc(dpcd, port_cap);

		if (bpc > 0)
			seq_printf(m, "\t\tMax bpc: %d\n", bpc);
	}
}
EXPORT_SYMBOL(drm_dp_downstream_debug);

/*
 * I2C-over-AUX implementation
 */

static u32 drm_dp_i2c_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA |
	       I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
	       I2C_FUNC_10BIT_ADDR;
}

static void drm_dp_i2c_msg_write_status_update(struct drm_dp_aux_msg *msg)
{
	/*
	 * In case of i2c defer or short i2c ack reply to a write,
	 * we need to switch to WRITE_STATUS_UPDATE to drain the
	 * rest of the message
	 */
	if ((msg->request & ~DP_AUX_I2C_MOT) == DP_AUX_I2C_WRITE) {
		msg->request &= DP_AUX_I2C_MOT;
		msg->request |= DP_AUX_I2C_WRITE_STATUS_UPDATE;
	}
}

#define AUX_PRECHARGE_LEN 10 /* 10 to 16 */
#define AUX_SYNC_LEN (16 + 4) /* preamble + AUX_SYNC_END */
#define AUX_STOP_LEN 4
#define AUX_CMD_LEN 4
#define AUX_ADDRESS_LEN 20
#define AUX_REPLY_PAD_LEN 4
#define AUX_LENGTH_LEN 8

/*
 * Calculate the duration of the AUX request/reply in usec. Gives the
 * "best" case estimate, ie. successful while as short as possible.
 */
static int drm_dp_aux_req_duration(const struct drm_dp_aux_msg *msg)
{
	int len = AUX_PRECHARGE_LEN + AUX_SYNC_LEN + AUX_STOP_LEN +
		AUX_CMD_LEN + AUX_ADDRESS_LEN + AUX_LENGTH_LEN;

	if ((msg->request & DP_AUX_I2C_READ) == 0)
		len += msg->size * 8;

	return len;
}

static int drm_dp_aux_reply_duration(const struct drm_dp_aux_msg *msg)
{
	int len = AUX_PRECHARGE_LEN + AUX_SYNC_LEN + AUX_STOP_LEN +
		AUX_CMD_LEN + AUX_REPLY_PAD_LEN;

	/*
	 * For read we expect what was asked. For writes there will
	 * be 0 or 1 data bytes. Assume 0 for the "best" case.
	 */
	if (msg->request & DP_AUX_I2C_READ)
		len += msg->size * 8;

	return len;
}

#define I2C_START_LEN 1
#define I2C_STOP_LEN 1
#define I2C_ADDR_LEN 9 /* ADDRESS + R/W + ACK/NACK */
#define I2C_DATA_LEN 9 /* DATA + ACK/NACK */

/*
 * Calculate the length of the i2c transfer in usec, assuming
 * the i2c bus speed is as specified. Gives the the "worst"
 * case estimate, ie. successful while as long as possible.
 * Doesn't account the the "MOT" bit, and instead assumes each
 * message includes a START, ADDRESS and STOP. Neither does it
 * account for additional random variables such as clock stretching.
 */
static int drm_dp_i2c_msg_duration(const struct drm_dp_aux_msg *msg,
				   int i2c_speed_khz)
{
	/* AUX bitrate is 1MHz, i2c bitrate as specified */
	return DIV_ROUND_UP((I2C_START_LEN + I2C_ADDR_LEN +
			     msg->size * I2C_DATA_LEN +
			     I2C_STOP_LEN) * 1000, i2c_speed_khz);
}

/*
 * Deterine how many retries should be attempted to successfully transfer
 * the specified message, based on the estimated durations of the
 * i2c and AUX transfers.
 */
static int drm_dp_i2c_retry_count(const struct drm_dp_aux_msg *msg,
			      int i2c_speed_khz)
{
	int aux_time_us = drm_dp_aux_req_duration(msg) +
		drm_dp_aux_reply_duration(msg);
	int i2c_time_us = drm_dp_i2c_msg_duration(msg, i2c_speed_khz);

	return DIV_ROUND_UP(i2c_time_us, aux_time_us + AUX_RETRY_INTERVAL);
}

/*
 * FIXME currently assumes 10 kHz as some real world devices seem
 * to require it. We should query/set the speed via DPCD if supported.
 */
static int dp_aux_i2c_speed_khz __read_mostly = 10;
module_param_unsafe(dp_aux_i2c_speed_khz, int, 0644);
MODULE_PARM_DESC(dp_aux_i2c_speed_khz,
		 "Assumed speed of the i2c bus in kHz, (1-400, default 10)");

/*
 * Transfer a single I2C-over-AUX message and handle various error conditions,
 * retrying the transaction as appropriate.  It is assumed that the
 * &drm_dp_aux.transfer function does not modify anything in the msg other than the
 * reply field.
 *
 * Returns bytes transferred on success, or a negative error code on failure.
 */
static int drm_dp_i2c_do_msg(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	unsigned int retry, defer_i2c;
	int ret;
	/*
	 * DP1.2 sections 2.7.7.1.5.6.1 and 2.7.7.1.6.6.1: A DP Source device
	 * is required to retry at least seven times upon receiving AUX_DEFER
	 * before giving up the AUX transaction.
	 *
	 * We also try to account for the i2c bus speed.
	 */
	int max_retries = max(7, drm_dp_i2c_retry_count(msg, dp_aux_i2c_speed_khz));

	for (retry = 0, defer_i2c = 0; retry < (max_retries + defer_i2c); retry++) {
		ret = aux->transfer(aux, msg);
		if (ret < 0) {
			if (ret == -EBUSY)
				continue;

			/*
			 * While timeouts can be errors, they're usually normal
			 * behavior (for instance, when a driver tries to
			 * communicate with a non-existant DisplayPort device).
			 * Avoid spamming the kernel log with timeout errors.
			 */
			if (ret == -ETIMEDOUT)
				DRM_DEBUG_KMS_RATELIMITED("%s: transaction timed out\n",
							  aux->name);
			else
				DRM_DEBUG_KMS("%s: transaction failed: %d\n",
					      aux->name, ret);
			return ret;
		}


		switch (msg->reply & DP_AUX_NATIVE_REPLY_MASK) {
		case DP_AUX_NATIVE_REPLY_ACK:
			/*
			 * For I2C-over-AUX transactions this isn't enough, we
			 * need to check for the I2C ACK reply.
			 */
			break;

		case DP_AUX_NATIVE_REPLY_NACK:
			DRM_DEBUG_KMS("%s: native nack (result=%d, size=%zu)\n",
				      aux->name, ret, msg->size);
			return -EREMOTEIO;

		case DP_AUX_NATIVE_REPLY_DEFER:
			DRM_DEBUG_KMS("%s: native defer\n", aux->name);
			/*
			 * We could check for I2C bit rate capabilities and if
			 * available adjust this interval. We could also be
			 * more careful with DP-to-legacy adapters where a
			 * long legacy cable may force very low I2C bit rates.
			 *
			 * For now just defer for long enough to hopefully be
			 * safe for all use-cases.
			 */
			usleep_range(AUX_RETRY_INTERVAL, AUX_RETRY_INTERVAL + 100);
			continue;

		default:
			DRM_ERROR("%s: invalid native reply %#04x\n",
				  aux->name, msg->reply);
			return -EREMOTEIO;
		}

		switch (msg->reply & DP_AUX_I2C_REPLY_MASK) {
		case DP_AUX_I2C_REPLY_ACK:
			/*
			 * Both native ACK and I2C ACK replies received. We
			 * can assume the transfer was successful.
			 */
			if (ret != msg->size)
				drm_dp_i2c_msg_write_status_update(msg);
			return ret;

		case DP_AUX_I2C_REPLY_NACK:
			DRM_DEBUG_KMS("%s: I2C nack (result=%d, size=%zu)\n",
				      aux->name, ret, msg->size);
			aux->i2c_nack_count++;
			return -EREMOTEIO;

		case DP_AUX_I2C_REPLY_DEFER:
			DRM_DEBUG_KMS("%s: I2C defer\n", aux->name);
			/* DP Compliance Test 4.2.2.5 Requirement:
			 * Must have at least 7 retries for I2C defers on the
			 * transaction to pass this test
			 */
			aux->i2c_defer_count++;
			if (defer_i2c < 7)
				defer_i2c++;
			usleep_range(AUX_RETRY_INTERVAL, AUX_RETRY_INTERVAL + 100);
			drm_dp_i2c_msg_write_status_update(msg);

			continue;

		default:
			DRM_ERROR("%s: invalid I2C reply %#04x\n",
				  aux->name, msg->reply);
			return -EREMOTEIO;
		}
	}

	DRM_DEBUG_KMS("%s: Too many retries, giving up\n", aux->name);
	return -EREMOTEIO;
}

static void drm_dp_i2c_msg_set_request(struct drm_dp_aux_msg *msg,
				       const struct i2c_msg *i2c_msg)
{
	msg->request = (i2c_msg->flags & I2C_M_RD) ?
		DP_AUX_I2C_READ : DP_AUX_I2C_WRITE;
	if (!(i2c_msg->flags & I2C_M_STOP))
		msg->request |= DP_AUX_I2C_MOT;
}

/*
 * Keep retrying drm_dp_i2c_do_msg until all data has been transferred.
 *
 * Returns an error code on failure, or a recommended transfer size on success.
 */
static int drm_dp_i2c_drain_msg(struct drm_dp_aux *aux, struct drm_dp_aux_msg *orig_msg)
{
	int err, ret = orig_msg->size;
	struct drm_dp_aux_msg msg = *orig_msg;

	while (msg.size > 0) {
		err = drm_dp_i2c_do_msg(aux, &msg);
		if (err <= 0)
			return err == 0 ? -EPROTO : err;

		if (err < msg.size && err < ret) {
			DRM_DEBUG_KMS("%s: Partial I2C reply: requested %zu bytes got %d bytes\n",
				      aux->name, msg.size, err);
			ret = err;
		}

		msg.size -= err;
		msg.buffer += err;
	}

	return ret;
}

/*
 * Bizlink designed DP->DVI-D Dual Link adapters require the I2C over AUX
 * packets to be as large as possible. If not, the I2C transactions never
 * succeed. Hence the default is maximum.
 */
static int dp_aux_i2c_transfer_size __read_mostly = DP_AUX_MAX_PAYLOAD_BYTES;
module_param_unsafe(dp_aux_i2c_transfer_size, int, 0644);
MODULE_PARM_DESC(dp_aux_i2c_transfer_size,
		 "Number of bytes to transfer in a single I2C over DP AUX CH message, (1-16, default 16)");

static int drm_dp_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
			   int num)
{
	struct drm_dp_aux *aux = adapter->algo_data;
	unsigned int i, j;
	unsigned transfer_size;
	struct drm_dp_aux_msg msg;
	int err = 0;

	dp_aux_i2c_transfer_size = clamp(dp_aux_i2c_transfer_size, 1, DP_AUX_MAX_PAYLOAD_BYTES);

	memset(&msg, 0, sizeof(msg));

	for (i = 0; i < num; i++) {
		msg.address = msgs[i].addr;
		drm_dp_i2c_msg_set_request(&msg, &msgs[i]);
		/* Send a bare address packet to start the transaction.
		 * Zero sized messages specify an address only (bare
		 * address) transaction.
		 */
		msg.buffer = NULL;
		msg.size = 0;
		err = drm_dp_i2c_do_msg(aux, &msg);

		/*
		 * Reset msg.request in case in case it got
		 * changed into a WRITE_STATUS_UPDATE.
		 */
		drm_dp_i2c_msg_set_request(&msg, &msgs[i]);

		if (err < 0)
			break;
		/* We want each transaction to be as large as possible, but
		 * we'll go to smaller sizes if the hardware gives us a
		 * short reply.
		 */
		transfer_size = dp_aux_i2c_transfer_size;
		for (j = 0; j < msgs[i].len; j += msg.size) {
			msg.buffer = msgs[i].buf + j;
			msg.size = min(transfer_size, msgs[i].len - j);

			err = drm_dp_i2c_drain_msg(aux, &msg);

			/*
			 * Reset msg.request in case in case it got
			 * changed into a WRITE_STATUS_UPDATE.
			 */
			drm_dp_i2c_msg_set_request(&msg, &msgs[i]);

			if (err < 0)
				break;
			transfer_size = err;
		}
		if (err < 0)
			break;
	}
	if (err >= 0)
		err = num;
	/* Send a bare address packet to close out the transaction.
	 * Zero sized messages specify an address only (bare
	 * address) transaction.
	 */
	msg.request &= ~DP_AUX_I2C_MOT;
	msg.buffer = NULL;
	msg.size = 0;
	(void)drm_dp_i2c_do_msg(aux, &msg);

	return err;
}

static const struct i2c_algorithm drm_dp_i2c_algo = {
	.functionality = drm_dp_i2c_functionality,
	.master_xfer = drm_dp_i2c_xfer,
};

static struct drm_dp_aux *i2c_to_aux(struct i2c_adapter *i2c)
{
	return container_of(i2c, struct drm_dp_aux, ddc);
}

static void lock_bus(struct i2c_adapter *i2c, unsigned int flags)
{
	mutex_lock(&i2c_to_aux(i2c)->hw_mutex);
}

static int trylock_bus(struct i2c_adapter *i2c, unsigned int flags)
{
	return mutex_trylock(&i2c_to_aux(i2c)->hw_mutex);
}

static void unlock_bus(struct i2c_adapter *i2c, unsigned int flags)
{
	mutex_unlock(&i2c_to_aux(i2c)->hw_mutex);
}

static const struct i2c_lock_operations drm_dp_i2c_lock_ops = {
	.lock_bus = lock_bus,
	.trylock_bus = trylock_bus,
	.unlock_bus = unlock_bus,
};

static int drm_dp_aux_get_crc(struct drm_dp_aux *aux, u8 *crc)
{
	u8 buf, count;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_TEST_SINK, &buf);
	if (ret < 0)
		return ret;

	WARN_ON(!(buf & DP_TEST_SINK_START));

	ret = drm_dp_dpcd_readb(aux, DP_TEST_SINK_MISC, &buf);
	if (ret < 0)
		return ret;

	count = buf & DP_TEST_COUNT_MASK;
	if (count == aux->crc_count)
		return -EAGAIN; /* No CRC yet */

	aux->crc_count = count;

	/*
	 * At DP_TEST_CRC_R_CR, there's 6 bytes containing CRC data, 2 bytes
	 * per component (RGB or CrYCb).
	 */
	ret = drm_dp_dpcd_read(aux, DP_TEST_CRC_R_CR, crc, 6);
	if (ret < 0)
		return ret;

	return 0;
}

static void drm_dp_aux_crc_work(struct work_struct *work)
{
	struct drm_dp_aux *aux = container_of(work, struct drm_dp_aux,
					      crc_work);
	struct drm_crtc *crtc;
	u8 crc_bytes[6];
	uint32_t crcs[3];
	int ret;

	if (WARN_ON(!aux->crtc))
		return;

	crtc = aux->crtc;
	while (crtc->crc.opened) {
		drm_crtc_wait_one_vblank(crtc);
		if (!crtc->crc.opened)
			break;

		ret = drm_dp_aux_get_crc(aux, crc_bytes);
		if (ret == -EAGAIN) {
			usleep_range(1000, 2000);
			ret = drm_dp_aux_get_crc(aux, crc_bytes);
		}

		if (ret == -EAGAIN) {
			DRM_DEBUG_KMS("%s: Get CRC failed after retrying: %d\n",
				      aux->name, ret);
			continue;
		} else if (ret) {
			DRM_DEBUG_KMS("%s: Failed to get a CRC: %d\n",
				      aux->name, ret);
			continue;
		}

		crcs[0] = crc_bytes[0] | crc_bytes[1] << 8;
		crcs[1] = crc_bytes[2] | crc_bytes[3] << 8;
		crcs[2] = crc_bytes[4] | crc_bytes[5] << 8;
		drm_crtc_add_crc_entry(crtc, false, 0, crcs);
	}
}

/**
 * drm_dp_aux_init() - minimally initialise an aux channel
 * @aux: DisplayPort AUX channel
 *
 * If you need to use the drm_dp_aux's i2c adapter prior to registering it
 * with the outside world, call drm_dp_aux_init() first. You must still
 * call drm_dp_aux_register() once the connector has been registered to
 * allow userspace access to the auxiliary DP channel.
 */
void drm_dp_aux_init(struct drm_dp_aux *aux)
{
	mutex_init(&aux->hw_mutex);
	mutex_init(&aux->cec.lock);
	INIT_WORK(&aux->crc_work, drm_dp_aux_crc_work);

	aux->ddc.algo = &drm_dp_i2c_algo;
	aux->ddc.algo_data = aux;
	aux->ddc.retries = 3;

	aux->ddc.lock_ops = &drm_dp_i2c_lock_ops;
}
EXPORT_SYMBOL(drm_dp_aux_init);

/**
 * drm_dp_aux_register() - initialise and register aux channel
 * @aux: DisplayPort AUX channel
 *
 * Automatically calls drm_dp_aux_init() if this hasn't been done yet.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_aux_register(struct drm_dp_aux *aux)
{
	int ret;

	if (!aux->ddc.algo)
		drm_dp_aux_init(aux);

	aux->ddc.class = I2C_CLASS_DDC;
	aux->ddc.owner = THIS_MODULE;
	aux->ddc.dev.parent = aux->dev;

	strlcpy(aux->ddc.name, aux->name ? aux->name : dev_name(aux->dev),
		sizeof(aux->ddc.name));

	ret = drm_dp_aux_register_devnode(aux);
	if (ret)
		return ret;

	ret = i2c_add_adapter(&aux->ddc);
	if (ret) {
		drm_dp_aux_unregister_devnode(aux);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_aux_register);

/**
 * drm_dp_aux_unregister() - unregister an AUX adapter
 * @aux: DisplayPort AUX channel
 */
void drm_dp_aux_unregister(struct drm_dp_aux *aux)
{
	drm_dp_aux_unregister_devnode(aux);
	i2c_del_adapter(&aux->ddc);
}
EXPORT_SYMBOL(drm_dp_aux_unregister);

#define PSR_SETUP_TIME(x) [DP_PSR_SETUP_TIME_ ## x >> DP_PSR_SETUP_TIME_SHIFT] = (x)

/**
 * drm_dp_psr_setup_time() - PSR setup in time usec
 * @psr_cap: PSR capabilities from DPCD
 *
 * Returns:
 * PSR setup time for the panel in microseconds,  negative
 * error code on failure.
 */
int drm_dp_psr_setup_time(const u8 psr_cap[EDP_PSR_RECEIVER_CAP_SIZE])
{
	static const u16 psr_setup_time_us[] = {
		PSR_SETUP_TIME(330),
		PSR_SETUP_TIME(275),
		PSR_SETUP_TIME(220),
		PSR_SETUP_TIME(165),
		PSR_SETUP_TIME(110),
		PSR_SETUP_TIME(55),
		PSR_SETUP_TIME(0),
	};
	int i;

	i = (psr_cap[1] & DP_PSR_SETUP_TIME_MASK) >> DP_PSR_SETUP_TIME_SHIFT;
	if (i >= ARRAY_SIZE(psr_setup_time_us))
		return -EINVAL;

	return psr_setup_time_us[i];
}
EXPORT_SYMBOL(drm_dp_psr_setup_time);

#undef PSR_SETUP_TIME

/**
 * drm_dp_start_crc() - start capture of frame CRCs
 * @aux: DisplayPort AUX channel
 * @crtc: CRTC displaying the frames whose CRCs are to be captured
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_start_crc(struct drm_dp_aux *aux, struct drm_crtc *crtc)
{
	u8 buf;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_TEST_SINK, &buf);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_writeb(aux, DP_TEST_SINK, buf | DP_TEST_SINK_START);
	if (ret < 0)
		return ret;

	aux->crc_count = 0;
	aux->crtc = crtc;
	schedule_work(&aux->crc_work);

	return 0;
}
EXPORT_SYMBOL(drm_dp_start_crc);

/**
 * drm_dp_stop_crc() - stop capture of frame CRCs
 * @aux: DisplayPort AUX channel
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_stop_crc(struct drm_dp_aux *aux)
{
	u8 buf;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_TEST_SINK, &buf);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_writeb(aux, DP_TEST_SINK, buf & ~DP_TEST_SINK_START);
	if (ret < 0)
		return ret;

	flush_work(&aux->crc_work);
	aux->crtc = NULL;

	return 0;
}
EXPORT_SYMBOL(drm_dp_stop_crc);

struct dpcd_quirk {
	u8 oui[3];
	u8 device_id[6];
	bool is_branch;
	u32 quirks;
};

#define OUI(first, second, third) { (first), (second), (third) }
#define DEVICE_ID(first, second, third, fourth, fifth, sixth) \
	{ (first), (second), (third), (fourth), (fifth), (sixth) }

#define DEVICE_ID_ANY	DEVICE_ID(0, 0, 0, 0, 0, 0)

static const struct dpcd_quirk dpcd_quirk_list[] = {
	/* Analogix 7737 needs reduced M and N at HBR2 link rates */
	{ OUI(0x00, 0x22, 0xb9), DEVICE_ID_ANY, true, BIT(DP_DPCD_QUIRK_CONSTANT_N) },
	/* LG LP140WF6-SPM1 eDP panel */
	{ OUI(0x00, 0x22, 0xb9), DEVICE_ID('s', 'i', 'v', 'a', 'r', 'T'), false, BIT(DP_DPCD_QUIRK_CONSTANT_N) },
	/* Apple panels need some additional handling to support PSR */
	{ OUI(0x00, 0x10, 0xfa), DEVICE_ID_ANY, false, BIT(DP_DPCD_QUIRK_NO_PSR) },
	/* CH7511 seems to leave SINK_COUNT zeroed */
	{ OUI(0x00, 0x00, 0x00), DEVICE_ID('C', 'H', '7', '5', '1', '1'), false, BIT(DP_DPCD_QUIRK_NO_SINK_COUNT) },
	/* Synaptics DP1.4 MST hubs can support DSC without virtual DPCD */
	{ OUI(0x90, 0xCC, 0x24), DEVICE_ID_ANY, true, BIT(DP_DPCD_QUIRK_DSC_WITHOUT_VIRTUAL_DPCD) },
};

#undef OUI

/*
 * Get a bit mask of DPCD quirks for the sink/branch device identified by
 * ident. The quirk data is shared but it's up to the drivers to act on the
 * data.
 *
 * For now, only the OUI (first three bytes) is used, but this may be extended
 * to device identification string and hardware/firmware revisions later.
 */
static u32
drm_dp_get_quirks(const struct drm_dp_dpcd_ident *ident, bool is_branch)
{
	const struct dpcd_quirk *quirk;
	u32 quirks = 0;
	int i;
	u8 any_device[] = DEVICE_ID_ANY;

	for (i = 0; i < ARRAY_SIZE(dpcd_quirk_list); i++) {
		quirk = &dpcd_quirk_list[i];

		if (quirk->is_branch != is_branch)
			continue;

		if (memcmp(quirk->oui, ident->oui, sizeof(ident->oui)) != 0)
			continue;

		if (memcmp(quirk->device_id, any_device, sizeof(any_device)) != 0 &&
		    memcmp(quirk->device_id, ident->device_id, sizeof(ident->device_id)) != 0)
			continue;

		quirks |= quirk->quirks;
	}

	return quirks;
}

#undef DEVICE_ID_ANY
#undef DEVICE_ID

struct edid_quirk {
	u8 mfg_id[2];
	u8 prod_id[2];
	u32 quirks;
};

#define MFG(first, second) { (first), (second) }
#define PROD_ID(first, second) { (first), (second) }

/*
 * Some devices have unreliable OUIDs where they don't set the device ID
 * correctly, and as a result we need to use the EDID for finding additional
 * DP quirks in such cases.
 */
static const struct edid_quirk edid_quirk_list[] = {
	/* Optional 4K AMOLED panel in the ThinkPad X1 Extreme 2nd Generation
	 * only supports DPCD backlight controls
	 */
	{ MFG(0x4c, 0x83), PROD_ID(0x41, 0x41), BIT(DP_QUIRK_FORCE_DPCD_BACKLIGHT) },
	/*
	 * Some Dell CML 2020 systems have panels support both AUX and PWM
	 * backlight control, and some only support AUX backlight control. All
	 * said panels start up in AUX mode by default, and we don't have any
	 * support for disabling HDR mode on these panels which would be
	 * required to switch to PWM backlight control mode (plus, I'm not
	 * even sure we want PWM backlight controls over DPCD backlight
	 * controls anyway...). Until we have a better way of detecting these,
	 * force DPCD backlight mode on all of them.
	 */
	{ MFG(0x06, 0xaf), PROD_ID(0x9b, 0x32), BIT(DP_QUIRK_FORCE_DPCD_BACKLIGHT) },
	{ MFG(0x06, 0xaf), PROD_ID(0xeb, 0x41), BIT(DP_QUIRK_FORCE_DPCD_BACKLIGHT) },
	{ MFG(0x4d, 0x10), PROD_ID(0xc7, 0x14), BIT(DP_QUIRK_FORCE_DPCD_BACKLIGHT) },
	{ MFG(0x4d, 0x10), PROD_ID(0xe6, 0x14), BIT(DP_QUIRK_FORCE_DPCD_BACKLIGHT) },
};

#undef MFG
#undef PROD_ID

/**
 * drm_dp_get_edid_quirks() - Check the EDID of a DP device to find additional
 * DP-specific quirks
 * @edid: The EDID to check
 *
 * While OUIDs are meant to be used to recognize a DisplayPort device, a lot
 * of manufacturers don't seem to like following standards and neglect to fill
 * the dev-ID in, making it impossible to only use OUIDs for determining
 * quirks in some cases. This function can be used to check the EDID and look
 * up any additional DP quirks. The bits returned by this function correspond
 * to the quirk bits in &drm_dp_quirk.
 *
 * Returns: a bitmask of quirks, if any. The driver can check this using
 * drm_dp_has_quirk().
 */
u32 drm_dp_get_edid_quirks(const struct edid *edid)
{
	const struct edid_quirk *quirk;
	u32 quirks = 0;
	int i;

	if (!edid)
		return 0;

	for (i = 0; i < ARRAY_SIZE(edid_quirk_list); i++) {
		quirk = &edid_quirk_list[i];
		if (memcmp(quirk->mfg_id, edid->mfg_id,
			   sizeof(edid->mfg_id)) == 0 &&
		    memcmp(quirk->prod_id, edid->prod_code,
			   sizeof(edid->prod_code)) == 0)
			quirks |= quirk->quirks;
	}

	DRM_DEBUG_KMS("DP sink: EDID mfg %*phD prod-ID %*phD quirks: 0x%04x\n",
		      (int)sizeof(edid->mfg_id), edid->mfg_id,
		      (int)sizeof(edid->prod_code), edid->prod_code, quirks);

	return quirks;
}
EXPORT_SYMBOL(drm_dp_get_edid_quirks);

/**
 * drm_dp_read_desc - read sink/branch descriptor from DPCD
 * @aux: DisplayPort AUX channel
 * @desc: Device decriptor to fill from DPCD
 * @is_branch: true for branch devices, false for sink devices
 *
 * Read DPCD 0x400 (sink) or 0x500 (branch) into @desc. Also debug log the
 * identification.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_read_desc(struct drm_dp_aux *aux, struct drm_dp_desc *desc,
		     bool is_branch)
{
	struct drm_dp_dpcd_ident *ident = &desc->ident;
	unsigned int offset = is_branch ? DP_BRANCH_OUI : DP_SINK_OUI;
	int ret, dev_id_len;

	ret = drm_dp_dpcd_read(aux, offset, ident, sizeof(*ident));
	if (ret < 0)
		return ret;

	desc->quirks = drm_dp_get_quirks(ident, is_branch);

	dev_id_len = strnlen(ident->device_id, sizeof(ident->device_id));

	DRM_DEBUG_KMS("%s: DP %s: OUI %*phD dev-ID %*pE HW-rev %d.%d SW-rev %d.%d quirks 0x%04x\n",
		      aux->name, is_branch ? "branch" : "sink",
		      (int)sizeof(ident->oui), ident->oui,
		      dev_id_len, ident->device_id,
		      ident->hw_rev >> 4, ident->hw_rev & 0xf,
		      ident->sw_major_rev, ident->sw_minor_rev,
		      desc->quirks);

	return 0;
}
EXPORT_SYMBOL(drm_dp_read_desc);

/**
 * drm_dp_dsc_sink_max_slice_count() - Get the max slice count
 * supported by the DSC sink.
 * @dsc_dpcd: DSC capabilities from DPCD
 * @is_edp: true if its eDP, false for DP
 *
 * Read the slice capabilities DPCD register from DSC sink to get
 * the maximum slice count supported. This is used to populate
 * the DSC parameters in the &struct drm_dsc_config by the driver.
 * Driver creates an infoframe using these parameters to populate
 * &struct drm_dsc_pps_infoframe. These are sent to the sink using DSC
 * infoframe using the helper function drm_dsc_pps_infoframe_pack()
 *
 * Returns:
 * Maximum slice count supported by DSC sink or 0 its invalid
 */
u8 drm_dp_dsc_sink_max_slice_count(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE],
				   bool is_edp)
{
	u8 slice_cap1 = dsc_dpcd[DP_DSC_SLICE_CAP_1 - DP_DSC_SUPPORT];

	if (is_edp) {
		/* For eDP, register DSC_SLICE_CAPABILITIES_1 gives slice count */
		if (slice_cap1 & DP_DSC_4_PER_DP_DSC_SINK)
			return 4;
		if (slice_cap1 & DP_DSC_2_PER_DP_DSC_SINK)
			return 2;
		if (slice_cap1 & DP_DSC_1_PER_DP_DSC_SINK)
			return 1;
	} else {
		/* For DP, use values from DSC_SLICE_CAP_1 and DSC_SLICE_CAP2 */
		u8 slice_cap2 = dsc_dpcd[DP_DSC_SLICE_CAP_2 - DP_DSC_SUPPORT];

		if (slice_cap2 & DP_DSC_24_PER_DP_DSC_SINK)
			return 24;
		if (slice_cap2 & DP_DSC_20_PER_DP_DSC_SINK)
			return 20;
		if (slice_cap2 & DP_DSC_16_PER_DP_DSC_SINK)
			return 16;
		if (slice_cap1 & DP_DSC_12_PER_DP_DSC_SINK)
			return 12;
		if (slice_cap1 & DP_DSC_10_PER_DP_DSC_SINK)
			return 10;
		if (slice_cap1 & DP_DSC_8_PER_DP_DSC_SINK)
			return 8;
		if (slice_cap1 & DP_DSC_6_PER_DP_DSC_SINK)
			return 6;
		if (slice_cap1 & DP_DSC_4_PER_DP_DSC_SINK)
			return 4;
		if (slice_cap1 & DP_DSC_2_PER_DP_DSC_SINK)
			return 2;
		if (slice_cap1 & DP_DSC_1_PER_DP_DSC_SINK)
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_dsc_sink_max_slice_count);

/**
 * drm_dp_dsc_sink_line_buf_depth() - Get the line buffer depth in bits
 * @dsc_dpcd: DSC capabilities from DPCD
 *
 * Read the DSC DPCD register to parse the line buffer depth in bits which is
 * number of bits of precision within the decoder line buffer supported by
 * the DSC sink. This is used to populate the DSC parameters in the
 * &struct drm_dsc_config by the driver.
 * Driver creates an infoframe using these parameters to populate
 * &struct drm_dsc_pps_infoframe. These are sent to the sink using DSC
 * infoframe using the helper function drm_dsc_pps_infoframe_pack()
 *
 * Returns:
 * Line buffer depth supported by DSC panel or 0 its invalid
 */
u8 drm_dp_dsc_sink_line_buf_depth(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE])
{
	u8 line_buf_depth = dsc_dpcd[DP_DSC_LINE_BUF_BIT_DEPTH - DP_DSC_SUPPORT];

	switch (line_buf_depth & DP_DSC_LINE_BUF_BIT_DEPTH_MASK) {
	case DP_DSC_LINE_BUF_BIT_DEPTH_9:
		return 9;
	case DP_DSC_LINE_BUF_BIT_DEPTH_10:
		return 10;
	case DP_DSC_LINE_BUF_BIT_DEPTH_11:
		return 11;
	case DP_DSC_LINE_BUF_BIT_DEPTH_12:
		return 12;
	case DP_DSC_LINE_BUF_BIT_DEPTH_13:
		return 13;
	case DP_DSC_LINE_BUF_BIT_DEPTH_14:
		return 14;
	case DP_DSC_LINE_BUF_BIT_DEPTH_15:
		return 15;
	case DP_DSC_LINE_BUF_BIT_DEPTH_16:
		return 16;
	case DP_DSC_LINE_BUF_BIT_DEPTH_8:
		return 8;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_dsc_sink_line_buf_depth);

/**
 * drm_dp_dsc_sink_supported_input_bpcs() - Get all the input bits per component
 * values supported by the DSC sink.
 * @dsc_dpcd: DSC capabilities from DPCD
 * @dsc_bpc: An array to be filled by this helper with supported
 *           input bpcs.
 *
 * Read the DSC DPCD from the sink device to parse the supported bits per
 * component values. This is used to populate the DSC parameters
 * in the &struct drm_dsc_config by the driver.
 * Driver creates an infoframe using these parameters to populate
 * &struct drm_dsc_pps_infoframe. These are sent to the sink using DSC
 * infoframe using the helper function drm_dsc_pps_infoframe_pack()
 *
 * Returns:
 * Number of input BPC values parsed from the DPCD
 */
int drm_dp_dsc_sink_supported_input_bpcs(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE],
					 u8 dsc_bpc[3])
{
	int num_bpc = 0;
	u8 color_depth = dsc_dpcd[DP_DSC_DEC_COLOR_DEPTH_CAP - DP_DSC_SUPPORT];

	if (color_depth & DP_DSC_12_BPC)
		dsc_bpc[num_bpc++] = 12;
	if (color_depth & DP_DSC_10_BPC)
		dsc_bpc[num_bpc++] = 10;
	if (color_depth & DP_DSC_8_BPC)
		dsc_bpc[num_bpc++] = 8;

	return num_bpc;
}
EXPORT_SYMBOL(drm_dp_dsc_sink_supported_input_bpcs);

/**
 * drm_dp_get_phy_test_pattern() - get the requested pattern from the sink.
 * @aux: DisplayPort AUX channel
 * @data: DP phy compliance test parameters.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_get_phy_test_pattern(struct drm_dp_aux *aux,
				struct drm_dp_phy_test_params *data)
{
	int err;
	u8 rate, lanes;

	err = drm_dp_dpcd_readb(aux, DP_TEST_LINK_RATE, &rate);
	if (err < 0)
		return err;
	data->link_rate = drm_dp_bw_code_to_link_rate(rate);

	err = drm_dp_dpcd_readb(aux, DP_TEST_LANE_COUNT, &lanes);
	if (err < 0)
		return err;
	data->num_lanes = lanes & DP_MAX_LANE_COUNT_MASK;

	if (lanes & DP_ENHANCED_FRAME_CAP)
		data->enhanced_frame_cap = true;

	err = drm_dp_dpcd_readb(aux, DP_PHY_TEST_PATTERN, &data->phy_pattern);
	if (err < 0)
		return err;

	switch (data->phy_pattern) {
	case DP_PHY_TEST_PATTERN_80BIT_CUSTOM:
		err = drm_dp_dpcd_read(aux, DP_TEST_80BIT_CUSTOM_PATTERN_7_0,
				       &data->custom80, sizeof(data->custom80));
		if (err < 0)
			return err;

		break;
	case DP_PHY_TEST_PATTERN_CP2520:
		err = drm_dp_dpcd_read(aux, DP_TEST_HBR2_SCRAMBLER_RESET,
				       &data->hbr2_reset,
				       sizeof(data->hbr2_reset));
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_get_phy_test_pattern);

/**
 * drm_dp_set_phy_test_pattern() - set the pattern to the sink.
 * @aux: DisplayPort AUX channel
 * @data: DP phy compliance test parameters.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_set_phy_test_pattern(struct drm_dp_aux *aux,
				struct drm_dp_phy_test_params *data, u8 dp_rev)
{
	int err, i;
	u8 link_config[2];
	u8 test_pattern;

	link_config[0] = drm_dp_link_rate_to_bw_code(data->link_rate);
	link_config[1] = data->num_lanes;
	if (data->enhanced_frame_cap)
		link_config[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, link_config, 2);
	if (err < 0)
		return err;

	test_pattern = data->phy_pattern;
	if (dp_rev < 0x12) {
		test_pattern = (test_pattern << 2) &
			       DP_LINK_QUAL_PATTERN_11_MASK;
		err = drm_dp_dpcd_writeb(aux, DP_TRAINING_PATTERN_SET,
					 test_pattern);
		if (err < 0)
			return err;
	} else {
		for (i = 0; i < data->num_lanes; i++) {
			err = drm_dp_dpcd_writeb(aux,
						 DP_LINK_QUAL_LANE0_SET + i,
						 test_pattern);
			if (err < 0)
				return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_set_phy_test_pattern);

static const char *dp_pixelformat_get_name(enum dp_pixelformat pixelformat)
{
	if (pixelformat < 0 || pixelformat > DP_PIXELFORMAT_RESERVED)
		return "Invalid";

	switch (pixelformat) {
	case DP_PIXELFORMAT_RGB:
		return "RGB";
	case DP_PIXELFORMAT_YUV444:
		return "YUV444";
	case DP_PIXELFORMAT_YUV422:
		return "YUV422";
	case DP_PIXELFORMAT_YUV420:
		return "YUV420";
	case DP_PIXELFORMAT_Y_ONLY:
		return "Y_ONLY";
	case DP_PIXELFORMAT_RAW:
		return "RAW";
	default:
		return "Reserved";
	}
}

static const char *dp_colorimetry_get_name(enum dp_pixelformat pixelformat,
					   enum dp_colorimetry colorimetry)
{
	if (pixelformat < 0 || pixelformat > DP_PIXELFORMAT_RESERVED)
		return "Invalid";

	switch (colorimetry) {
	case DP_COLORIMETRY_DEFAULT:
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "sRGB";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.601";
		case DP_PIXELFORMAT_Y_ONLY:
			return "DICOM PS3.14";
		case DP_PIXELFORMAT_RAW:
			return "Custom Color Profile";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_RGB_WIDE_FIXED: /* and DP_COLORIMETRY_BT709_YCC */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "Wide Fixed";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.709";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_RGB_WIDE_FLOAT: /* and DP_COLORIMETRY_XVYCC_601 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "Wide Float";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "xvYCC 601";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_OPRGB: /* and DP_COLORIMETRY_XVYCC_709 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "OpRGB";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "xvYCC 709";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_DCI_P3_RGB: /* and DP_COLORIMETRY_SYCC_601 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "DCI-P3";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "sYCC 601";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_RGB_CUSTOM: /* and DP_COLORIMETRY_OPYCC_601 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "Custom Profile";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "OpYCC 601";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_BT2020_RGB: /* and DP_COLORIMETRY_BT2020_CYCC */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "BT.2020 RGB";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.2020 CYCC";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_BT2020_YCC:
		switch (pixelformat) {
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.2020 YCC";
		default:
			return "Reserved";
		}
	default:
		return "Invalid";
	}
}

static const char *dp_dynamic_range_get_name(enum dp_dynamic_range dynamic_range)
{
	switch (dynamic_range) {
	case DP_DYNAMIC_RANGE_VESA:
		return "VESA range";
	case DP_DYNAMIC_RANGE_CTA:
		return "CTA range";
	default:
		return "Invalid";
	}
}

static const char *dp_content_type_get_name(enum dp_content_type content_type)
{
	switch (content_type) {
	case DP_CONTENT_TYPE_NOT_DEFINED:
		return "Not defined";
	case DP_CONTENT_TYPE_GRAPHICS:
		return "Graphics";
	case DP_CONTENT_TYPE_PHOTO:
		return "Photo";
	case DP_CONTENT_TYPE_VIDEO:
		return "Video";
	case DP_CONTENT_TYPE_GAME:
		return "Game";
	default:
		return "Reserved";
	}
}

void drm_dp_vsc_sdp_log(const char *level, struct device *dev,
			const struct drm_dp_vsc_sdp *vsc)
{
#define DP_SDP_LOG(fmt, ...) dev_printk(level, dev, fmt, ##__VA_ARGS__)
	DP_SDP_LOG("DP SDP: %s, revision %u, length %u\n", "VSC",
		   vsc->revision, vsc->length);
	DP_SDP_LOG("    pixelformat: %s\n",
		   dp_pixelformat_get_name(vsc->pixelformat));
	DP_SDP_LOG("    colorimetry: %s\n",
		   dp_colorimetry_get_name(vsc->pixelformat, vsc->colorimetry));
	DP_SDP_LOG("    bpc: %u\n", vsc->bpc);
	DP_SDP_LOG("    dynamic range: %s\n",
		   dp_dynamic_range_get_name(vsc->dynamic_range));
	DP_SDP_LOG("    content type: %s\n",
		   dp_content_type_get_name(vsc->content_type));
#undef DP_SDP_LOG
}
EXPORT_SYMBOL(drm_dp_vsc_sdp_log);
