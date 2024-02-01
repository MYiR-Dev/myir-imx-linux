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
/*
 * Public General Config
 */
#define AUTOCONF_INCLUDED

#define RTL871X_MODULE_NAME "8733BU"
#define DRV_NAME "rtl8733bu"

/* Set CONFIG_RTL8733B from Makefile */
#ifndef CONFIG_RTL8733B
#define CONFIG_RTL8733B
#endif
#define CONFIG_USB_HCI
#define PLATFORM_LINUX


/*
 * Wi-Fi Functions Config
 */
#define CONFIG_80211N_HT
#define CONFIG_RECV_REORDERING_CTRL

/* Set CONFIG_IOCTL_CFG80211 from Makefile */
#ifdef CONFIG_IOCTL_CFG80211
	/*
	 * Indecate new sta asoc through cfg80211_new_sta
	 * If kernel version >= 3.2 or
	 * version < 3.2 but already apply cfg80211 patch,
	 * RTW_USE_CFG80211_STA_EVENT must be defiend!
	 */
	/* Set RTW_USE_CFG80211_STA_EVENT from Makefile */
	#define CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER
	/*#define CONFIG_DEBUG_CFG80211*/
	#define CONFIG_SET_SCAN_DENY_TIMER
#endif /* CONFIG_IOCTL_CFG80211 */

#ifdef CONFIG_AP_MODE
	/* #define CONFIG_INTERRUPT_BASED_TXBCN */ /* Tx Beacon when driver BCN_OK ,BCN_ERR interrupt occurs */
	#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_INTERRUPT_BASED_TXBCN)
		#undef CONFIG_INTERRUPT_BASED_TXBCN
	#endif
	#ifdef CONFIG_INTERRUPT_BASED_TXBCN
		/* #define CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */
		#define CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
	#endif

	#define CONFIG_NATIVEAP_MLME
	#ifndef CONFIG_NATIVEAP_MLME
		#define CONFIG_HOSTAPD_MLME
	#endif
	/* #define CONFIG_FIND_BEST_CHANNEL */
#endif

#ifdef CONFIG_P2P
	/* The CONFIG_WFD is for supporting the Wi-Fi display */
	#define CONFIG_WFD

	#define CONFIG_P2P_REMOVE_GROUP_INFO
	/* #define CONFIG_DBG_P2P */
	#define CONFIG_P2P_PS
	/* #define CONFIG_P2P_IPS */
	#define CONFIG_P2P_OP_CHK_SOCIAL_CH
	#define CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT  /* Replace CONFIG_P2P_CHK_INVITE_CH_LIST flag */
	/*#define CONFIG_P2P_INVITE_IOT*/
#endif

/* Set CONFIG_TDLS from Makefile */
#ifdef CONFIG_TDLS
	#define CONFIG_TDLS_DRIVER_SETUP
/*
	#ifndef CONFIG_WFD
		#define CONFIG_WFD
	#endif
*/
	/* #define CONFIG_TDLS_AUTOSETUP */
	#define CONFIG_TDLS_AUTOCHECKALIVE
	#define CONFIG_TDLS_CH_SW		/* Enable "CONFIG_TDLS_CH_SW" by default, however limit it to only work in wifi logo test mode but not in normal mode currently */
#endif

/*#define CONFIG_RTW_80211K*/

/*
 * Hareware/Firmware Related Config
 */
/*#define CONFIG_ANTENNA_DIVERSITY*/
#ifdef CONFIG_ANTENNA_DIVERSITY
	#define CONFIG_HW_ANTENNA_DIVERSITY
#endif

/*#define SUPPORT_HW_RFOFF_DETECTED*/
//#define CONFIG_RTW_LED
#ifdef CONFIG_RTW_LED
	#define CONFIG_RTW_SW_LED
	#ifdef CONFIG_RTW_SW_LED
		/* #define CONFIG_RTW_LED_HANDLED_BY_CMD_THREAD */
	#endif
#endif /* CONFIG_RTW_LED */

#define CONFIG_XMIT_ACK
#ifdef CONFIG_XMIT_ACK
	#define CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#endif

#define DISABLE_BB_RF	0
#define RTW_NOTCH_FILTER 0 /* 0:Disable, 1:Enable, */


/*
 * Software feature Related Config
 */


/*
 * Interface Related Config
 */
 /* #define CONFIG_SUPPORT_USB_INT */
#ifdef CONFIG_SUPPORT_USB_INT
	/* #define CONFIG_USB_INTERRUPT_IN_PIPE */
#endif /* CONFIG_SUPPORT_USB_INT */

#ifndef CONFIG_MINIMAL_MEMORY_USAGE
	#define CONFIG_USB_TX_AGGREGATION
	#define CONFIG_USB_RX_AGGREGATION
#endif

#ifdef CONFIG_USB_TX_AGGREGATION
	/* #define CONFIG_TX_EARLY_MODE */
#endif

/* #define CONFIG_REDUCE_USB_TX_INT	*/ /* Trade-off: Improve performance, but may cause TX URBs blocked by USB Host/Bus driver on few platforms. */

/*
 * CONFIG_USE_USB_BUFFER_ALLOC_XX uses Linux USB Buffer alloc API and is for Linux platform only now!
 */
/* #define CONFIG_USE_USB_BUFFER_ALLOC_TX 1 */	/* Trade-off: For TX path, improve stability on some platforms, but may cause performance degrade on other platforms. */
/* #define CONFIG_USE_USB_BUFFER_ALLOC_RX 1	*/ /* For RX path */
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX

#else
	#define CONFIG_PREALLOC_RECV_SKB
	#ifdef CONFIG_PREALLOC_RECV_SKB
		/* #define CONFIG_FIX_NR_BULKIN_BUFFER */ /* only use PREALLOC_RECV_SKB buffer, don't alloc skb at runtime */
	#endif
#endif

/*
 * USB VENDOR REQ BUFFER ALLOCATION METHOD
 * if not set we'll use function local variable (stack memory)
 */
/* #define CONFIG_USB_VENDOR_REQ_BUFFER_DYNAMIC_ALLOCATE */
#define CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC
#define CONFIG_USB_VENDOR_REQ_MUTEX
#define CONFIG_VENDOR_REQ_RETRY
/* #define CONFIG_USB_SUPPORT_ASYNC_VDN_REQ */


/*
 * Auto Config Section
 */
/* #define CONFIG_MAC_LOOPBACK_DRIVER */
#ifdef CONFIG_MAC_LOOPBACK_DRIVER
#undef CONFIG_IOCTL_CFG80211
#undef CONFIG_AP_MODE
#undef CONFIG_NATIVEAP_MLME
#undef CONFIG_POWER_SAVING
#undef CONFIG_BT_COEXIST
#undef CONFIG_ANTENNA_DIVERSITY
#undef SUPPORT_HW_RFOFF_DETECTED
#endif /* CONFIG_MAC_LOOPBACK_DRIVER */

#ifdef CONFIG_MP_INCLUDED
	#define MP_DRIVER 1
	#define CONFIG_MP_IWPRIV_SUPPORT	1
	/*
	 #undef CONFIG_USB_TX_AGGREGATION
	 #undef CONFIG_USB_RX_AGGREGATION
	*/
#else
	#define MP_DRIVER 0
#endif

#ifdef CONFIG_POWER_SAVING
	#define CONFIG_IPS
	#ifdef CONFIG_IPS
	/* #define CONFIG_IPS_LEVEL_2 1*/ /*enable this to set default IPS mode to IPS_LEVEL_2*/
	#define CONFIG_IPS_CHECK_IN_WD /* Do IPS Check in WatchDog.	*/
	/* #define CONFIG_FWLPS_IN_IPS */
	#endif
	/* #define SUPPORT_HW_RFOFF_DETECTED */

	#define CONFIG_LPS
	#if defined(CONFIG_LPS)
		/* #define CONFIG_LPS_LCLK */
	#endif

	#ifdef CONFIG_LPS_LCLK
		#ifdef CONFIG_POWER_SAVING
			/* #define CONFIG_XMIT_THREAD_MODE */
		#endif /* CONFIG_POWER_SAVING */
		#ifndef CONFIG_SUPPORT_USB_INT
			#define LPS_RPWM_WAIT_MS 300
			#define CONFIG_DETECT_CPWM_BY_POLLING
		#endif /* !CONFIG_SUPPORT_USB_INT */
		/* #define DBG_CHECK_FW_PS_STATE */
	#endif /* CONFIG_LPS_LCLK */

	#ifdef CONFIG_LPS
		#define CONFIG_WMMPS_STA
	#endif /* CONFIG_LPS */
#endif /*CONFIG_POWER_SAVING*/

#ifdef CONFIG_BT_COEXIST
	/* for ODM and outsrc BT-Coex */
	#ifndef CONFIG_LPS
		#define CONFIG_LPS	/* download reserved page to FW */
	#endif
#endif /* !CONFIG_BT_COEXIST */

#define CONFIG_GLOBAL_UI_PID


/*
 * Others
 */
#define CONFIG_SKB_COPY	/* for amsdu */

#define CONFIG_NEW_SIGNAL_STAT_PROCESS

#define CONFIG_EMBEDDED_FWIMG
#ifdef CONFIG_EMBEDDED_FWIMG
	#define	LOAD_FW_HEADER_FROM_DRIVER
#endif
/* #define CONFIG_FILE_FWIMG */
#define CONFIG_LONG_DELAY_ISSUE

#define CONFIG_RX_PACKET_APPEND_FCS

#define ENABLE_USB_DROP_INCORRECT_OUT

/* #define CONFIG_SIGNAL_DISPLAY_DBM */ /*display RX signal with dbm */
#ifdef CONFIG_SIGNAL_DISPLAY_DBM
/* #define CONFIG_BACKGROUND_NOISE_MONITOR */
#endif

/* #define CONFIG_EASY_REPLACEMENT */
/* #define CONFIG_RESUME_IN_WORKQUEUE */

#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	#define CONFIG_NO_EFUSE
	#define CONFIG_NO_FW
	#define CONFIG_NO_PHYDM
	#undef CONFIG_XMIT_ACK
	#undef CONFIG_POWER_SAVING
	#undef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#endif

#ifdef CONFIG_TWO_MAC_DRIVER
	#define CONFIG_NO_EFUSE
	/* #define CONFIG_NO_FW */
	#define CONFIG_NO_PHYDM
	#undef CONFIG_XMIT_ACK
	#undef CONFIG_POWER_SAVING
	#undef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#endif

/*
 * Platform  Related Config
 */
#if defined(CONFIG_PLATFORM_ACTIONS_ATM702X)
	#ifdef CONFIG_USB_TX_AGGREGATION
		#undef CONFIG_USB_TX_AGGREGATION
	#endif
	#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
		#define CONFIG_USE_USB_BUFFER_ALLOC_TX
	#endif
	#ifndef CONFIG_USE_USB_BUFFER_ALLOC_RX
		#define CONFIG_USE_USB_BUFFER_ALLOC_RX
	#endif
#endif


/*
 * Debug Related Config
 */
#define DBG	1
#define DBG_CONFIG_ERROR_DETECT
/* #define CONFIG_DISABLE_ODM */
#define CONFIG_CCV_FW
