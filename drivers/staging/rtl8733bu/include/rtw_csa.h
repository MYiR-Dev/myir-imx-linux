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
#ifndef __RTW_CSA_H_
#define __RTW_CSA_H_

#ifdef CONFIG_ECSA
#define CSA_IE_LEN 3 /* Length of Channel Switch Announcement element */
#define ECSA_IE_LEN 4 /* Length of Extended Channel Switch Announcement element */
#define CS_WR_DATA_LEN 5 /* Length of Channel Switch Wrapper element */

#define AP_CSA_DISABLE 0
#define AP_SWITCH_CH_CSA 1
#define STA_RX_CSA 2
#define CSA_STA_JOINBSS 3
#define CSA_STA_DISCONN_ON_DFS 4
#define CSA_IE_REMOVE 0xff
#define DEFAULT_CSA_CNT 3

#define CSA_STOP_TX 1

#define MAX_CSA_CNT 10

/* channel width defined in 802.11-2016, Table 9-252 VHT operation information subfields
* 0 for 20 MHz or 40 MHz
* 1 for 80 MHz, 160 MHz or 80+80 MHz
* 2 for 160 MHz (deprecated)
* 3 for non-contiguous 80+80 MHz (deprecated)
*/
#define CH_WIDTH_20_40M 0
#define CH_WIDTH_80_160M 1

enum sec_ch_offset_ie {
	SC_IE_CHAN_OFFSET_NO_EXT = 0, /*SCN - no secondary channel*/
	SC_IE_CHAN_OFFSET_UPPER = 1, /*SCA - secondary channel above*/
	SC_IE_CHAN_OFFSET_NO_DEF = 2, /*Reserved*/
	SC_IE_CHAN_OFFSET_LOWER = 3, /*SCB - secondary channel below*/
};

enum csa_trigger_type {
	/* AP mode trigger channel switch via driver core layer */
	CSA_AP_CORE_SWITCH_CH,

	/* AP mode trigger channel switch via chan_switch command from hostapd or wpa_supplicant */
	CSA_AP_CFG80211_SWITCH_CH,

	/* STA mode receive CSA/ECSA IE from associating AP */
	CSA_STA_RX_CSA_IE,

	/* STA mode disconnect with DFS AP and DFS region not supported, so switching AP mode to non-DFS channel */
	CSA_STA_DISCONNECT_ON_DFS
};

void reset_all_ecsa_param(struct _ADAPTER *a);
void reset_ecsa_param(struct _ADAPTER *a);
bool rtw_is_ecsa_enabled(struct _ADAPTER *a);
bool rtw_mr_is_ecsa_running(struct rf_ctl_t *rfctl);
bool rtw_hal_is_csa_support(struct _ADAPTER *a);
void rtw_ap_stop_ecsa_hdl(struct _ADAPTER *a);
void rtw_ecsa_update_sta_mlme(_adapter *adapter, u8 *pframe, u32 packet_len);
void rtw_csa_switch_hdl(struct dvobj_priv *dvobj, u8 need_discon);
#ifdef CONFIG_AP_MODE
void rtw_cfg80211_build_csa_beacon(struct _ADAPTER *a,
	struct cfg80211_csa_settings *params,
	u8 csa_ch, u8 csa_bw, u8 csa_offset);
void rtw_csa_update_clients_ramask(struct _ADAPTER *a);
#endif
bool rtw_trigger_phl_ecsa_start(struct _ADAPTER *trigger_iface, enum csa_trigger_type trigger_type);
bool rtw_hal_trigger_csa_start(struct _ADAPTER *a, enum csa_trigger_type trigger_type,
	u8 csa_mode, u8 ecsa_op_class, u8 csa_switch_cnt,
	u8 csa_band, u8 csa_ch, u8 csa_bw, u8 csa_offset,
	u8 csa_ch_width, u8 csa_ch_freq_seg0, u8 csa_ch_freq_seg1);
bool rtw_hal_dfs_trigger_csa(struct _ADAPTER *a,
	u8 band_idx, u8 u_ch, u8 u_bw, u8 u_offset);

#endif /* CONFIG_ECSA */
#endif
