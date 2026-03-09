/******************************************************************************
 *
 * Copyright(c) 2019 - 2024 Realtek Corporation.
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
#define _RTW_CSA_C_
#include <drv_types.h>

#ifdef CONFIG_ECSA
void reset_all_ecsa_param(struct _ADAPTER *a)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);

	RTW_INFO("CSA : Reset STA and AP CSA parameter\n");
	rfctl->csa_mode = 0;
	rfctl->ecsa_op_class = 0;
	rfctl->csa_ch = 0;
	rfctl->csa_switch_cnt = 0;
	rfctl->csa_band = 0;
	rfctl->csa_bw = 0;
	rfctl->csa_ch_offset = 0;
	rfctl->csa_ch_width = 0;
	rfctl->csa_ch_freq_seg0 = 0;
	rfctl->csa_ch_freq_seg1 = 0;
#ifdef CONFIG_AP_MODE
	rfctl->ap_csa_en = AP_CSA_DISABLE;
	rfctl->ap_csa_wait_update_bcn = 0;
	rfctl->ap_csa_mode = 0;
	rfctl->ap_ecsa_op_class = 0;
	rfctl->ap_csa_ch = 0;
	rfctl->ap_csa_switch_cnt = 0;
	rfctl->ap_csa_band = 0;
	rfctl->ap_csa_bw = 0;
	rfctl->ap_csa_ch_width = 0;
	rfctl->ap_csa_ch_offset = 0;
	rfctl->ap_csa_ch_freq_seg0 = 0;
	rfctl->ap_csa_ch_freq_seg1 = 0;
	rfctl->ap_csa_remove_ie = 0;
	#ifdef CONFIG_CONCURRENT_MODE
	_rtw_memset(&(rfctl->csa_candidate_network), 0, sizeof(struct wlan_network));
	#endif
#endif
}

void reset_ecsa_param(struct _ADAPTER *a)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);

	if (MLME_IS_STA(a)) {
		RTW_INFO("CSA : Reset STA CSA parameter\n");
		rfctl->csa_mode = 0;
		rfctl->ecsa_op_class = 0;
		rfctl->csa_ch = 0;
		rfctl->csa_switch_cnt = 0;
		rfctl->csa_band = 0;
		rfctl->csa_bw = 0;
		rfctl->csa_ch_offset = 0;
		rfctl->csa_ch_width = 0;
		rfctl->csa_ch_freq_seg0 = 0;
		rfctl->csa_ch_freq_seg1 = 0;
	} else if (MLME_IS_AP(a)) {
		RTW_INFO("CSA : Reset AP CSA parameter\n");
#ifdef CONFIG_AP_MODE
		rfctl->ap_csa_en = AP_CSA_DISABLE;
		rfctl->ap_csa_wait_update_bcn = 0;
		rfctl->ap_csa_mode = 0;
		rfctl->ap_ecsa_op_class = 0;
		rfctl->ap_csa_ch = 0;
		rfctl->ap_csa_switch_cnt = 0;
		rfctl->ap_csa_band = 0;
		rfctl->ap_csa_bw = 0;
		rfctl->ap_csa_ch_width = 0;
		rfctl->ap_csa_ch_offset = 0;
		rfctl->ap_csa_ch_freq_seg0 = 0;
		rfctl->ap_csa_ch_freq_seg1 = 0;
		rfctl->ap_csa_remove_ie = 0;
		#ifdef CONFIG_CONCURRENT_MODE
		_rtw_memset(&(rfctl->csa_candidate_network), 0, sizeof(struct wlan_network));
		#endif
#endif
	}
}

bool rtw_is_ecsa_enabled(struct _ADAPTER *a)
{
	return true;
}

bool rtw_mr_is_ecsa_running(struct rf_ctl_t *rfctl)
{
	if (rfctl->ap_csa_en != AP_CSA_DISABLE || rfctl->csa_ch != 0)
		return _TRUE;
	else
		return _FALSE;
}

bool rtw_hal_is_csa_support(struct _ADAPTER *a)
{
	/* TODO : check AC IC ability of beacon early interrupt */
	return true;
}

#ifdef CONFIG_AP_MODE
void rtw_build_csa_ie(struct _ADAPTER *a)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);
	WLAN_BSSID_EX *pnetwork = &(a->mlmeextpriv.mlmext_info.network);
	u8 csa_data[CSA_IE_LEN] = {0};

	/*
	* [0] : Channel Switch Mode
	* [1] : New Channel Number
	* [2] : Channel Switch Count
	*/
	csa_data[0] = rfctl->ap_csa_mode;
	csa_data[1] = rfctl->ap_csa_ch;
	csa_data[2] = rfctl->ap_csa_switch_cnt;
	rtw_add_bcn_ie(a, pnetwork, WLAN_EID_CHANNEL_SWITCH, csa_data, CSA_IE_LEN);
	RTW_INFO("CSA : build channel switch announcement IE by driver\n");
	RTW_INFO("CSA : mode = %u, ch = %u, switch count = %u\n",
			csa_data[0], csa_data[1], csa_data[2]);
}

void rtw_build_ecsa_ie(struct _ADAPTER *a)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);
	WLAN_BSSID_EX *pnetwork = &(a->mlmeextpriv.mlmext_info.network);
	u8 ecsa_data[ECSA_IE_LEN] = {0};

	/*
	* [0] : Channel Switch Mode
	* [1] : New Operating Class
	* [2] : New Channel Number
	* [3] : Channel Switch Count
	*/
	ecsa_data[0] = rfctl->ap_csa_mode;
	ecsa_data[1] = rfctl->ap_ecsa_op_class;
	ecsa_data[2] = rfctl->ap_csa_ch;
	ecsa_data[3] = rfctl->ap_csa_switch_cnt;
	rtw_add_bcn_ie(a, pnetwork, WLAN_EID_ECSA, ecsa_data, ECSA_IE_LEN);
	RTW_INFO("CSA : build extended channel switch announcement IE by driver\n");
	RTW_INFO("CSA : mode = %u, op_class = %u, ch = %u, switch count = %u\n",
			ecsa_data[0], ecsa_data[1], ecsa_data[2], ecsa_data[3]);
}

void rtw_build_sec_offset_ie(struct _ADAPTER *a, u8 seconday_offset)
{
	WLAN_BSSID_EX *pnetwork = &(a->mlmeextpriv.mlmext_info.network);

	switch (seconday_offset) {
	case HAL_PRIME_CHNL_OFFSET_LOWER:
		seconday_offset = SC_IE_CHAN_OFFSET_UPPER;
		break;
	case HAL_PRIME_CHNL_OFFSET_UPPER:
		seconday_offset = SC_IE_CHAN_OFFSET_LOWER;
		break;
	default:
		seconday_offset = SC_IE_CHAN_OFFSET_NO_EXT;
		break;
	}

	rtw_add_bcn_ie(a, pnetwork, WLAN_EID_SECONDARY_CHANNEL_OFFSET, &seconday_offset, 1);
	RTW_INFO("CSA : build secondary channel offset IE by driver, sec_offset = %u\n", seconday_offset);
}

void rtw_build_wide_bw_cs_ie(struct _ADAPTER *a)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);
	WLAN_BSSID_EX *pnetwork = &(a->mlmeextpriv.mlmext_info.network);
	u8 csw_data[CS_WR_DATA_LEN] = {0};
	u8 ch_width, seg_0;

	switch (rfctl->ap_csa_bw) {
	case CHANNEL_WIDTH_40:
		ch_width = 0;
		break;
	case CHANNEL_WIDTH_80:
		ch_width = 1;
		break;
	default:
		ch_width = 1;
		break;
	}

	seg_0 = rtw_get_center_ch(rfctl->ap_csa_ch, rfctl->ap_csa_bw, rfctl->ap_csa_ch_offset);

	/*
	* subfields of Wide Bandwidth Channel Switch subelement
	* [1] : Length
	* [2] : New Channel Width
	* [3] : New Channel Center Frequency Segment 0
	* [4] : New Channel Center Frequency Segment 1
	*/
	csw_data[0] = WLAN_EID_VHT_WIDE_BW_CHSWITCH;
	csw_data[1] = 3;
	csw_data[2] = ch_width;
	csw_data[3] = seg_0;
	csw_data[4] = 0;
	rtw_add_bcn_ie(a, pnetwork, WLAN_EID_CHANNEL_SWITCH_WRAPPER, csw_data, CS_WR_DATA_LEN);
	RTW_INFO("CSA : build channel switch wrapper IE by driver\n");
	RTW_INFO("CSA : channel width = %u, segment_0 = %u, segment_1 = %u\n",
				csw_data[2], csw_data[3], csw_data[4]);
}

void rtw_core_build_ecsa_beacon(struct _ADAPTER *a, u8 is_vht)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);
	WLAN_BSSID_EX *pnetwork = &(a->mlmeextpriv.mlmext_info.network);
	struct ieee80211_info_element *ie;
	u8 *ies;
	uint ies_len;
	bool cs_add = false, ecs_add = false, sco_add = false, csw_add = false;

	ies = pnetwork->IEs + _BEACON_IE_OFFSET_;
	ies_len = pnetwork->IELength - _BEACON_IE_OFFSET_;

	/* Check the missing IE of beacon, then build it by driver */
	for_each_ie(ie, ies, ies_len) {
		switch (ie->id) {
		case WLAN_EID_CHANNEL_SWITCH:
			#ifdef DBG_CSA
			RTW_INFO("CSA : find CSA IE\n");
			#endif
			cs_add = true;
			break;

		case WLAN_EID_ECSA:
			#ifdef DBG_CSA
			RTW_INFO("CSA : find ECSA IE\n");
			#endif
			ecs_add = true;
			break;

		case WLAN_EID_SECONDARY_CHANNEL_OFFSET:
			#ifdef DBG_CSA
			RTW_INFO("CSA : find SEC_CH_OFFSET IE\n");
			#endif
			sco_add = true;
			break;

		case WLAN_EID_CHANNEL_SWITCH_WRAPPER:
			#ifdef DBG_CSA
			RTW_INFO("CSA : find CH_SW_WRAPPER IE\n");
			#endif
			csw_add = true;
			break;

		default:
			break;
		}
	}

	/* Build Channel Switch Announcement element */
	if (!cs_add)
		rtw_build_csa_ie(a);

	/* Build Extended Channel Switch Announcement element */
	if (!ecs_add && rtw_is_ecsa_enabled(a))
		rtw_build_ecsa_ie(a);

	/* Build Secondary Channel Offset element */
	if (!sco_add && rfctl->ap_csa_ch_offset != CHAN_OFFSET_NO_EXT)
		rtw_build_sec_offset_ie(a, rfctl->ap_csa_ch_offset);

	/* Build Channel Switch Wrapper element which only include Wide Bandwidth Channel Switch subelement */
	if (!csw_add && is_vht && (rfctl->ap_csa_bw >= CHANNEL_WIDTH_40 && rfctl->ap_csa_bw <= CHANNEL_WIDTH_80_80))
		rtw_build_wide_bw_cs_ie(a);
}

void rtw_cfg80211_build_csa_beacon(struct _ADAPTER *a,
	struct cfg80211_csa_settings *params,
	u8 csa_ch, u8 csa_bw, u8 csa_offset)
{
	WLAN_BSSID_EX *pnetwork = &(a->mlmeextpriv.mlmext_info.network);
	struct ieee80211_info_element *ie;
	u8 *ies;
	uint ies_len;

	if (params->beacon_csa.tail) {
		ies = (u8 *)params->beacon_csa.tail;
		ies_len = params->beacon_csa.tail_len;
		RTW_INFO("CSA : Parsing beacon content from cfg80211_csa_settings\n");

		for_each_ie(ie, ies, ies_len) {
			#ifdef DBG_CSA
			RTW_INFO("CSA : for each IE, element id = %u, len = %u\n", ie->id, ie->len);
			#endif
			switch (ie->id) {
			case WLAN_EID_CHANNEL_SWITCH:
				RTW_INFO("CSA : add channel switch announcement IE to beacon\n");
				RTW_INFO("CSA : mode = %u, ch = %u, switch count = %u\n",
						ie->data[0], ie->data[1], ie->data[2]);
				rtw_add_bcn_ie(a, pnetwork, ie->id, ie->data, ie->len);
				break;

			case WLAN_EID_ECSA:
				RTW_INFO("CSA : add extended channel switch announcement IE to beacon\n");
				RTW_INFO("CSA : mode = %u, op_class = %u, ch = %u, switch count = %u\n",
						ie->data[0], ie->data[1], ie->data[2], ie->data[3]);
				rtw_add_bcn_ie(a, pnetwork, ie->id, ie->data, ie->len);
				break;

			/* Secondary channel offset element is not necessary for channel switching to BW_20M */
			case WLAN_EID_SECONDARY_CHANNEL_OFFSET:
				RTW_INFO("CSA : add secondary channel offset IE to beacon, sec_offset = %u\n",
						ie->data[0]);
				rtw_add_bcn_ie(a, pnetwork, ie->id, ie->data, ie->len);
				break;

			case WLAN_EID_CHANNEL_SWITCH_WRAPPER:
				RTW_INFO("CSA : add channel switch wrapper IE to beacon\n");
				RTW_INFO("CSA : channel width = %u, segment_0 = %u, segment_1 = %u\n",
						ie->data[2], ie->data[3], ie->data[4]);
				rtw_add_bcn_ie(a, pnetwork, ie->id, ie->data, ie->len);
				break;

			default:
				break;
			}
		}
	}
}

static void rtw_csa_ap_update_sta_rainfo(_adapter *adapter,
	struct sta_info *psta, const struct sta_info *ap_self_psta)
{
	#ifdef CONFIG_80211AC_VHT
	u8 vht_enable = ap_self_psta->vhtpriv.vht_option;
	#endif

	psta->cmn.bw_mode = adapter->mlmeextpriv.cur_bwmode;
	psta->htpriv.ch_offset = adapter->mlmeextpriv.cur_ch_offset;
	#ifdef CONFIG_80211AC_VHT
	psta->vhtpriv.vht_option = vht_enable;
	psta->cmn.ra_info.is_vht_enable = vht_enable;
	#endif
}

void rtw_csa_update_clients_ramask(struct _ADAPTER *a)
{
	struct sta_priv *pstapriv = &a->stapriv;
	struct sta_info *psta;
	struct wlan_network *cur_network = &(a->mlmepriv.cur_network);
	const struct sta_info *ap_self_psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
	const struct sta_info *bmc_psta = rtw_get_bcmc_stainfo(a);
	_list *plist, *phead;
	_irqL irqL;
	u8 i;

	RTW_INFO("CSA : "FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(a));
	/* update RA mask of all clients */
	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);
	for (i = 0; i < NUM_STA; i++) {
		phead = &(pstapriv->sta_hash[i]);
		plist = get_next(phead);

		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
			psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
			plist = get_next(plist);

			if (psta && psta != ap_self_psta && psta != bmc_psta) {
				rtw_csa_ap_update_sta_rainfo(a, psta, ap_self_psta);
				/* update RA mask */
				rtw_dm_ra_mask_wk_cmd(a, psta);
			}
		}
	}
	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);
}

static void rtw_ap_send_csa_action_frame(struct _ADAPTER *a)
{
	_list *phead, *plist;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &(a->stapriv);
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	rtw_stapriv_asoc_list_lock(pstapriv);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	/* for each sta in asoc_queue */
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		issue_action_spct_ch_switch(a, psta->cmn.mac_addr);
	}
	rtw_stapriv_asoc_list_unlock(pstapriv);

	issue_action_spct_ch_switch(a, bc_addr);
}

void rtw_ap_stop_ecsa_hdl(struct _ADAPTER *a)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);
	struct mlme_priv *pmlmepriv;
	_adapter *linking_adapter;

#ifdef CONFIG_CONCURRENT_MODE
	/* STA mode should keep executing joinbss_cmd after AP mode stop */
	if (rfctl->ap_csa_en == CSA_STA_JOINBSS) {
		linking_adapter = rtw_mi_get_linking_adapter(a);
		if (linking_adapter) {
			rfctl->ap_csa_en = AP_CSA_DISABLE;
			pmlmepriv = &(linking_adapter->mlmepriv);
			rtw_joinbss_cmd(linking_adapter, &(rfctl->csa_candidate_network));
		} else {
			rfctl->ap_csa_en = AP_CSA_DISABLE;
			RTW_ERR("Can't find the linking STA\n");
		}
	}
#endif

}

#endif /* CONFIG_AP_MODE */

static void rtw_ecsa_update_sta_cur_network(_adapter *adapter, u8 *pframe, u32 packet_len)
{
	WLAN_BSSID_EX *cur_network = &(adapter->mlmepriv.cur_network.network);

	cur_network->IELength = packet_len - sizeof(struct rtw_ieee80211_hdr_3addr);
	_rtw_memcpy(cur_network->IEs, (pframe + sizeof(struct rtw_ieee80211_hdr_3addr)),
				cur_network->IELength);
	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));
}

void rtw_ecsa_update_sta_mlme(_adapter *adapter, u8 *pframe, u32 packet_len)
{
	rtw_ecsa_update_sta_cur_network(adapter, pframe, packet_len);
}

void rtw_csa_switch_hdl(struct dvobj_priv *dvobj, u8 need_discon)
{
	struct rf_ctl_t *rfctl = dvobj_to_rfctl(dvobj);
	struct rtw_chset *chset = &rfctl->chset;
	_adapter *pri_adapter = dvobj_get_primary_adapter(dvobj);
	_adapter *sta_iface = NULL;
	struct mlme_ext_priv *pmlmeext = &pri_adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo;
	u8 ifbmp_m = rtw_mi_get_ap_mesh_ifbmp(pri_adapter);
	u8 ifbmp_s = rtw_mi_get_ld_sta_ifbmp(pri_adapter);
	enum band_type req_band;
	s16 req_ch;
	u8 req_bw = CHANNEL_WIDTH_20;
	u8 req_offset = CHAN_OFFSET_NO_EXT;
	u32 csa_wait_bcn_ms;
	u8 i;

	if (!ifbmp_s)
		return;

#ifdef CONFIG_TX_DUTY
	rtw_hal_pause_tx_duty(pri_adapter, 1);
#endif
	rtw_hal_macid_sleep_all_used(pri_adapter);

	if (!need_discon) {
		req_band = rfctl->csa_band;
		req_ch = rfctl->csa_ch;
		req_bw = rfctl->csa_bw;
		req_offset = rfctl->csa_ch_offset;
	} else {
		if (ifbmp_m) {
			req_ch = REQ_CH_NONE;
			RTW_INFO("CSA : "FUNC_ADPT_FMT" No valid channel, so channel seleted by AP/MESH ifaces\n",
				FUNC_ADPT_ARG(pri_adapter));
		} else {
			if (!IsSupported24G(dvobj_to_regsty(dvobj)->wireless_mode)
				#ifdef CONFIG_DFS_MASTER
				|| rfctl->radar_detected
				#endif
			)
				req_ch = 36;
			else
				req_ch = 1;
			RTW_INFO("CSA : "FUNC_ADPT_FMT" No valid channel, switch to ch %d, then disconnect with AP\n",
				FUNC_ADPT_ARG(pri_adapter), req_ch);
		}
	}

	/* check all STA ifaces status */
	for (i = 0; i < dvobj->iface_nums; i++) {
		sta_iface = dvobj->padapters[i];
		if (!sta_iface || !(ifbmp_s & BIT(sta_iface->iface_id)))
			continue;

		if (need_discon) {
			/* CSA channel not available or not valid, then disconnect */
			set_fwstate(&sta_iface->mlmepriv, WIFI_OP_CH_SWITCHING);
			issue_deauth(sta_iface, get_bssid(&sta_iface->mlmepriv), WLAN_REASON_DEAUTH_LEAVING);
		} else {
			/* update STA mode ch/bw/offset */
			sta_iface->mlmeextpriv.cur_channel = req_ch;
			sta_iface->mlmeextpriv.cur_bwmode = req_bw;
			sta_iface->mlmeextpriv.cur_ch_offset = req_offset;
			/* updaet STA mode DSConfig , ap mode will update in rtw_change_bss_chbw_cmd */
			sta_iface->mlmepriv.cur_network.network.Configuration.DSConfig = req_ch;
			set_fwstate(&sta_iface->mlmepriv, WIFI_CSA_UPDATE_BEACON);
			#ifdef CONFIG_80211D
			rtw_csa_update_regulatory(sta_iface, req_band, req_ch);
			#endif
		}
	}

	if (!need_discon) {
		/* Set STA mode wait beacon timeout timer */
		if (rtw_chset_is_dfs_chbw(chset, req_ch, req_bw, req_offset))
			csa_wait_bcn_ms = CAC_TIME_MS + 10000;
		else
			csa_wait_bcn_ms = 10000;
		RTW_INFO("CSA : set csa_wait_bcn_timer to %u ms\n", csa_wait_bcn_ms);
		_set_timer(&pmlmeext->csa_timer, csa_wait_bcn_ms);
	}

#ifdef CONFIG_AP_MODE
	if (ifbmp_m) {
		u8 execlude = 0;

		if (need_discon)
			execlude = ifbmp_s;
		/* trigger channel selection with consideraton of asoc STA ifaces */
		rtw_change_bss_chbw_cmd(dvobj_get_primary_adapter(dvobj), RTW_CMDF_DIRECTLY
			, ifbmp_m, execlude, req_ch, REQ_BW_ORI, REQ_OFFSET_NONE);
	} else
#endif
	{
		/* no AP/MESH iface, switch DFS status and channel directly */
		rtw_warn_on(req_ch <= 0);
		#if CONFIG_DFS && CONFIG_IEEE80211_BAND_5GHZ
		if (need_discon)
			rtw_dfs_rd_en_dec_on_mlme_act(pri_adapter, NULL, MLME_OPCH_SWITCH, ifbmp_s);
		else
			rtw_dfs_rd_en_dec_on_mlme_act(pri_adapter, NULL, MLME_OPCH_SWITCH, 0);
		#endif
		LeaveAllPowerSaveModeDirect(pri_adapter);
		set_channel_bwmode(pri_adapter, req_ch, req_offset, req_bw);
		/* update union ch/bw/offset for STA only */
		rtw_mi_update_union_chan_inf(pri_adapter, req_ch, req_offset, req_bw);
		rtw_rfctl_update_op_mode(rfctl, 0, 0, 0);
	}

	/* make asoc STA ifaces disconnect */
	if (need_discon) {
		for (i = 0; i < dvobj->iface_nums; i++) {
			sta_iface = dvobj->padapters[i];
			if (!sta_iface || !(ifbmp_s & BIT(sta_iface->iface_id)))
				continue;

			rtw_disassoc_cmd(sta_iface, 0, RTW_CMDF_DIRECTLY);
			rtw_indicate_disconnect(sta_iface, 0, _FALSE);
			rtw_free_assoc_resources(sta_iface, _TRUE);
			rtw_free_network_queue(sta_iface, _TRUE);

			pmlmeinfo = &(sta_iface->mlmeextpriv.mlmext_info);
			pmlmeinfo->disconnect_occurred_time = rtw_systime_to_ms(rtw_get_current_time());

			if (rtw_chset_is_dfs_ch(adapter_to_chset(sta_iface), req_ch)) {
				RTW_INFO("Switched to DFS band (ch %d) again!!\n", req_ch);
				pmlmeinfo->disconnect_code = DISCONNECTION_BY_DRIVER_DUE_TO_RECEIVE_CSA_DFS;
			} else {
				pmlmeinfo->disconnect_code = DISCONNECTION_BY_DRIVER_DUE_TO_RECEIVE_CSA_NON_DFS;
			}
			pmlmeinfo->wifi_reason_code = WLAN_REASON_DEAUTH_LEAVING;
		}
	}

	for (i = 0; i < dvobj->iface_nums; i++) {
		sta_iface = dvobj->padapters[i];
		if (sta_iface && (ifbmp_s & BIT(sta_iface->iface_id))) {
			reset_ecsa_param(sta_iface);
			break;
		}
	}

	rtw_hal_macid_wakeup_all_used(pri_adapter);
	rtw_mi_os_xmit_schedule(pri_adapter);
#ifdef CONFIG_TX_DUTY
	rtw_hal_pause_tx_duty(pri_adapter, 0);
#endif
}

/* Get ch/bw/offset of CSA from adapter, and check these parameters is valid or not */
static bool rtw_sta_get_ecsa_setting(struct _ADAPTER *a)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);
	struct rtw_chset *chset = adapter_to_chset(a);
	u8 ifbmp_m = rtw_mi_get_ap_mesh_ifbmp(a);
	u8 csa_band = rfctl->csa_band;
	u8 csa_chan = rfctl->csa_ch;
	u8 csa_offset = rfctl->csa_ch_offset;
	u8 csa_op_class = rfctl->ecsa_op_class;
	bool get_bw_offset_from_opclass;
	u8 req_band, req_ch, req_bw, req_offset;

	req_band = csa_band;
	req_ch = csa_chan;
	req_bw = CHANNEL_WIDTH_20;
	req_offset = CHAN_OFFSET_NO_EXT;

	if (rtw_chset_search_bch(chset, csa_band, csa_chan) < 0
		|| rtw_chset_is_bch_non_ocp(chset, csa_band, csa_chan)) {
		RTW_INFO("CSA : channel %u is not supported, so do not switching channel\n", csa_chan);
		return _FALSE;
	}

	get_bw_offset_from_opclass = rtw_get_bw_offset_by_op_class_ch(csa_op_class,
					req_ch, &req_bw, &req_offset);

	if (!get_bw_offset_from_opclass) {
		/* Transform channel_width to bandwidth 20/40/80M */
		switch (rfctl->csa_ch_width) {
		case CH_WIDTH_80_160M:
			req_bw = CHANNEL_WIDTH_80;
			break;
		case CH_WIDTH_20_40M:
			/*
			* We don't know the actual offset of channel 5 to 9
			* if offset is SC_IE_CHAN_OFFSET_NO_EXT and bandwidth is 40MHz,
			* so force its bandwidth to 20MHz
			*/
			if ((csa_band == BAND_ON_24G && req_ch >= 5 && req_ch <=9) &&
			   csa_offset == SC_IE_CHAN_OFFSET_NO_EXT)
				req_bw = CHANNEL_WIDTH_20;
			else
				req_bw = CHANNEL_WIDTH_40;
			break;
		default:
			req_bw = CHANNEL_WIDTH_20;
			csa_offset = SC_IE_CHAN_OFFSET_NO_EXT;
			break;
		}

		/* Transform Secondary Channel Offset element to driver's offset */
		switch (csa_offset) {
		case SC_IE_CHAN_OFFSET_UPPER:
			req_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
			break;
		case SC_IE_CHAN_OFFSET_LOWER:
			req_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
			break;
		default:
			req_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			break;
		}

		/* Get correct offset and check ch/bw/offset is valid or not */
		if (!rtw_get_offset_by_bchbw(req_band, req_ch, req_bw, &req_offset)) {
			req_bw = CHANNEL_WIDTH_20;
			req_offset = CHAN_OFFSET_NO_EXT;
		}
	}

	rfctl->csa_ch = req_ch;
	rfctl->csa_band = rtw_get_band_type(req_ch);
	rfctl->csa_bw = req_bw;
	rfctl->csa_ch_offset = req_offset;

	/* bw/offset is limited by SW capability */
	if (rtw_adjust_chbw(a, rfctl->csa_ch, &rfctl->csa_bw, &rfctl->csa_ch_offset)) {
		RTW_INFO("CSA : "FUNC_ADPT_FMT" limited by sw cap bw (%u) to (%u)\n",
			FUNC_ADPT_ARG(a), req_bw, rfctl->csa_bw);
	}

	return _TRUE;
}

static void rtw_sta_ecsa_invalid_hdl(struct _ADAPTER *a)
{
	u8 need_discon = true;
	rtw_set_csa_cmd(a, need_discon);
}

bool rtw_trigger_phl_ecsa_start(struct _ADAPTER *trigger_iface,
	enum csa_trigger_type trigger_type)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(trigger_iface);
	struct mlme_ext_priv *mlmeext = &(trigger_iface->mlmeextpriv);
	u8 is_vht = 0;
	u8 ori_band, ori_ch, ori_bw, ori_offset;
	u8 new_band, new_ch, new_bw, new_offset;

#ifdef CONFIG_80211AC_VHT
	is_vht = trigger_iface->mlmepriv.vhtpriv.vht_option;
#endif
	ori_band = rtw_get_band_type(mlmeext->cur_channel);
	ori_ch = mlmeext->cur_channel;
	ori_bw = mlmeext->cur_bwmode;
	ori_offset = mlmeext->cur_ch_offset;

	switch (trigger_type) {
	case CSA_AP_CORE_SWITCH_CH:
		if (rfctl->ap_csa_en == CSA_STA_JOINBSS) {
			trigger_iface = rtw_mi_get_ap_adapter(trigger_iface);
			mlmeext = &(trigger_iface->mlmeextpriv);
			ori_band = rtw_get_band_type(mlmeext->cur_channel);
			ori_ch = mlmeext->cur_channel;
			ori_bw = mlmeext->cur_bwmode;
			ori_offset = mlmeext->cur_ch_offset;
		}

		new_band = rfctl->ap_csa_band;
		new_ch = rfctl->ap_csa_ch;
		new_bw = rfctl->ap_csa_bw;
		new_offset = rfctl->ap_csa_ch_offset;

		/* Build CSA beacon by self */
		rtw_core_build_ecsa_beacon(trigger_iface, is_vht);

		rtw_set_ap_csa_cmd(trigger_iface);
		break;

	case CSA_AP_CFG80211_SWITCH_CH:
		new_band = rfctl->ap_csa_band;
		new_ch = rfctl->ap_csa_ch;
		new_bw = rfctl->ap_csa_bw;
		new_offset = rfctl->ap_csa_ch_offset;

		/* Build CSA beacon by self */
		rtw_core_build_ecsa_beacon(trigger_iface, is_vht);

		/* Send CSA action frame to clients */
		rtw_ap_send_csa_action_frame(trigger_iface);

		rtw_set_ap_csa_cmd(trigger_iface);
		break;

	case CSA_STA_RX_CSA_IE:
	{
		struct dvobj_priv *dvobj = adapter_to_dvobj(trigger_iface);
		_adapter *pri_adapter = dvobj_get_primary_adapter(dvobj);
		WLAN_BSSID_EX *pnetwork = &(pri_adapter->mlmeextpriv.mlmext_info.network);
		u32 countdown;

		if (!rtw_sta_get_ecsa_setting(trigger_iface)) {
			rtw_sta_ecsa_invalid_hdl(trigger_iface);
			return false;
		}

		new_band = rfctl->csa_band;
		new_ch = rfctl->csa_ch;
		new_bw = rfctl->csa_bw;
		new_offset = rfctl->csa_ch_offset;

		/* concurrent mode with AP/GO */
		if (rfctl->ap_csa_en == STA_RX_CSA) {
			_adapter * ap_iface = rtw_mi_get_ap_adapter(pri_adapter);

			/* AP mode use original bw/offset */
			mlmeext = &(ap_iface->mlmeextpriv);
			rfctl->ap_csa_bw = mlmeext->cur_bwmode;
			rfctl->ap_csa_ch_offset = mlmeext->cur_ch_offset;

			/* Get correct offset and check ch/bw/offset is valid or not */
			if (!rtw_get_offset_by_bchbw(rfctl->ap_csa_band,
					rfctl->ap_csa_ch,
					rfctl->ap_csa_bw,
					&rfctl->ap_csa_ch_offset)) {
				rfctl->ap_csa_bw = CHANNEL_WIDTH_20;
				rfctl->ap_csa_ch_offset = CHAN_OFFSET_NO_EXT;
			}

			rtw_adjust_chbw(ap_iface,
				rfctl->ap_csa_ch,
				&rfctl->ap_csa_bw,
				&rfctl->ap_csa_ch_offset);
			#ifdef CONFIG_80211AC_VHT
			is_vht = ap_iface->mlmepriv.vhtpriv.vht_option;
			#endif

			/* Build CSA beacon by self */
			rtw_core_build_ecsa_beacon(ap_iface, is_vht);

			rtw_set_ap_csa_cmd(ap_iface);
		}

		set_fwstate(&pri_adapter->mlmepriv, WIFI_CSA_SKIP_CHECK_BEACON);
		countdown = pnetwork->Configuration.BeaconPeriod * (rfctl->csa_switch_cnt + 1); /* ms */
		RTW_INFO("CSA : set countdown timer to %d ms\n", countdown);
		_set_timer(&pri_adapter->mlmeextpriv.csa_timer, countdown);
		break;
	}

	case CSA_STA_DISCONNECT_ON_DFS:
		new_band = rfctl->ap_csa_band;
		new_ch = rfctl->ap_csa_ch;
		new_bw = rfctl->ap_csa_bw;
		new_offset = rfctl->ap_csa_ch_offset;

		/* Build CSA beacon by self */
		rtw_core_build_ecsa_beacon(trigger_iface, is_vht);

		/* Send CSA action frame to clients */
		rtw_ap_send_csa_action_frame(trigger_iface);

		rtw_set_ap_csa_cmd(trigger_iface);
		break;
	default:
		goto err_hdl;
	}

	RTW_INFO("CSA : Trigger interface("ADPT_FMT")"
		"current:%u,%u,%u,%u ==> new:%u,%u,%u,%u\n",
	ADPT_ARG(trigger_iface),
	ori_band, ori_ch, ori_bw, ori_offset,
	new_band, new_ch, new_bw, new_offset);

	return true;

err_hdl:
	reset_ecsa_param(trigger_iface);
	return false;
}

/*
 * @csa_ch_width gets from Channel Switch Wrapper IE
 * 0 for 40 MHz
 * 1 for 80 MHz, 160 MHz or 80+80 MHz
 * 255 for initial value, defined by driver
 *
 * @ecsa_op_class gets from ECSA IE
 */
bool rtw_hal_trigger_csa_start(struct _ADAPTER *a, enum csa_trigger_type trigger_type,
	u8 csa_mode, u8 ecsa_op_class, u8 csa_switch_cnt,
	u8 csa_band, u8 csa_ch, u8 csa_bw, u8 csa_offset,
	u8 csa_ch_width, u8 csa_ch_freq_seg0, u8 csa_ch_freq_seg1)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);

	switch (trigger_type) {
	case CSA_AP_CORE_SWITCH_CH:
#ifdef CONFIG_AP_MODE
		rfctl->ap_csa_en = CSA_STA_JOINBSS;
		rfctl->ap_csa_wait_update_bcn = 0;
		rfctl->ap_csa_mode = csa_mode;
		rfctl->ap_ecsa_op_class = ecsa_op_class;
		rfctl->ap_csa_ch = csa_ch;
		rfctl->ap_csa_switch_cnt = csa_switch_cnt;
		rfctl->ap_csa_band = csa_band;
		rfctl->ap_csa_bw = csa_bw;
		rfctl->ap_csa_ch_width = csa_ch_width;
		rfctl->ap_csa_ch_offset = csa_offset;
		rfctl->ap_csa_ch_freq_seg0 = csa_ch_freq_seg0;
		rfctl->ap_csa_ch_freq_seg1 = csa_ch_freq_seg1;
#endif
		break;

	case CSA_AP_CFG80211_SWITCH_CH:
#ifdef CONFIG_AP_MODE
		rfctl->ap_csa_en = AP_SWITCH_CH_CSA;
		rfctl->ap_csa_wait_update_bcn = 0;
		rfctl->ap_csa_mode = csa_mode;
		rfctl->ap_ecsa_op_class = ecsa_op_class;
		rfctl->ap_csa_ch = csa_ch;
		rfctl->ap_csa_switch_cnt = csa_switch_cnt;
		rfctl->ap_csa_band = csa_band;
		rfctl->ap_csa_bw = csa_bw;
		rfctl->ap_csa_ch_width = csa_ch_width;
		rfctl->ap_csa_ch_offset = csa_offset;
		rfctl->ap_csa_ch_freq_seg0 = csa_ch_freq_seg0;
		rfctl->ap_csa_ch_freq_seg1 = csa_ch_freq_seg1;
#endif
		break;

	case CSA_STA_RX_CSA_IE:
		rfctl->csa_mode = csa_mode;
		rfctl->ecsa_op_class = ecsa_op_class;
		rfctl->csa_ch = csa_ch;
		rfctl->csa_switch_cnt = csa_switch_cnt;
		rfctl->csa_band = csa_band;
		rfctl->csa_bw = csa_bw;
		rfctl->csa_ch_offset = csa_offset;
		rfctl->csa_ch_width = csa_ch_width;
		rfctl->csa_ch_freq_seg0 = csa_ch_freq_seg0;
		rfctl->csa_ch_freq_seg1 = csa_ch_freq_seg1;

		if (rtw_mi_get_ap_mesh_ifbmp(a)) {
#ifdef CONFIG_AP_MODE
			rfctl->ap_csa_en = STA_RX_CSA;
			rfctl->ap_csa_wait_update_bcn = 0;
			rfctl->ap_csa_mode = csa_mode;
			rfctl->ap_csa_ch = csa_ch;
			rfctl->ap_csa_switch_cnt = csa_switch_cnt;
			rfctl->ap_csa_band = csa_band;
#endif
		}
		break;

	case CSA_STA_DISCONNECT_ON_DFS:
		rfctl->ap_csa_en = CSA_STA_DISCONN_ON_DFS;
		rfctl->ap_csa_wait_update_bcn = 0;
		rfctl->ap_csa_mode = csa_mode;
		rfctl->ap_ecsa_op_class = ecsa_op_class;
		rfctl->ap_csa_ch = csa_ch;
		rfctl->ap_csa_switch_cnt = csa_switch_cnt;
		rfctl->ap_csa_band = csa_band;
		rfctl->ap_csa_bw = csa_bw;
		rfctl->ap_csa_ch_width = csa_ch_width;
		rfctl->ap_csa_ch_offset = csa_offset;
		rfctl->ap_csa_ch_freq_seg0 = csa_ch_freq_seg0;
		rfctl->ap_csa_ch_freq_seg1 = csa_ch_freq_seg1;
		break;
	}

	return rtw_trigger_phl_ecsa_start(a, trigger_type);
}

bool rtw_hal_dfs_trigger_csa(struct _ADAPTER *a,
	u8 band_idx, u8 u_ch, u8 u_bw, u8 u_offset)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(a);
	enum band_type band;
	u8 ch, bw, offset;
	bool ch_avail, ret = true;
	u8 csa_mode, ecsa_op_class, csa_switch_cnt;
	u8 csa_ch_width = 0, csa_ch_freq_seg0 = 0, csa_ch_freq_seg1 = 0;

	#ifndef PRIVATE_R
	/* select new chan_def */
	ch_avail = rtw_rfctl_choose_bchbw(rfctl, BAND_MAX, 0, u_bw, 0,
				BAND_ON_5G, u_ch, u_offset,
				&band, &ch, &bw, &offset,
				false, false, __func__);
	if (!ch_avail)
		return false;
	#else
	/* only select CH36 or CH149 */
	band = BAND_ON_5G;
	ch = rfctl->p2p_park_ch;
	bw = CHANNEL_WIDTH_20;
	offset = CHAN_OFFSET_NO_EXT;
	#endif

	RTW_INFO(FUNC_HWBAND_FMT" CSA to %s-%u,%u,%u\n",
		FUNC_HWBAND_ARG(band_idx), band_str(band), ch, bw, offset);

	csa_mode = CSA_STOP_TX;
	ecsa_op_class = rtw_get_op_class_by_chbw(ch, bw, offset);
	csa_switch_cnt = 2;

	ret = rtw_hal_trigger_csa_start(a, CSA_STA_DISCONNECT_ON_DFS,
		csa_mode, ecsa_op_class, csa_switch_cnt,
		band, ch, bw, offset,
		csa_ch_width, csa_ch_freq_seg0, csa_ch_freq_seg1);

	return ret;
}

#endif /* CONFIG_ECSA */
