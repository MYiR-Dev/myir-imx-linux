/******************************************************************************
 *
 * Copyright(c) 2019 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#define _RTL8733BU_RECV_C_

#include <drv_types.h>			/* PADAPTER, rtw_xmit.h and etc. */
#include <hal_data.h>			/* HAL_DATA_TYPE */
#include "../../hal_halmac.h"		/* RX desc */
#include "../rtl8733b.h"		/* rtl8733b_query_rx_desc, rtl8733b_c2h_handler_no_io() */

int rtl8733bu_init_recv_priv(PADAPTER padapter)
{
	return usb_init_recv_priv(padapter, INTERRUPT_MSG_FORMAT_LEN);
}

void rtl8733bu_free_recv_priv(PADAPTER padapter)
{
	usb_free_recv_priv(padapter, INTERRUPT_MSG_FORMAT_LEN);
}

static u8 recvbuf2recvframe_proccess_c2h(PADAPTER padapter, u8 *pbuf, s32 transfer_len)
{
	u8 ret = _SUCCESS;

	/* send rx desc + c2h content to halmac */
	rtl8733b_c2h_handler_no_io(padapter, pbuf, transfer_len);

	return ret;
}

#ifdef CONFIG_MAC_LOOPBACK_DRIVER
void dump_pkt_data(u8 *pbuf, u32 pkt_len)
{
	u32 i, j = 1;

	RTW_INFO("packet length = %u\n", pkt_len);
	for (i = 0; (i + 4) < pkt_len; i += 4) {
		if (j % 4 == 1)
			RTW_PRINT("idx:%u:", i);
		_RTW_PRINT(" 0x%02x 0x%02x 0x%02x 0x%02x", pbuf[i], pbuf[i + 1], pbuf[i + 2], pbuf[i + 3]);
		if ((j++) % 4 == 0)
			_RTW_PRINT("\n");
	}

	for (; i < pkt_len ; i++) {
		_RTW_PRINT(" 0x%02x", pbuf[i]);
	}
	_RTW_PRINT("\n ======================== \n");
}

void usb_recv_loopback(PADAPTER padapter, u8 *pbuf, u32 pkt_len)
{
	PLOOPBACKDATA ploopback;
	u8 *prxbuf;

	dump_pkt_data(pbuf, pkt_len);

	ploopback = padapter->ploopback;
	if (ploopback) {
		ploopback->rxsize = pkt_len;
		prxbuf = ploopback->rxbuf;
	} else {
		prxbuf = rtw_malloc(pkt_len);
		if (prxbuf == NULL) {
			RTW_INFO("%s: malloc fail size=%d\n", __func__, pkt_len);
			return;
		}
	}

	_rtw_memcpy(prxbuf, pbuf, pkt_len);

	if (ploopback) {
		_rtw_up_sema(&ploopback->sema);
		RTW_INFO("Up the loopback semaphore\n");
	} else {
		rtw_mfree(prxbuf, pkt_len);
	}
}
#endif /* CONFIG_MAC_LOOPBACK_DRIVER */

static u8 recvbuf2recvframe_proccess_normal_rx
(PADAPTER padapter, u8 *pbuf, struct rx_pkt_attrib *pattrib, union recv_frame *precvframe, _pkt *pskb)
{
	u8 ret = _SUCCESS;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &precvpriv->free_recv_queue;

#ifdef CONFIG_RX_PACKET_APPEND_FCS
	if (check_fwstate(&padapter->mlmepriv, WIFI_MONITOR_STATE) == _FALSE) {
		if (rtl8733b_rx_fcs_appended(padapter))
			pattrib->pkt_len -= IEEE80211_FCS_LEN;
	}
#endif

#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	usb_recv_loopback(padapter, pbuf, pattrib->pkt_len + pattrib->shift_sz + pattrib->drvinfo_sz + RXDESC_SIZE);
#endif

	if (rtw_os_alloc_recvframe(padapter, precvframe,
		(pbuf + pattrib->shift_sz + pattrib->drvinfo_sz + RXDESC_SIZE), pskb) == _FAIL) {

		rtw_free_recvframe(precvframe, pfree_recv_queue);
		ret = _FAIL;
		goto exit;
	}

#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	/* dump_pkt_data((precvframe->u.hdr.pkt)->data, (precvframe->u.hdr.pkt)->len); */
#endif

	recvframe_put(precvframe, pattrib->pkt_len);

	pre_recv_entry(precvframe, pattrib->physt ? (pbuf + RXDESC_OFFSET) : NULL);

exit:
	return ret;
}

int recvbuf2recvframe(PADAPTER padapter, void *ptr)
{
	u8 *pbuf;
	u8 pkt_cnt = 0;
	u32 pkt_offset;
	s32 transfer_len;
	u8 *pphy_status = NULL;
	union recv_frame *precvframe = NULL;
	struct rx_pkt_attrib *pattrib = NULL;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &precvpriv->free_recv_queue;
	_pkt *pskb;

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	pskb = NULL;
	transfer_len = (s32)((struct recv_buf *)ptr)->transfer_len;
	pbuf = ((struct recv_buf *)ptr)->pbuf;
#else /* !CONFIG_USE_USB_BUFFER_ALLOC_RX */
	pskb = (_pkt *)ptr;
	transfer_len = (s32)pskb->len;
	pbuf = pskb->data;
#endif /* CONFIG_USE_USB_BUFFER_ALLOC_RX */


#ifdef CONFIG_USB_RX_AGGREGATION
	pkt_cnt = GET_RX_DESC_DMA_AGG_NUM_8733B(pbuf);
#endif

	do {
		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if (precvframe == NULL) {
			RTW_INFO("%s()-%d: rtw_alloc_recvframe() failed! RX Drop!\n", __func__, __LINE__);
			goto _exit_recvbuf2recvframe;
		}

		_rtw_init_listhead(&precvframe->u.hdr.list);
		precvframe->u.hdr.precvbuf = NULL;	/* can't access the precvbuf for new arch */
		precvframe->u.hdr.len = 0;

		rtl8733b_query_rx_desc(precvframe, pbuf);

		pattrib = &precvframe->u.hdr.attrib;

		if ((padapter->registrypriv.mp_mode == 0) && ((pattrib->crc_err) || (pattrib->icv_err))) {
			RTW_INFO("%s: RX Warning! crc_err=%d icv_err=%d, skip!\n", __func__, pattrib->crc_err, pattrib->icv_err);

			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		pkt_offset = RXDESC_SIZE + pattrib->drvinfo_sz + pattrib->shift_sz + pattrib->pkt_len;

		if ((pattrib->pkt_len <= 0) || (pkt_offset > transfer_len)) {
			RTW_INFO("%s()-%d: RX Warning!,pkt_len<=0(%d) or pkt_offset(%d)> transfer_len(%d)\n"
				, __func__, __LINE__, pattrib->pkt_len, pkt_offset, transfer_len);
			if (pkt_offset > transfer_len)
				RTW_INFO("%s()-%d: RX Warning!,RXDESC_SIZE(%d), drvinfo_sz(%d), shift_sz(%d),pkt_len(%d)\n"
					, __func__, __LINE__, RXDESC_SIZE, pattrib->drvinfo_sz, pattrib->shift_sz, pattrib->pkt_len);

			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		switch (pattrib->pkt_rpt_type) {
		case C2H_PACKET:
			/* C2H_PACKET doesn't use recvframe, so free it */
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			if (recvbuf2recvframe_proccess_c2h(padapter, pbuf, transfer_len) == _FAIL)
				goto _exit_recvbuf2recvframe;
			break;
		case TX_REPORT1:
		case TX_REPORT2:
		case HIS_REPORT:
			RTW_INFO("%s: [WARNNING] RX type(%d) not be handled!\n", __func__, pattrib->pkt_rpt_type);
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			break;
		case NORMAL_RX:
		default:
			if (recvbuf2recvframe_proccess_normal_rx(padapter, pbuf, pattrib, precvframe, pskb) == _FAIL)
				goto _exit_recvbuf2recvframe;
			break;
		}

#ifdef CONFIG_USB_RX_AGGREGATION
		/* jaguar 8-byte alignment */
		pkt_offset = (u16)_RND8(pkt_offset);
		pkt_cnt--;
		pbuf += pkt_offset;
#endif
		transfer_len -= pkt_offset;
		precvframe = NULL;

	} while (transfer_len > 0);

_exit_recvbuf2recvframe:

	return _SUCCESS;
}
